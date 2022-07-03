/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009 Martin Gräßlin <mgraesslin@kde.org>

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
//#define QT_CLEAN_NAMESPACE
#include "tabbox.h"

#include "tabbox_client_model.h"
#include "tabbox_config.h"
#include "tabbox_desktop_chain.h"
#include "tabbox_desktop_model.h"
#include "tabbox_logging.h"
#include "tabbox_x11_filter.h"

#include "base/platform.h"
#include "base/x11/grabs.h"
#include "base/x11/xcb/proto.h"
#include "input/pointer_redirect.h"
#include "input/redirect.h"
#include "input/xkb/helpers.h"
#include "render/effects.h"
#include "win/screen.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/virtual_desktops.h"

#include "win/controlling.h"
#include "win/focus_chain.h"
#include "win/meta.h"
#include "win/scene.h"
#include "win/stacking.h"
#include "win/stacking_order.h"
#include "win/util.h"
#include "win/x11/window.h"

#include <QAction>
#include <QKeyEvent>
// KDE
#include <KConfig>
#include <KConfigGroup>
#include <KGlobalAccel>
#include <KLazyLocalizedString>
#include <KLocalizedString>
#include <kkeyserver.h>
// X11
#include <X11/keysym.h>
#include <X11/keysymdef.h>
// xcb
#include <xcb/xcb_keysyms.h>

// specify externals before namespace

