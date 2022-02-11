/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "user_actions_menu.h"

#include "base/logging.h"
#include "base/platform.h"
#include "main.h"
#include "scripting/platform.h"
#include "toplevel.h"
#include "win/controlling.h"
#include "win/net.h"
#include "win/screen.h"
#include "workspace.h"

#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

#include <KAuthorized>
#include <KConfig>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KProcess>
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QWindow>
#include <QtConcurrentRun>

namespace KWin::win
{

struct show_on_desktop_action_data {
    uint desktop;
    bool move_to_single;
};

user_actions_menu::user_actions_menu(QObject* parent)
    : QObject(parent)
    , m_menu(nullptr)
    , m_desktopMenu(nullptr)
    , m_multipleDesktopsMenu(nullptr)
    , m_screenMenu(nullptr)
    , m_scriptsMenu(nullptr)
    , m_resizeOperation(nullptr)
    , m_moveOperation(nullptr)
    , m_maximizeOperation(nullptr)
    , m_keepAboveOperation(nullptr)
    , m_keepBelowOperation(nullptr)
    , m_fullScreenOperation(nullptr)
    , m_noBorderOperation(nullptr)
    , m_minimizeOperation(nullptr)
    , m_closeOperation(nullptr)
    , m_shortcutOperation(nullptr)
{
}

user_actions_menu::~user_actions_menu()
{
    discard();
}

bool user_actions_menu::isShown() const
{
    return m_menu && m_menu->isVisible();
}

bool user_actions_menu::hasClient() const
{
    return m_client && isShown();
}

void user_actions_menu::close()
{
    if (!m_menu) {
        return;
    }
    m_menu->close();
    m_client.clear();
}

bool user_actions_menu::isMenuClient(Toplevel const* window) const
{
    return window && window == m_client;
}

void user_actions_menu::show(const QRect& pos, Toplevel* window)
{
    Q_ASSERT(window);
    QPointer<Toplevel> cl(window);
    // Presumably client will never be nullptr,
    // but play it safe and make sure not to crash.
    if (cl.isNull()) {
        return;
    }
    if (isShown()) { // recursion
        return;
    }
    if (win::is_desktop(cl.data()) || win::is_dock(cl.data())) {
        return;
    }
    if (!KAuthorized::authorizeAction(QStringLiteral("kwin_rmb"))) {
        return;
    }
    m_client = cl;
    init();
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        m_menu->popup(pos.bottomLeft());
    } else {
        m_menu->exec(pos.bottomLeft());
    }
}

void user_actions_menu::grabInput()
{
    m_menu->windowHandle()->setMouseGrabEnabled(true);
    m_menu->windowHandle()->setKeyboardGrabEnabled(true);
}

void user_actions_menu::helperDialog(const QString& message, Toplevel* window)
{
    QStringList args;
    QString type;
    auto shortcut = [](const QString& name) {
        QAction* action = workspace()->findChild<QAction*>(name);
        Q_ASSERT(action != nullptr);
        const auto shortcuts = KGlobalAccel::self()->shortcut(action);
        return QStringLiteral("%1 (%2)")
            .arg(action->text())
            .arg(shortcuts.isEmpty() ? QString()
                                     : shortcuts.first().toString(QKeySequence::NativeText));
    };
    if (message == QStringLiteral("noborderaltf3")) {
        args << QStringLiteral("--msgbox")
             << i18n(
                    "You have selected to show a window without its border.\n"
                    "Without the border, you will not be able to enable the border "
                    "again using the mouse: use the window operations menu instead, "
                    "activated using the %1 keyboard shortcut.",
                    shortcut(QStringLiteral("Window Operations Menu")));
        type = QStringLiteral("altf3warning");
    } else if (message == QLatin1String("fullscreenaltf3")) {
        args << QStringLiteral("--msgbox")
             << i18n(
                    "You have selected to show a window in fullscreen mode.\n"
                    "If the application itself does not have an option to turn the fullscreen "
                    "mode off you will not be able to disable it "
                    "again using the mouse: use the window operations menu instead, "
                    "activated using the %1 keyboard shortcut.",
                    shortcut(QStringLiteral("Window Operations Menu")));
        type = QStringLiteral("altf3warning");
    } else
        abort();
    if (!type.isEmpty()) {
        KConfig cfg(QStringLiteral("kwin_dialogsrc"));
        KConfigGroup cg(&cfg, "Notification Messages"); // Depends on KMessageBox
        if (!cg.readEntry(type, true))
            return;
        args << QStringLiteral("--dontagain") << QLatin1String("kwin_dialogsrc:") + type;
    }
    if (window) {
        args << QStringLiteral("--embed") << QString::number(window->xcb_window());
    }
    QtConcurrent::run([args]() { KProcess::startDetached(QStringLiteral("kdialog"), args); });
}

