/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

/*

 This file contains things relevant to direct user actions, such as
 responses to global keyboard shortcuts, or selecting actions
 from the window operations menu.

*/

///////////////////////////////////////////////////////////////////////////////
// NOTE: if you change the menu, keep
//       plasma-desktop/applets/taskmanager/package/contents/ui/ContextMenu.qml
//       in sync
//////////////////////////////////////////////////////////////////////////////

#include "useractions.h"
#include "input/cursor.h"
#include "render/post/night_color_manager.h"
#include "render/compositor.h"
#include "workspace.h"
#include "render/effects.h"
#include "render/platform.h"
#include "rules/rule_book.h"
#include "screens.h"
#include "utils.h"
#include "scripting/platform.h"

#include "win/app_menu.h"
#include "win/controlling.h"
#include "win/input.h"
#include "win/layers.h"
#include "win/net.h"
#include "win/screen.h"
#include "win/stacking_order.h"
#include "win/virtual_desktops.h"
#include "win/x11/window.h"

#include <KProcess>

#include <QAction>
#include <QCheckBox>
#include <QtConcurrentRun>
#include <QPointer>
#include <QPushButton>

#include <KGlobalAccel>
#include <KLocalizedString>
#include <kconfig.h>
#include <QMenu>
#include <QRegularExpression>
#include <QWidgetAction>
#include <QWindow>

#include <kauthorized.h>

#include "killwindow.h"
#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

