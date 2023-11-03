/*
SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef TABBOXHANDLER_H
#define TABBOXHANDLER_H

#include "kwin_export.h"
#include "tabbox_client.h"
#include "tabbox_config.h"

#include <QModelIndex>
#include <QPixmap>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

/**
 * @file
 * This file contains the classes which hide KWin core from tabbox.
 * It defines the pure virtual classes tabbox_handler and tabbox_client.
 * The classes have to be implemented in KWin Core.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.4
 */

class QKeyEvent;
class QQmlEngine;

namespace KWin
{
/**
 * The tabbox is a model based view for displaying a list while switching windows or desktops.
 * This functionality is mostly referred to as Alt+Tab. TabBox itself does not provide support for
 * switching windows or desktops. This has to be done outside of TabBox inside an independent
 * controller.
 *
 * The main entrance point to tabbox is the class tabbox_handler, which has to be subclassed and
 * implemented. The class Tabbox_client, which represents a window client inside tabbox, has to be
 * implemented as well.
 *
 * The behavior of the tabbox is defined by the tabbox_config and has to be set in the
 * tabbox_handler. If the tabbox should be used to switch desktops as well as clients it is
 * sufficient to just provide different tabbox_config objects instead of creating an own handler for
 * each mode.
 *
 * In order to use the tabbox the tabbox_config has to be set. This defines if the model for
 * desktops or for clients will be used. The model has to be initialized by calling
 * tabbox_handler::create_model(), as the model is undefined when the tabbox is not active. The
 * tabbox is activated by tabbox_handler::show(). Depending on the current set tabbox_config it is
 * possible that the highlight windows effect activated and that the view is not displayed at all.
 * As already mentioned the tabbox does not handle any updating of the selected item. This has to be
 * done by invoking tabbox_handler::set_current_index(). Nevertheless the tabbox_handler provides
 * methods to query for the model index or the next or previous item, for a cursor position or for a
 * given item (that is tabbox_client or desktop). By invoking tabbox_handler::hide() the view, the
 * optional highlight windows effect are removed. The model is invalidated immediately. So if it is
 * necessary to retrieve the last selected item this has to be done before calling the hide method.
 *
 * The layout of the tabbox View and the items is completely customizable. Therefore tabbox provides
 * a widget layout_config which includes a live preview (in kcmkwin/kwintabbox). The layout of items
 * can be defined by an xml document. That way the user is able to define own custom layouts. The
 * view itself is made up of two widgets: one to show the complete list and one to show only the
 * selected item. This way it is possible to have a view which shows for example a list containing
 * only small icons and nevertheless show the title of the currently selected client.
 */
namespace win
{

class tabbox_desktop_model;
class tabbox_client_model;
class tabbox_config;
class tabbox_handler_private;

using tabbox_client_list = std::vector<tabbox_client*>;

/**
 * This class is a wrapper around KWin Workspace. It is used for accessing the
 * required core methods from inside tabbox and has to be implemented in KWin core.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.4
 */
class KWIN_EXPORT tabbox_handler : public QObject
{
    Q_OBJECT
public:
    tabbox_handler(std::function<QQmlEngine*(void)> qml_engine, QObject* parent);
    ~tabbox_handler() override;

