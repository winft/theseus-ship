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
// own
#include "tabbox.h"
// tabbox
#include "tabbox/clientmodel.h"
#include "tabbox/desktopmodel.h"
#include "tabbox/tabboxconfig.h"
#include "tabbox/desktopchain.h"
#include "tabbox/tabbox_logging.h"
#include "tabbox/x11_filter.h"

#include "base/platform.h"
#include "base/x11/grabs.h"
#include "base/x11/xcb/proto.h"
#include "render/effects.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/redirect.h"
#include "input/xkb/helpers.h"
#include "screens.h"
#include "win/screen_edges.h"
#include "win/virtual_desktops.h"
#include "workspace.h"

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

namespace TabBox
{

TabBoxHandlerImpl::TabBoxHandlerImpl(TabBox* tabBox)
    : TabBoxHandler(tabBox)
    , m_tabBox(tabBox)
    , m_desktopFocusChain(new DesktopChainManager(this))
{
    // connects for DesktopFocusChainManager
    auto vds = win::virtual_desktop_manager::self();
    connect(vds, &win::virtual_desktop_manager::countChanged, m_desktopFocusChain, &DesktopChainManager::resize);
    connect(vds, &win::virtual_desktop_manager::currentChanged, m_desktopFocusChain, &DesktopChainManager::addDesktop);
}

TabBoxHandlerImpl::~TabBoxHandlerImpl()
{
}

int TabBoxHandlerImpl::activeScreen() const
{
    return kwinApp()->get_base().screens.current();
}

int TabBoxHandlerImpl::currentDesktop() const
{
    return win::virtual_desktop_manager::self()->current();
}

QString TabBoxHandlerImpl::desktopName(TabBoxClient* client) const
{
    if (TabBoxClientImpl* c = static_cast< TabBoxClientImpl* >(client)) {
        if (!c->client()->isOnAllDesktops())
            return win::virtual_desktop_manager::self()->name(c->client()->desktop());
    }
    return win::virtual_desktop_manager::self()->name(win::virtual_desktop_manager::self()->current());
}

QString TabBoxHandlerImpl::desktopName(int desktop) const
{
    return win::virtual_desktop_manager::self()->name(desktop);
}

std::weak_ptr<TabBoxClient> TabBoxHandlerImpl::nextClientFocusChain(TabBoxClient* client) const
{
    if (TabBoxClientImpl* c = static_cast< TabBoxClientImpl* >(client)) {
        auto next = win::focus_chain::self()->nextMostRecentlyUsed(c->client());
        if (next) {
            return next->control->tabbox();
        }
    }
    return std::weak_ptr<TabBoxClient>();
}

std::weak_ptr<TabBoxClient> TabBoxHandlerImpl::firstClientFocusChain() const
{
    if (auto c = win::focus_chain::self()->firstMostRecentlyUsed()) {
        return c->control->tabbox();
    } else {
        return std::weak_ptr<TabBoxClient>();
    }
}

bool TabBoxHandlerImpl::isInFocusChain(TabBoxClient *client) const
{
    if (TabBoxClientImpl *c = static_cast<TabBoxClientImpl*>(client)) {
        return win::focus_chain::self()->contains(c->client());
    }
    return false;
}

int TabBoxHandlerImpl::nextDesktopFocusChain(int desktop) const
{
    return m_desktopFocusChain->next(desktop);
}

int TabBoxHandlerImpl::numberOfDesktops() const
{
    return win::virtual_desktop_manager::self()->count();
}

std::weak_ptr<TabBoxClient> TabBoxHandlerImpl::activeClient() const
{
    if (Workspace::self()->activeClient())
        return Workspace::self()->activeClient()->control->tabbox();
    else
        return std::weak_ptr<TabBoxClient>();
}

bool TabBoxHandlerImpl::checkDesktop(TabBoxClient* client, int desktop) const
{
    auto current = (static_cast< TabBoxClientImpl* >(client))->client();

    switch (config().clientDesktopMode()) {
    case TabBoxConfig::AllDesktopsClients:
        return true;
    case TabBoxConfig::ExcludeCurrentDesktopClients:
        return !current->isOnDesktop(desktop);
    default:       // TabBoxConfig::OnlyCurrentDesktopClients
        return current->isOnDesktop(desktop);
    }
}

bool TabBoxHandlerImpl::checkApplications(TabBoxClient* client) const
{
    auto current = (static_cast< TabBoxClientImpl* >(client))->client();
    TabBoxClientImpl* c;

    switch (config().clientApplicationsMode()) {
    case TabBoxConfig::OneWindowPerApplication:
        // check if the list already contains an entry of this application
        for (auto client_weak : clientList()) {
            auto client = client_weak.lock();
            if (!client) {
                continue;
            }
            if ((c = dynamic_cast< TabBoxClientImpl* >(client.get()))) {
                if (win::belong_to_same_client(c->client(), current, win::same_client_check::allow_cross_process)) {
                    return false;
                }
            }
        }
        return true;
    case TabBoxConfig::AllWindowsCurrentApplication: {
        auto pointer = tabBox->activeClient().lock();
        if (!pointer) {
            return false;
        }
        if ((c = dynamic_cast< TabBoxClientImpl* >(pointer.get()))) {
            if (win::belong_to_same_client(c->client(), current, win::same_client_check::allow_cross_process)) {
                return true;
            }
        }
        return false;
    }
    default:       // TabBoxConfig::AllWindowsAllApplications
      return true;
    }
}

bool TabBoxHandlerImpl::checkMinimized(TabBoxClient* client) const
{
    switch (config().clientMinimizedMode()) {
    case TabBoxConfig::ExcludeMinimizedClients:
        return !client->isMinimized();
    case TabBoxConfig::OnlyMinimizedClients:
        return client->isMinimized();
    default:       // TabBoxConfig::IgnoreMinimizedStatus
        return true;
    }
}

bool TabBoxHandlerImpl::checkMultiScreen(TabBoxClient* client) const
{
    auto current = (static_cast< TabBoxClientImpl* >(client))->client();

    switch (config().clientMultiScreenMode()) {
    case TabBoxConfig::IgnoreMultiScreen:
        return true;
    case TabBoxConfig::ExcludeCurrentScreenClients:
        return current->screen() != kwinApp()->get_base().screens.current();
    default:       // TabBoxConfig::OnlyCurrentScreenClients
        return current->screen() == kwinApp()->get_base().screens.current();
    }
}

std::weak_ptr<TabBoxClient> TabBoxHandlerImpl::clientToAddToList(TabBoxClient* client, int desktop) const
{
    if (!client) {
        return std::weak_ptr<TabBoxClient>();
    }
    Toplevel* ret = nullptr;
    auto current = (static_cast< TabBoxClientImpl* >(client))->client();

    bool addClient = checkDesktop(client, desktop)
                  && checkApplications(client)
                  && checkMinimized(client)
                  && checkMultiScreen(client);
    addClient = addClient && win::wants_tab_focus(current) && !current->control->skip_switcher();
    if (addClient) {
        // don't add windows that have modal dialogs
        auto modal = current->findModal();
        if (!modal || !modal->control || modal == current) {
            ret = current;
        } else {
            auto const cl = clientList();
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
    return ret ? ret->control->tabbox() : std::weak_ptr<TabBoxClient>();
}

TabBoxClientList TabBoxHandlerImpl::stackingOrder() const
{
    auto const stacking = workspace()->stacking_order->sorted();
    TabBoxClientList ret;
    for (auto const& toplevel : stacking) {
        if (toplevel->control) {
            ret.push_back(toplevel->control->tabbox());
        }
    }
    return ret;
}

bool TabBoxHandlerImpl::isKWinCompositing() const {
    return Workspace::self()->compositing();
}

void TabBoxHandlerImpl::raiseClient(TabBoxClient* c) const
{
    win::raise_window(workspace(), static_cast<TabBoxClientImpl*>(c)->client());
}

void TabBoxHandlerImpl::restack(TabBoxClient *c, TabBoxClient *under)
{
    win::restack(workspace(),
                 static_cast<TabBoxClientImpl*>(c)->client(),
                 static_cast<TabBoxClientImpl*>(under)->client(),
                 true);
}

void TabBoxHandlerImpl::elevateClient(TabBoxClient *c, QWindow *tabbox, bool b) const
{
    auto cl = static_cast<TabBoxClientImpl*>(c)->client();
    win::elevate(cl, b);
    if (auto w = Workspace::self()->findInternal(tabbox)) {
        win::elevate(w, b);
    }
}

std::weak_ptr<TabBoxClient> TabBoxHandlerImpl::desktopClient() const
{
    for (auto const& window : workspace()->stacking_order->sorted()) {
        if (window->control && win::is_desktop(window) && window->isOnCurrentDesktop() && window->screen() == kwinApp()->get_base().screens.current()) {
            return window->control->tabbox();
        }
    }
    return std::weak_ptr<TabBoxClient>();
}

void TabBoxHandlerImpl::activateAndClose()
{
    m_tabBox->accept();
}

void TabBoxHandlerImpl::highlightWindows(TabBoxClient *window, QWindow *controller)
{
    if (!effects) {
        return;
    }
    QVector<EffectWindow*> windows;
    if (window) {
        windows << static_cast<TabBoxClientImpl*>(window)->client()->render->effect.get();
    }
    if (auto t = workspace()->findInternal(controller)) {
        windows << t->render->effect.get();
    }
    static_cast<render::effects_handler_impl*>(effects)->highlightWindows(windows);
}

bool TabBoxHandlerImpl::noModifierGrab() const
{
    return m_tabBox->noModifierGrab();
}

/*********************************************************
* TabBoxClientImpl
*********************************************************/

TabBoxClientImpl::TabBoxClientImpl(Toplevel* window)
    : TabBoxClient()
    , m_client(window)
{
}

TabBoxClientImpl::~TabBoxClientImpl()
{
}

QString TabBoxClientImpl::caption() const
{
    if (win::is_desktop(m_client))
        return i18nc("Special entry in alt+tab list for minimizing all windows",
                     "Show Desktop");
    return win::caption(m_client);
}

QIcon TabBoxClientImpl::icon() const
{
    if (win::is_desktop(m_client)) {
        return QIcon::fromTheme(QStringLiteral("user-desktop"));
    }
    return m_client->control->icon();
}

bool TabBoxClientImpl::isMinimized() const
{
    return m_client->control->minimized();
}

int TabBoxClientImpl::x() const
{
    return m_client->pos().x();
}

int TabBoxClientImpl::y() const
{
    return m_client->pos().y();
}

int TabBoxClientImpl::width() const
{
    return m_client->size().width();
}

int TabBoxClientImpl::height() const
{
    return m_client->size().height();
}

bool TabBoxClientImpl::isCloseable() const
{
    return m_client->isCloseable();
}

void TabBoxClientImpl::close()
{
    m_client->closeWindow();
}

bool TabBoxClientImpl::isFirstInTabBox() const
{
    return m_client->control->first_in_tabbox();
}

QUuid TabBoxClientImpl::internalId() const
{
    return m_client->internalId();
}

/*********************************************************
* TabBox
*********************************************************/
TabBox *TabBox::s_self = nullptr;

TabBox *TabBox::create(QObject *parent)
{
    Q_ASSERT(!s_self);
    s_self = new TabBox(parent);
    return s_self;
}

TabBox::TabBox(QObject *parent)
    : QObject(parent)
    , m_displayRefcount(0)
    , m_desktopGrab(false)
    , m_tabGrab(false)
    , m_noModifierGrab(false)
    , m_forcedGlobalMouseGrab(false)
    , m_ready(false)
{
    m_isShown = false;
    m_defaultConfig = TabBoxConfig();
    m_defaultConfig.setTabBoxMode(TabBoxConfig::ClientTabBox);
    m_defaultConfig.setClientDesktopMode(TabBoxConfig::OnlyCurrentDesktopClients);
    m_defaultConfig.setClientApplicationsMode(TabBoxConfig::AllWindowsAllApplications);
    m_defaultConfig.setClientMinimizedMode(TabBoxConfig::IgnoreMinimizedStatus);
    m_defaultConfig.setShowDesktopMode(TabBoxConfig::DoNotShowDesktopClient);
    m_defaultConfig.setClientMultiScreenMode(TabBoxConfig::IgnoreMultiScreen);
    m_defaultConfig.setClientSwitchingMode(TabBoxConfig::FocusChainSwitching);

    m_alternativeConfig = TabBoxConfig();
    m_alternativeConfig.setTabBoxMode(TabBoxConfig::ClientTabBox);
    m_alternativeConfig.setClientDesktopMode(TabBoxConfig::AllDesktopsClients);
    m_alternativeConfig.setClientApplicationsMode(TabBoxConfig::AllWindowsAllApplications);
    m_alternativeConfig.setClientMinimizedMode(TabBoxConfig::IgnoreMinimizedStatus);
    m_alternativeConfig.setShowDesktopMode(TabBoxConfig::DoNotShowDesktopClient);
    m_alternativeConfig.setClientMultiScreenMode(TabBoxConfig::IgnoreMultiScreen);
    m_alternativeConfig.setClientSwitchingMode(TabBoxConfig::FocusChainSwitching);

    m_defaultCurrentApplicationConfig = m_defaultConfig;
    m_defaultCurrentApplicationConfig.setClientApplicationsMode(TabBoxConfig::AllWindowsCurrentApplication);

    m_alternativeCurrentApplicationConfig = m_alternativeConfig;
    m_alternativeCurrentApplicationConfig.setClientApplicationsMode(TabBoxConfig::AllWindowsCurrentApplication);

    m_desktopConfig = TabBoxConfig();
    m_desktopConfig.setTabBoxMode(TabBoxConfig::DesktopTabBox);
    m_desktopConfig.setShowTabBox(true);
    m_desktopConfig.setShowDesktopMode(TabBoxConfig::DoNotShowDesktopClient);
    m_desktopConfig.setDesktopSwitchingMode(TabBoxConfig::MostRecentlyUsedDesktopSwitching);

    m_desktopListConfig = TabBoxConfig();
    m_desktopListConfig.setTabBoxMode(TabBoxConfig::DesktopTabBox);
    m_desktopListConfig.setShowTabBox(true);
    m_desktopListConfig.setShowDesktopMode(TabBoxConfig::DoNotShowDesktopClient);
    m_desktopListConfig.setDesktopSwitchingMode(TabBoxConfig::StaticDesktopSwitching);
    m_tabBox = new TabBoxHandlerImpl(this);
    QTimer::singleShot(0, this, &TabBox::handlerReady);

    m_tabBoxMode = TabBoxDesktopMode; // init variables
    connect(&m_delayedShowTimer, &QTimer::timeout, this, &TabBox::show);
    connect(Workspace::self(), &Workspace::configChanged, this, &TabBox::reconfigure);
}

TabBox::~TabBox()
{
    s_self = nullptr;
}

void TabBox::handlerReady()
{
    m_tabBox->setConfig(m_defaultConfig);
    reconfigure();
    m_ready = true;
}

template <typename Slot>
void TabBox::key(const char *actionName, Slot slot, const QKeySequence &shortcut)
{
    QAction *a = new QAction(this);
    a->setProperty("componentName", QStringLiteral(KWIN_NAME));
    a->setObjectName(QString::fromUtf8(actionName));
    a->setText(i18n(actionName));
    KGlobalAccel::self()->setGlobalShortcut(a, QList<QKeySequence>() << shortcut);
    kwinApp()->input->redirect->registerShortcut(shortcut, a, TabBox::self(), slot);
    auto cuts = KGlobalAccel::self()->shortcut(a);
    globalShortcutChanged(a, cuts.isEmpty() ? QKeySequence() : cuts.first());
}

static const char s_windows[]        = I18N_NOOP("Walk Through Windows");
static const char s_windowsRev[]     = I18N_NOOP("Walk Through Windows (Reverse)");
static const char s_windowsAlt[]     = I18N_NOOP("Walk Through Windows Alternative");
static const char s_windowsAltRev[]  = I18N_NOOP("Walk Through Windows Alternative (Reverse)");
static const char s_app[]            = I18N_NOOP("Walk Through Windows of Current Application");
static const char s_appRev[]         = I18N_NOOP("Walk Through Windows of Current Application (Reverse)");
static const char s_appAlt[]         = I18N_NOOP("Walk Through Windows of Current Application Alternative");
static const char s_appAltRev[]      = I18N_NOOP("Walk Through Windows of Current Application Alternative (Reverse)");
static const char s_desktops[]       = I18N_NOOP("Walk Through Desktops");
static const char s_desktopsRev[]    = I18N_NOOP("Walk Through Desktops (Reverse)");
static const char s_desktopList[]    = I18N_NOOP("Walk Through Desktop List");
static const char s_desktopListRev[] = I18N_NOOP("Walk Through Desktop List (Reverse)");

void TabBox::initShortcuts()
{
    key(s_windows,        &TabBox::slotWalkThroughWindows, Qt::ALT + Qt::Key_Tab);
    key(s_windowsRev,     &TabBox::slotWalkBackThroughWindows, Qt::ALT + Qt::SHIFT + Qt::Key_Backtab);
    key(s_app,            &TabBox::slotWalkThroughCurrentAppWindows, Qt::ALT + Qt::Key_QuoteLeft);
    key(s_appRev,         &TabBox::slotWalkBackThroughCurrentAppWindows, Qt::ALT + Qt::Key_AsciiTilde);
    key(s_windowsAlt,     &TabBox::slotWalkThroughWindowsAlternative);
    key(s_windowsAltRev,  &TabBox::slotWalkBackThroughWindowsAlternative);
    key(s_appAlt,         &TabBox::slotWalkThroughCurrentAppWindowsAlternative);
    key(s_appAltRev,      &TabBox::slotWalkBackThroughCurrentAppWindowsAlternative);
    key(s_desktops,       &TabBox::slotWalkThroughDesktops);
    key(s_desktopsRev,    &TabBox::slotWalkBackThroughDesktops);
    key(s_desktopList,    &TabBox::slotWalkThroughDesktopList);
    key(s_desktopListRev, &TabBox::slotWalkBackThroughDesktopList);

    connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutChanged, this, &TabBox::globalShortcutChanged);
}

void TabBox::globalShortcutChanged(QAction *action, const QKeySequence &seq)
{
    if (qstrcmp(qPrintable(action->objectName()), s_windows) == 0) {
        m_cutWalkThroughWindows = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_windowsRev) == 0) {
        m_cutWalkThroughWindowsReverse = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_app) == 0) {
        m_cutWalkThroughCurrentAppWindows = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_appRev) == 0) {
        m_cutWalkThroughCurrentAppWindowsReverse = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_windowsAlt) == 0) {
        m_cutWalkThroughWindowsAlternative = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_windowsAltRev) == 0) {
        m_cutWalkThroughWindowsAlternativeReverse = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_appAlt) == 0) {
        m_cutWalkThroughCurrentAppWindowsAlternative = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_appAltRev) == 0) {
        m_cutWalkThroughCurrentAppWindowsAlternativeReverse = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_desktops) == 0) {
        m_cutWalkThroughDesktops = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_desktopsRev) == 0) {
        m_cutWalkThroughDesktopsReverse = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_desktopList) == 0) {
        m_cutWalkThroughDesktopList = seq;
    } else if (qstrcmp(qPrintable(action->objectName()), s_desktopListRev) == 0) {
        m_cutWalkThroughDesktopListReverse = seq;
    }
}