namespace KWin
{

struct ShowOnDesktopActionData {
    uint desktop;
    bool moveToSingle;
};

UserActionsMenu::UserActionsMenu(QObject *parent)
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

UserActionsMenu::~UserActionsMenu()
{
    discard();
}

bool UserActionsMenu::isShown() const
{
    return m_menu && m_menu->isVisible();
}

bool UserActionsMenu::hasClient() const
{
    return m_client && isShown();
}

void UserActionsMenu::close()
{
    if (!m_menu) {
        return;
    }
    m_menu->close();
    m_client.clear();
}

bool UserActionsMenu::isMenuClient(Toplevel const* window) const
{
    return window && window == m_client;
}

void UserActionsMenu::show(const QRect &pos, Toplevel* window)
{
    Q_ASSERT(window);
    QPointer<Toplevel> cl(window);
    // Presumably client will never be nullptr,
    // but play it safe and make sure not to crash.
    if (cl.isNull()) {
        return;
    }
    if (isShown()) {  // recursion
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

void UserActionsMenu::grabInput()
{
    m_menu->windowHandle()->setMouseGrabEnabled(true);
    m_menu->windowHandle()->setKeyboardGrabEnabled(true);
}

void UserActionsMenu::helperDialog(const QString& message, Toplevel* window)
{
    QStringList args;
    QString type;
    auto shortcut = [](const QString &name) {
        QAction* action = Workspace::self()->findChild<QAction*>(name);
        Q_ASSERT(action != nullptr);
        const auto shortcuts = KGlobalAccel::self()->shortcut(action);
        return QStringLiteral("%1 (%2)").arg(action->text())
                             .arg(shortcuts.isEmpty() ? QString() : shortcuts.first().toString(QKeySequence::NativeText));
    };
    if (message == QStringLiteral("noborderaltf3")) {
        args << QStringLiteral("--msgbox") << i18n(
                 "You have selected to show a window without its border.\n"
                 "Without the border, you will not be able to enable the border "
                 "again using the mouse: use the window operations menu instead, "
                 "activated using the %1 keyboard shortcut.",
                 shortcut(QStringLiteral("Window Operations Menu")));
        type = QStringLiteral("altf3warning");
    } else if (message == QLatin1String("fullscreenaltf3")) {
        args << QStringLiteral("--msgbox") << i18n(
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
        KConfigGroup cg(&cfg, "Notification Messages");  // Depends on KMessageBox
        if (!cg.readEntry(type, true))
            return;
        args << QStringLiteral("--dontagain") << QLatin1String("kwin_dialogsrc:") + type;
    }
    if (window) {
        args << QStringLiteral("--embed") << QString::number(window->xcb_window());
    }
    QtConcurrent::run([args]() {
        KProcess::startDetached(QStringLiteral("kdialog"), args);
    });
}


QStringList configModules(bool controlCenter)
{
    QStringList args;
    args <<  QStringLiteral("kwindecoration");
    if (controlCenter)
        args << QStringLiteral("kwinoptions");
    else if (KAuthorized::authorizeControlModule(QStringLiteral("kde-kwinoptions.desktop")))
        args << QStringLiteral("kwinactions") << QStringLiteral("kwinfocus") <<  QStringLiteral("kwinmoving") << QStringLiteral("kwinadvanced")
             << QStringLiteral("kwinrules") << QStringLiteral("kwincompositing") << QStringLiteral("kwineffects")
#ifdef KWIN_BUILD_TABBOX
             << QStringLiteral("kwintabbox")
#endif
             << QStringLiteral("kwinscreenedges")
             << QStringLiteral("kwinscripts")
             ;
    return args;
}

void UserActionsMenu::init()
{
    if (m_menu) {
        return;
    }
    m_menu = new QMenu;
    connect(m_menu, &QMenu::aboutToShow, this, &UserActionsMenu::menuAboutToShow);
    connect(m_menu, &QMenu::triggered, this, &UserActionsMenu::slotWindowOperation, Qt::QueuedConnection);

    QMenu *advancedMenu = new QMenu(m_menu);
    connect(advancedMenu, &QMenu::aboutToShow, [this, advancedMenu]() {
        if (m_client) {
            advancedMenu->setPalette(m_client->control->palette().q_palette());
        }
    });

    auto setShortcut = [](QAction *action, const QString &actionName) {
        const auto shortcuts = KGlobalAccel::self()->shortcut(Workspace::self()->findChild<QAction*>(actionName));
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

    QAction *action = advancedMenu->addAction(i18n("Configure Special &Window Settings..."));
    action->setIcon(QIcon::fromTheme(QStringLiteral("preferences-system-windows-actions")));
    action->setData(Options::WindowRulesOp);
    m_rulesOperation = action;

    action = advancedMenu->addAction(i18n("Configure S&pecial Application Settings..."));
    action->setIcon(QIcon::fromTheme(QStringLiteral("preferences-system-windows-actions")));
    action->setData(Options::ApplicationRulesOp);
    m_applicationRulesOperation = action;
    if (!kwinApp()->config()->isImmutable() &&
            !KAuthorized::authorizeControlModules(configModules(true)).isEmpty()) {
        advancedMenu->addSeparator();
        action = advancedMenu->addAction(i18nc("Entry in context menu of window decoration to open the configuration module of KWin",
                                        "Configure W&indow Manager..."));
        action->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
        connect(action, &QAction::triggered, this,
            [this]() {
                // opens the KWin configuration
                QStringList args;
                args << QStringLiteral("--icon") << QStringLiteral("preferences-system-windows");
                const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                            QStringLiteral("kservices5/kwinfocus.desktop"));
                if (!path.isEmpty()) {
                    args << QStringLiteral("--desktopfile") << path;
                }
                args << configModules(false);
                QProcess *p = new Process(this);
                p->setArguments(args);
                p->setProcessEnvironment(kwinApp()->processStartupEnvironment());
                p->setProgram(QStringLiteral("kcmshell5"));
                connect(p, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), p, &QProcess::deleteLater);
                connect(p, &QProcess::errorOccurred, this, [p](QProcess::ProcessError e) {
                    if (e == QProcess::FailedToStart) {
                        qCDebug(KWIN_CORE) << "Failed to start kcmshell5";
                    }
                });
                p->start();
            }
        );
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

void UserActionsMenu::discard()
{
    delete m_menu;
    m_menu = nullptr;
    m_desktopMenu = nullptr;
    m_multipleDesktopsMenu = nullptr;
    m_screenMenu = nullptr;
    m_scriptsMenu = nullptr;
}

void UserActionsMenu::menuAboutToShow()
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
    if (kwinApp()->get_base().screens.count() == 1 || (!m_client->isMovable() && !m_client->isMovableAcrossScreens())) {
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

        QAction *action = m_scriptsMenu->menuAction();
        // set it as the first item after desktop
        m_menu->insertAction(m_closeOperation, action);
        action->setText(i18n("&Extensions"));
    }

    m_rulesOperation->setEnabled(m_client->supportsWindowRules());
    m_applicationRulesOperation->setEnabled(m_client->supportsWindowRules());
}

void UserActionsMenu::initDesktopPopup()
{
    if (kwinApp()->operationMode() == Application::OperationModeWaylandOnly ||
        kwinApp()->operationMode() == Application::OperationModeXwayland) {
        if (m_multipleDesktopsMenu) {
            return;
        }

        m_multipleDesktopsMenu = new QMenu(m_menu);
        connect(m_multipleDesktopsMenu, &QMenu::triggered, this,   &UserActionsMenu::slotToggleOnVirtualDesktop);
        connect(m_multipleDesktopsMenu, &QMenu::aboutToShow, this, &UserActionsMenu::multipleDesktopsPopupAboutToShow);

        QAction *action = m_multipleDesktopsMenu->menuAction();
        // set it as the first item
        m_menu->insertAction(m_maximizeOperation, action);
        action->setText(i18n("&Desktops"));
        action->setIcon(QIcon::fromTheme(QStringLiteral("virtual-desktops")));

    } else {
        if (m_desktopMenu)
            return;

        m_desktopMenu = new QMenu(m_menu);
        connect(m_desktopMenu, &QMenu::triggered,   this, &UserActionsMenu::slotSendToDesktop);
        connect(m_desktopMenu, &QMenu::aboutToShow, this, &UserActionsMenu::desktopPopupAboutToShow);

        QAction *action = m_desktopMenu->menuAction();
        // set it as the first item
        m_menu->insertAction(m_maximizeOperation, action);
        action->setText(i18n("Move to &Desktop"));
        action->setIcon(QIcon::fromTheme(QStringLiteral("virtual-desktops")));
    }
}

void UserActionsMenu::initScreenPopup()
{
    if (m_screenMenu) {
        return;
    }

    m_screenMenu = new QMenu(m_menu);
    connect(m_screenMenu, &QMenu::triggered,   this, &UserActionsMenu::slotSendToScreen);
    connect(m_screenMenu, &QMenu::aboutToShow, this, &UserActionsMenu::screenPopupAboutToShow);

    QAction *action = m_screenMenu->menuAction();
    // set it as the first item after desktop
    m_menu->insertAction(m_minimizeOperation, action);
    action->setText(i18n("Move to &Screen"));
    action->setIcon(QIcon::fromTheme(QStringLiteral("computer")));
}

void UserActionsMenu::desktopPopupAboutToShow()
{
    if (!m_desktopMenu)
        return;
    auto const vds = win::virtual_desktop_manager::self();

    m_desktopMenu->clear();
    if (m_client) {
        m_desktopMenu->setPalette(m_client->control->palette().q_palette());
    }
    QActionGroup *group = new QActionGroup(m_desktopMenu);
    QAction *action = m_desktopMenu->addAction(i18n("&All Desktops"));
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
        action = m_desktopMenu->addAction(basic_name.arg(i).arg(vds->name(i).replace(QLatin1Char('&'), QStringLiteral("&&"))));
        action->setData(i);
        action->setCheckable(true);
        group->addAction(action);

        if (m_client && !m_client->isOnAllDesktops() && m_client->isOnDesktop(i)) {
            action->setChecked(true);
        }
    }

    m_desktopMenu->addSeparator();
    action = m_desktopMenu->addAction(i18nc("Create a new desktop and move the window there", "&New Desktop"));
    action->setData(vds->count() + 1);

    if (vds->count() >= vds->maximum())
        action->setEnabled(false);
}

void UserActionsMenu::multipleDesktopsPopupAboutToShow()
{
    if (!m_multipleDesktopsMenu)
        return;
    auto const vds = win::virtual_desktop_manager::self();

    m_multipleDesktopsMenu->clear();
    if (m_client) {
        m_multipleDesktopsMenu->setPalette(m_client->control->palette().q_palette());
    }

    QAction *action = m_multipleDesktopsMenu->addAction(i18n("&All Desktops"));
    action->setData(QVariant::fromValue(ShowOnDesktopActionData{0, false}));
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

        QAction *action = m_multipleDesktopsMenu->addAction(basic_name.arg(i).arg(vds->name(i).replace(QLatin1Char('&'), QStringLiteral("&&"))));
        action->setData(QVariant::fromValue(ShowOnDesktopActionData{i, false}));
        action->setCheckable(true);
        if (m_client && !m_client->isOnAllDesktops() && m_client->isOnDesktop(i)) {
            action->setChecked(true);
        }
    }

    m_multipleDesktopsMenu->addSeparator();

    for (uint i = 1; i <= vds->count(); ++i) {
        QString name = i18n("Move to %1 %2", i, vds->name(i));
        QAction *action = m_multipleDesktopsMenu->addAction(name);
        action->setData(QVariant::fromValue(ShowOnDesktopActionData{i, true}));
    }

    m_multipleDesktopsMenu->addSeparator();

    bool allowNewDesktops = vds->count() < vds->maximum();
    uint countPlusOne = vds->count() + 1;

    action = m_multipleDesktopsMenu->addAction(i18nc("Create a new desktop and add the window to that desktop", "Add to &New Desktop"));
    action->setData(QVariant::fromValue(ShowOnDesktopActionData{countPlusOne, false}));
    action->setEnabled(allowNewDesktops);

    action = m_multipleDesktopsMenu->addAction(i18nc("Create a new desktop and move the window to that desktop", "Move to New Desktop"));
    action->setData(QVariant::fromValue(ShowOnDesktopActionData{countPlusOne, true}));
    action->setEnabled(allowNewDesktops);
}

void UserActionsMenu::screenPopupAboutToShow()
{
    if (!m_screenMenu) {
        return;
    }
    m_screenMenu->clear();

    if (!m_client) {
        return;
    }

    m_screenMenu->setPalette(m_client->control->palette().q_palette());
    QActionGroup *group = new QActionGroup(m_screenMenu);
    auto const& screens = kwinApp()->get_base().screens;

    for (int i = 0; i< screens.count(); ++i) {
        // assumption: there are not more than 9 screens attached.
        QAction *action = m_screenMenu->addAction(i18nc("@item:inmenu List of all Screens to send a window to. First argument is a number, second the output identifier. E.g. Screen 1 (HDMI1)",
                                                        "Screen &%1 (%2)", (i+1), screens.name(i)));
        action->setData(i);
        action->setCheckable(true);
        if (m_client && i == m_client->screen()) {
            action->setChecked(true);
        }
        group->addAction(action);
    }
}

void UserActionsMenu::slotWindowOperation(QAction *action)
{
    if (!action->data().isValid())
        return;

    Options::WindowOperation op = static_cast< Options::WindowOperation >(action->data().toInt());
    auto c = m_client ? m_client : QPointer<Toplevel>(Workspace::self()->activeClient());
    if (c.isNull())
        return;
    QString type;
    switch(op) {
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
    QMetaObject::invokeMethod(workspace(), "performWindowOperation",
                              Qt::QueuedConnection,
                              Q_ARG(KWin::Toplevel*, c),
                              Q_ARG(Options::WindowOperation, op));
}

void UserActionsMenu::slotSendToDesktop(QAction *action)
{
    bool ok = false;
    uint desk = action->data().toUInt(&ok);
    if (!ok) {
        return;
    }
    if (m_client.isNull())
        return;
    Workspace *ws = Workspace::self();
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

void UserActionsMenu::slotToggleOnVirtualDesktop(QAction *action)
{
    if (m_client.isNull()) {
        return;
    }

    if (!action->data().canConvert<ShowOnDesktopActionData>()) {
        return;
    }
    ShowOnDesktopActionData data = action->data().value<ShowOnDesktopActionData>();

    auto vds = win::virtual_desktop_manager::self();
    if (data.desktop == 0) {
        // the 'on_all_desktops' menu entry
        win::set_on_all_desktops(m_client.data(), !m_client->isOnAllDesktops());
        return;
    } else if (data.desktop > vds->count()) {
        vds->setCount(data.desktop);
    }

    if (data.moveToSingle) {
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

void UserActionsMenu::slotSendToScreen(QAction *action)
{
    const int screen = action->data().toInt();
    if (m_client.isNull()) {
        return;
    }
    if (screen >= kwinApp()->get_base().screens.count()) {
        return;
    }

    Workspace::self()->sendClientToScreen(m_client.data(), screen);
}

//****************************************
// ShortcutDialog
//****************************************
ShortcutDialog::ShortcutDialog(const QKeySequence& cut)
    : _shortcut(cut)
{
    m_ui.setupUi(this);
    m_ui.keySequenceEdit->setKeySequence(cut);
    m_ui.warning->hide();

    // Listen to changed shortcuts
    connect(m_ui.keySequenceEdit, &QKeySequenceEdit::editingFinished, this, &ShortcutDialog::keySequenceChanged);
    connect(m_ui.clearButton, &QToolButton::clicked, [this]{
        _shortcut = QKeySequence();
    });
    m_ui.keySequenceEdit->setFocus();

    setWindowFlags(Qt::Popup | Qt::X11BypassWindowManagerHint);
}

void ShortcutDialog::accept()
{
    QKeySequence seq = shortcut();
    if (!seq.isEmpty()) {
        if (seq[0] == Qt::Key_Escape) {
            reject();
            return;
        }
        if (seq[0] == Qt::Key_Space
        || (seq[0] & Qt::KeyboardModifierMask) == 0) {
            // clear
            m_ui.keySequenceEdit->clear();
            QDialog::accept();
            return;
        }
    }
    QDialog::accept();
}

void ShortcutDialog::done(int r)
{
    QDialog::done(r);
    Q_EMIT dialogDone(r == Accepted);
}

void ShortcutDialog::keySequenceChanged()
{
    activateWindow(); // where is the kbd focus lost? cause of popup state?
    QKeySequence seq = m_ui.keySequenceEdit->keySequence();
    if (_shortcut == seq)
        return; // don't try to update the same

    if (seq.isEmpty()) { // clear
        _shortcut = seq;
        return;
    }
    if (seq.count() > 1) {
        seq = QKeySequence(seq[0]);
        m_ui.keySequenceEdit->setKeySequence(seq);
    }

    // Check if the key sequence is used currently
    QString sc = seq.toString();
    // NOTICE - seq.toString() & the entries in "conflicting" randomly get invalidated after the next call (if no sc has been set & conflicting isn't empty?!)
    QList<KGlobalShortcutInfo> conflicting = KGlobalAccel::getGlobalShortcutsByKey(seq);
    if (!conflicting.isEmpty()) {
        const KGlobalShortcutInfo &conflict = conflicting.at(0);
        m_ui.warning->setText(i18nc("'%1' is a keyboard shortcut like 'ctrl+w'",
        "<b>%1</b> is already in use", sc));
        m_ui.warning->setToolTip(i18nc("keyboard shortcut '%1' is used by action '%2' in application '%3'",
        "<b>%1</b> is used by %2 in %3", sc, conflict.friendlyName(), conflict.componentFriendlyName()));
        m_ui.warning->show();
        m_ui.keySequenceEdit->setKeySequence(shortcut());
    } else if (seq != _shortcut) {
        m_ui.warning->hide();
        if (QPushButton *ok = m_ui.buttonBox->button(QDialogButtonBox::Ok))
            ok->setFocus();
    }

    _shortcut = seq;
}

QKeySequence ShortcutDialog::shortcut() const
{
    return _shortcut;
}

//****************************************
// Workspace
//****************************************

void Workspace::slotIncreaseWindowOpacity()
{
    if (!active_client) {
        return;
    }
    active_client->setOpacity(qMin(active_client->opacity() + 0.05, 1.0));
}

void Workspace::slotLowerWindowOpacity()
{
    if (!active_client) {
        return;
    }
    active_client->setOpacity(qMax(active_client->opacity() - 0.05, 0.05));
}

void Workspace::closeActivePopup()
{
    if (active_popup) {
        active_popup->close();
        active_popup = nullptr;
        active_popup_client = nullptr;
    }
    m_userActionsMenu->close();
}


template <typename Slot>
void Workspace::initShortcut(const QString &actionName, const QString &description, const QKeySequence &shortcut, Slot slot, const QVariant &data)
{
    initShortcut(actionName, description, shortcut, this, slot, data);
}

template <typename T, typename Slot>
void Workspace::initShortcut(const QString &actionName, const QString &description, const QKeySequence &shortcut, T *receiver, Slot slot, const QVariant &data)
{
    QAction *a = new QAction(this);
    a->setProperty("componentName", QStringLiteral(KWIN_NAME));
    a->setObjectName(actionName);
    a->setText(description);
    if (data.isValid()) {
        a->setData(data);
    }
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << shortcut);
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << shortcut);
    kwinApp()->input->redirect->registerShortcut(shortcut, a, receiver, slot);
}

/**
 * Creates the global accel object \c keys.
 */
void Workspace::initShortcuts()
{
// Some shortcuts have Tarzan-speech like names, they need extra
// normal human descriptions with DEF2() the others can use DEF()
// new DEF3 allows to pass data to the action, replacing the %1 argument in the name

#define DEF2( name, descr, key, fnSlot )                            \
    initShortcut(QStringLiteral(name), i18n(descr), key, &Workspace::fnSlot);

#define DEF( name, key, fnSlot )                                    \
    initShortcut(QStringLiteral(name), i18n(name), key, &Workspace::fnSlot);

#define DEF3( name, key, fnSlot, value )                            \
    initShortcut(QStringLiteral(name).arg(value), i18n(name, value), key, &Workspace::fnSlot, value);

#define DEF4( name, descr, key, functor ) \
    initShortcut(QStringLiteral(name), i18n(descr), key, functor);

#define DEF5( name, key, functor, value )                            \
    initShortcut(QStringLiteral(name).arg(value), i18n(name, value), key, functor, value);

#define DEF6( name, key, target, fnSlot )                                    \
    initShortcut(QStringLiteral(name), i18n(name), key, target, &fnSlot);


DEF(I18N_NOOP("Window Operations Menu"),
    Qt::ALT + Qt::Key_F3, slotWindowOperations);
DEF2("Window Close", I18N_NOOP("Close Window"),
     Qt::ALT + Qt::Key_F4, slotWindowClose);
DEF2("Window Maximize", I18N_NOOP("Maximize Window"),
     Qt::META + Qt::Key_PageUp, slotWindowMaximize);
DEF2("Window Maximize Vertical", I18N_NOOP("Maximize Window Vertically"),
     0, slotWindowMaximizeVertical);
DEF2("Window Maximize Horizontal", I18N_NOOP("Maximize Window Horizontally"),
     0, slotWindowMaximizeHorizontal);
DEF2("Window Minimize", I18N_NOOP("Minimize Window"),
     Qt::META + Qt::Key_PageDown, slotWindowMinimize);
DEF2("Window Move", I18N_NOOP("Move Window"),
     0, slotWindowMove);
DEF2("Window Resize", I18N_NOOP("Resize Window"),
     0, slotWindowResize);
DEF2("Window Raise", I18N_NOOP("Raise Window"),
     0, slotWindowRaise);
DEF2("Window Lower", I18N_NOOP("Lower Window"),
     0, slotWindowLower);
DEF(I18N_NOOP("Toggle Window Raise/Lower"),
    0, slotWindowRaiseOrLower);
DEF2("Window Fullscreen", I18N_NOOP("Make Window Fullscreen"),
     0, slotWindowFullScreen);
DEF2("Window No Border", I18N_NOOP("Hide Window Border"),
     0, slotWindowNoBorder);
DEF2("Window Above Other Windows", I18N_NOOP("Keep Window Above Others"),
     0, slotWindowAbove);
DEF2("Window Below Other Windows", I18N_NOOP("Keep Window Below Others"),
     0, slotWindowBelow);
DEF(I18N_NOOP("Activate Window Demanding Attention"),
    Qt::CTRL + Qt::ALT + Qt::Key_A, slotActivateAttentionWindow);
DEF(I18N_NOOP("Setup Window Shortcut"),
    0, slotSetupWindowShortcut);
DEF2("Window Pack Right", I18N_NOOP("Pack Window to the Right"),
     0, slotWindowPackRight);
DEF2("Window Pack Left", I18N_NOOP("Pack Window to the Left"),
     0, slotWindowPackLeft);
DEF2("Window Pack Up", I18N_NOOP("Pack Window Up"),
     0, slotWindowPackUp);
DEF2("Window Pack Down", I18N_NOOP("Pack Window Down"),
     0, slotWindowPackDown);
DEF2("Window Grow Horizontal", I18N_NOOP("Pack Grow Window Horizontally"),
     0, slotWindowGrowHorizontal);
DEF2("Window Grow Vertical", I18N_NOOP("Pack Grow Window Vertically"),
     0, slotWindowGrowVertical);
DEF2("Window Shrink Horizontal", I18N_NOOP("Pack Shrink Window Horizontally"),
     0, slotWindowShrinkHorizontal);
DEF2("Window Shrink Vertical", I18N_NOOP("Pack Shrink Window Vertically"),
     0, slotWindowShrinkVertical);
DEF4("Window Quick Tile Left", I18N_NOOP("Quick Tile Window to the Left"),
     Qt::META + Qt::Key_Left, std::bind(&Workspace::quickTileWindow, this, win::quicktiles::left));
DEF4("Window Quick Tile Right", I18N_NOOP("Quick Tile Window to the Right"),
     Qt::META + Qt::Key_Right, std::bind(&Workspace::quickTileWindow, this, win::quicktiles::right));
DEF4("Window Quick Tile Top", I18N_NOOP("Quick Tile Window to the Top"),
     Qt::META + Qt::Key_Up, std::bind(&Workspace::quickTileWindow, this, win::quicktiles::top));
DEF4("Window Quick Tile Bottom", I18N_NOOP("Quick Tile Window to the Bottom"),
     Qt::META + Qt::Key_Down, std::bind(&Workspace::quickTileWindow, this, win::quicktiles::bottom));
DEF4("Window Quick Tile Top Left", I18N_NOOP("Quick Tile Window to the Top Left"),
     0, std::bind(&Workspace::quickTileWindow, this, win::quicktiles::top | win::quicktiles::left));
DEF4("Window Quick Tile Bottom Left", I18N_NOOP("Quick Tile Window to the Bottom Left"),
     0, std::bind(&Workspace::quickTileWindow, this, win::quicktiles::bottom | win::quicktiles::left));
DEF4("Window Quick Tile Top Right", I18N_NOOP("Quick Tile Window to the Top Right"),
     0, std::bind(&Workspace::quickTileWindow, this, win::quicktiles::top | win::quicktiles::right));
DEF4("Window Quick Tile Bottom Right", I18N_NOOP("Quick Tile Window to the Bottom Right"),
     0, std::bind(&Workspace::quickTileWindow, this, win::quicktiles::bottom | win::quicktiles::right));
DEF4("Switch Window Up", I18N_NOOP("Switch to Window Above"),
     Qt::META + Qt::ALT + Qt::Key_Up, std::bind(static_cast<void (Workspace::*)(Direction)>(&Workspace::switchWindow), this, DirectionNorth));
DEF4("Switch Window Down", I18N_NOOP("Switch to Window Below"),
     Qt::META + Qt::ALT + Qt::Key_Down, std::bind(static_cast<void (Workspace::*)(Direction)>(&Workspace::switchWindow), this, DirectionSouth));
DEF4("Switch Window Right", I18N_NOOP("Switch to Window to the Right"),
     Qt::META + Qt::ALT + Qt::Key_Right, std::bind(static_cast<void (Workspace::*)(Direction)>(&Workspace::switchWindow), this, DirectionEast));
DEF4("Switch Window Left", I18N_NOOP("Switch to Window to the Left"),
     Qt::META + Qt::ALT + Qt::Key_Left, std::bind(static_cast<void (Workspace::*)(Direction)>(&Workspace::switchWindow), this, DirectionWest));
DEF2("Increase Opacity", I18N_NOOP("Increase Opacity of Active Window by 5 %"),
    0, slotIncreaseWindowOpacity);
DEF2("Decrease Opacity", I18N_NOOP("Decrease Opacity of Active Window by 5 %"),
    0, slotLowerWindowOpacity);

DEF2("Window On All Desktops", I18N_NOOP("Keep Window on All Desktops"),
     0, slotWindowOnAllDesktops);

for (int i = 1; i < 21; ++i) {
    DEF5(I18N_NOOP("Window to Desktop %1"),        0, std::bind(&Workspace::slotWindowToDesktop, this, i), i);
}
DEF(I18N_NOOP("Window to Next Desktop"),           0, slotWindowToNextDesktop);
DEF(I18N_NOOP("Window to Previous Desktop"),       0, slotWindowToPreviousDesktop);
DEF(I18N_NOOP("Window One Desktop to the Right"),  0, slotWindowToDesktopRight);
DEF(I18N_NOOP("Window One Desktop to the Left"),   0, slotWindowToDesktopLeft);
DEF(I18N_NOOP("Window One Desktop Up"),            0, slotWindowToDesktopUp);
DEF(I18N_NOOP("Window One Desktop Down"),          0, slotWindowToDesktopDown);

for (int i = 0; i < 8; ++i) {
    DEF3(I18N_NOOP("Window to Screen %1"),         0, slotWindowToScreen, i);
}
DEF(I18N_NOOP("Window to Next Screen"),            0, slotWindowToNextScreen);
DEF(I18N_NOOP("Window to Previous Screen"),        0, slotWindowToPrevScreen);
DEF(I18N_NOOP("Show Desktop"),                     Qt::META + Qt::Key_D, slotToggleShowDesktop);

for (int i = 0; i < 8; ++i) {
    DEF3(I18N_NOOP("Switch to Screen %1"),         0, slotSwitchToScreen, i);
}

DEF(I18N_NOOP("Switch to Next Screen"),            0, slotSwitchToNextScreen);
DEF(I18N_NOOP("Switch to Previous Screen"),        0, slotSwitchToPrevScreen);

DEF(I18N_NOOP("Kill Window"),                      Qt::CTRL + Qt::ALT + Qt::Key_Escape, slotKillWindow);
DEF6(I18N_NOOP("Suspend Compositing"),             Qt::SHIFT + Qt::ALT + Qt::Key_F12,
                                                   render::compositor::self(),
                                                   render::compositor::toggleCompositing);
DEF6(I18N_NOOP("Invert Screen Colors"),            0, kwinApp()->get_base().render.get(), render::platform::invertScreen);

#undef DEF
#undef DEF2
#undef DEF3
#undef DEF4
#undef DEF5
#undef DEF6

#ifdef KWIN_BUILD_TABBOX
    TabBox::TabBox::self()->initShortcuts();
#endif
    win::virtual_desktop_manager::self()->initShortcuts();
    kwinApp()->get_base().render->night_color->init_shortcuts();
    m_userActionsMenu->discard(); // so that it's recreated next time
}

void Workspace::setupWindowShortcut(Toplevel* window)
{
    Q_ASSERT(client_keys_dialog == nullptr);
    // TODO: PORT ME (KGlobalAccel related)
    //keys->setEnabled( false );
    //disable_shortcuts_keys->setEnabled( false );
    //client_keys->setEnabled( false );
    client_keys_dialog = new ShortcutDialog(window->control->shortcut());
    client_keys_client = window;

    connect(client_keys_dialog, &ShortcutDialog::dialogDone, this, &Workspace::setupWindowShortcutDone);

    auto area = clientArea(ScreenArea, window);
    auto size = client_keys_dialog->sizeHint();

    auto pos = win::frame_to_client_pos(window, window->pos());
    if (pos.x() + size.width() >= area.right()) {
        pos.setX(area.right() - size.width());
    }
    if (pos.y() + size.height() >= area.bottom()) {
        pos.setY(area.bottom() - size.height());
    }

    client_keys_dialog->move(pos);
    client_keys_dialog->show();
    active_popup = client_keys_dialog;
    active_popup_client = window;
}

void Workspace::setupWindowShortcutDone(bool ok)
{
//    keys->setEnabled( true );
//    disable_shortcuts_keys->setEnabled( true );
//    client_keys->setEnabled( true );
    if (ok)
        win::set_shortcut(client_keys_client, client_keys_dialog->shortcut().toString());
    closeActivePopup();
    client_keys_dialog->deleteLater();
    client_keys_dialog = nullptr;
    client_keys_client = nullptr;
    if (active_client)
        active_client->takeFocus();
}

void Workspace::clientShortcutUpdated(Toplevel* window)
{
    QString key = QStringLiteral("_k_session:%1").arg(window->xcb_window());
    QAction* action = findChild<QAction*>(key);
    if (!window->control->shortcut().isEmpty()) {
        if (action == nullptr) { // new shortcut
            action = new QAction(this);
            kwinApp()->input->setup_action_for_global_accel(action);
            action->setProperty("componentName", QStringLiteral(KWIN_NAME));
            action->setObjectName(key);
            action->setText(i18n("Activate Window (%1)", win::caption(window)));
            connect(action, &QAction::triggered,
                    window, std::bind(&Workspace::activateClient, this, window, true));
        }

        // no autoloading, since it's configured explicitly here and is not meant to be reused
        // (the key is the window id anyway, which is kind of random)
        KGlobalAccel::self()->setShortcut(action,
                                          QList<QKeySequence>() << window->control->shortcut(),
                                          KGlobalAccel::NoAutoloading);
        action->setEnabled(true);
    } else {
        KGlobalAccel::self()->removeAllShortcuts(action);
        delete action;
    }
}

void Workspace::performWindowOperation(Toplevel* window, Options::WindowOperation op)
{
    if (!window) {
        return;
    }

    auto cursor = input::get_cursor();

    if (op == Options::MoveOp || op == Options::UnrestrictedMoveOp) {
        cursor->set_pos(window->frameGeometry().center());
    }
    if (op == Options::ResizeOp || op == Options::UnrestrictedResizeOp) {
        cursor->set_pos(window->frameGeometry().bottomRight());
    }

    switch(op) {
    case Options::MoveOp:
        window->performMouseCommand(Options::MouseMove, cursor->pos());
        break;
    case Options::UnrestrictedMoveOp:
        window->performMouseCommand(Options::MouseUnrestrictedMove, cursor->pos());
        break;
    case Options::ResizeOp:
        window->performMouseCommand(Options::MouseResize, cursor->pos());
        break;
    case Options::UnrestrictedResizeOp:
        window->performMouseCommand(Options::MouseUnrestrictedResize, cursor->pos());
        break;
    case Options::CloseOp:
        QMetaObject::invokeMethod(window, "closeWindow", Qt::QueuedConnection);
        break;
    case Options::MaximizeOp:
        win::maximize(window, window->maximizeMode() == win::maximize_mode::full
                      ? win::maximize_mode::restore : win::maximize_mode::full);
        break;
    case Options::HMaximizeOp:
        win::maximize(window, window->maximizeMode() ^ win::maximize_mode::horizontal);
        break;
    case Options::VMaximizeOp:
        win::maximize(window, window->maximizeMode() ^ win::maximize_mode::vertical);
        break;
    case Options::RestoreOp:
        win::maximize(window, win::maximize_mode::restore);
        break;
    case Options::MinimizeOp:
        win::set_minimized(window, true);
        break;
    case Options::OnAllDesktopsOp:
        win::set_on_all_desktops(window, !window->isOnAllDesktops());
        break;
    case Options::FullScreenOp:
        window->setFullScreen(!window->control->fullscreen(), true);
        break;
    case Options::NoBorderOp:
        window->setNoBorder(!window->noBorder());
        break;
    case Options::KeepAboveOp: {
        Blocker blocker(stacking_order);
        bool was = window->control->keep_above();
        win::set_keep_above(window, !window->control->keep_above());
        if (was && !window->control->keep_above()) {
            win::raise_window(this, window);
        }
        break;
    }
    case Options::KeepBelowOp: {
        Blocker blocker(stacking_order);
        bool was = window->control->keep_below();
        win::set_keep_below(window, !window->control->keep_below());
        if (was && !window->control->keep_below()) {
            win::lower_window(workspace(), window);
        }
        break;
    }
    case Options::WindowRulesOp:
        RuleBook::self()->edit(window, false);
        break;
    case Options::ApplicationRulesOp:
        RuleBook::self()->edit(window, true);
        break;
    case Options::SetupWindowShortcutOp:
        setupWindowShortcut(window);
        break;
    case Options::LowerOp:
        win::lower_window(workspace(), window);
        break;
    case Options::OperationsOp:
    case Options::NoOp:
        break;
    }
}

void Workspace::slotActivateAttentionWindow()
{
    if (attention_chain.size() > 0) {
        activateClient(attention_chain.front());
    }
}

static uint senderValue(QObject *sender)
{
    QAction *act = qobject_cast<QAction*>(sender);
    bool ok = false; uint i = -1;
    if (act)
        i = act->data().toUInt(&ok);
    if (ok)
        return i;
    return -1;
}

#define USABLE_ACTIVE_CLIENT (active_client && !(win::is_desktop(active_client) || win::is_dock(active_client)))

void Workspace::slotWindowToDesktop(uint i)
{
    if (USABLE_ACTIVE_CLIENT) {
        if (i < 1)
            return;

        if (i >= 1 && i <= win::virtual_desktop_manager::self()->count())
            sendClientToDesktop(active_client, i, true);
    }
}

static bool screenSwitchImpossible()
{
    if (!kwinApp()->get_base().screens.isCurrentFollowsMouse())
        return false;
    QStringList args;
    args << QStringLiteral("--passivepopup") << i18n("The window manager is configured to consider the screen with the mouse on it as active one.\n"
                                     "Therefore it is not possible to switch to a screen explicitly.") << QStringLiteral("20");
    KProcess::startDetached(QStringLiteral("kdialog"), args);
    return true;
}

void Workspace::slotSwitchToScreen()
{
    if (screenSwitchImpossible())
        return;
    const int i = senderValue(sender());
    if (i > -1)
        setCurrentScreen(i);
}

void Workspace::slotSwitchToNextScreen()
{
    if (screenSwitchImpossible()) {
        return;
    }
    auto const& screens = kwinApp()->get_base().screens;
    setCurrentScreen((screens.current() + 1) % screens.count());
}

void Workspace::slotSwitchToPrevScreen()
{
    if (screenSwitchImpossible()) {
        return;
    }
    auto const& screens = kwinApp()->get_base().screens;
    setCurrentScreen((screens.current() + screens.count() - 1) % screens.count());
}

void Workspace::slotWindowToScreen()
{
    if (USABLE_ACTIVE_CLIENT) {
        const int i = senderValue(sender());
        if (i < 0)
            return;
        if (i >= 0 && i <= kwinApp()->get_base().screens.count()) {
            sendClientToScreen(active_client, i);
        }
    }
}

void Workspace::slotWindowToNextScreen()
{
    if (USABLE_ACTIVE_CLIENT) {
        sendClientToScreen(active_client, (active_client->screen() + 1) % kwinApp()->get_base().screens.count());
    }
}

void Workspace::slotWindowToPrevScreen()
{
    if (USABLE_ACTIVE_CLIENT) {
        auto const& screens = kwinApp()->get_base().screens;
        sendClientToScreen(active_client, (active_client->screen() + screens.count() - 1) % screens.count());
    }
}

/**
 * Maximizes the active client.
 */
void Workspace::slotWindowMaximize()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::MaximizeOp);
}

/**
 * Maximizes the active client vertically.
 */
void Workspace::slotWindowMaximizeVertical()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::VMaximizeOp);
}