QStringList configModules(bool controlCenter)
{
    QStringList args;
    args << QStringLiteral("kwindecoration");
    if (controlCenter)
        args << QStringLiteral("kwinoptions");
    else if (KAuthorized::authorizeControlModule(QStringLiteral("kde-kwinoptions.desktop")))
        args << QStringLiteral("kwinactions") << QStringLiteral("kwinfocus")
             << QStringLiteral("kwinmoving") << QStringLiteral("kwinadvanced")
             << QStringLiteral("kwinrules") << QStringLiteral("kwincompositing")
             << QStringLiteral("kwineffects")
#ifdef KWIN_BUILD_TABBOX
             << QStringLiteral("kwintabbox")
#endif
             << QStringLiteral("kwinscreenedges") << QStringLiteral("kwinscripts");
    return args;
}

void user_actions_menu::init()
{
    if (m_menu) {
        return;
    }
    m_menu = new QMenu;
    connect(m_menu, &QMenu::aboutToShow, this, &user_actions_menu::menuAboutToShow);
    connect(m_menu,
            &QMenu::triggered,
            this,
            &user_actions_menu::slotWindowOperation,
            Qt::QueuedConnection);

    QMenu* advancedMenu = new QMenu(m_menu);
    connect(advancedMenu, &QMenu::aboutToShow, [this, advancedMenu]() {
        if (m_client) {
            advancedMenu->setPalette(m_client->control->palette().q_palette());
        }
    });

    auto setShortcut = [](QAction* action, const QString& actionName) {
        const auto shortcuts
            = KGlobalAccel::self()->shortcut(workspace()->findChild<QAction*>(actionName));
        if (!shortcuts.isEmpty()) {
            action->setShortcut(shortcuts.first());
        }
    };

    m_moveOperation = advancedMenu->addAction(i18n("&Move"));
    m_moveOperation->setIcon(QIcon::fromTheme(QStringLiteral("transform-move")));
    setShortcut(m_moveOperation, QStringLiteral("Window Move"));
    m_moveOperation->setData(Options::UnrestrictedMoveOp);

    m_resizeOperation = advancedMenu->addAction(i18n("&Resize"));
    m_resizeOperation->setIcon(QIcon::fromTheme(QStringLiteral("transform-scale")));
    setShortcut(m_resizeOperation, QStringLiteral("Window Resize"));
    m_resizeOperation->setData(Options::ResizeOp);

    m_keepAboveOperation = advancedMenu->addAction(i18n("Keep &Above Others"));
    m_keepAboveOperation->setIcon(QIcon::fromTheme(QStringLiteral("window-keep-above")));
    setShortcut(m_keepAboveOperation, QStringLiteral("Window Above Other Windows"));
    m_keepAboveOperation->setCheckable(true);
    m_keepAboveOperation->setData(Options::KeepAboveOp);

    m_keepBelowOperation = advancedMenu->addAction(i18n("Keep &Below Others"));
    m_keepBelowOperation->setIcon(QIcon::fromTheme(QStringLiteral("window-keep-below")));
    setShortcut(m_keepBelowOperation, QStringLiteral("Window Below Other Windows"));
    m_keepBelowOperation->setCheckable(true);
    m_keepBelowOperation->setData(Options::KeepBelowOp);

    m_fullScreenOperation = advancedMenu->addAction(i18n("&Fullscreen"));
    m_fullScreenOperation->setIcon(QIcon::fromTheme(QStringLiteral("view-fullscreen")));
    setShortcut(m_fullScreenOperation, QStringLiteral("Window Fullscreen"));
    m_fullScreenOperation->setCheckable(true);
    m_fullScreenOperation->setData(Options::FullScreenOp);

    m_noBorderOperation = advancedMenu->addAction(i18n("&No Border"));
    m_noBorderOperation->setIcon(QIcon::fromTheme(QStringLiteral("edit-none-border")));
    setShortcut(m_noBorderOperation, QStringLiteral("Window No Border"));
    m_noBorderOperation->setCheckable(true);
    m_noBorderOperation->setData(Options::NoBorderOp);

    advancedMenu->addSeparator();

    m_shortcutOperation = advancedMenu->addAction(i18n("Set Window Short&cut..."));
    m_shortcutOperation->setIcon(QIcon::fromTheme(QStringLiteral("configure-shortcuts")));
    setShortcut(m_shortcutOperation, QStringLiteral("Setup Window Shortcut"));
    m_shortcutOperation->setData(Options::SetupWindowShortcutOp);

    QAction* action = advancedMenu->addAction(i18n("Configure Special &Window Settings..."));
    action->setIcon(QIcon::fromTheme(QStringLiteral("preferences-system-windows-actions")));
    action->setData(Options::WindowRulesOp);
    m_rulesOperation = action;

    action = advancedMenu->addAction(i18n("Configure S&pecial Application Settings..."));
    action->setIcon(QIcon::fromTheme(QStringLiteral("preferences-system-windows-actions")));
    action->setData(Options::ApplicationRulesOp);
    m_applicationRulesOperation = action;
    if (!kwinApp()->config()->isImmutable()
        && !KAuthorized::authorizeControlModules(configModules(true)).isEmpty()) {
        advancedMenu->addSeparator();
        action = advancedMenu->addAction(i18nc(
            "Entry in context menu of window decoration to open the configuration module of KWin",
            "Configure W&indow Manager..."));
        action->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
        connect(action, &QAction::triggered, this, [this]() {
            // opens the KWin configuration
            QStringList args;
            args << QStringLiteral("--icon") << QStringLiteral("preferences-system-windows");
            const QString path
                = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                         QStringLiteral("kservices5/kwinfocus.desktop"));
            if (!path.isEmpty()) {
                args << QStringLiteral("--desktopfile") << path;
            }
            args << configModules(false);
            QProcess* p = new QProcess(this);
            p->setArguments(args);
            p->setProcessEnvironment(kwinApp()->processStartupEnvironment());
            p->setProgram(QStringLiteral("kcmshell5"));
            connect(p,
                    static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                    p,
                    &QProcess::deleteLater);
            connect(p, &QProcess::errorOccurred, this, [](QProcess::ProcessError e) {
                if (e == QProcess::FailedToStart) {
                    qCDebug(KWIN_CORE) << "Failed to start kcmshell5";
                }
            });
            p->start();
        });
    }

    m_maximizeOperation = m_menu->addAction(i18n("Ma&ximize"));
    m_maximizeOperation->setIcon(QIcon::fromTheme(QStringLiteral("window-maximize")));
    setShortcut(m_maximizeOperation, QStringLiteral("Window Maximize"));
    m_maximizeOperation->setCheckable(true);
    m_maximizeOperation->setData(Options::MaximizeOp);

    m_minimizeOperation = m_menu->addAction(i18n("Mi&nimize"));
    m_minimizeOperation->setIcon(QIcon::fromTheme(QStringLiteral("window-minimize")));
    setShortcut(m_minimizeOperation, QStringLiteral("Window Minimize"));
    m_minimizeOperation->setData(Options::MinimizeOp);

    action = m_menu->addMenu(advancedMenu);
    action->setText(i18n("&More Actions"));
    action->setIcon(QIcon::fromTheme(QStringLiteral("overflow-menu")));

    m_closeOperation = m_menu->addAction(i18n("&Close"));
    m_closeOperation->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    setShortcut(m_closeOperation, QStringLiteral("Window Close"));
    m_closeOperation->setData(Options::CloseOp);
}