namespace KWin
{

namespace win
{

tabbox_handler_impl::tabbox_handler_impl(win::tabbox* tabbox)
    : tabbox_handler(&tabbox->space, tabbox)
    , m_tabbox(tabbox)
    , m_desktop_focus_chain(new tabbox_desktop_chain_manager(this))
{
    // connects for DesktopFocusChainManager
    auto vds = tabbox->space.virtual_desktop_manager.get();
    connect(vds,
            &win::virtual_desktop_manager::countChanged,
            m_desktop_focus_chain,
            &tabbox_desktop_chain_manager::resize);
    connect(vds,
            &win::virtual_desktop_manager::currentChanged,
            m_desktop_focus_chain,
            &tabbox_desktop_chain_manager::add_desktop);
}

tabbox_handler_impl::~tabbox_handler_impl()
{
}

int tabbox_handler_impl::active_screen() const
{
    auto output = win::get_current_output(m_tabbox->space);
    if (!output) {
        return 0;
    }
    return base::get_output_index(kwinApp()->get_base().get_outputs(), *output);
}

int tabbox_handler_impl::current_desktop() const
{
    return m_tabbox->space.virtual_desktop_manager->current();
}

QString tabbox_handler_impl::desktop_name(tabbox_client* client) const
{
    auto& vds = m_tabbox->space.virtual_desktop_manager;

    if (tabbox_client_impl* c = static_cast<tabbox_client_impl*>(client)) {
        if (!c->client()->isOnAllDesktops())
            return vds->name(c->client()->desktop());
    }

    return vds->name(vds->current());
}

QString tabbox_handler_impl::desktop_name(int desktop) const
{
    return m_tabbox->space.virtual_desktop_manager->name(desktop);
}

std::weak_ptr<tabbox_client>
tabbox_handler_impl::next_client_focus_chain(tabbox_client* client) const
{
    if (tabbox_client_impl* c = static_cast<tabbox_client_impl*>(client)) {
        auto next = focus_chain_next_latest_use(m_tabbox->space.focus_chain, c->client());
        if (next) {
            return next->control->tabbox();
        }
    }
    return std::weak_ptr<tabbox_client>();
}

std::weak_ptr<tabbox_client> tabbox_handler_impl::first_client_focus_chain() const
{
    if (auto c = focus_chain_first_latest_use<Toplevel>(m_tabbox->space.focus_chain)) {
        return c->control->tabbox();
    } else {
        return std::weak_ptr<tabbox_client>();
    }
}

bool tabbox_handler_impl::is_in_focus_chain(tabbox_client* client) const
{
    if (tabbox_client_impl* c = static_cast<tabbox_client_impl*>(client)) {
        return contains(m_tabbox->space.focus_chain.chains.latest_use, c->client());
    }
    return false;
}

int tabbox_handler_impl::next_desktop_focus_chain(int desktop) const
{
    return m_desktop_focus_chain->next(desktop);
}

int tabbox_handler_impl::number_of_desktops() const
{
    return m_tabbox->space.virtual_desktop_manager->count();
}

std::weak_ptr<tabbox_client> tabbox_handler_impl::active_client() const
{
    if (auto win = m_tabbox->space.active_client) {
        return win->control->tabbox();
    } else {
        return std::weak_ptr<tabbox_client>();
    }
}

bool tabbox_handler_impl::check_desktop(tabbox_client* client, int desktop) const
{
    auto current = (static_cast<tabbox_client_impl*>(client))->client();

    switch (config().client_desktop_mode()) {
    case tabbox_config::AllDesktopsClients:
        return true;
    case tabbox_config::ExcludeCurrentDesktopClients:
        return !current->isOnDesktop(desktop);
    default: // TabBoxConfig::OnlyCurrentDesktopClients
        return current->isOnDesktop(desktop);
    }
}

bool tabbox_handler_impl::check_applications(tabbox_client* client) const
{
    auto current = (static_cast<tabbox_client_impl*>(client))->client();
    tabbox_client_impl* c;

    switch (config().client_applications_mode()) {
    case tabbox_config::OneWindowPerApplication:
        // check if the list already contains an entry of this application
        for (auto client_weak : client_list()) {
            auto client = client_weak.lock();
            if (!client) {
                continue;
            }
            if ((c = dynamic_cast<tabbox_client_impl*>(client.get()))) {
                if (win::belong_to_same_client(
                        c->client(), current, win::same_client_check::allow_cross_process)) {
                    return false;
                }
            }
        }
        return true;
    case tabbox_config::AllWindowsCurrentApplication: {
        auto pointer = tabbox_handle->active_client().lock();
        if (!pointer) {
            return false;
        }
        if ((c = dynamic_cast<tabbox_client_impl*>(pointer.get()))) {
            if (win::belong_to_same_client(
                    c->client(), current, win::same_client_check::allow_cross_process)) {
                return true;
            }
        }
        return false;
    }
    default: // tabbox_config::AllWindowsAllApplications
        return true;
    }
}

bool tabbox_handler_impl::check_minimized(tabbox_client* client) const
{
    switch (config().client_minimized_mode()) {
    case tabbox_config::ExcludeMinimizedClients:
        return !client->is_minimized();
    case tabbox_config::OnlyMinimizedClients:
        return client->is_minimized();
    default: // tabbox_config::IgnoreMinimizedStatus
        return true;
    }
}

bool tabbox_handler_impl::check_multi_screen(tabbox_client* client) const
{
    auto current_window = (static_cast<tabbox_client_impl*>(client))->client();
    auto current_output = win::get_current_output(m_tabbox->space);

    switch (config().client_multi_screen_mode()) {
    case tabbox_config::IgnoreMultiScreen:
        return true;
    case tabbox_config::ExcludeCurrentScreenClients:
        return current_window->central_output != current_output;
    default: // tabbox_config::OnlyCurrentScreenClients
        return current_window->central_output == current_output;
    }
}

std::weak_ptr<tabbox_client> tabbox_handler_impl::client_to_add_to_list(tabbox_client* client,
                                                                        int desktop) const
{
    if (!client) {
        return std::weak_ptr<tabbox_client>();
    }
    Toplevel* ret = nullptr;
    auto current = (static_cast<tabbox_client_impl*>(client))->client();

    bool add_client = check_desktop(client, desktop) && check_applications(client)
        && check_minimized(client) && check_multi_screen(client);
    add_client = add_client && win::wants_tab_focus(current) && !current->control->skip_switcher();
    if (add_client) {
        // don't add windows that have modal dialogs
        auto modal = current->findModal();
        if (!modal || !modal->control || modal == current) {
            ret = current;
        } else {
            auto const cl = client_list();
            if (std::find_if(cl.cbegin(),
                             cl.cend(),
                             [modal_client = modal->control->tabbox().lock()](auto const& client) {
                                 return client.lock() == modal_client;
                             })
                == cl.cend()) {
                ret = modal;
            }
        }
    }
    return ret ? ret->control->tabbox() : std::weak_ptr<tabbox_client>();
}

tabbox_client_list tabbox_handler_impl::stacking_order() const
{
    auto const stacking = m_tabbox->space.stacking_order->stack;
    tabbox_client_list ret;
    for (auto const& toplevel : stacking) {
        if (toplevel->control) {
            ret.push_back(toplevel->control->tabbox());
        }
    }
    return ret;
}

bool tabbox_handler_impl::is_kwin_compositing() const
{
    return m_tabbox->space.compositing();
}

void tabbox_handler_impl::raise_client(tabbox_client* c) const
{
    win::raise_window(&m_tabbox->space, static_cast<tabbox_client_impl*>(c)->client());
}

void tabbox_handler_impl::restack(tabbox_client* c, tabbox_client* under)
{
    win::restack(&m_tabbox->space,
                 static_cast<tabbox_client_impl*>(c)->client(),
                 static_cast<tabbox_client_impl*>(under)->client(),
                 true);
}

void tabbox_handler_impl::elevate_client(tabbox_client* c, QWindow* tabbox, bool b) const
{
    auto cl = static_cast<tabbox_client_impl*>(c)->client();
    win::elevate(cl, b);
    if (auto w = m_tabbox->space.findInternal(tabbox)) {
        win::elevate(w, b);
    }
}

std::weak_ptr<tabbox_client> tabbox_handler_impl::desktop_client() const
{
    for (auto const& window : m_tabbox->space.stacking_order->stack) {
        if (window->control && win::is_desktop(window) && window->isOnCurrentDesktop()
            && window->central_output == win::get_current_output(m_tabbox->space)) {
            return window->control->tabbox();
        }
    }
    return std::weak_ptr<tabbox_client>();
}

void tabbox_handler_impl::activate_and_close()
{
    m_tabbox->accept();
}

void tabbox_handler_impl::highlight_windows(tabbox_client* window, QWindow* controller)
{
    auto& effects = m_tabbox->space.render.effects;
    if (!effects) {
        return;
    }

    QVector<EffectWindow*> windows;
    if (window) {
        windows << static_cast<tabbox_client_impl*>(window)->client()->render->effect.get();
    }
    if (auto t = m_tabbox->space.findInternal(controller)) {
        windows << t->render->effect.get();
    }

    effects->highlightWindows(windows);
}

bool tabbox_handler_impl::no_modifier_grab() const
{
    return m_tabbox->no_modifier_grab();
}

/*********************************************************
 * tabbox_client_impl
 *********************************************************/

tabbox_client_impl::tabbox_client_impl(Toplevel* window)
    : tabbox_client()
    , m_client(window)
{
}

tabbox_client_impl::~tabbox_client_impl()
{
}

QString tabbox_client_impl::caption() const
{
    if (win::is_desktop(m_client))
        return i18nc("Special entry in alt+tab list for minimizing all windows", "Show Desktop");
    return win::caption(m_client);
}

QIcon tabbox_client_impl::icon() const
{
    if (win::is_desktop(m_client)) {
        return QIcon::fromTheme(QStringLiteral("user-desktop"));
    }
    return m_client->control->icon();
}

bool tabbox_client_impl::is_minimized() const
{
    return m_client->control->minimized();
}

int tabbox_client_impl::x() const
{
    return m_client->pos().x();
}

int tabbox_client_impl::y() const
{
    return m_client->pos().y();
}

int tabbox_client_impl::width() const
{
    return m_client->size().width();
}

int tabbox_client_impl::height() const
{
    return m_client->size().height();
}

bool tabbox_client_impl::is_closeable() const
{
    return m_client->isCloseable();
}

void tabbox_client_impl::close()
{
    m_client->closeWindow();
}

bool tabbox_client_impl::is_first_in_tabbox() const
{
    return m_client->control->first_in_tabbox();
}

QUuid tabbox_client_impl::internal_id() const
{
    return m_client->internal_id;
}

/*********************************************************
 * TabBox
 *********************************************************/
tabbox::tabbox(win::space& space)
    : space{space}
{
    m_default_config = tabbox_config();
    m_default_config.set_tabbox_mode(tabbox_config::ClientTabBox);
    m_default_config.set_client_desktop_mode(tabbox_config::OnlyCurrentDesktopClients);
    m_default_config.set_client_applications_mode(tabbox_config::AllWindowsAllApplications);
    m_default_config.set_client_minimized_mode(tabbox_config::IgnoreMinimizedStatus);
    m_default_config.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
    m_default_config.set_client_multi_screen_mode(tabbox_config::IgnoreMultiScreen);
    m_default_config.set_client_switching_mode(tabbox_config::FocusChainSwitching);

    m_alternative_config = tabbox_config();
    m_alternative_config.set_tabbox_mode(tabbox_config::ClientTabBox);
    m_alternative_config.set_client_desktop_mode(tabbox_config::AllDesktopsClients);
    m_alternative_config.set_client_applications_mode(tabbox_config::AllWindowsAllApplications);
    m_alternative_config.set_client_minimized_mode(tabbox_config::IgnoreMinimizedStatus);
    m_alternative_config.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
    m_alternative_config.set_client_multi_screen_mode(tabbox_config::IgnoreMultiScreen);
    m_alternative_config.set_client_switching_mode(tabbox_config::FocusChainSwitching);

    m_default_current_application_config = m_default_config;
    m_default_current_application_config.set_client_applications_mode(
        tabbox_config::AllWindowsCurrentApplication);

    m_alternative_current_application_config = m_alternative_config;
    m_alternative_current_application_config.set_client_applications_mode(
        tabbox_config::AllWindowsCurrentApplication);

    m_desktop_config = tabbox_config();
    m_desktop_config.set_tabbox_mode(tabbox_config::DesktopTabBox);
    m_desktop_config.set_show_tabbox(true);
    m_desktop_config.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
    m_desktop_config.set_desktop_switching_mode(tabbox_config::MostRecentlyUsedDesktopSwitching);

    m_desktop_list_config = tabbox_config();
    m_desktop_list_config.set_tabbox_mode(tabbox_config::DesktopTabBox);
    m_desktop_list_config.set_show_tabbox(true);
    m_desktop_list_config.set_show_desktop_mode(tabbox_config::DoNotShowDesktopClient);
    m_desktop_list_config.set_desktop_switching_mode(tabbox_config::StaticDesktopSwitching);
    m_tabbox = new tabbox_handler_impl(this);
    QTimer::singleShot(0, this, &tabbox::handler_ready);

    m_tabbox_mode = TabBoxDesktopMode; // init variables
    connect(&m_delayed_show_timer, &QTimer::timeout, this, &tabbox::show);
    connect(space.qobject.get(), &win::space_qobject::configChanged, this, &tabbox::reconfigure);
}

tabbox::~tabbox() = default;

void tabbox::handler_ready()
{
    m_tabbox->set_config(m_default_config);
    reconfigure();
    m_ready = true;
}

template<typename Slot>
void tabbox::key(const KLazyLocalizedString& action_name, Slot slot, const QKeySequence& shortcut)
{
    QAction* a = new QAction(this);
    a->setProperty("componentName", QStringLiteral(KWIN_NAME));
    a->setObjectName(QString::fromUtf8(action_name.untranslatedText()));
    a->setText(action_name.toString());
    KGlobalAccel::self()->setGlobalShortcut(a, QList<QKeySequence>() << shortcut);
    kwinApp()->input->registerShortcut(shortcut, a, this, slot);
    auto cuts = KGlobalAccel::self()->shortcut(a);
    global_shortcut_changed(a, cuts.isEmpty() ? QKeySequence() : cuts.first());
}

static constexpr const auto s_windows = kli18n("Walk Through Windows");
static constexpr const auto s_windowsRev = kli18n("Walk Through Windows (Reverse)");
static constexpr const auto s_windowsAlt = kli18n("Walk Through Windows Alternative");
static constexpr const auto s_windowsAltRev = kli18n("Walk Through Windows Alternative (Reverse)");
static constexpr const auto s_app = kli18n("Walk Through Windows of Current Application");
static constexpr const auto s_appRev
    = kli18n("Walk Through Windows of Current Application (Reverse)");
static constexpr const auto s_appAlt
    = kli18n("Walk Through Windows of Current Application Alternative");
static constexpr const auto s_appAltRev
    = kli18n("Walk Through Windows of Current Application Alternative (Reverse)");
static constexpr const auto s_desktops = kli18n("Walk Through Desktops");
static constexpr const auto s_desktopsRev = kli18n("Walk Through Desktops (Reverse)");
static constexpr const auto s_desktopList = kli18n("Walk Through Desktop List");
static constexpr const auto s_desktopListRev = kli18n("Walk Through Desktop List (Reverse)");

void tabbox::init_shortcuts()
{
    key(s_windows, &tabbox::slot_walk_through_windows, Qt::ALT + Qt::Key_Tab);
    key(s_windowsRev,
        &tabbox::slot_walk_back_through_windows,
        Qt::ALT + Qt::SHIFT + Qt::Key_Backtab);
    key(s_app, &tabbox::slot_walk_through_current_app_windows, Qt::ALT + Qt::Key_QuoteLeft);
    key(s_appRev,
        &tabbox::slot_walk_back_through_current_app_windows,
        Qt::ALT + Qt::Key_AsciiTilde);
    key(s_windowsAlt, &tabbox::slot_walk_through_windows_alternative);
    key(s_windowsAltRev, &tabbox::slot_walk_back_through_windows_alternative);
    key(s_appAlt, &tabbox::slot_walk_through_current_app_windows_alternative);
    key(s_appAltRev, &tabbox::slot_walk_back_through_current_app_windows_alternative);
    key(s_desktops, &tabbox::slot_walk_through_desktops);
    key(s_desktopsRev, &tabbox::slot_walk_back_through_desktops);
    key(s_desktopList, &tabbox::slot_walk_through_desktop_list);
    key(s_desktopListRev, &tabbox::slot_walk_back_through_desktop_list);

    connect(KGlobalAccel::self(),
            &KGlobalAccel::globalShortcutChanged,
            this,
            &tabbox::global_shortcut_changed);
}

void tabbox::global_shortcut_changed(QAction* action, const QKeySequence& seq)
{
    if (qstrcmp(qPrintable(action->objectName()), s_windows.untranslatedText()) == 0) {
        m_cut_walk_through_windows = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_windowsRev.untranslatedText()) == 0) {
        m_cut_walk_through_windows_reverse = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_app.untranslatedText()) == 0) {
        m_cut_walk_through_current_app_windows = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_appRev.untranslatedText()) == 0) {
        m_cut_walk_through_current_app_windows_reverse = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_windowsAlt.untranslatedText()) == 0) {
        m_cut_walk_through_windows_alternative = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_windowsAltRev.untranslatedText()) == 0) {
        m_cut_walk_through_windows_alternative_reverse = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_appAlt.untranslatedText()) == 0) {
        m_cut_walk_through_current_app_windows_alternative = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_appAltRev.untranslatedText()) == 0) {
        m_cut_walk_through_current_app_windows_alternative_reverse = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_desktops.untranslatedText()) == 0) {
        m_cut_walk_through_desktops = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_desktopsRev.untranslatedText()) == 0) {
        m_cut_walk_through_desktops_reverse = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_desktopList.untranslatedText()) == 0) {
        m_cut_walk_through_desktop_list = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_desktopListRev.untranslatedText())
               == 0) {
        m_cut_walk_through_desktop_list_reverse = seq;
    }
}