/**
 * Maximizes the active client horiozontally.
 */
void Workspace::slotWindowMaximizeHorizontal()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::HMaximizeOp);
}


/**
 * Minimizes the active client.
 */
void Workspace::slotWindowMinimize()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::MinimizeOp);
}

/**
 * Raises the active client.
 */
void Workspace::slotWindowRaise()
{
    if (USABLE_ACTIVE_CLIENT) {
        win::raise_window(this, active_client);
    }
}

/**
 * Lowers the active client.
 */
void Workspace::slotWindowLower()
{
    if (USABLE_ACTIVE_CLIENT) {
        win::lower_window(workspace(), active_client);
        // As this most likely makes the window no longer visible change the
        // keyboard focus to the next available window.
        //activateNextClient( c ); // Doesn't work when we lower a child window
        if (active_client->control->active() && options->focusPolicyIsReasonable()) {
            if (options->isNextFocusPrefersMouse()) {
                auto next = clientUnderMouse(active_client->screen());
                if (next && next != active_client)
                    request_focus(next);
            } else {
                activateClient(
                    win::top_client_on_desktop(workspace(), win::virtual_desktop_manager::self()->current(), -1));
            }
        }
    }
}

/**
 * Does a toggle-raise-and-lower on the active client.
 */
void Workspace::slotWindowRaiseOrLower()
{
    if (USABLE_ACTIVE_CLIENT)
        win::raise_or_lower_client(workspace(), active_client);
}