void user_actions_menu::discard()
{
    delete m_menu;
    m_menu = nullptr;
    m_desktopMenu = nullptr;
    m_multipleDesktopsMenu = nullptr;
    m_screenMenu = nullptr;
    m_scriptsMenu = nullptr;
}

void user_actions_menu::menuAboutToShow()
{
    if (m_client.isNull() || !m_menu)
        return;

    if (win::virtual_desktop_manager::self()->count() == 1) {
        delete m_desktopMenu;
        m_desktopMenu = nullptr;
        delete m_multipleDesktopsMenu;
        m_multipleDesktopsMenu = nullptr;
    } else {
        initDesktopPopup();
    }
    if (kwinApp()->get_base().screens.count() == 1
        || (!m_client->isMovable() && !m_client->isMovableAcrossScreens())) {
        delete m_screenMenu;
        m_screenMenu = nullptr;
    } else {
        initScreenPopup();
    }

    m_menu->setPalette(m_client->control->palette().q_palette());
    m_resizeOperation->setEnabled(m_client->isResizable());
    m_moveOperation->setEnabled(m_client->isMovableAcrossScreens());
    m_maximizeOperation->setEnabled(m_client->isMaximizable());
    m_maximizeOperation->setChecked(m_client->maximizeMode() == win::maximize_mode::full);
    m_keepAboveOperation->setChecked(m_client->control->keep_above());
    m_keepBelowOperation->setChecked(m_client->control->keep_below());
    m_fullScreenOperation->setEnabled(m_client->userCanSetFullScreen());
    m_fullScreenOperation->setChecked(m_client->control->fullscreen());
    m_noBorderOperation->setEnabled(m_client->userCanSetNoBorder());
    m_noBorderOperation->setChecked(m_client->noBorder());
    m_minimizeOperation->setEnabled(m_client->isMinimizable());
    m_closeOperation->setEnabled(m_client->isCloseable());
    m_shortcutOperation->setEnabled(m_client->control->rules().checkShortcut(QString()).isNull());

    // drop the existing scripts menu
    delete m_scriptsMenu;
    m_scriptsMenu = nullptr;
    // ask scripts whether they want to add entries for the given Client
    auto scriptActions
        = workspace()->scripting->actionsForUserActionMenu(m_client.data(), m_scriptsMenu);
    if (!scriptActions.isEmpty()) {
        m_scriptsMenu = new QMenu(m_menu);
        m_scriptsMenu->setPalette(m_client->control->palette().q_palette());
        m_scriptsMenu->addActions(scriptActions);

        QAction* action = m_scriptsMenu->menuAction();
        // set it as the first item after desktop
        m_menu->insertAction(m_closeOperation, action);
        action->setText(i18n("&Extensions"));
    }

    m_rulesOperation->setEnabled(m_client->supportsWindowRules());
    m_applicationRulesOperation->setEnabled(m_client->supportsWindowRules());
}

