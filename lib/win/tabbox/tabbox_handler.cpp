/*
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "tabbox_handler.h"

#include "tabbox_client_model.h"
#include "tabbox_config.h"

#include "script/platform.h"
#include "tabbox_logging.h"
#include "tabbox_switcher_item.h"

#include <QKeyEvent>
#include <QModelIndex>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <qpa/qwindowsysteminterface.h>
// KDE
#include <KLocalizedString>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <KProcess>

namespace KWin
{
namespace win
{

class tabbox_handler_private
{
public:
    tabbox_handler_private(tabbox_handler* q);

    ~tabbox_handler_private();

    /**
     * Updates the current highlight window state
     */
    void update_highlight_windows();
    /**
     * Ends window highlighting
     */
    void end_highlight_windows(bool abort = false);

    void show();
    QQuickWindow* window() const;
    win::tabbox_switcher_item* switcher_item() const;

    win::tabbox_client_model* client_model() const;

    tabbox_handler* q; // public pointer
    // members
    tabbox_config config;
    QScopedPointer<QQmlContext> m_qml_context;
    QScopedPointer<QQmlComponent> m_qml_component;
    QObject* m_main_item;
    QMap<QString, QObject*> m_client_tabboxes;
    win::tabbox_client_model* m_client_model;
    QModelIndex index;
    /**
     * Indicates if the tabbox is shown.
     */
    bool is_shown;
    tabbox_client *last_raised_client, *last_raised_client_succ;
    int wheel_angle_delta = 0;

private:
    QObject* create_switcher_item();
};

tabbox_handler_private::tabbox_handler_private(tabbox_handler* q)
    : m_qml_context()
    , m_qml_component(nullptr)
    , m_main_item(nullptr)
{
    this->q = q;
    is_shown = false;
    last_raised_client = nullptr;
    last_raised_client_succ = nullptr;
    config = tabbox_config();
    m_client_model = new win::tabbox_client_model(q);
}

tabbox_handler_private::~tabbox_handler_private()
{
    for (auto it = m_client_tabboxes.constBegin(); it != m_client_tabboxes.constEnd(); ++it) {
        delete it.value();
    }
}

QQuickWindow* tabbox_handler_private::window() const
{
    if (!m_main_item) {
        return nullptr;
    }
    if (QQuickWindow* w = qobject_cast<QQuickWindow*>(m_main_item)) {
        return w;
    }
    return m_main_item->findChild<QQuickWindow*>();
}

win::tabbox_switcher_item* tabbox_handler_private::switcher_item() const
{
    if (!m_main_item) {
        return nullptr;
    }
    if (win::tabbox_switcher_item* i = qobject_cast<win::tabbox_switcher_item*>(m_main_item)) {
        return i;
    } else if (QQuickWindow* w = qobject_cast<QQuickWindow*>(m_main_item)) {
        return w->contentItem()->findChild<win::tabbox_switcher_item*>();
    }
    return m_main_item->findChild<win::tabbox_switcher_item*>();
}

win::tabbox_client_model* tabbox_handler_private::client_model() const
{
    return m_client_model;
}

void tabbox_handler_private::update_highlight_windows()
{
    if (!is_shown) {
        return;
    }

    tabbox_client* current_client = q->client(index);
    QWindow* w = window();

    if (q->is_kwin_compositing()) {
        if (last_raised_client)
            q->elevate_client(last_raised_client, w, false);
        last_raised_client = current_client;
        if (current_client)
            q->elevate_client(current_client, w, true);
    } else {
        if (last_raised_client) {
            if (last_raised_client_succ)
                q->restack(last_raised_client, last_raised_client_succ);
            // TODO lastRaisedClient->setMinimized( lastRaisedClientWasMinimized );
        }

        last_raised_client = current_client;
        if (last_raised_client) {
            // TODO if ( (lastRaisedClientWasMinimized = lastRaisedClient->isMinimized()) )
            //         lastRaisedClient->setMinimized( false );
            auto order = q->stacking_order();
            auto succ_idx = order.size() + 1;
            for (size_t i = 0; i < order.size(); ++i) {
                if (order.at(i) == last_raised_client) {
                    succ_idx = i + 1;
                    break;
                }
            }
            last_raised_client_succ = (succ_idx < order.size()) ? order.at(succ_idx) : nullptr;
            q->raise_client(last_raised_client);
        }
    }

    if (config.is_show_tabbox() && w) {
        q->highlight_windows(current_client, w);
    } else {
        q->highlight_windows(current_client);
    }
}

void tabbox_handler_private::end_highlight_windows(bool abort)
{
    tabbox_client* current_client = q->client(index);
    QWindow* w = window();

    if (current_client)
        q->elevate_client(current_client, w, false);
    if (abort && last_raised_client && last_raised_client_succ)
        q->restack(last_raised_client, last_raised_client_succ);
    last_raised_client = nullptr;
    last_raised_client_succ = nullptr;
    // highlight windows
    q->highlight_windows();
}