void Workspace::slotWindowOnAllDesktops()
{
    if (USABLE_ACTIVE_CLIENT)
        win::set_on_all_desktops(active_client, !active_client->isOnAllDesktops());
}

void Workspace::slotWindowFullScreen()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::FullScreenOp);
}

void Workspace::slotWindowNoBorder()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::NoBorderOp);
}

void Workspace::slotWindowAbove()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::KeepAboveOp);
}

void Workspace::slotWindowBelow()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::KeepBelowOp);
}
void Workspace::slotSetupWindowShortcut()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::SetupWindowShortcutOp);
}

/**
 * Toggles show desktop.
 */
void Workspace::slotToggleShowDesktop()
{
    setShowingDesktop(!showingDesktop());
}

template <typename Direction>
void windowToDesktop(Toplevel* window)
{
    auto vds = win::virtual_desktop_manager::self();
    Workspace *ws = Workspace::self();
    Direction functor;
    // TODO: why is options->isRollOverDesktops() not honored?
    const auto desktop = functor(nullptr, true);
    if (window && !win::is_desktop(window) && !win::is_dock(window)) {
        ws->setMoveResizeClient(window);
        vds->setCurrent(desktop);
        ws->setMoveResizeClient(nullptr);
    }
}

/**
 * Moves the active client to the next desktop.
 */