void tabbox::set_mode(TabBoxMode mode)
{
    m_tabbox_mode = mode;
    switch (mode) {
    case TabBoxWindowsMode:
        m_tabbox->set_config(m_default_config);
        break;
    case TabBoxWindowsAlternativeMode:
        m_tabbox->set_config(m_alternative_config);
        break;
    case TabBoxCurrentAppWindowsMode:
        m_tabbox->set_config(m_default_current_application_config);
        break;
    case TabBoxCurrentAppWindowsAlternativeMode:
        m_tabbox->set_config(m_alternative_current_application_config);
        break;
    case TabBoxDesktopMode:
        m_tabbox->set_config(m_desktop_config);
        break;
    case TabBoxDesktopListMode:
        m_tabbox->set_config(m_desktop_list_config);
        break;
    }
}

void tabbox::reset(bool partial_reset)
{
    switch (m_tabbox->config().tabbox_mode()) {
    case tabbox_config::ClientTabBox:
        m_tabbox->create_model(partial_reset);
        if (!partial_reset) {
            if (space.active_client) {
                set_current_client(space.active_client);
            }

            // it's possible that the active client is not part of the model
            // in that case the index is invalid
            if (!m_tabbox->current_index().isValid())
                set_current_index(m_tabbox->first());
        } else {
            if (!m_tabbox->current_index().isValid()
                || !m_tabbox->client(m_tabbox->current_index()))
                set_current_index(m_tabbox->first());
        }
        break;
    case tabbox_config::DesktopTabBox:
        m_tabbox->create_model();

        if (!partial_reset)
            set_current_desktop(space.virtual_desktop_manager->current());
        break;
    }

    Q_EMIT tabbox_updated();
}