    /**
     * @return The id of the active screen
     */
    virtual int active_screen() const = 0;
    /**
     * @return The current active tabbox_client or NULL
     * if there is no active client.
     */
    virtual tabbox_client* active_client() const = 0;
    /**
     * @param client The client which is starting point to find the next client
     * @return The next tabbox_client in focus chain
     */
    virtual tabbox_client* next_client_focus_chain(tabbox_client* client) const = 0;
    /**
     * This method is used by the client_model to find an entrance into the focus chain in case
     * there is no active Client.
     *
     * @return The first Client of the focus chain
     * @since 4.9.1
     */
    virtual tabbox_client* first_client_focus_chain() const = 0;
    /**
     * Checks whether the given @p client is part of the focus chain at all.
     * This is useful to figure out whether the currently active Client can be used
     * as a starting point to construct the recently used list.
     *
     * In case the @p client is not in the focus chain it is recommended to use the
     * Client returned by first_client_focus_chain.
     *
     * The method accepts a @c null Client and in that case @c false is returned.
     * @param client The Client to check whether it is in the Focus Chain
     * @return @c true in case the Client is part of the focus chain, @c false otherwise.
     * @since 4.9.2
     */
    virtual bool is_in_focus_chain(tabbox_client* client) const = 0;
    /**
     * @param client The client whose desktop name should be retrieved
     * @return The desktop name of the given tabbox_client. If the client is
     * on all desktops the name of current desktop will be returned.
     */
    virtual QString desktop_name(tabbox_client* client) const = 0;
    /**
     * @param desktop The desktop whose name should be retrieved
     * @return The desktop name of given desktop
     */
    virtual QString desktop_name(int desktop) const = 0;
    /**
     * @return The number of current desktop
     */
    virtual int current_desktop() const = 0;
    /**
     * @return The number of virtual desktops
     */
    virtual int number_of_desktops() const = 0;
    /**
     * @param desktop The desktop which is the starting point to find the next desktop
     * @return The next desktop in the current focus chain.
     */
    virtual int next_desktop_focus_chain(int desktop) const = 0;

    /**
     * whether KWin is currently compositing and it's related features (elevating) can be used
     */
    virtual bool is_kwin_compositing() const = 0;

    /**
     * De-/Elevate a client using the compositor (if enabled)
     */
    virtual void elevate_client(tabbox_client* c, QWindow* tabbox, bool elevate) const = 0;

    /**
     * Raise a client (w/o activating it)
     */
    virtual void raise_client(tabbox_client* c) const = 0;

    /**
     * @param c The client to be restacked
     * @param under The client the other one will be placed below
     */
    virtual void restack(tabbox_client* c, tabbox_client* under) = 0;

    virtual void highlight_windows(tabbox_client* window = nullptr, QWindow* controller = nullptr)
        = 0;

    /**
     * @return The current stacking order of tabbox_clients
     */
    virtual tabbox_client_list stacking_order() const = 0;
    /**
     * Determines if given client will be added to the list:
     * <UL>
     * <LI>Depends on desktop</LI>
     * <LI>if the client wants to have tab focus.</LI>
     * <LI>The client won't be added if it has modal dialogs</LI>
     * <LI>In that case the modal dialog will be returned if it isn't already
     * included</LI>
     * <LI>Won't be added if it isn't on active screen when using separate
     * screen focus</LI>
     * </UL>
     * @param client The client to be checked for inclusion
     * @param desktop The desktop the client should be on. This is irrelevant if allDesktops is set
     * @param allDesktops Add clients from all desktops or only from current
     * @return The client to be included in the list or NULL if it isn't to be included
     */
    virtual tabbox_client* client_to_add_to_list(tabbox_client* client, int desktop) const = 0;
    /**
     * @return The first desktop window in the stacking order.
     */
    virtual tabbox_client* desktop_client() const = 0;
    /**
     * Activates the currently selected client and closes the tabbox.
     */
    virtual void activate_and_close() = 0;

    /**
     * @return The currently used tabbox_config
     */
    const tabbox_config& config() const;
    /**
     * Call this method when you want to change the currently used tabbox_config.
     * It fires the signal config_changed.
     * @param config Updates the currently used tabbox_config to config
     */
    void set_config(const tabbox_config& config);

    /**
     * Call this method to show the TabBoxView. Depending on current
     * configuration this method might not do anything.
     * If highlight windows effect is to be used it will be activated.
     * Highlight windows and outline are not shown if
     * tabbox_config::TabBoxMode is Tabbox_config::DesktopTabBox.
     * @see tabbox_config::is_show_tabbox
     * @see tabbox_config::is_highlight_windows
     */
    void show();
    /**
     * Hides the TabBoxView if shown.
     * Deactivates highlight windows effect if active.
     * @see show
     */
    void hide(bool abort = false);