void Workspace::slotWindowToNextDesktop()
{
    if (USABLE_ACTIVE_CLIENT)
        windowToNextDesktop(active_client);
}

void Workspace::windowToNextDesktop(Toplevel* window)
{
    windowToDesktop<win::virtual_desktop_next>(window);
}

/**
 * Moves the active client to the previous desktop.
 */
void Workspace::slotWindowToPreviousDesktop()
{
    if (USABLE_ACTIVE_CLIENT)
        windowToPreviousDesktop(active_client);
}

void Workspace::windowToPreviousDesktop(Toplevel* window)
{
    windowToDesktop<win::virtual_desktop_previous>(window);
}

template <typename Direction>
void activeClientToDesktop()
{
    auto vds = win::virtual_desktop_manager::self();
    Workspace *ws = Workspace::self();
    const int current = vds->current();
    Direction functor;
    const int d = functor(current, options->isRollOverDesktops());
    if (d == current) {
        return;
    }
    ws->setMoveResizeClient(ws->activeClient());
    vds->setCurrent(d);
    ws->setMoveResizeClient(nullptr);
}

void Workspace::slotWindowToDesktopRight()
{
    if (USABLE_ACTIVE_CLIENT) {
        activeClientToDesktop<win::virtual_desktop_right>();
    }
}