void tabbox::next_prev(bool next)
{
    set_current_index(m_tabbox->next_prev(next), false);
    Q_EMIT tabbox_updated();
}

Toplevel* tabbox::current_client()
{
    if (auto client
        = static_cast<tabbox_client_impl*>(m_tabbox->client(m_tabbox->current_index()))) {
        for (auto win : space.m_windows) {
            if (win == client->client()) {
                return win;
            }
        }
    }
    return nullptr;
}

QList<Toplevel*> tabbox::current_client_list()
{
    auto const list = m_tabbox->client_list();
    QList<Toplevel*> ret;

    for (auto& client_pointer : list) {
        auto client = client_pointer.lock();
        if (!client) {
            continue;
        }
        if (auto c = static_cast<tabbox_client_impl const*>(client.get())) {
            ret.append(c->client());
        }
    }
    return ret;
}

int tabbox::current_desktop()
{
    return m_tabbox->desktop(m_tabbox->current_index());
}

QList<int> tabbox::current_desktop_list()
{
    return m_tabbox->desktop_list();
}

void tabbox::set_current_client(Toplevel* window)
{
    set_current_index(m_tabbox->index(window->control->tabbox().lock().get()));
}

void tabbox::set_current_desktop(int new_desktop)
{
    set_current_index(m_tabbox->desktop_index(new_desktop));
}

void tabbox::set_current_index(QModelIndex index, bool notify_effects)
{
    if (!index.isValid())
        return;
    m_tabbox->set_current_index(index);
    if (notify_effects) {
        Q_EMIT tabbox_updated();
    }
}

void tabbox::show()
{
    Q_EMIT tabbox_added(m_tabbox_mode);
    if (is_displayed()) {
        m_is_shown = false;
        return;
    }
    space.setShowingDesktop(false);
    reference();
    m_is_shown = true;
    m_tabbox->show();
}

void tabbox::hide(bool abort)
{
    m_delayed_show_timer.stop();
    if (m_is_shown) {
        m_is_shown = false;
        unreference();
    }
    Q_EMIT tabbox_closed();
    if (is_displayed())
        qCDebug(KWIN_TABBOX) << "Tab box was not properly closed by an effect";
    m_tabbox->hide(abort);
    if (kwinApp()->x11Connection()) {
        base::x11::xcb::sync();
    }
}

void tabbox::reconfigure()
{
    KSharedConfigPtr c = kwinApp()->config();
    KConfigGroup config = c->group("TabBox");

    load_config(c->group("TabBox"), m_default_config);
    load_config(c->group("TabBoxAlternative"), m_alternative_config);

    m_default_current_application_config = m_default_config;
    m_default_current_application_config.set_client_applications_mode(
        tabbox_config::AllWindowsCurrentApplication);
    m_alternative_current_application_config = m_alternative_config;
    m_alternative_current_application_config.set_client_applications_mode(
        tabbox_config::AllWindowsCurrentApplication);

    m_tabbox->set_config(m_default_config);

    m_delay_show = config.readEntry<bool>("ShowDelay", true);
    m_delay_show_time = config.readEntry<int>("DelayTime", 90);

    const QString default_desktop_layout = QStringLiteral("org.kde.breeze.desktop");
    m_desktop_config.set_layout_name(config.readEntry("DesktopLayout", default_desktop_layout));
    m_desktop_list_config.set_layout_name(
        config.readEntry("DesktopListLayout", default_desktop_layout));

    QList<ElectricBorder>* borders = &m_border_activate;
    QString border_config = QStringLiteral("BorderActivate");
    for (int i = 0; i < 2; ++i) {
        for (auto const& border : qAsConst(*borders)) {
            space.edges->unreserve(border, this);
        }
        borders->clear();
        QStringList list = config.readEntry(border_config, QStringList());
        for (auto const& s : qAsConst(list)) {
            bool ok;
            const int i = s.toInt(&ok);
            if (!ok)
                continue;
            borders->append(ElectricBorder(i));
            space.edges->reserve(ElectricBorder(i), this, "toggle");
        }
        borders = &m_border_alternative_activate;
        border_config = QStringLiteral("BorderAlternativeActivate");
    }

    auto touch_config = [this, config](const QString& key,
                                       QHash<ElectricBorder, QAction*>& actions,
                                       TabBoxMode mode,
                                       const QStringList& defaults = QStringList{}) {
        // fist erase old config
        for (auto it = actions.begin(); it != actions.end();) {
            delete it.value();
            it = actions.erase(it);
        }
        // now new config
        const QStringList list = config.readEntry(key, defaults);
        for (const auto& s : list) {
            bool ok;
            const int i = s.toInt(&ok);
            if (!ok) {
                continue;
            }
            QAction* a = new QAction(this);
            connect(a, &QAction::triggered, this, std::bind(&tabbox::toggle_mode, this, mode));
            space.edges->reserveTouch(ElectricBorder(i), a);
            actions.insert(ElectricBorder(i), a);
        }
    };
    touch_config(QStringLiteral("TouchBorderActivate"), m_touch_activate, TabBoxWindowsMode);
    touch_config(QStringLiteral("TouchBorderAlternativeActivate"),
                 m_touch_alternative_activate,
                 TabBoxWindowsAlternativeMode);
}