void TabBox::setMode(TabBoxMode mode)
{
    m_tabBoxMode = mode;
    switch(mode) {
    case TabBoxWindowsMode:
        m_tabBox->setConfig(m_defaultConfig);
        break;
    case TabBoxWindowsAlternativeMode:
        m_tabBox->setConfig(m_alternativeConfig);
        break;
    case TabBoxCurrentAppWindowsMode:
        m_tabBox->setConfig(m_defaultCurrentApplicationConfig);
        break;
    case TabBoxCurrentAppWindowsAlternativeMode:
        m_tabBox->setConfig(m_alternativeCurrentApplicationConfig);
        break;
    case TabBoxDesktopMode:
        m_tabBox->setConfig(m_desktopConfig);
        break;
    case TabBoxDesktopListMode:
        m_tabBox->setConfig(m_desktopListConfig);
        break;
    }
}

void TabBox::reset(bool partial_reset)
{
    switch(m_tabBox->config().tabBoxMode()) {
    case TabBoxConfig::ClientTabBox:
        m_tabBox->createModel(partial_reset);
        if (!partial_reset) {
            if (Workspace::self()->activeClient())
                setCurrentClient(Workspace::self()->activeClient());
            // it's possible that the active client is not part of the model
            // in that case the index is invalid
            if (!m_tabBox->currentIndex().isValid())
                setCurrentIndex(m_tabBox->first());
        } else {
            if (!m_tabBox->currentIndex().isValid() || !m_tabBox->client(m_tabBox->currentIndex()))
                setCurrentIndex(m_tabBox->first());
        }
        break;
    case TabBoxConfig::DesktopTabBox:
        m_tabBox->createModel();

        if (!partial_reset)
            setCurrentDesktop(win::virtual_desktop_manager::self()->current());
        break;
    }

    Q_EMIT tabBoxUpdated();
}

