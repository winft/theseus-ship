/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config-kwin.h"

#include "desktop_space.h"
#include "move.h"
#include "net.h"
#include "screen.h"
#include "window_operation.h"

#include "base/logging.h"
#include "main.h"
#include "scripting/platform.h"
#include "toplevel.h"

#if KWIN_BUILD_TABBOX
#include "tabbox/tabbox.h"
#endif

#include <KAuthorized>
#include <KConfig>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KProcess>
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QRect>
#include <QWindow>
#include <QtConcurrentRun>

namespace KWin::win
{

template<typename Space>
void perform_window_operation(Space& space, Toplevel* window, base::options::WindowOperation op);

struct user_actions_menu_desktop_action_data {
    uint desktop;
    bool move_to_single;
};

/**
 * @brief Menu shown for a Client.
 *
 * The user_actions_menu implements the Menu which is shown on:
 * @li context-menu event on Window decoration
 * @li window menu button
 * @li Keyboard Shortcut (by default Alt+F3)
 *
 * The menu contains various window management related actions for the Client the menu is opened
 * for, this is normally the active Client.
 *
 * The menu which is shown is tried to be as close as possible to the menu implemented in
 * libtaskmanager, though there are differences as there are some actions only the window manager
 * can provide and on the other hand the libtaskmanager cares also about things like e.g. grouping.
 *
 * Whenever the menu is changed it should be tried to also adjust the menu in libtaskmanager.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 */
template<typename Space>
class user_actions_menu
{
public:
    explicit user_actions_menu(Space& space)
        : qobject{std::make_unique<QObject>()}
        , space{space}
    {
    }
    ~user_actions_menu()
    {
        discard();
    }

    /**
     * Discards the constructed menu, so that it gets recreates
     * on next show event.
     * @see show
     */
    void discard()
    {
        delete m_menu;
        m_menu = nullptr;
        m_desktopMenu = nullptr;
        m_multipleDesktopsMenu = nullptr;
        m_screenMenu = nullptr;
        m_scriptsMenu = nullptr;
    }

    /**
     * @returns Whether the menu is currently visible
     */
    bool isShown() const
    {
        return m_menu && m_menu->isVisible();
    }

    /**
     * grabs keyboard and mouse, workaround(?) for bug #351112
     */
    void grabInput()
    {
        m_menu->windowHandle()->setMouseGrabEnabled(true);
        m_menu->windowHandle()->setKeyboardGrabEnabled(true);
    }

    /**
     * @returns Whether the menu has a Client set to operate on.
     */
    bool hasClient() const
    {
        return m_client && isShown();
    }

    /**
     * Checks whether the given Client @p c is the Client
     * for which the Menu is shown.
     * @param c The Client to compare
     * @returns Whether the Client is the one related to this Menu
     */
    bool isMenuClient(Toplevel const* window) const
    {
        return window && window == m_client;
    }

    /**
     * Closes the Menu and prepares it for next usage.
     */
    void close()
    {
        if (!m_menu) {
            return;
        }
        m_menu->close();
        m_client.clear();
    }

