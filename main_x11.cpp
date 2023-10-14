/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "main_x11.h"

#include <config-kwin.h>
#include "main.h"

#include "base/options.h"
#include "base/seat/backend/logind/session.h"
#include "base/x11/selection_owner.h"
#include "base/x11/xcb/helpers.h"
#include "desktop/screen_locker_watcher.h"
#include "input/x11/platform.h"
#include "input/x11/redirect.h"
#include "render/shortcuts_init.h"
#include "script/platform.h"
#include "win/shortcuts_init.h"
#include "win/x11/space.h"
#include "win/x11/space_event.h"
#include "win/x11/xcb_event_filter.h"

#include <KConfigGroup>
#include <KCrash>
#include <KLocalizedString>
#include <KPluginMetaData>
#include <KSignalHandler>

#include <qplatformdefs.h>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QVBoxLayout>
#include <QtGui/private/qtx11extras_p.h>
#include <QtDBus>

// system
#include <iostream>
#include <unistd.h>

Q_LOGGING_CATEGORY(KWIN_CORE, "kwin_core", QtWarningMsg)

constexpr char kwin_internal_name[]{"kwin_x11"};

namespace KWin
{

class AlternativeWMDialog : public QDialog
{
public:
    AlternativeWMDialog()
        : QDialog() {
        QWidget* mainWidget = new QWidget(this);
        QVBoxLayout* layout = new QVBoxLayout(mainWidget);
        QString text = i18n(
                           "KWin is unstable.\n"
                           "It seems to have crashed several times in a row.\n"
                           "You can select another window manager to run:");
        QLabel* textLabel = new QLabel(text, mainWidget);
        layout->addWidget(textLabel);
        wmList = new QComboBox(mainWidget);
        wmList->setEditable(true);
        layout->addWidget(wmList);

        addWM(QStringLiteral("metacity"));
        addWM(QStringLiteral("openbox"));
        addWM(QStringLiteral("fvwm2"));
        addWM(kwin_internal_name);

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(mainWidget);
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setDefault(true);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        mainLayout->addWidget(buttons);

        raise();
    }

    void addWM(const QString& wm) {
        // TODO: Check if WM is installed
        if (!QStandardPaths::findExecutable(wm).isEmpty())
            wmList->addItem(wm);
    }
    QString selectedWM() const {
        return wmList->currentText();
    }

private:
    QComboBox* wmList;
};

class KWinSelectionOwner : public base::x11::selection_owner
{
    Q_OBJECT
public:
    KWinSelectionOwner(xcb_connection_t* con, int screen)
        : selection_owner(make_selection_atom(con, screen), screen)
        , con{con}
    {
    }

private:
    bool genericReply(xcb_atom_t target_P, xcb_atom_t property_P, xcb_window_t requestor_P) override {
        if (target_P == xa_version) {
            int32_t version[] = { 2, 0 };
            xcb_change_property(con, XCB_PROP_MODE_REPLACE, requestor_P,
                                property_P, XCB_ATOM_INTEGER, 32, 2, version);
        } else
            return selection_owner::genericReply(target_P, property_P, requestor_P);
        return true;
    }

    void replyTargets(xcb_atom_t property_P, xcb_window_t requestor_P) override {
        selection_owner::replyTargets(property_P, requestor_P);
        xcb_atom_t atoms[ 1 ] = { xa_version };
        // PropModeAppend !
        xcb_change_property(con, XCB_PROP_MODE_APPEND, requestor_P,
                            property_P, XCB_ATOM_ATOM, 32, 1, atoms);
    }

    void getAtoms() override {
        selection_owner::getAtoms();
        if (xa_version == XCB_ATOM_NONE) {
            const QByteArray name(QByteArrayLiteral("VERSION"));
            unique_cptr<xcb_intern_atom_reply_t> atom(xcb_intern_atom_reply(
                con,
                xcb_intern_atom_unchecked(con, false, name.length(), name.constData()),
                nullptr));
            if (atom) {
                xa_version = atom->atom;
            }
        }
    }