void Workspace::slotWindowToDesktopLeft()
{
    if (USABLE_ACTIVE_CLIENT) {
        activeClientToDesktop<win::virtual_desktop_left>();
    }
}

void Workspace::slotWindowToDesktopUp()
{
    if (USABLE_ACTIVE_CLIENT) {
        activeClientToDesktop<win::virtual_desktop_above>();
    }
}

void Workspace::slotWindowToDesktopDown()
{
    if (USABLE_ACTIVE_CLIENT) {
        activeClientToDesktop<win::virtual_desktop_below>();
    }
}

/**
 * Kill Window feature, similar to xkill.
 */
void Workspace::slotKillWindow()
{
    if (m_windowKiller.isNull()) {
        m_windowKiller.reset(new KillWindow());
    }
    m_windowKiller->start();
}

/**
 * Switches to the nearest window in given direction.
 */
void Workspace::switchWindow(Direction direction)
{
    if (!active_client)
        return;
    auto c = active_client;
    int desktopNumber = c->isOnAllDesktops() ? win::virtual_desktop_manager::self()->current() : c->desktop();

    // Centre of the active window
    QPoint curPos(c->pos().x() + c->size().width() / 2,
                  c->pos().y() + c->size().height() / 2);

    if (!switchWindow(c, direction, curPos, desktopNumber)) {
        auto opposite = [&] {
            switch(direction) {
            case DirectionNorth:
                return QPoint(curPos.x(), kwinApp()->get_base().screens.geometry().height());
            case DirectionSouth:
                return QPoint(curPos.x(), 0);
            case DirectionEast:
                return QPoint(0, curPos.y());
            case DirectionWest:
                return QPoint(kwinApp()->get_base().screens.geometry().width(), curPos.y());
            default:
                Q_UNREACHABLE();
            }
        };

        switchWindow(c, direction, opposite(), desktopNumber);
    }
}