void user_actions_menu::initDesktopPopup()
{
    if (kwinApp()->operationMode() == Application::OperationModeWaylandOnly
        || kwinApp()->operationMode() == Application::OperationModeXwayland) {
        if (m_multipleDesktopsMenu) {
            return;
        }

        m_multipleDesktopsMenu = new QMenu(m_menu);
        connect(m_multipleDesktopsMenu,
                &QMenu::triggered,
                this,
                &user_actions_menu::slotToggleOnVirtualDesktop);
        connect(m_multipleDesktopsMenu,
                &QMenu::aboutToShow,
                this,
                &user_actions_menu::multipleDesktopsPopupAboutToShow);

        QAction* action = m_multipleDesktopsMenu->menuAction();
        // set it as the first item
        m_menu->insertAction(m_maximizeOperation, action);
        action->setText(i18n("&Desktops"));
        action->setIcon(QIcon::fromTheme(QStringLiteral("virtual-desktops")));

    } else {
        if (m_desktopMenu)
            return;

        m_desktopMenu = new QMenu(m_menu);
        connect(m_desktopMenu, &QMenu::triggered, this, &user_actions_menu::slotSendToDesktop);
        connect(
            m_desktopMenu, &QMenu::aboutToShow, this, &user_actions_menu::desktopPopupAboutToShow);

        QAction* action = m_desktopMenu->menuAction();
        // set it as the first item
        m_menu->insertAction(m_maximizeOperation, action);
        action->setText(i18n("Move to &Desktop"));
        action->setIcon(QIcon::fromTheme(QStringLiteral("virtual-desktops")));
    }
}

void user_actions_menu::initScreenPopup()
{
    if (m_screenMenu) {
        return;
    }

    m_screenMenu = new QMenu(m_menu);
    connect(m_screenMenu, &QMenu::triggered, this, &user_actions_menu::slotSendToScreen);
    connect(m_screenMenu, &QMenu::aboutToShow, this, &user_actions_menu::screenPopupAboutToShow);

    QAction* action = m_screenMenu->menuAction();
    // set it as the first item after desktop
    m_menu->insertAction(m_minimizeOperation, action);
    action->setText(i18n("Move to &Screen"));
    action->setIcon(QIcon::fromTheme(QStringLiteral("computer")));
}