    /**
     * @brief Shows the menu at the given @p pos for the given @p client.
     *
     * @param pos The position where the menu should be shown.
     * @param client The Client for which the Menu has to be shown.
     */
    void show(const QRect& pos, Toplevel* window)
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

private:
    /**
     * The menu will become visible soon.
     *
     * Adjust the items according to the respective Client.
     */
    void handle_menu_about_to_show()
    {
        if (m_client.isNull() || !m_menu)
            return;

        if (space.virtual_desktop_manager->count() == 1) {
            delete m_desktopMenu;
            m_desktopMenu = nullptr;
            delete m_multipleDesktopsMenu;
            m_multipleDesktopsMenu = nullptr;
        } else {
            initDesktopPopup();
        }

        if (kwinApp()->get_base().get_outputs().size() == 1
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
        m_shortcutOperation->setEnabled(
            m_client->control->rules().checkShortcut(QString()).isNull());

        // drop the existing scripts menu
        delete m_scriptsMenu;
        m_scriptsMenu = nullptr;
        // ask scripts whether they want to add entries for the given Client
        auto scriptActions
            = space.scripting->actionsForUserActionMenu(m_client.data(), m_scriptsMenu);
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

    /**
     * Adjusts the desktop popup to the current values and the location of
     * the Client.
     */
    void handle_desktop_popup_about_to_show()
    {
        if (!m_desktopMenu)
            return;
        auto const vds = space.virtual_desktop_manager.get();

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
            action = m_desktopMenu->addAction(basic_name.arg(i).arg(
                vds->name(i).replace(QLatin1Char('&'), QStringLiteral("&&"))));
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

    /**
     * Adjusts the multipleDesktopsMenu popup to the current values and the location of
     * the Client, Wayland only.
     */
    void handle_multiple_desktops_popup_about_to_show()
    {
        if (!m_multipleDesktopsMenu)
            return;
        auto const vds = space.virtual_desktop_manager.get();

        m_multipleDesktopsMenu->clear();
        if (m_client) {
            m_multipleDesktopsMenu->setPalette(m_client->control->palette().q_palette());
        }

        QAction* action = m_multipleDesktopsMenu->addAction(i18n("&All Desktops"));
        action->setData(QVariant::fromValue(user_actions_menu_desktop_action_data{0, false}));
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

            QAction* action = m_multipleDesktopsMenu->addAction(basic_name.arg(i).arg(
                vds->name(i).replace(QLatin1Char('&'), QStringLiteral("&&"))));
            action->setData(QVariant::fromValue(user_actions_menu_desktop_action_data{i, false}));
            action->setCheckable(true);
            if (m_client && !m_client->isOnAllDesktops() && m_client->isOnDesktop(i)) {
                action->setChecked(true);
            }
        }

        m_multipleDesktopsMenu->addSeparator();

        for (uint i = 1; i <= vds->count(); ++i) {
            QString name = i18n("Move to %1 %2", i, vds->name(i));
            QAction* action = m_multipleDesktopsMenu->addAction(name);
            action->setData(QVariant::fromValue(user_actions_menu_desktop_action_data{i, true}));
        }

        m_multipleDesktopsMenu->addSeparator();

        bool allowNewDesktops = vds->count() < vds->maximum();
        uint countPlusOne = vds->count() + 1;

        action = m_multipleDesktopsMenu->addAction(i18nc(
            "Create a new desktop and add the window to that desktop", "Add to &New Desktop"));
        action->setData(
            QVariant::fromValue(user_actions_menu_desktop_action_data{countPlusOne, false}));
        action->setEnabled(allowNewDesktops);

        action = m_multipleDesktopsMenu->addAction(i18nc(
            "Create a new desktop and move the window to that desktop", "Move to New Desktop"));
        action->setData(
            QVariant::fromValue(user_actions_menu_desktop_action_data{countPlusOne, true}));
        action->setEnabled(allowNewDesktops);
    }

    /**
     * Adjusts the screen popup to the current values and the location of
     * the Client.
     */
    void handle_screen_popup_about_to_show()
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
        auto const& outputs = kwinApp()->get_base().get_outputs();

        for (size_t i = 0; i < outputs.size(); ++i) {
            // assumption: there are not more than 9 screens attached.
            QAction* action = m_screenMenu->addAction(
                i18nc("@item:inmenu List of all Screens to send a window to. First argument is a "
                      "number, second the output identifier. E.g. Screen 1 (HDMI1)",
                      "Screen &%1 (%2)",
                      (i + 1),
                      outputs.at(i)->name()));
            action->setData(static_cast<int>(i));
            action->setCheckable(true);
            if (m_client && outputs.at(i) == m_client->central_output) {
                action->setChecked(true);
            }
            group->addAction(action);
        }
    }

    /**
     * Sends the client to desktop \a desk
     *
     * @param action Invoked Action containing the Desktop as data element
     */
    void send_to_desktop(QAction* action)
    {
        bool ok = false;
        uint desk = action->data().toUInt(&ok);
        if (!ok) {
            return;
        }
        if (m_client.isNull()) {
            return;
        }

        auto& vds = space.virtual_desktop_manager;
        if (desk == 0) {
            // the 'on_all_desktops' menu entry
            if (m_client) {
                win::set_on_all_desktops(m_client.data(), !m_client->isOnAllDesktops());
            }
            return;
        } else if (desk > vds->count()) {
            vds->setCount(desk);
        }

        send_window_to_desktop(space, m_client.data(), desk, false);
    }

    /**
     * Toggle whether the Client is on a desktop (Wayland only)
     *
     * @param action Invoked Action containing the Desktop as data element
     */
    void toggle_on_desktop(QAction* action)
    {
        if (m_client.isNull()) {
            return;
        }

        if (!action->data().canConvert<user_actions_menu_desktop_action_data>()) {
            return;
        }
        auto data = action->data().value<user_actions_menu_desktop_action_data>();

        auto vds = space.virtual_desktop_manager.get();
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
            auto virtualDesktop = vds->desktopForX11Id(data.desktop);
            if (m_client->desktops().contains(virtualDesktop)) {
                win::leave_desktop(m_client.data(), virtualDesktop);
            } else {
                win::enter_desktop(m_client.data(), virtualDesktop);
            }
        }
    }