void TabBox::nextPrev(bool next)
{
    setCurrentIndex(m_tabBox->nextPrev(next), false);
    Q_EMIT tabBoxUpdated();
}

Toplevel* TabBox::currentClient()
{
    if (TabBoxClientImpl* client = static_cast< TabBoxClientImpl* >(m_tabBox->client(m_tabBox->currentIndex()))) {
        if (!Workspace::self()->hasClient(client->client()))
            return nullptr;
        return client->client();
    } else
        return nullptr;
}

QList<Toplevel*> TabBox::currentClientList()
{
    auto const list = m_tabBox->clientList();
    QList<Toplevel*> ret;

    for (auto& clientPointer : list) {
        auto client = clientPointer.lock();
        if (!client) {
            continue;
        }
        if (auto c = static_cast<TabBoxClientImpl const*>(client.get())) {
            ret.append(c->client());
        }
    }
    return ret;
}

int TabBox::currentDesktop()
{
    return m_tabBox->desktop(m_tabBox->currentIndex());
}

QList< int > TabBox::currentDesktopList()
{
    return m_tabBox->desktopList();
}

void TabBox::setCurrentClient(Toplevel* window)
{
    setCurrentIndex(m_tabBox->index(window->control->tabbox().lock().get()));
}

void TabBox::setCurrentDesktop(int newDesktop)
{
    setCurrentIndex(m_tabBox->desktopIndex(newDesktop));
}