bool Workspace::switchWindow(Toplevel *c, Direction direction, QPoint curPos, int d)
{
    Toplevel* switchTo = nullptr;
    int bestScore = 0;

    auto clist = stacking_order->sorted();
    for (auto i = clist.rbegin(); i != clist.rend(); ++i) {
        auto client = *i;
        if (!client->control) {
            continue;
        }
        if (win::wants_tab_focus(client) && *i != c &&
                client->isOnDesktop(d) && !client->control->minimized()) {
            // Centre of the other window
            const QPoint other(client->pos().x() + client->size().width() / 2,
                               client->pos().y() + client->size().height() / 2);

            int distance;
            int offset;
            switch(direction) {
            case DirectionNorth:
                distance = curPos.y() - other.y();
                offset = qAbs(other.x() - curPos.x());
                break;
            case DirectionEast:
                distance = other.x() - curPos.x();
                offset = qAbs(other.y() - curPos.y());
                break;
            case DirectionSouth:
                distance = other.y() - curPos.y();
                offset = qAbs(other.x() - curPos.x());
                break;
            case DirectionWest:
                distance = curPos.x() - other.x();
                offset = qAbs(other.y() - curPos.y());
                break;
            default:
                distance = -1;
                offset = -1;
            }

            if (distance > 0) {
                // Inverse score
                int score = distance + offset + ((offset * offset) / distance);
                if (score < bestScore || !switchTo) {
                    switchTo = client;
                    bestScore = score;
                }
            }
        }
    }
    if (switchTo) {
        activateClient(switchTo);
    }

    return switchTo;
}