QObject* tabbox_handler_private::create_switcher_item()
{
    // first try look'n'feel package
    QString file = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("plasma/look-and-feel/%1/contents/windowswitcher/WindowSwitcher.qml")
            .arg(config.layout_name()));
    if (file.isNull()) {
        const QString type = QStringLiteral("KWin/WindowSwitcher");

        KPackage::Package pkg
            = KPackage::PackageLoader::self()->loadPackage(type, config.layout_name());

        if (!pkg.isValid()) {
            // load default
            qCWarning(KWIN_TABBOX) << "Could not load window switcher package"
                                   << config.layout_name() << ". Falling back to default";
            pkg = KPackage::PackageLoader::self()->loadPackage(
                type, tabbox_config::default_layout_name());
        }

        file = pkg.filePath("mainscript");
    }
    if (file.isNull()) {
        qCDebug(KWIN_TABBOX) << "Could not find QML file for window switcher";
        return nullptr;
    }
    m_qml_component->loadUrl(QUrl::fromLocalFile(file));
    if (m_qml_component->isError()) {
        qCWarning(KWIN_TABBOX) << "Component failed to load: " << m_qml_component->errors();
        QStringList args;
        args << QStringLiteral("--passivepopup")
             << i18n(
                    "The Window Switcher installation is broken, resources are missing.\n"
                    "Contact your distribution about this.")
             << QStringLiteral("20");
        KProcess::startDetached(QStringLiteral("kdialog"), args);
        m_qml_component.reset(nullptr);
    } else {
        QObject* object = m_qml_component->create(m_qml_context.data());
        m_client_tabboxes.insert(config.layout_name(), object);
        return object;
    }
    return nullptr;
}

void tabbox_handler_private::show()
{
    if (m_qml_context.isNull()) {
        qmlRegisterType<win::tabbox_switcher_item>("org.kde.kwin", 3, 0, "TabBoxSwitcher");
        m_qml_context.reset(new QQmlContext(q->qml_engine()));
    }
    if (m_qml_component.isNull()) {
        m_qml_component.reset(new QQmlComponent(q->qml_engine()));
    }
    auto find_main_item = [this](const QMap<QString, QObject*>& tabBoxes) -> QObject* {
        auto it = tabBoxes.constFind(config.layout_name());
        if (it != tabBoxes.constEnd()) {
            return it.value();
        }
        return nullptr;
    };
    m_main_item = nullptr;
    m_main_item = find_main_item(m_client_tabboxes);
    if (!m_main_item) {
        m_main_item = create_switcher_item();
        if (!m_main_item) {
            return;
        }
    }
    if (win::tabbox_switcher_item* item = switcher_item()) {
        // In case the model isn't yet set (see below), index will be reset and therefore we
        // need to save the current index row (https://bugs.kde.org/show_bug.cgi?id=333511).
        int indexRow = index.row();
        if (!item->model()) {
            item->set_model(client_model());
        }
        item->set_all_desktops(config.client_desktop_mode() == tabbox_config::AllDesktopsClients);
        item->set_current_index(indexRow);
        item->set_no_modifier_grab(q->no_modifier_grab());

        Q_EMIT item->about_to_show();

        // When SwitcherItem gets hidden, destroy also the window and main item
        QObject::connect(item, &tabbox_switcher_item::visible_changed, q, [this, item]() {
            if (!item->is_visible()) {
                if (auto win = window()) {
                    win->hide();
                    win->destroy();
                }
                m_main_item = nullptr;
            }
        });

        // everything is prepared, so let's make the whole thing visible
        item->set_visible(true);
    }
    if (QWindow* w = window()) {
        wheel_angle_delta = 0;
        w->installEventFilter(q);
        // pretend to activate the window to enable accessibility notifications
        QWindowSystemInterface::handleWindowActivated(w, Qt::TabFocusReason);
    }
}

/***********************************************
 * TabBoxHandler
 ***********************************************/

tabbox_handler::tabbox_handler(std::function<QQmlEngine*(void)> qml_engine, QObject* parent)
    : QObject(parent)
    , qml_engine{qml_engine}
{
    KWin::win::tabbox_handle = this;
    d = new tabbox_handler_private(this);
}

tabbox_handler::~tabbox_handler()
{
    delete d;
}

const KWin::win::tabbox_config& tabbox_handler::config() const
{
    return d->config;
}

void tabbox_handler::set_config(const tabbox_config& config)
{
    d->config = config;
    Q_EMIT config_changed();
}

void tabbox_handler::show()
{
    d->is_shown = true;
    d->last_raised_client = nullptr;
    d->last_raised_client_succ = nullptr;
    if (d->config.is_show_tabbox()) {
        d->show();
    }
    if (d->config.is_highlight_windows()) {
        // TODO(romangg): Do we need to sync here with base::x11::xcb::sync like before?

        // TODO this should be
        // QMetaObject::invokeMethod(this, "init_highlight_windows", Qt::QueuedConnection);
        // but we somehow need to cross > 1 event cycle (likely because of queued invocation in the
        // effects) to ensure the EffectWindow is present when updateHighlightWindows, thus
        // elevating the window/tabbox
        QTimer::singleShot(1, this, &tabbox_handler::init_highlight_windows);
    }
}