void tabbox::load_config(const KConfigGroup& config, tabbox_config& tabBoxConfig)
{
    tabBoxConfig.set_client_desktop_mode(tabbox_config::ClientDesktopMode(
        config.readEntry<int>("DesktopMode", tabbox_config::default_desktop_mode())));
    tabBoxConfig.set_client_applications_mode(tabbox_config::ClientApplicationsMode(
        config.readEntry<int>("ApplicationsMode", tabbox_config::default_applications_mode())));
    tabBoxConfig.set_client_minimized_mode(tabbox_config::ClientMinimizedMode(
        config.readEntry<int>("MinimizedMode", tabbox_config::default_minimized_mode())));
    tabBoxConfig.set_show_desktop_mode(tabbox_config::ShowDesktopMode(
        config.readEntry<int>("ShowDesktopMode", tabbox_config::default_show_desktop_mode())));
    tabBoxConfig.set_client_multi_screen_mode(tabbox_config::ClientMultiScreenMode(
        config.readEntry<int>("MultiScreenMode", tabbox_config::default_multi_screen_mode())));
    tabBoxConfig.set_client_switching_mode(tabbox_config::ClientSwitchingMode(
        config.readEntry<int>("SwitchingMode", tabbox_config::default_switching_mode())));

    tabBoxConfig.set_show_tabbox(
        config.readEntry<bool>("ShowTabBox", tabbox_config::default_show_tabbox()));
    tabBoxConfig.set_highlight_windows(
        config.readEntry<bool>("HighlightWindows", tabbox_config::default_highlight_window()));

    tabBoxConfig.set_layout_name(
        config.readEntry<QString>("LayoutName", tabbox_config::default_layout_name()));
}

void tabbox::delayed_show()
{
    if (is_displayed() || m_delayed_show_timer.isActive())
        // already called show - no need to call it twice
        return;

    if (!m_delay_show_time) {
        show();
        return;
    }

    m_delayed_show_timer.setSingleShot(true);
    m_delayed_show_timer.start(m_delay_show_time);
}

bool tabbox::handle_mouse_event(QMouseEvent* event)
{
    if (!m_is_shown && is_displayed()) {
        // tabbox has been replaced, check effects
        if (auto& effects = space.render.effects;
            effects && effects->checkInputWindowEvent(event)) {
            return true;
        }
    }
    switch (event->type()) {
    case QEvent::MouseMove:
        if (!m_tabbox->contains_pos(event->globalPos())) {
            // filter out all events which are not on the TabBox window.
            // We don't want windows to react on the mouse events
            return true;
        }
        return false;
    case QEvent::MouseButtonPress:
        if ((!m_is_shown && is_displayed()) || !m_tabbox->contains_pos(event->globalPos())) {
            close(); // click outside closes tab
            return true;
        }
        // fall through
    case QEvent::MouseButtonRelease:
    default:
        // we do not filter it out, the intenal filter takes care
        return false;
    }
    return false;
}

bool tabbox::handle_wheel_event(QWheelEvent* event)
{
    if (!m_is_shown && is_displayed()) {
        // tabbox has been replaced, check effects
        if (auto& effects = space.render.effects;
            effects && effects->checkInputWindowEvent(event)) {
            return true;
        }
    }
    if (event->angleDelta().y() == 0) {
        return false;
    }
    const QModelIndex index = m_tabbox->next_prev(event->angleDelta().y() > 0);
    if (index.isValid()) {
        set_current_index(index);
    }
    return true;
}

void tabbox::grabbed_key_event(QKeyEvent* event)
{
    Q_EMIT tabbox_key_event(event);
    if (!m_is_shown && is_displayed()) {
        // tabbox has been replaced, check effects
        return;
    }
    if (m_no_modifier_grab) {
        if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Space) {
            accept();
            return;
        }
    }
    m_tabbox->grabbed_key_event(event);
}

struct KeySymbolsDeleter {
    static inline void cleanup(xcb_key_symbols_t* symbols)
    {
        xcb_key_symbols_free(symbols);
    }
};

/**
 * Handles alt-tab / control-tab
 */
static bool areKeySymXsDepressed(const uint keySyms[], int nKeySyms)
{
    base::x11::xcb::query_keymap keys;

    QScopedPointer<xcb_key_symbols_t, KeySymbolsDeleter> symbols(
        xcb_key_symbols_alloc(connection()));
    if (symbols.isNull() || !keys) {
        return false;
    }
    const auto keymap = keys->keys;

    bool depressed = false;
    for (int iKeySym = 0; iKeySym < nKeySyms; iKeySym++) {
        uint keySymX = keySyms[iKeySym];
        xcb_keycode_t* keyCodes = xcb_key_symbols_get_keycode(symbols.data(), keySymX);
        if (!keyCodes) {
            continue;
        }

        int j = 0;
        while (keyCodes[j] != XCB_NO_SYMBOL) {
            const xcb_keycode_t keyCodeX = keyCodes[j++];
            int i = keyCodeX / 8;
            char mask = 1 << (keyCodeX - (i * 8));

            if (i < 0 || i >= 32) {
                continue;
            }

            qCDebug(KWIN_TABBOX) << iKeySym << ": keySymX=0x" << QString::number(keySymX, 16)
                                 << " i=" << i << " mask=0x" << QString::number(mask, 16)
                                 << " keymap[i]=0x" << QString::number(keymap[i], 16);

            if (keymap[i] & mask) {
                depressed = true;
                break;
            }
        }

        free(keyCodes);
    }

    return depressed;
}