/**
 * Shows the window operations popup menu for the active client.
 */
void Workspace::slotWindowOperations()
{
    if (!active_client)
        return;
    auto pos = win::frame_to_client_pos(active_client, active_client->pos());
    showWindowMenu(QRect(pos, pos), active_client);
}

void Workspace::showWindowMenu(const QRect &pos, Toplevel* window)
{
    m_userActionsMenu->show(pos, window);
}

void Workspace::showApplicationMenu(const QRect &pos, Toplevel* window, int actionId)
{
    win::app_menu::self()->showApplicationMenu(window->pos() + pos.bottomLeft(), window, actionId);
}

/**
 * Closes the active client.
 */
void Workspace::slotWindowClose()
{
    // TODO: why?
//     if ( tab_box->isVisible())
//         return;
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::CloseOp);
}

/**
 * Starts keyboard move mode for the active client.
 */
void Workspace::slotWindowMove()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::UnrestrictedMoveOp);
}

/**
 * Starts keyboard resize mode for the active client.
 */
void Workspace::slotWindowResize()
{
    if (USABLE_ACTIVE_CLIENT)
        performWindowOperation(active_client, Options::UnrestrictedResizeOp);
}

#undef USABLE_ACTIVE_CLIENT

void Toplevel::setShortcutInternal()
{
    updateCaption();
    workspace()->clientShortcutUpdated(this);
}

bool Workspace::shortcutAvailable(const QKeySequence &cut, Toplevel* ignore) const
{
    if (ignore && cut == ignore->control->shortcut())
        return true;

    if (!KGlobalAccel::getGlobalShortcutsByKey(cut).isEmpty()) {
        return false;
    }
    for (auto const& client : m_allClients) {
        if (client != ignore && client->control->shortcut() == cut)
            return false;
    }
    return true;
}

} // namespace

Q_DECLARE_METATYPE(KWin::ShowOnDesktopActionData);