void TabBox::setCurrentIndex(QModelIndex index, bool notifyEffects)
{
    if (!index.isValid())
        return;
    m_tabBox->setCurrentIndex(index);
    if (notifyEffects) {
        Q_EMIT tabBoxUpdated();
    }
}

void TabBox::show()
{
    Q_EMIT tabBoxAdded(m_tabBoxMode);
    if (isDisplayed()) {
        m_isShown = false;
        return;
    }
    workspace()->setShowingDesktop(false);
    reference();
    m_isShown = true;
    m_tabBox->show();
}

void TabBox::hide(bool abort)
{
    m_delayedShowTimer.stop();
    if (m_isShown) {
        m_isShown = false;
        unreference();
    }
    Q_EMIT tabBoxClosed();
    if (isDisplayed())
        qCDebug(KWIN_TABBOX) << "Tab box was not properly closed by an effect";
    m_tabBox->hide(abort);
    if (kwinApp()->x11Connection()) {
        base::x11::xcb::sync();
    }
}

void TabBox::reconfigure()
{
    KSharedConfigPtr c = kwinApp()->config();
    KConfigGroup config = c->group("TabBox");

    loadConfig(c->group("TabBox"), m_defaultConfig);
    loadConfig(c->group("TabBoxAlternative"), m_alternativeConfig);

    m_defaultCurrentApplicationConfig = m_defaultConfig;
    m_defaultCurrentApplicationConfig.setClientApplicationsMode(TabBoxConfig::AllWindowsCurrentApplication);
    m_alternativeCurrentApplicationConfig = m_alternativeConfig;
    m_alternativeCurrentApplicationConfig.setClientApplicationsMode(TabBoxConfig::AllWindowsCurrentApplication);

    m_tabBox->setConfig(m_defaultConfig);

    m_delayShow = config.readEntry<bool>("ShowDelay", true);
    m_delayShowTime = config.readEntry<int>("DelayTime", 90);

    const QString defaultDesktopLayout = QStringLiteral("org.kde.breeze.desktop");
    m_desktopConfig.setLayoutName(config.readEntry("DesktopLayout", defaultDesktopLayout));
    m_desktopListConfig.setLayoutName(config.readEntry("DesktopListLayout", defaultDesktopLayout));

    QList<ElectricBorder> *borders = &m_borderActivate;
    QString borderConfig = QStringLiteral("BorderActivate");
    for (int i = 0; i < 2; ++i) {
        for (auto const& border : qAsConst(*borders)) {
            workspace()->edges->unreserve(border, this);
        }
        borders->clear();
        QStringList list = config.readEntry(borderConfig, QStringList());
        for (auto const& s : qAsConst(list)) {
            bool ok;
            const int i = s.toInt(&ok);
            if (!ok)
                continue;
            borders->append(ElectricBorder(i));
            workspace()->edges->reserve(ElectricBorder(i), this, "toggle");
        }
        borders = &m_borderAlternativeActivate;
        borderConfig = QStringLiteral("BorderAlternativeActivate");
    }

    auto touchConfig = [this, config] (const QString &key, QHash<ElectricBorder, QAction *> &actions, TabBoxMode mode, const QStringList &defaults = QStringList{}) {
        // fist erase old config
        for (auto it = actions.begin(); it != actions.end(); ) {
            delete it.value();
            it = actions.erase(it);
        }
        // now new config
        const QStringList list = config.readEntry(key, defaults);
        for (const auto &s : list) {
            bool ok;
            const int i = s.toInt(&ok);
            if (!ok) {
                continue;
            }
            QAction *a = new QAction(this);
            connect(a, &QAction::triggered, this, std::bind(&TabBox::toggleMode, this, mode));
            workspace()->edges->reserveTouch(ElectricBorder(i), a);
            actions.insert(ElectricBorder(i), a);
        }
    };
    touchConfig(QStringLiteral("TouchBorderActivate"), m_touchActivate, TabBoxWindowsMode);
    touchConfig(QStringLiteral("TouchBorderAlternativeActivate"), m_touchAlternativeActivate, TabBoxWindowsAlternativeMode);
}