void tabbox_handler::init_highlight_windows()
{
    d->update_highlight_windows();
}

void tabbox_handler::hide(bool abort)
{
    d->is_shown = false;
    if (d->config.is_highlight_windows()) {
        d->end_highlight_windows(abort);
    }
    if (win::tabbox_switcher_item* item = d->switcher_item()) {
        Q_EMIT item->about_to_hide();
        if (item->get_automatically_hide()) {
            item->set_visible(false);
        }
    }
}

QModelIndex tabbox_handler::next_prev(bool forward) const
{
    QModelIndex ret;
    auto model = d->client_model();

    if (forward) {
        int column = d->index.column() + 1;
        int row = d->index.row();
        if (column == model->columnCount()) {
            column = 0;
            row++;
            if (row == model->rowCount())
                row = 0;
        }
        ret = model->index(row, column);
        if (!ret.isValid())
            ret = model->index(0, 0);
    } else {
        int column = d->index.column() - 1;
        int row = d->index.row();
        if (column < 0) {
            column = model->columnCount() - 1;
            row--;
            if (row < 0)
                row = model->rowCount() - 1;
        }
        ret = model->index(row, column);
        if (!ret.isValid()) {
            row = model->rowCount() - 1;
            for (int i = model->columnCount() - 1; i >= 0; i--) {
                ret = model->index(row, i);
                if (ret.isValid())
                    break;
            }
        }
    }
    if (ret.isValid())
        return ret;
    else
        return d->index;
}

void tabbox_handler::set_current_index(const QModelIndex& index)
{
    if (d->index == index) {
        return;
    }
    if (!index.isValid()) {
        return;
    }

    d->index = index;

    if (d->config.is_highlight_windows()) {
        d->update_highlight_windows();
    }

    Q_EMIT selected_index_changed();
}

const QModelIndex& tabbox_handler::current_index() const
{
    return d->index;
}

void tabbox_handler::grabbed_key_event(QKeyEvent* event) const
{
    if (!d->m_main_item || !d->window()) {
        return;
    }
    QCoreApplication::sendEvent(d->window(), event);
}

bool tabbox_handler::contains_pos(const QPoint& pos) const
{
    if (!d->m_main_item) {
        return false;
    }
    QWindow* w = d->window();
    if (w) {
        return w->geometry().contains(pos);
    }
    return false;
}

QModelIndex tabbox_handler::index(win::tabbox_client* client) const
{
    return d->client_model()->index(client);
}

tabbox_client_list tabbox_handler::client_list() const
{
    return d->client_model()->client_list();
}

tabbox_client* tabbox_handler::client(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return nullptr;
    }
    tabbox_client* c = static_cast<tabbox_client*>(
        d->client_model()->data(index, win::tabbox_client_model::ClientRole).value<void*>());
    return c;
}

void tabbox_handler::create_model(bool partial_reset)
{
    d->client_model()->create_client_list(partial_reset);
    bool last_raised = false;
    bool last_raised_succ = false;

    for (auto const& client : stacking_order()) {
        if (client == d->last_raised_client) {
            last_raised = true;
        }
        if (client == d->last_raised_client_succ) {
            last_raised_succ = true;
        }
    }

    if (d->last_raised_client && !last_raised) {
        d->last_raised_client = nullptr;
    }
    if (d->last_raised_client_succ && !last_raised_succ) {
        d->last_raised_client_succ = nullptr;
    }
}

QModelIndex tabbox_handler::first() const
{
    return d->client_model()->index(0, 0);
}

bool tabbox_handler::eventFilter(QObject* watched, QEvent* e)
{
    if (e->type() == QEvent::Wheel && watched == d->window()) {
        QWheelEvent* event = static_cast<QWheelEvent*>(e);
        // On x11 the delta for vertical scrolling might also be on X for whatever reason
        const int delta = qAbs(event->angleDelta().x()) > qAbs(event->angleDelta().y())
            ? event->angleDelta().x()
            : event->angleDelta().y();
        d->wheel_angle_delta += delta;
        while (d->wheel_angle_delta <= -120) {
            d->wheel_angle_delta += 120;
            const QModelIndex index = next_prev(true);
            if (index.isValid()) {
                set_current_index(index);
            }
        }
        while (d->wheel_angle_delta >= 120) {
            d->wheel_angle_delta -= 120;
            const QModelIndex index = next_prev(false);
            if (index.isValid()) {
                set_current_index(index);
            }
        }
        return true;
    }
    // pass on
    return QObject::eventFilter(watched, e);
}

tabbox_handler* tabbox_handle = nullptr;

} // namespace win
} // namespace KWin