static bool areModKeysDepressedX11(const QKeySequence& seq)
{
    uint rgKeySyms[10];
    int nKeySyms = 0;
    int mod = seq[seq.count() - 1] & Qt::KeyboardModifierMask;

    if (mod & Qt::SHIFT) {
        rgKeySyms[nKeySyms++] = XK_Shift_L;
        rgKeySyms[nKeySyms++] = XK_Shift_R;
    }
    if (mod & Qt::CTRL) {
        rgKeySyms[nKeySyms++] = XK_Control_L;
        rgKeySyms[nKeySyms++] = XK_Control_R;
    }
    if (mod & Qt::ALT) {
        rgKeySyms[nKeySyms++] = XK_Alt_L;
        rgKeySyms[nKeySyms++] = XK_Alt_R;
    }
    if (mod & Qt::META) {
        // It would take some code to determine whether the Win key
        // is associated with Super or Meta, so check for both.
        // See bug #140023 for details.
        rgKeySyms[nKeySyms++] = XK_Super_L;
        rgKeySyms[nKeySyms++] = XK_Super_R;
        rgKeySyms[nKeySyms++] = XK_Meta_L;
        rgKeySyms[nKeySyms++] = XK_Meta_R;
    }

    return areKeySymXsDepressed(rgKeySyms, nKeySyms);
}

static bool areModKeysDepressedWayland(const QKeySequence& seq)
{
    const int mod = seq[seq.count() - 1] & Qt::KeyboardModifierMask;
    auto const mods
        = input::xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(kwinApp()->input);
    if ((mod & Qt::SHIFT) && mods.testFlag(Qt::ShiftModifier)) {
        return true;
    }
    if ((mod & Qt::CTRL) && mods.testFlag(Qt::ControlModifier)) {
        return true;
    }
    if ((mod & Qt::ALT) && mods.testFlag(Qt::AltModifier)) {
        return true;
    }
    if ((mod & Qt::META) && mods.testFlag(Qt::MetaModifier)) {
        return true;
    }
    return false;
}

static bool areModKeysDepressed(const QKeySequence& seq)
{
    if (seq.isEmpty())
        return false;
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        return areModKeysDepressedWayland(seq);
    } else {
        return areModKeysDepressedX11(seq);
    }
}

void tabbox::navigating_through_windows(bool forward, const QKeySequence& shortcut, TabBoxMode mode)
{
    if (!m_ready || is_grabbed()) {
        return;
    }
    if (!kwinApp()->options->focusPolicyIsReasonable()) {
        // ungrabXKeyboard(); // need that because of accelerator raw mode
        //  CDE style raise / lower
        cde_walk_through_windows(forward);
    } else {
        if (areModKeysDepressed(shortcut)) {
            if (start_kde_walk_through_windows(mode))
                kde_walk_through_windows(forward);
        } else
            // if the shortcut has no modifiers, don't show the tabbox,
            // don't grab, but simply go to the next window
            kde_one_step_through_windows(forward, mode);
    }
}

void tabbox::slot_walk_through_windows()
{
    navigating_through_windows(true, m_cut_walk_through_windows, TabBoxWindowsMode);
}

void tabbox::slot_walk_back_through_windows()
{
    navigating_through_windows(false, m_cut_walk_through_windows_reverse, TabBoxWindowsMode);
}

void tabbox::slot_walk_through_windows_alternative()
{
    navigating_through_windows(
        true, m_cut_walk_through_windows_alternative, TabBoxWindowsAlternativeMode);
}

void tabbox::slot_walk_back_through_windows_alternative()
{
    navigating_through_windows(
        false, m_cut_walk_through_windows_alternative_reverse, TabBoxWindowsAlternativeMode);
}

void tabbox::slot_walk_through_current_app_windows()
{
    navigating_through_windows(
        true, m_cut_walk_through_current_app_windows, TabBoxCurrentAppWindowsMode);
}

void tabbox::slot_walk_back_through_current_app_windows()
{
    navigating_through_windows(
        false, m_cut_walk_through_current_app_windows_reverse, TabBoxCurrentAppWindowsMode);
}

void tabbox::slot_walk_through_current_app_windows_alternative()
{
    navigating_through_windows(true,
                               m_cut_walk_through_current_app_windows_alternative,
                               TabBoxCurrentAppWindowsAlternativeMode);
}

void tabbox::slot_walk_back_through_current_app_windows_alternative()
{
    navigating_through_windows(false,
                               m_cut_walk_through_current_app_windows_alternative_reverse,
                               TabBoxCurrentAppWindowsAlternativeMode);
}

void tabbox::slot_walk_through_desktops()
{
    if (!m_ready || is_grabbed()) {
        return;
    }
    if (areModKeysDepressed(m_cut_walk_through_desktops)) {
        if (start_walk_through_desktops())
            walk_through_desktops(true);
    } else {
        one_step_through_desktops(true);
    }
}

void tabbox::slot_walk_back_through_desktops()
{
    if (!m_ready || is_grabbed()) {
        return;
    }
    if (areModKeysDepressed(m_cut_walk_through_desktops_reverse)) {
        if (start_walk_through_desktops())
            walk_through_desktops(false);
    } else {
        one_step_through_desktops(false);
    }
}

void tabbox::slot_walk_through_desktop_list()
{
    if (!m_ready || is_grabbed()) {
        return;
    }
    if (areModKeysDepressed(m_cut_walk_through_desktop_list)) {
        if (start_walk_through_desktop_list())
            walk_through_desktops(true);
    } else {
        one_step_through_desktop_list(true);
    }
}

void tabbox::slot_walk_back_through_desktop_list()
{
    if (!m_ready || is_grabbed()) {
        return;
    }
    if (areModKeysDepressed(m_cut_walk_through_desktop_list_reverse)) {
        if (start_walk_through_desktop_list())
            walk_through_desktops(false);
    } else {
        one_step_through_desktop_list(false);
    }
}

bool tabbox::toggle(ElectricBorder eb)
{
    if (m_border_alternative_activate.contains(eb)) {
        return toggle_mode(TabBoxWindowsAlternativeMode);
    } else {
        return toggle_mode(TabBoxWindowsMode);
    }
}