void TabBox::loadConfig(const KConfigGroup& config, TabBoxConfig& tabBoxConfig)
{
    tabBoxConfig.setClientDesktopMode(TabBoxConfig::ClientDesktopMode(
                                       config.readEntry<int>("DesktopMode", TabBoxConfig::defaultDesktopMode())));
    tabBoxConfig.setClientApplicationsMode(TabBoxConfig::ClientApplicationsMode(
                                       config.readEntry<int>("ApplicationsMode", TabBoxConfig::defaultApplicationsMode())));
    tabBoxConfig.setClientMinimizedMode(TabBoxConfig::ClientMinimizedMode(
                                       config.readEntry<int>("MinimizedMode", TabBoxConfig::defaultMinimizedMode())));
    tabBoxConfig.setShowDesktopMode(TabBoxConfig::ShowDesktopMode(
                                       config.readEntry<int>("ShowDesktopMode", TabBoxConfig::defaultShowDesktopMode())));
    tabBoxConfig.setClientMultiScreenMode(TabBoxConfig::ClientMultiScreenMode(
                                       config.readEntry<int>("MultiScreenMode", TabBoxConfig::defaultMultiScreenMode())));
    tabBoxConfig.setClientSwitchingMode(TabBoxConfig::ClientSwitchingMode(
                                            config.readEntry<int>("SwitchingMode", TabBoxConfig::defaultSwitchingMode())));

    tabBoxConfig.setShowTabBox(config.readEntry<bool>("ShowTabBox",
                               TabBoxConfig::defaultShowTabBox()));
    tabBoxConfig.setHighlightWindows(config.readEntry<bool>("HighlightWindows",
                                     TabBoxConfig::defaultHighlightWindow()));

    tabBoxConfig.setLayoutName(config.readEntry<QString>("LayoutName", TabBoxConfig::defaultLayoutName()));
}

void TabBox::delayedShow()
{
    if (isDisplayed() || m_delayedShowTimer.isActive())
        // already called show - no need to call it twice
        return;

    if (!m_delayShowTime) {
        show();
        return;
    }

    m_delayedShowTimer.setSingleShot(true);
    m_delayedShowTimer.start(m_delayShowTime);
}