    /**
     * Sets the current model index in the view and updates
     * highlight windows if active.
     * @param index The current Model index
     */
    void set_current_index(const QModelIndex& index);
    /**
     * @returns the current index
     */
    const QModelIndex& current_index() const;

    /**
     * Retrieves the next or previous item of the current item.
     * @param forward next or previous item
     * @return The next or previous item. If there is no matching item
     * the current item will be returned.
     */
    QModelIndex next_prev(bool forward) const;

    /**
     * Initializes the model based on the current config.
     * This method has to be invoked before showing the tabbox.
     * It can also be invoked when clients are added or removed.
     * In that case partialReset has to be true.
     *
     * @param partial_reset Keep the currently selected item or regenerate everything
     */
    void create_model(bool partial_reset = false);

    /**
     * @param desktop The desktop whose index should be retrieved
     * @return The model index of given desktop. If TabBoxMode is not
     * tabbox_config::DesktopTabBox an invalid model index will be returned.
     */
    QModelIndex desktop_index(int desktop) const;
    /**
     * @return The current list of desktops.
     * If TabBoxMode is not tabbox_config::DesktopTabBox an empty list will
     * be returned.
     * @see desktop_model::desktop_list
     */
    QList<int> desktop_list() const;
    /**
     * @return The desktop for given model index. If the index is not valid
     * or TabBoxMode is not tabbox_config::DesktopTabBox -1 will be returned.
     * @see desktop_model::desktop_index
     */
    int desktop(const QModelIndex& index) const;

    /**
     * Handles additional grabbed key events by the tabbox controller.
     * @param event The key event which has been grabbed
     */
    virtual void grabbed_key_event(QKeyEvent* event) const;
    /**
     * @param pos The position to be tested in global coordinates
     * @return True if the view contains the point, otherwise false.
     */
    bool contains_pos(const QPoint& pos) const;
    /**
     * @param client The tabbox_client whose index should be returned
     * @return Returns the ModelIndex of given tabbox_client or an invalid ModelIndex
     * if the model does not contain the given tabbox_client.
     * @see client_model::index
     */
    QModelIndex index(tabbox_client* client) const;
    /**
     * @return Returns the current list of tabbox_clients.
     * If TabBoxMode is not tabbox_config::ClientTabBox an empty list will
     * be returned.
     * @see client_model::client_list
     */
    tabbox_client_list client_list() const;
    /**
     * @param index The index of the client to be returned
     * @return Returns the tabbox_client at given model index. If
     * the index is invalid, does not point to a Client or the list
     * is empty, NULL will be returned.
     */
    tabbox_client* client(const QModelIndex& index) const;
    /**
     * @return The first model index. That is the model index at position 0, 0.
     * It is valid, as desktop has at least one desktop and if there are no
     * clients an empty item is created.
     */
    QModelIndex first() const;

    bool eventFilter(QObject* watcher, QEvent* event) override;

    /**
     * @returns whether the tabbox operates in a no modifier grab mode.
     * In this mode a click on an item should directly accept and close the tabbox.
     */
    virtual bool no_modifier_grab() const = 0;

Q_SIGNALS:
    /**
     * This signal is fired when the tabbox_Config changes
     * @see set_config
     */
    void config_changed();
    void selected_index_changed();

private Q_SLOTS:
    void init_highlight_windows();

private:
    friend class tabbox_handler_private;
    tabbox_handler_private* d;
    std::function<QQmlEngine*(void)> qml_engine;
};

/**
 * Pointer to the global tabbox_handler object.
 */
KWIN_EXPORT extern tabbox_handler* tabbox_handle;

} // namespace win
} // namespace KWin

#endif // TABBOXHANDLER_H