    /**
     * Sends the Client to screen \a screen
     *
     * @param action Invoked Action containing the Screen as data element
     */
    void send_to_screen(QAction* action)
    {
        size_t const screen = action->data().toInt();
        if (m_client.isNull()) {
            return;
        }

        auto output = base::get_output(kwinApp()->get_base().get_outputs(), screen);
        if (!output) {
            return;
        }

        win::send_to_screen(space, m_client.data(), *output);
    }

    /**
     * Performs a window operation.
     *
     * @param action Invoked Action containing the Window Operation to perform for the Client
     */
    void perform_window_operation(QAction* action)
    {
        if (!action->data().isValid())
            return;

        auto op = static_cast<base::options::WindowOperation>(action->data().toInt());
        auto c = m_client ? m_client : QPointer<Toplevel>(space.active_client);
        if (c.isNull())
            return;
        QString type;
        switch (op) {
        case base::options::FullScreenOp:
            if (!c->control->fullscreen() && c->userCanSetFullScreen())
                type = QStringLiteral("fullscreenaltf3");
            break;
        case base::options::NoBorderOp:
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
        qRegisterMetaType<base::options::WindowOperation>();
        QMetaObject::invokeMethod(space.qobject.get(), [&space = this->space, c, op] {
            win::perform_window_operation(space, c, op);
        });
    }

    /// Creates the menu if not already created.
    void init()
    {
        auto configModules = [](bool controlCenter) -> QStringList {
            QStringList args;
            args << QStringLiteral("kwindecoration");
            if (controlCenter)
                args << QStringLiteral("kwinoptions");
            else if (KAuthorized::authorizeControlModule(QStringLiteral("kde-kwinoptions.desktop")))
                args << QStringLiteral("kwinactions") << QStringLiteral("kwinfocus")
                     << QStringLiteral("kwinmoving") << QStringLiteral("kwinadvanced")
                     << QStringLiteral("kwinrules") << QStringLiteral("kwincompositing")
                     << QStringLiteral("kwineffects")
#if KWIN_BUILD_TABBOX
                     << QStringLiteral("kwintabbox")
#endif
                     << QStringLiteral("kwinscreenedges") << QStringLiteral("kwinscripts");
            return args;
        };

        if (m_menu) {
            return;
        }
        m_menu = new QMenu;
        QObject::connect(
            m_menu, &QMenu::aboutToShow, qobject.get(), [this] { handle_menu_about_to_show(); });
        QObject::connect(
            m_menu,
            &QMenu::triggered,
            qobject.get(),
            [this](auto action) { perform_window_operation(action); },
            Qt::QueuedConnection);

        QMenu* advancedMenu = new QMenu(m_menu);
        QObject::connect(advancedMenu, &QMenu::aboutToShow, qobject.get(), [this, advancedMenu]() {
            if (m_client) {
                advancedMenu->setPalette(m_client->control->palette().q_palette());
            }
        });

        auto setShortcut = [this](QAction* action, const QString& actionName) {
            const auto shortcuts = KGlobalAccel::self()->shortcut(
                space.qobject->template findChild<QAction*>(actionName));
            if (!shortcuts.isEmpty()) {
                action->setShortcut(shortcuts.first());
            }
        };

        m_moveOperation = advancedMenu->addAction(i18n("&Move"));
        m_moveOperation->setIcon(QIcon::fromTheme(QStringLiteral("transform-move")));
        setShortcut(m_moveOperation, QStringLiteral("Window Move"));
        m_moveOperation->setData(base::options::UnrestrictedMoveOp);

        m_resizeOperation = advancedMenu->addAction(i18n("&Resize"));
        m_resizeOperation->setIcon(QIcon::fromTheme(QStringLiteral("transform-scale")));
        setShortcut(m_resizeOperation, QStringLiteral("Window Resize"));
        m_resizeOperation->setData(base::options::ResizeOp);

        m_keepAboveOperation = advancedMenu->addAction(i18n("Keep &Above Others"));
        m_keepAboveOperation->setIcon(QIcon::fromTheme(QStringLiteral("window-keep-above")));
        setShortcut(m_keepAboveOperation, QStringLiteral("Window Above Other Windows"));
        m_keepAboveOperation->setCheckable(true);
        m_keepAboveOperation->setData(base::options::KeepAboveOp);

        m_keepBelowOperation = advancedMenu->addAction(i18n("Keep &Below Others"));
        m_keepBelowOperation->setIcon(QIcon::fromTheme(QStringLiteral("window-keep-below")));
        setShortcut(m_keepBelowOperation, QStringLiteral("Window Below Other Windows"));
        m_keepBelowOperation->setCheckable(true);
        m_keepBelowOperation->setData(base::options::KeepBelowOp);

        m_fullScreenOperation = advancedMenu->addAction(i18n("&Fullscreen"));
        m_fullScreenOperation->setIcon(QIcon::fromTheme(QStringLiteral("view-fullscreen")));
        setShortcut(m_fullScreenOperation, QStringLiteral("Window Fullscreen"));
        m_fullScreenOperation->setCheckable(true);
        m_fullScreenOperation->setData(base::options::FullScreenOp);

        m_noBorderOperation = advancedMenu->addAction(i18n("&No Border"));
        m_noBorderOperation->setIcon(QIcon::fromTheme(QStringLiteral("edit-none-border")));
        setShortcut(m_noBorderOperation, QStringLiteral("Window No Border"));
        m_noBorderOperation->setCheckable(true);
        m_noBorderOperation->setData(base::options::NoBorderOp);

        advancedMenu->addSeparator();

        m_shortcutOperation = advancedMenu->addAction(i18n("Set Window Short&cut..."));
        m_shortcutOperation->setIcon(QIcon::fromTheme(QStringLiteral("configure-shortcuts")));
        setShortcut(m_shortcutOperation, QStringLiteral("Setup Window Shortcut"));
        m_shortcutOperation->setData(base::options::SetupWindowShortcutOp);

        QAction* action = advancedMenu->addAction(i18n("Configure Special &Window Settings..."));
        action->setIcon(QIcon::fromTheme(QStringLiteral("preferences-system-windows-actions")));
        action->setData(base::options::WindowRulesOp);
        m_rulesOperation = action;

        action = advancedMenu->addAction(i18n("Configure S&pecial Application Settings..."));
        action->setIcon(QIcon::fromTheme(QStringLiteral("preferences-system-windows-actions")));
        action->setData(base::options::ApplicationRulesOp);
        m_applicationRulesOperation = action;
        if (!kwinApp()->config()->isImmutable()
            && !KAuthorized::authorizeControlModules(configModules(true)).isEmpty()) {
            advancedMenu->addSeparator();
            action
                = advancedMenu->addAction(i18nc("Entry in context menu of window decoration to "
                                                "open the configuration module of KWin",
                                                "Configure W&indow Manager..."));
            action->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
            QObject::connect(action, &QAction::triggered, qobject.get(), [this, configModules]() {
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
                auto p = new QProcess(qobject.get());
                p->setArguments(args);
                p->setProcessEnvironment(kwinApp()->processStartupEnvironment());
                p->setProgram(QStringLiteral("kcmshell5"));
                QObject::connect(
                    p,
                    static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                    p,
                    &QProcess::deleteLater);
                QObject::connect(
                    p, &QProcess::errorOccurred, qobject.get(), [](QProcess::ProcessError e) {
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
        m_maximizeOperation->setData(base::options::MaximizeOp);

        m_minimizeOperation = m_menu->addAction(i18n("Mi&nimize"));
        m_minimizeOperation->setIcon(QIcon::fromTheme(QStringLiteral("window-minimize")));
        setShortcut(m_minimizeOperation, QStringLiteral("Window Minimize"));
        m_minimizeOperation->setData(base::options::MinimizeOp);

        action = m_menu->addMenu(advancedMenu);
        action->setText(i18n("&More Actions"));
        action->setIcon(QIcon::fromTheme(QStringLiteral("overflow-menu")));

        m_closeOperation = m_menu->addAction(i18n("&Close"));
        m_closeOperation->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
        setShortcut(m_closeOperation, QStringLiteral("Window Close"));
        m_closeOperation->setData(base::options::CloseOp);
    }

    /// Creates the Move to Desktop sub-menu.
    void initDesktopPopup()
    {
        if (kwinApp()->operationMode() == Application::OperationModeWaylandOnly
            || kwinApp()->operationMode() == Application::OperationModeXwayland) {
            if (m_multipleDesktopsMenu) {
                return;
            }

            m_multipleDesktopsMenu = new QMenu(m_menu);
            QObject::connect(m_multipleDesktopsMenu,
                             &QMenu::triggered,
                             qobject.get(),
                             [this](auto action) { toggle_on_desktop(action); });
            QObject::connect(m_multipleDesktopsMenu, &QMenu::aboutToShow, qobject.get(), [this] {
                handle_multiple_desktops_popup_about_to_show();
            });

            QAction* action = m_multipleDesktopsMenu->menuAction();
            // set it as the first item
            m_menu->insertAction(m_maximizeOperation, action);
            action->setText(i18n("&Desktops"));
            action->setIcon(QIcon::fromTheme(QStringLiteral("virtual-desktops")));

        } else {
            if (m_desktopMenu)
                return;

            m_desktopMenu = new QMenu(m_menu);
            QObject::connect(m_desktopMenu, &QMenu::triggered, qobject.get(), [this](auto action) {
                send_to_desktop(action);
            });
            QObject::connect(m_desktopMenu, &QMenu::aboutToShow, qobject.get(), [this] {
                handle_desktop_popup_about_to_show();
            });

            QAction* action = m_desktopMenu->menuAction();
            // set it as the first item
            m_menu->insertAction(m_maximizeOperation, action);
            action->setText(i18n("Move to &Desktop"));
            action->setIcon(QIcon::fromTheme(QStringLiteral("virtual-desktops")));
        }
    }

    /// Creates the Move to Screen sub-menu.
    void initScreenPopup()
    {
        if (m_screenMenu) {
            return;
        }

        m_screenMenu = new QMenu(m_menu);
        QObject::connect(m_screenMenu, &QMenu::triggered, qobject.get(), [this](auto action) {
            send_to_screen(action);
        });
        QObject::connect(m_screenMenu, &QMenu::aboutToShow, qobject.get(), [this] {
            handle_screen_popup_about_to_show();
        });

        QAction* action = m_screenMenu->menuAction();
        // set it as the first item after desktop
        m_menu->insertAction(m_minimizeOperation, action);
        action->setText(i18n("Move to &Screen"));
        action->setIcon(QIcon::fromTheme(QStringLiteral("computer")));
    }

    /**
     * Shows a helper Dialog to inform the user how to get back in case he triggered
     * an action which hides the window decoration (e.g. NoBorder or Fullscreen).
     * @param message The message type to be shown
     * @param c The Client for which the dialog should be shown.
     */
    void helperDialog(const QString& message, Toplevel* window)
    {
        QStringList args;
        QString type;
        auto shortcut = [this](const QString& name) {
            auto action = space.qobject->template findChild<QAction*>(name);
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
            args << QStringLiteral("--embed") << QString::number(window->xcb_window);
        }
        QtConcurrent::run([args]() { KProcess::startDetached(QStringLiteral("kdialog"), args); });
    }

    /// The actual main context menu which is show when the user_actions_menu is invoked.
    QMenu* m_menu{nullptr};

    /// The move to desktop sub menu.
    QMenu* m_desktopMenu{nullptr};

    /// The move to desktop sub menu, with the Wayland protocol.
    QMenu* m_multipleDesktopsMenu{nullptr};

    /// The move to screen sub menu.
    QMenu* m_screenMenu{nullptr};

    /// Menu for further entries added by scripts.
    QMenu* m_scriptsMenu{nullptr};

    QAction* m_resizeOperation{nullptr};
    QAction* m_moveOperation{nullptr};
    QAction* m_maximizeOperation{nullptr};
    QAction* m_shadeOperation{nullptr};
    QAction* m_keepAboveOperation{nullptr};
    QAction* m_keepBelowOperation{nullptr};
    QAction* m_fullScreenOperation{nullptr};
    QAction* m_noBorderOperation{nullptr};
    QAction* m_minimizeOperation{nullptr};
    QAction* m_closeOperation{nullptr};
    QAction* m_shortcutOperation{nullptr};

    /// The Client for which the menu is shown.
    QPointer<Toplevel> m_client;

    QAction* m_rulesOperation{nullptr};
    QAction* m_applicationRulesOperation{nullptr};

    std::unique_ptr<QObject> qobject;
    Space& space;
};

}

Q_DECLARE_METATYPE(KWin::win::user_actions_menu_desktop_action_data);