    xcb_atom_t make_selection_atom(xcb_connection_t* con, int screen_P) {
        if (screen_P < 0)
            screen_P = QX11Info::appScreen();
        QByteArray screen(QByteArrayLiteral("WM_S"));
        screen.append(QByteArray::number(screen_P));
        unique_cptr<xcb_intern_atom_reply_t> atom(xcb_intern_atom_reply(
            con,
            xcb_intern_atom_unchecked(con, false, screen.length(), screen.constData()),
            nullptr));
        if (!atom) {
            return XCB_ATOM_NONE;
        }
        return atom->atom;
    }
    static xcb_atom_t xa_version;
    xcb_connection_t* con;
};
xcb_atom_t KWinSelectionOwner::xa_version = XCB_ATOM_NONE;

//************************************
// ApplicationX11
//************************************

int ApplicationX11::crashes = 0;

ApplicationX11::ApplicationX11(int &argc, char **argv)
    : QApplication(argc, argv)
    , base{base::config(KConfig::OpenFlag::FullConfig, "kwinrc")}
    , owner()
    , m_replace(false)
{
    app_init();

    base.x11_data.connection = QX11Info::connection();
    base.x11_data.root_window = QX11Info::appRootWindow();
}

ApplicationX11::~ApplicationX11()
{
    base.space.reset();
    if (!owner.isNull() && owner->ownerWindow() != XCB_WINDOW_NONE) {
        // If there was no --replace (no new WM)
        xcb_set_input_focus(base.x11_data.connection,
                            XCB_INPUT_FOCUS_POINTER_ROOT,
                            XCB_INPUT_FOCUS_POINTER_ROOT,
                            base.x11_data.time);
    }
}

void ApplicationX11::setReplace(bool replace)
{
    m_replace = replace;
}

void ApplicationX11::lostSelection()
{
    sendPostedEvents();
    event_filter.reset();
    base.space.reset();
    base.render.reset();

    // Remove windowmanager privileges
    base::x11::xcb::select_input(
        base.x11_data.connection, base.x11_data.root_window, XCB_EVENT_MASK_PROPERTY_CHANGE);
    quit();
}

void ApplicationX11::start()
{
    setQuitOnLastWindowClosed(false);
    setQuitLockEnabled(false);

    using base_t = base::x11::platform;
    base.is_crash_restart = crashes > 0;

    crashChecking();
    base.x11_data.screen_number = QX11Info::appScreen();
    base::x11::xcb::extensions::create(base.x11_data);

    owner.reset(new KWinSelectionOwner(base.x11_data.connection, base.x11_data.screen_number));
    connect(owner.data(), &KWinSelectionOwner::failedToClaimOwnership, []{
        fputs(i18n("kwin: unable to claim manager selection, another wm running? (try using --replace)\n").toLocal8Bit().constData(), stderr);
        ::exit(1);
    });
    connect(owner.data(), &KWinSelectionOwner::lostOwnership, this, &ApplicationX11::lostSelection);
    connect(owner.data(), &KWinSelectionOwner::claimedOwnership, [this]{
        base.options = base::create_options(base::operation_mode::x11, base.config.main);

        // Check  whether another windowmanager is running
        const uint32_t maskValues[] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT};
        unique_cptr<xcb_generic_error_t> redirectCheck(
            xcb_request_check(base.x11_data.connection,
                              xcb_change_window_attributes_checked(base.x11_data.connection,
                                                                   base.x11_data.root_window,
                                                                   XCB_CW_EVENT_MASK,
                                                                   maskValues)));
        if (redirectCheck) {
            fputs(i18n("kwin: another window manager is running (try using --replace)\n").toLocal8Bit().constData(), stderr);
            // If this is a crash-restart, DrKonqi may have stopped the process w/o killing the connection.
            if (crashes == 0) {
                ::exit(1);
            }
        }

        base.session = std::make_unique<base::seat::backend::logind::session>();
        base.render = std::make_unique<render::backend::x11::platform<base_t>>(base);
        base.input = std::make_unique<input::x11::platform<base_t>>(base);

        base.update_outputs();
        auto render = static_cast<render::backend::x11::platform<base_t>*>(base.render.get());
        try {
            render->init();
        } catch (std::exception const&) {
            std::cerr <<  "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
            ::exit(1);
        }

        try {
            base.space = std::make_unique<base_t::space_t>(*base.render, *base.input);
        } catch(std::exception& ex) {
            qCCritical(KWIN_CORE) << "Abort since space creation fails with:" << ex.what();
            exit(1);
        }

        win::init_shortcuts(*base.space);
        render::init_shortcuts(*base.render);

        event_filter = std::make_unique<win::x11::xcb_event_filter<base_t::space_t>>(*base.space);
        installNativeEventFilter(event_filter.get());

        base.script = std::make_unique<scripting::platform<base_t::space_t>>(*base.space);
        render->start(*base.space);

        // Trigger possible errors, there's still a chance to abort.
        base::x11::xcb::sync(base.x11_data.connection);
        notifyKSplash();
    });

    // we need to do an XSync here, otherwise the QPA might crash us later on
    base::x11::xcb::sync(base.x11_data.connection);
    owner->claim(m_replace || crashes > 0, true);
}

void ApplicationX11::setupCrashHandler()
{
    KCrash::setEmergencySaveFunction(ApplicationX11::crashHandler);
}