bool TabBox::handleMouseEvent(QMouseEvent *event)
{
    if (!m_isShown && isDisplayed()) {
        // tabbox has been replaced, check effects
        if (effects && static_cast<render::effects_handler_impl*>(effects)->checkInputWindowEvent(event)) {
            return true;
        }
    }
    switch (event->type()) {
    case QEvent::MouseMove:
        if (!m_tabBox->containsPos(event->globalPos())) {
            // filter out all events which are not on the TabBox window.
            // We don't want windows to react on the mouse events
            return true;
        }
        return false;
    case QEvent::MouseButtonPress:
        if ((!m_isShown && isDisplayed()) || !m_tabBox->containsPos(event->globalPos())) {
            close();  // click outside closes tab
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

bool TabBox::handleWheelEvent(QWheelEvent *event)
{
    if (!m_isShown && isDisplayed()) {
        // tabbox has been replaced, check effects
        if (effects && static_cast<render::effects_handler_impl*>(effects)->checkInputWindowEvent(event)) {
            return true;
        }
    }
    if (event->angleDelta().y() == 0) {
        return false;
    }
    const QModelIndex index = m_tabBox->nextPrev(event->angleDelta().y() > 0);
    if (index.isValid()) {
        setCurrentIndex(index);
    }
    return true;
}

void TabBox::grabbedKeyEvent(QKeyEvent* event)
{
    Q_EMIT tabBoxKeyEvent(event);
    if (!m_isShown && isDisplayed()) {
        // tabbox has been replaced, check effects
        return;
    }
    if (m_noModifierGrab) {
        if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return || event->key() == Qt::Key_Space) {
            accept();
            return;
        }
    }
    m_tabBox->grabbedKeyEvent(event);
}

struct KeySymbolsDeleter
{
    static inline void cleanup(xcb_key_symbols_t *symbols)
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

    QScopedPointer<xcb_key_symbols_t, KeySymbolsDeleter> symbols(xcb_key_symbols_alloc(connection()));
    if (symbols.isNull() || !keys) {
        return false;
    }
    const auto keymap = keys->keys;

    bool depressed = false;
    for (int iKeySym = 0; iKeySym < nKeySyms; iKeySym++) {
        uint keySymX = keySyms[ iKeySym ];
        xcb_keycode_t *keyCodes = xcb_key_symbols_get_keycode(symbols.data(), keySymX);
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

            qCDebug(KWIN_TABBOX)    << iKeySym << ": keySymX=0x" << QString::number(keySymX, 16)
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

static bool areModKeysDepressedX11(const QKeySequence &seq)
{
    uint rgKeySyms[10];
    int nKeySyms = 0;
    int mod = seq[seq.count()-1] & Qt::KeyboardModifierMask;

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

static bool areModKeysDepressedWayland(const QKeySequence &seq)
{
    const int mod = seq[seq.count()-1] & Qt::KeyboardModifierMask;
    auto const mods = input::xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(kwinApp()->input);
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

static bool areModKeysDepressed(const QKeySequence& seq) {
    if (seq.isEmpty())
        return false;
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        return areModKeysDepressedWayland(seq);
    } else {
        return areModKeysDepressedX11(seq);
    }
}

void TabBox::navigatingThroughWindows(bool forward, const QKeySequence &shortcut, TabBoxMode mode)
{
    if (!m_ready || isGrabbed()) {
        return;
    }
    if (!options->focusPolicyIsReasonable()) {
        //ungrabXKeyboard(); // need that because of accelerator raw mode
        // CDE style raise / lower
        CDEWalkThroughWindows(forward);
    } else {
        if (areModKeysDepressed(shortcut)) {
            if (startKDEWalkThroughWindows(mode))
                KDEWalkThroughWindows(forward);
        } else
            // if the shortcut has no modifiers, don't show the tabbox,
            // don't grab, but simply go to the next window
            KDEOneStepThroughWindows(forward, mode);
    }
}

void TabBox::slotWalkThroughWindows()
{
    navigatingThroughWindows(true, m_cutWalkThroughWindows, TabBoxWindowsMode);
}

void TabBox::slotWalkBackThroughWindows()
{
    navigatingThroughWindows(false, m_cutWalkThroughWindowsReverse, TabBoxWindowsMode);
}

void TabBox::slotWalkThroughWindowsAlternative()
{
    navigatingThroughWindows(true, m_cutWalkThroughWindowsAlternative, TabBoxWindowsAlternativeMode);
}

void TabBox::slotWalkBackThroughWindowsAlternative()
{
    navigatingThroughWindows(false, m_cutWalkThroughWindowsAlternativeReverse, TabBoxWindowsAlternativeMode);
}

void TabBox::slotWalkThroughCurrentAppWindows()
{
    navigatingThroughWindows(true, m_cutWalkThroughCurrentAppWindows, TabBoxCurrentAppWindowsMode);
}

void TabBox::slotWalkBackThroughCurrentAppWindows()
{
    navigatingThroughWindows(false, m_cutWalkThroughCurrentAppWindowsReverse, TabBoxCurrentAppWindowsMode);
}

void TabBox::slotWalkThroughCurrentAppWindowsAlternative()
{
    navigatingThroughWindows(true, m_cutWalkThroughCurrentAppWindowsAlternative, TabBoxCurrentAppWindowsAlternativeMode);
}

void TabBox::slotWalkBackThroughCurrentAppWindowsAlternative()
{
    navigatingThroughWindows(false, m_cutWalkThroughCurrentAppWindowsAlternativeReverse, TabBoxCurrentAppWindowsAlternativeMode);
}

void TabBox::slotWalkThroughDesktops()
{
    if (!m_ready || isGrabbed()) {
        return;
    }
    if (areModKeysDepressed(m_cutWalkThroughDesktops)) {
        if (startWalkThroughDesktops())
            walkThroughDesktops(true);
    } else {
        oneStepThroughDesktops(true);
    }
}

void TabBox::slotWalkBackThroughDesktops()
{
    if (!m_ready || isGrabbed()) {
        return;
    }
    if (areModKeysDepressed(m_cutWalkThroughDesktopsReverse)) {
        if (startWalkThroughDesktops())
            walkThroughDesktops(false);
    } else {
        oneStepThroughDesktops(false);
    }
}

void TabBox::slotWalkThroughDesktopList()
{
    if (!m_ready || isGrabbed()) {
        return;
    }
    if (areModKeysDepressed(m_cutWalkThroughDesktopList)) {
        if (startWalkThroughDesktopList())
            walkThroughDesktops(true);
    } else {
        oneStepThroughDesktopList(true);
    }
}

void TabBox::slotWalkBackThroughDesktopList()
{
    if (!m_ready || isGrabbed()) {
        return;
    }
    if (areModKeysDepressed(m_cutWalkThroughDesktopListReverse)) {
        if (startWalkThroughDesktopList())
            walkThroughDesktops(false);
    } else {
        oneStepThroughDesktopList(false);
    }
}

bool TabBox::toggle(ElectricBorder eb)
{
    if (m_borderAlternativeActivate.contains(eb)) {
        return toggleMode(TabBoxWindowsAlternativeMode);
    } else {
        return toggleMode(TabBoxWindowsMode);
    }
}

bool TabBox::toggleMode(TabBoxMode mode)
{
    if (!options->focusPolicyIsReasonable())
        return false; // not supported.
    if (isDisplayed()) {
        accept();
        return true;
    }
    if (!establishTabBoxGrab())
        return false;
    m_noModifierGrab = m_tabGrab = true;
    setMode(mode);
    reset();
    show();
    return true;
}

bool TabBox::startKDEWalkThroughWindows(TabBoxMode mode)
{
    if (!establishTabBoxGrab())
        return false;
    m_tabGrab = true;
    m_noModifierGrab = false;
    setMode(mode);
    reset();
    return true;
}

bool TabBox::startWalkThroughDesktops(TabBoxMode mode)
{
    if (!establishTabBoxGrab())
        return false;
    m_desktopGrab = true;
    m_noModifierGrab = false;
    setMode(mode);
    reset();
    return true;
}

bool TabBox::startWalkThroughDesktops()
{
    return startWalkThroughDesktops(TabBoxDesktopMode);
}

bool TabBox::startWalkThroughDesktopList()
{
    return startWalkThroughDesktops(TabBoxDesktopListMode);
}

void TabBox::KDEWalkThroughWindows(bool forward)
{
    nextPrev(forward);
    delayedShow();
}

void TabBox::walkThroughDesktops(bool forward)
{
    nextPrev(forward);
    delayedShow();
}

void TabBox::CDEWalkThroughWindows(bool forward)
{
    Toplevel* c = nullptr;
// this function find the first suitable client for unreasonable focus
// policies - the topmost one, with some exceptions (can't be keepabove/below,
// otherwise it gets stuck on them)
//     Q_ASSERT(Workspace::self()->block_stacking_updates == 0);
    for (int i = workspace()->stacking_order->sorted().size() - 1;
            i >= 0 ;
            --i) {
        auto window = workspace()->stacking_order->sorted().at(i);
        if (window->control && window->isOnCurrentDesktop() &&
                !win::is_special_window(window)
                && window->isShown() && win::wants_tab_focus(window)
                && !window->control->keep_above() && !window->control->keep_below()) {
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

    Toplevel* firstClient = nullptr;
    do {
        nc = forward ? nextClientStatic(nc) : previousClientStatic(nc);
        if (!firstClient) {
            // When we see our first client for the second time,
            // it's time to stop.
            firstClient = nc;
        } else if (nc == firstClient) {
            // No candidates found.
            nc = nullptr;
            break;
        }
    } while (nc && nc != c &&
            ((!options_traverse_all && !nc->isOnDesktop(currentDesktop())) ||
             nc->control->minimized() || !win::wants_tab_focus(nc) || nc->control->keep_above() ||
             nc->control->keep_below()));
    if (nc) {
        if (c && c != nc)
            win::lower_window(workspace(), c);
        if (options->focusPolicyIsReasonable()) {
            Workspace::self()->activateClient(nc);
        } else {
            if (!nc->isOnDesktop(currentDesktop()))
                setCurrentDesktop(nc->desktop());
            win::raise_window(workspace(), nc);
        }
    }
}

void TabBox::KDEOneStepThroughWindows(bool forward, TabBoxMode mode)
{
    setMode(mode);
    reset();
    nextPrev(forward);
    if (auto c = currentClient()) {
        Workspace::self()->activateClient(c);
    }
}

void TabBox::oneStepThroughDesktops(bool forward, TabBoxMode mode)
{
    setMode(mode);
    reset();
    nextPrev(forward);
    if (currentDesktop() != -1)
        setCurrentDesktop(currentDesktop());
}

void TabBox::oneStepThroughDesktops(bool forward)
{
    oneStepThroughDesktops(forward, TabBoxDesktopMode);
}

void TabBox::oneStepThroughDesktopList(bool forward)
{
    oneStepThroughDesktops(forward, TabBoxDesktopListMode);
}

void TabBox::keyPress(int keyQt)
{
    enum Direction { Backward = -1, Steady = 0, Forward = 1 };
    Direction direction(Steady);

    auto contains = [](const QKeySequence &shortcut, int key) -> bool {
        for (int i = 0; i < shortcut.count(); ++i) {
            if (shortcut[i] == key) {
                return true;
            }
        }
        return false;
    };

    // tests whether a shortcut matches and handles pitfalls on ShiftKey invocation
    auto directionFor = [keyQt, contains](const QKeySequence &forward, const QKeySequence &backward) -> Direction {
        if (contains(forward, keyQt))
            return Forward;
        if (contains(backward, keyQt))
            return Backward;
        if (!(keyQt & Qt::ShiftModifier))
            return Steady;

        // Before testing the unshifted key (Ctrl+A vs. Ctrl+Shift+a etc.), see whether this is +Shift+Tab
        // and check that against +Shift+Backtab (as well)
        Qt::KeyboardModifiers mods = Qt::ShiftModifier|Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier|Qt::KeypadModifier|Qt::GroupSwitchModifier;
        mods &= keyQt;
        if ((keyQt & ~mods) == Qt::Key_Tab) {
            if (contains(forward, mods | Qt::Key_Backtab))
                return Forward;
            if (contains(backward, mods | Qt::Key_Backtab))
                return Backward;
        }

        // if the shortcuts do not match, try matching again after filtering the shift key from keyQt
        // it is needed to handle correctly the ALT+~ shorcut for example as it is coded as ALT+SHIFT+~ in keyQt
        if (contains(forward, keyQt & ~Qt::ShiftModifier))
            return Forward;
        if (contains(backward, keyQt & ~Qt::ShiftModifier))
            return Backward;

        return Steady;
    };

    if (m_tabGrab) {
        static const int ModeCount = 4;
        static const TabBoxMode modes[ModeCount] = {
            TabBoxWindowsMode, TabBoxWindowsAlternativeMode,
            TabBoxCurrentAppWindowsMode, TabBoxCurrentAppWindowsAlternativeMode
        };
        const QKeySequence cuts[2*ModeCount] = {
            // forward
            m_cutWalkThroughWindows, m_cutWalkThroughWindowsAlternative,
            m_cutWalkThroughCurrentAppWindows, m_cutWalkThroughCurrentAppWindowsAlternative,
            // backward
            m_cutWalkThroughWindowsReverse, m_cutWalkThroughWindowsAlternativeReverse,
            m_cutWalkThroughCurrentAppWindowsReverse, m_cutWalkThroughCurrentAppWindowsAlternativeReverse
        };
        bool testedCurrent = false; // in case of collision, prefer to stay in the current mode
        int i = 0, j = 0;
        while (true) {
            if (!testedCurrent && modes[i] != mode()) {
                ++j;
                i = (i+1) % ModeCount;
                continue;
            }
            if (testedCurrent && modes[i] == mode()) {
                break;
            }
            testedCurrent = true;
            direction = directionFor(cuts[i], cuts[i+ModeCount]);
            if (direction != Steady) {
                if (modes[i] != mode()) {
                    accept(false);
                    setMode(modes[i]);
                    auto replayWithChangedTabboxMode = [this, direction]() {
                        reset();
                        nextPrev(direction == Forward);
                    };
                    QTimer::singleShot(50, this, replayWithChangedTabboxMode);
                }
                break;
            } else if (++j > 2*ModeCount) { // guarding counter for invalid modes
                qCDebug(KWIN_TABBOX) << "Invalid TabBoxMode";
                return;
            }
            i = (i+1) % ModeCount;
        }
        if (direction != Steady) {
            qCDebug(KWIN_TABBOX) << "== " << cuts[i].toString() << " or " << cuts[i+ModeCount].toString();
            KDEWalkThroughWindows(direction == Forward);
        }
    } else if (m_desktopGrab) {
        direction = directionFor(m_cutWalkThroughDesktops, m_cutWalkThroughDesktopsReverse);
        if (direction == Steady)
            direction = directionFor(m_cutWalkThroughDesktopList, m_cutWalkThroughDesktopListReverse);
        if (direction != Steady)
            walkThroughDesktops(direction == Forward);
    }

    if (m_desktopGrab || m_tabGrab) {
        if (((keyQt & ~Qt::KeyboardModifierMask) == Qt::Key_Escape) && direction == Steady) {
            // if Escape is part of the shortcut, don't cancel
            close(true);
        } else if (direction == Steady) {
            QKeyEvent* event = new QKeyEvent(QEvent::KeyPress, keyQt & ~Qt::KeyboardModifierMask, Qt::NoModifier);
            grabbedKeyEvent(event);
        }
    }
}

void TabBox::close(bool abort)
{
    if (isGrabbed()) {
        removeTabBoxGrab();
    }
    hide(abort);
    kwinApp()->input->redirect->pointer()->setEnableConstraints(true);
    m_tabGrab = false;
    m_desktopGrab = false;
    m_noModifierGrab = false;
}

void TabBox::accept(bool closeTabBox)
{
    auto c = currentClient();
    if (closeTabBox)
        close();
    if (c) {
        Workspace::self()->activateClient(c);
        if (win::is_desktop(c))
            Workspace::self()->setShowingDesktop(!Workspace::self()->showingDesktop());
    }
}

void TabBox::modifiersReleased()
{
    if (m_noModifierGrab) {
        return;
    }
    if (m_tabGrab) {
        bool old_control_grab = m_desktopGrab;
        accept();
        m_desktopGrab = old_control_grab;
    }
    if (m_desktopGrab) {
        bool old_tab_grab = m_tabGrab;
        int desktop = currentDesktop();
        close();
        m_tabGrab = old_tab_grab;
        if (desktop != -1) {
            setCurrentDesktop(desktop);
            win::virtual_desktop_manager::self()->setCurrent(desktop);
        }
    }
}

int TabBox::nextDesktopStatic(int iDesktop) const
{
    win::virtual_desktop_next functor;
    return functor(iDesktop, true);
}

int TabBox::previousDesktopStatic(int iDesktop) const
{
    win::virtual_desktop_previous functor;
    return functor(iDesktop, true);
}

/**
 * Auxiliary functions to travers all clients according to the static
 * order. Useful for the CDE-style Alt-tab feature.
 */
Toplevel* TabBox::nextClientStatic(Toplevel* c) const
{
    const auto &list = Workspace::self()->allClientList();
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
Toplevel* TabBox::previousClientStatic(Toplevel* c) const
{
    const auto &list = Workspace::self()->allClientList();
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

bool TabBox::establishTabBoxGrab()
{
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        m_forcedGlobalMouseGrab = true;
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
    Q_ASSERT(!m_forcedGlobalMouseGrab);
    m_forcedGlobalMouseGrab = true;
    if (Workspace::self()->activeClient() != nullptr)
        Workspace::self()->activeClient()->control->update_mouse_grab();
    m_x11EventFilter.reset(new X11Filter);
    return true;
}

void TabBox::removeTabBoxGrab()
{
    if (kwinApp()->shouldUseWaylandForCompositing()) {
        m_forcedGlobalMouseGrab = false;
        return;
    }
    kwinApp()->update_x11_time_from_clock();
    base::x11::ungrab_keyboard();
    Q_ASSERT(m_forcedGlobalMouseGrab);
    m_forcedGlobalMouseGrab = false;
    if (Workspace::self()->activeClient() != nullptr)
        Workspace::self()->activeClient()->control->update_mouse_grab();
    m_x11EventFilter.reset();
}
} // namespace TabBox
} // namespace