bool tabbox::toggle_mode(TabBoxMode mode)
{
    if (!kwinApp()->options->focusPolicyIsReasonable())
        return false; // not supported.
    if (is_displayed()) {
        accept();
        return true;
    }
    if (!establish_tabbox_grab())
        return false;
    m_no_modifier_grab = m_tab_grab = true;
    set_mode(mode);
    reset();
    show();
    return true;
}

bool tabbox::start_kde_walk_through_windows(TabBoxMode mode)
{
    if (!establish_tabbox_grab())
        return false;
    m_tab_grab = true;
    m_no_modifier_grab = false;
    set_mode(mode);
    reset();
    return true;
}

bool tabbox::start_walk_through_desktops(TabBoxMode mode)
{
    if (!establish_tabbox_grab())
        return false;
    m_desktop_grab = true;
    m_no_modifier_grab = false;
    set_mode(mode);
    reset();
    return true;
}

bool tabbox::start_walk_through_desktops()
{
    return start_walk_through_desktops(TabBoxDesktopMode);
}

bool tabbox::start_walk_through_desktop_list()
{
    return start_walk_through_desktops(TabBoxDesktopListMode);
}

void tabbox::kde_walk_through_windows(bool forward)
{
    next_prev(forward);
    delayed_show();
}

void tabbox::walk_through_desktops(bool forward)
{
    next_prev(forward);
    delayed_show();
}

void tabbox::cde_walk_through_windows(bool forward)
{
    Toplevel* c = nullptr;
    // this function find the first suitable client for unreasonable focus
    // policies - the topmost one, with some exceptions (can't be keepabove/below,
    // otherwise it gets stuck on them)
    //     Q_ASSERT(space.block_stacking_updates == 0);
    for (int i = space.stacking_order->stack.size() - 1; i >= 0; --i) {
        auto window = space.stacking_order->stack.at(i);
        if (window->control && window->isOnCurrentDesktop() && !win::is_special_window(window)
            && window->isShown() && win::wants_tab_focus(window) && !window->control->keep_above()
            && !window->control->keep_below()) {
            c = window;
            break;
        }
    }
    auto nc = c;
    bool options_traverse_all;
    {
        KConfigGroup group(kwinApp()->config(), "TabBox");
        options_traverse_all = group.readEntry("TraverseAll", false);
    }

    Toplevel* first_client = nullptr;
    do {
        nc = forward ? next_client_static(nc) : previous_client_static(nc);
        if (!first_client) {
            // When we see our first client for the second time,
            // it's time to stop.
            first_client = nc;
        } else if (nc == first_client) {
            // No candidates found.
            nc = nullptr;
            break;
        }
    } while (nc && nc != c
             && ((!options_traverse_all && !nc->isOnDesktop(current_desktop()))
                 || nc->control->minimized() || !win::wants_tab_focus(nc)
                 || nc->control->keep_above() || nc->control->keep_below()));
    if (nc) {
        if (c && c != nc)
            win::lower_window(&space, c);
        if (kwinApp()->options->focusPolicyIsReasonable()) {
            space.activateClient(nc);
        } else {
            if (!nc->isOnDesktop(current_desktop()))
                set_current_desktop(nc->desktop());
            win::raise_window(&space, nc);
        }
    }
}

void tabbox::kde_one_step_through_windows(bool forward, TabBoxMode mode)
{
    set_mode(mode);
    reset();
    next_prev(forward);
    if (auto c = current_client()) {
        space.activateClient(c);
    }
}

void tabbox::one_step_through_desktops(bool forward, TabBoxMode mode)
{
    set_mode(mode);
    reset();
    next_prev(forward);
    if (current_desktop() != -1)
        set_current_desktop(current_desktop());
}

void tabbox::one_step_through_desktops(bool forward)
{
    one_step_through_desktops(forward, TabBoxDesktopMode);
}

void tabbox::one_step_through_desktop_list(bool forward)
{
    one_step_through_desktops(forward, TabBoxDesktopListMode);
}

void tabbox::key_press(int keyQt)
{
    enum Direction { Backward = -1, Steady = 0, Forward = 1 };
    Direction direction(Steady);

    auto contains = [](const QKeySequence& shortcut, int key) -> bool {
        for (int i = 0; i < shortcut.count(); ++i) {
            if (shortcut[i] == key) {
                return true;
            }
        }
        return false;
    };

    // tests whether a shortcut matches and handles pitfalls on ShiftKey invocation
    auto direction_for = [keyQt, contains](const QKeySequence& forward,
                                           const QKeySequence& backward) -> Direction {
        if (contains(forward, keyQt))
            return Forward;
        if (contains(backward, keyQt))
            return Backward;
        if (!(keyQt & Qt::ShiftModifier))
            return Steady;

        // Before testing the unshifted key (Ctrl+A vs. Ctrl+Shift+a etc.), see whether this is
        // +Shift+Tab and check that against +Shift+Backtab (as well)
        Qt::KeyboardModifiers mods = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier
            | Qt::MetaModifier | Qt::KeypadModifier | Qt::GroupSwitchModifier;
        mods &= keyQt;
        if ((keyQt & ~mods) == Qt::Key_Tab) {
            if (contains(forward, mods | Qt::Key_Backtab))
                return Forward;
            if (contains(backward, mods | Qt::Key_Backtab))
                return Backward;
        }

        // if the shortcuts do not match, try matching again after filtering the shift key from
        // keyQt it is needed to handle correctly the ALT+~ shorcut for example as it is coded as
        // ALT+SHIFT+~ in keyQt
        if (contains(forward, keyQt & ~Qt::ShiftModifier))
            return Forward;
        if (contains(backward, keyQt & ~Qt::ShiftModifier))
            return Backward;

        return Steady;
    };

    if (m_tab_grab) {
        static const int mode_count = 4;
        static const TabBoxMode modes[mode_count] = {TabBoxWindowsMode,
                                                     TabBoxWindowsAlternativeMode,
                                                     TabBoxCurrentAppWindowsMode,
                                                     TabBoxCurrentAppWindowsAlternativeMode};
        const QKeySequence cuts[2 * mode_count]
            = {// forward
               m_cut_walk_through_windows,
               m_cut_walk_through_windows_alternative,
               m_cut_walk_through_current_app_windows,
               m_cut_walk_through_current_app_windows_alternative,
               // backward
               m_cut_walk_through_windows_reverse,
               m_cut_walk_through_windows_alternative_reverse,
               m_cut_walk_through_current_app_windows_reverse,
               m_cut_walk_through_current_app_windows_alternative_reverse};
        bool tested_current = false; // in case of collision, prefer to stay in the current mode
        int i = 0, j = 0;
        while (true) {
            if (!tested_current && modes[i] != mode()) {
                ++j;
                i = (i + 1) % mode_count;
                continue;
            }
            if (tested_current && modes[i] == mode()) {
                break;
            }
            tested_current = true;
            direction = direction_for(cuts[i], cuts[i + mode_count]);
            if (direction != Steady) {
                if (modes[i] != mode()) {
                    accept(false);
                    set_mode(modes[i]);
                    auto replayWithChangedTabboxMode = [this, direction]() {
                        reset();
                        next_prev(direction == Forward);
                    };
                    QTimer::singleShot(50, this, replayWithChangedTabboxMode);
                }
                break;
            } else if (++j > 2 * mode_count) { // guarding counter for invalid modes
                qCDebug(KWIN_TABBOX) << "Invalid TabBoxMode";
                return;
            }
            i = (i + 1) % mode_count;
        }
        if (direction != Steady) {
            qCDebug(KWIN_TABBOX) << "== " << cuts[i].toString() << " or "
                                 << cuts[i + mode_count].toString();
            kde_walk_through_windows(direction == Forward);
        }
    } else if (m_desktop_grab) {
        direction = direction_for(m_cut_walk_through_desktops, m_cut_walk_through_desktops_reverse);
        if (direction == Steady)
            direction = direction_for(m_cut_walk_through_desktop_list,
                                      m_cut_walk_through_desktop_list_reverse);
        if (direction != Steady)
            walk_through_desktops(direction == Forward);
    }

    if (m_desktop_grab || m_tab_grab) {
        if (((keyQt & ~Qt::KeyboardModifierMask) == Qt::Key_Escape) && direction == Steady) {
            // if Escape is part of the shortcut, don't cancel
            close(true);
        } else if (direction == Steady) {
            QKeyEvent event(QEvent::KeyPress, keyQt & ~Qt::KeyboardModifierMask, Qt::NoModifier);
            grabbed_key_event(&event);
        }
    }
}