void ApplicationX11::crashChecking()
{
    setupCrashHandler();
    if (crashes >= 4) {
        // Something has gone seriously wrong
        AlternativeWMDialog dialog;
        auto cmd = QString(kwin_internal_name);
        if (dialog.exec() == QDialog::Accepted)
            cmd = dialog.selectedWM();
        else
            ::exit(1);
        if (cmd.length() > 500) {
            qCDebug(KWIN_CORE) << "Command is too long, truncating";
            cmd = cmd.left(500);
        }
        qCDebug(KWIN_CORE) << "Starting" << cmd << "and exiting";
        char buf[1024];
        sprintf(buf, "%s &", cmd.toLatin1().data());
        system(buf);
        ::exit(1);
    }
    if (crashes >= 2) {
        // Disable compositing if we have had too many crashes
        qCDebug(KWIN_CORE) << "Too many crashes recently, disabling compositing";
        KConfigGroup compgroup(KSharedConfig::openConfig(), "Compositing");
        compgroup.writeEntry("Enabled", false);
    }
    // Reset crashes count if we stay up for more that 15 seconds
    QTimer::singleShot(15 * 1000, this, [] { crashes = 0; });
}

void ApplicationX11::notifyKSplash()
{
    // Tell KSplash that KWin has started
    QDBusMessage ksplashProgressMessage = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KSplash"),
                                                                            QStringLiteral("/KSplash"),
                                                                            QStringLiteral("org.kde.KSplash"),
                                                                            QStringLiteral("setStage"));
    ksplashProgressMessage.setArguments(QList<QVariant>() << QStringLiteral("wm"));
    QDBusConnection::sessionBus().asyncCall(ksplashProgressMessage);
}

void ApplicationX11::crashHandler(int signal)
{
    crashes++;

    fprintf(stderr, "Application::crashHandler() called with signal %d; recent crashes: %d\n", signal, crashes);
    char cmd[1024];
    sprintf(cmd, "%s --crashes %d &",
            QFile::encodeName(QCoreApplication::applicationFilePath()).constData(), crashes);

    sleep(1);
    system(cmd);
}

} // namespace

int main(int argc, char * argv[])
{
    KWin::app_setup_malloc();
    KWin::app_setup_localized_string();

    int primaryScreen = 0;
    xcb_connection_t *c = xcb_connect(nullptr, &primaryScreen);
    if (!c || xcb_connection_has_error(c)) {
        fprintf(stderr, "%s: FATAL ERROR while trying to open display %s\n",
                argv[0], qgetenv("DISPLAY").constData());
        exit(1);
    }

    xcb_disconnect(c);
    c = nullptr;

    signal(SIGPIPE, SIG_IGN);

    // enforce xcb plugin, unfortunately command line switch has precedence
    setenv("QT_QPA_PLATFORM", "xcb", true);

    // disable highdpi scaling
    setenv("QT_ENABLE_HIGHDPI_SCALING", "0", true);

    qunsetenv("QT_DEVICE_PIXEL_RATIO");
    qunsetenv("QT_SCALE_FACTOR");
    qunsetenv("QT_SCREEN_SCALE_FACTORS");

    // KSMServer talks to us directly on DBus.
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);
    // For sharing thumbnails between our scene graph and qtquick.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    // shared opengl contexts must have the same reset notification policy
    format.setOptions(QSurfaceFormat::ResetNotification);
    // disables vsync for any QtQuick windows we create (BUG 406180)
    format.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(format);

    KWin::ApplicationX11 a(argc, argv);

    // reset QT_QPA_PLATFORM so we don't propagate it to our children (e.g. apps launched from the overview effect)
    qunsetenv("QT_QPA_PLATFORM");
    qunsetenv("QT_ENABLE_HIGHDPI_SCALING");

    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);
    KSignalHandler::self()->watchSignal(SIGHUP);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived,
                     &a, &QCoreApplication::exit);

    KWin::app_create_about_data();

    QCommandLineOption replaceOption(QStringLiteral("replace"), i18n("Replace already-running ICCCM2.0-compliant window manager"));

    QCommandLineParser parser;
    KWin::app_setup_command_line(&parser);
    parser.addOption(replaceOption);

    parser.process(a);
    KWin::app_process_command_line(a, &parser);
    a.setReplace(parser.isSet(replaceOption));

    // perform sanity checks
    if (a.platformName().toLower() != QStringLiteral("xcb")) {
        fprintf(stderr, "%s: FATAL ERROR expecting platform xcb but got platform %s\n",
                argv[0], qPrintable(a.platformName()));
        exit(1);
    }
    if (!QX11Info::display()) {
        fprintf(stderr, "%s: FATAL ERROR KWin requires Xlib support in the xcb plugin. Do not configure Qt with -no-xcb-xlib\n",
                argv[0]);
        exit(1);
    }

    a.start();

    return a.exec();
}

#include "main_x11.moc"