void user_actions_menu::desktopPopupAboutToShow()
{
    if (!m_desktopMenu)
        return;
    auto const vds = win::virtual_desktop_manager::self();

    m_desktopMenu->clear();
    if (m_client) {
        m_desktopMenu->setPalette(m_client->control->palette().q_palette());
    }
    QActionGroup* group = new QActionGroup(m_desktopMenu);
    QAction* action = m_desktopMenu->addAction(i18n("&All Desktops"));
    action->setData(0);
    action->setCheckable(true);
    group->addAction(action);

    if (m_client && m_client->isOnAllDesktops()) {
        action->setChecked(true);
    }
    m_desktopMenu->addSeparator();

    const uint BASE = 10;

    for (uint i = 1; i <= vds->count(); ++i) {
        QString basic_name(QStringLiteral("%1  %2"));
        if (i < BASE) {
            basic_name.prepend(QLatin1Char('&'));
        }
        action = m_desktopMenu->addAction(
            basic_name.arg(i).arg(vds->name(i).replace(QLatin1Char('&'), QStringLiteral("&&"))));
        action->setData(i);
        action->setCheckable(true);
        group->addAction(action);

        if (m_client && !m_client->isOnAllDesktops() && m_client->isOnDesktop(i)) {
            action->setChecked(true);
        }
    }

    m_desktopMenu->addSeparator();
    action = m_desktopMenu->addAction(
        i18nc("Create a new desktop and move the window there", "&New Desktop"));
    action->setData(vds->count() + 1);

    if (vds->count() >= vds->maximum())
        action->setEnabled(false);
}

void user_actions_menu::multipleDesktopsPopupAboutToShow()
{
    if (!m_multipleDesktopsMenu)
        return;
    auto const vds = win::virtual_desktop_manager::self();

    m_multipleDesktopsMenu->clear();
    if (m_client) {
        m_multipleDesktopsMenu->setPalette(m_client->control->palette().q_palette());
    }

    QAction* action = m_multipleDesktopsMenu->addAction(i18n("&All Desktops"));
    action->setData(QVariant::fromValue(show_on_desktop_action_data{0, false}));
    action->setCheckable(true);
    if (m_client && m_client->isOnAllDesktops()) {
        action->setChecked(true);
    }

    m_multipleDesktopsMenu->addSeparator();

    const uint BASE = 10;

    for (uint i = 1; i <= vds->count(); ++i) {
        QString basic_name(QStringLiteral("%1  %2"));
        if (i < BASE) {
            basic_name.prepend(QLatin1Char('&'));
        }

        QAction* action = m_multipleDesktopsMenu->addAction(
            basic_name.arg(i).arg(vds->name(i).replace(QLatin1Char('&'), QStringLiteral("&&"))));
        action->setData(QVariant::fromValue(show_on_desktop_action_data{i, false}));
        action->setCheckable(true);
        if (m_client && !m_client->isOnAllDesktops() && m_client->isOnDesktop(i)) {
            action->setChecked(true);
        }
    }

    m_multipleDesktopsMenu->addSeparator();

    for (uint i = 1; i <= vds->count(); ++i) {
        QString name = i18n("Move to %1 %2", i, vds->name(i));
        QAction* action = m_multipleDesktopsMenu->addAction(name);
        action->setData(QVariant::fromValue(show_on_desktop_action_data{i, true}));
    }

    m_multipleDesktopsMenu->addSeparator();

    bool allowNewDesktops = vds->count() < vds->maximum();
    uint countPlusOne = vds->count() + 1;

    action = m_multipleDesktopsMenu->addAction(
        i18nc("Create a new desktop and add the window to that desktop", "Add to &New Desktop"));
    action->setData(QVariant::fromValue(show_on_desktop_action_data{countPlusOne, false}));
    action->setEnabled(allowNewDesktops);

    action = m_multipleDesktopsMenu->addAction(
        i18nc("Create a new desktop and move the window to that desktop", "Move to New Desktop"));
    action->setData(QVariant::fromValue(show_on_desktop_action_data{countPlusOne, true}));
    action->setEnabled(allowNewDesktops);
}