void tabbox::close(bool abort)
{
    if (is_grabbed()) {
        remove_tabbox_grab();
    }
    hide(abort);
    kwinApp()->input->redirect->pointer()->setEnableConstraints(true);
    m_tab_grab = false;
    m_desktop_grab = false;
    m_no_modifier_grab = false;
}

void tabbox::accept(bool closeTabBox)
{
    auto c = current_client();
    if (closeTabBox)
        close();
    if (c) {
        space.activateClient(c);
        if (win::is_desktop(c))
            space.setShowingDesktop(!space.showingDesktop());
    }
}

void tabbox::modifiers_released()
{
    if (m_no_modifier_grab) {
        return;
    }
    if (m_tab_grab) {
        bool old_control_grab = m_desktop_grab;
        accept();
        m_desktop_grab = old_control_grab;
    }
    if (m_desktop_grab) {
        bool old_tab_grab = m_tab_grab;
        int desktop = current_desktop();
        close();
        m_tab_grab = old_tab_grab;
        if (desktop != -1) {
            set_current_desktop(desktop);
            space.virtual_desktop_manager->setCurrent(desktop);
        }
    }
}

int tabbox::next_desktop_static(int iDesktop) const
{
    win::virtual_desktop_next functor(*space.virtual_desktop_manager);
    return functor(iDesktop, true);
}

int tabbox::previous_desktop_static(int iDesktop) const
{
    win::virtual_desktop_previous functor(*space.virtual_desktop_manager);
    return functor(iDesktop, true);
}

std::vector<Toplevel*> get_windows_with_control(std::vector<Toplevel*>& windows)
{
    std::vector<Toplevel*> with_control;
    for (auto win : windows) {
        if (win->control) {
            with_control.push_back(win);
        }
    }
    return with_control;
}

/**
 * Auxiliary functions to travers all clients according to the static
 * order. Useful for the CDE-style Alt-tab feature.
 */
Toplevel* tabbox::next_client_static(Toplevel* c) const
{
    auto const& list = get_windows_with_control(space.m_windows);
    if (!c || list.empty()) {
        return nullptr;
    }
    auto pos = index_of(list, c);
    if (pos == -1) {
        return list.front();
    }
    ++pos;
    if (pos == static_cast<int>(list.size())) {
        return list.front();
    }
    return list.at(pos);
}

/**
 * Auxiliary functions to travers all clients according to the static
 * order. Useful for the CDE-style Alt-tab feature.
 */
Toplevel* tabbox::previous_client_static(Toplevel* c) const
{
    auto const& list = get_windows_with_control(space.m_windows);
    if (!c || list.empty()) {
        return nullptr;
    }

    auto pos = index_of(list, c);
    if (pos == -1) {
        return list.back();
    }
    if (pos == 0) {
        return list.back();
    }
    --pos;
    return list.at(pos);
}

bool tabbox::establish_tabbox_grab()
{
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        m_forced_global_mouse_grab = true;
        return true;
    }
    kwinApp()->update_x11_time_from_clock();
    if (!base::x11::grab_keyboard())
        return false;
    // Don't try to establish a global mouse grab using XGrabPointer, as that would prevent
    // using Alt+Tab while DND (#44972). However force passive grabs on all windows
    // in order to catch MouseRelease events and close the tabbox (#67416).
    // All clients already have passive grabs in their wrapper windows, so check only
    // the active client, which may not have it.
    Q_ASSERT(!m_forced_global_mouse_grab);
    m_forced_global_mouse_grab = true;
    if (space.active_client) {
        space.active_client->control->update_mouse_grab();
    }
    m_x11_event_filter.reset(new tabbox_x11_filter(*this));
    return true;
}

void tabbox::remove_tabbox_grab()
{
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        m_forced_global_mouse_grab = false;
        return;
    }
    kwinApp()->update_x11_time_from_clock();
    base::x11::ungrab_keyboard();
    Q_ASSERT(m_forced_global_mouse_grab);
    m_forced_global_mouse_grab = false;
    if (space.active_client) {
        space.active_client->control->update_mouse_grab();
    }
    m_x11_event_filter.reset();
}
} // namespace win
} // namespace