void user_actions_menu::screenPopupAboutToShow()
{
    if (!m_screenMenu) {
        return;
    }
    m_screenMenu->clear();

    if (!m_client) {
        return;
    }

    m_screenMenu->setPalette(m_client->control->palette().q_palette());
    QActionGroup* group = new QActionGroup(m_screenMenu);
    auto const& screens = kwinApp()->get_base().screens;

    for (int i = 0; i < screens.count(); ++i) {
        // assumption: there are not more than 9 screens attached.
        QAction* action = m_screenMenu->addAction(
            i18nc("@item:inmenu List of all Screens to send a window to. First argument is a "
                  "number, second the output identifier. E.g. Screen 1 (HDMI1)",
                  "Screen &%1 (%2)",
                  (i + 1),
                  screens.name(i)));
        action->setData(i);
        action->setCheckable(true);
        if (m_client && i == m_client->screen()) {
            action->setChecked(true);
        }
        group->addAction(action);
    }
}

void user_actions_menu::slotWindowOperation(QAction* action)
{
    if (!action->data().isValid())
        return;

    Options::WindowOperation op = static_cast<Options::WindowOperation>(action->data().toInt());
    auto c = m_client ? m_client : QPointer<Toplevel>(workspace()->activeClient());
    if (c.isNull())
        return;
    QString type;
    switch (op) {
    case Options::FullScreenOp:
        if (!c->control->fullscreen() && c->userCanSetFullScreen())
            type = QStringLiteral("fullscreenaltf3");
        break;
    case Options::NoBorderOp:
        if (!c->noBorder() && c->userCanSetNoBorder())
            type = QStringLiteral("noborderaltf3");
        break;
    default:
        break;
    }
    if (!type.isEmpty())
        helperDialog(type, c);
    // need to delay performing the window operation as we need to have the
    // user actions menu closed before we destroy the decoration. Otherwise Qt crashes
    qRegisterMetaType<Options::WindowOperation>();
    QMetaObject::invokeMethod(workspace(),
                              "performWindowOperation",
                              Qt::QueuedConnection,
                              Q_ARG(KWin::Toplevel*, c),
                              Q_ARG(Options::WindowOperation, op));
}

void user_actions_menu::slotSendToDesktop(QAction* action)
{
    bool ok = false;
    uint desk = action->data().toUInt(&ok);
    if (!ok) {
        return;
    }
    if (m_client.isNull())
        return;
    auto ws = workspace();
    auto vds = win::virtual_desktop_manager::self();
    if (desk == 0) {
        // the 'on_all_desktops' menu entry
        if (m_client) {
            win::set_on_all_desktops(m_client.data(), !m_client->isOnAllDesktops());
        }
        return;
    } else if (desk > vds->count()) {
        vds->setCount(desk);
    }

    ws->sendClientToDesktop(m_client.data(), desk, false);
}

void user_actions_menu::slotToggleOnVirtualDesktop(QAction* action)
{
    if (m_client.isNull()) {
        return;
    }

    if (!action->data().canConvert<show_on_desktop_action_data>()) {
        return;
    }
    show_on_desktop_action_data data = action->data().value<show_on_desktop_action_data>();

    auto vds = win::virtual_desktop_manager::self();
    if (data.desktop == 0) {
        // the 'on_all_desktops' menu entry
        win::set_on_all_desktops(m_client.data(), !m_client->isOnAllDesktops());
        return;
    } else if (data.desktop > vds->count()) {
        vds->setCount(data.desktop);
    }

    if (data.move_to_single) {
        win::set_desktop(m_client.data(), data.desktop);
    } else {
        auto virtualDesktop = win::virtual_desktop_manager::self()->desktopForX11Id(data.desktop);
        if (m_client->desktops().contains(virtualDesktop)) {
            win::leave_desktop(m_client.data(), virtualDesktop);
        } else {
            win::enter_desktop(m_client.data(), virtualDesktop);
        }
    }
}

void user_actions_menu::slotSendToScreen(QAction* action)
{
    const int screen = action->data().toInt();
    if (m_client.isNull()) {
        return;
    }
    if (screen >= kwinApp()->get_base().screens.count()) {
        return;
    }

    workspace()->sendClientToScreen(m_client.data(), screen);
}

}

Q_DECLARE_METATYPE(KWin::win::show_on_desktop_action_data);
