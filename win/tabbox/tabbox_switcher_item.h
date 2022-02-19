/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_TABBOX_SWITCHERITEM_H
#define KWIN_TABBOX_SWITCHERITEM_H

#include <QObject>
#include <QRect>

class QAbstractItemModel;

namespace KWin
{
namespace win
{

class tabbox_switcher_item : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* model READ model NOTIFY model_changed)
    Q_PROPERTY(QRect screenGeometry READ screen_geometry NOTIFY screen_geometry_changed)
    Q_PROPERTY(bool visible READ is_visible NOTIFY visible_changed)
    Q_PROPERTY(bool allDesktops READ is_all_desktops NOTIFY all_desktops_changed)
    Q_PROPERTY(
        int currentIndex READ current_index WRITE set_current_index NOTIFY current_index_changed)
    Q_PROPERTY(bool noModifierGrab READ no_modifier_grab NOTIFY no_modifier_grab_changed)
    Q_PROPERTY(bool compositing READ compositing NOTIFY compositing_changed)

    /**
     * The main QML item that will be displayed in the Dialog
     */
    Q_PROPERTY(QObject* item READ item WRITE set_item NOTIFY item_changed)

    Q_CLASSINFO("DefaultProperty", "item")
public:
    tabbox_switcher_item(QObject* parent = nullptr);
    ~tabbox_switcher_item() override;

    QAbstractItemModel* model() const;
    QRect screen_geometry() const;
    bool is_visible() const;
    bool is_all_desktops() const;
    int current_index() const;
    void set_current_index(int index);
    QObject* item() const;
    void set_item(QObject* item);
    bool no_modifier_grab() const
    {
        return m_no_modifier_grab;
    }
    bool compositing();

    // for usage from outside
    void set_model(QAbstractItemModel* model);
    void set_all_desktops(bool all);
    void set_visible(bool visible);
    void set_no_modifier_grab(bool set);

Q_SIGNALS:
    void visible_changed();
    void current_index_changed(int index);
    void model_changed();
    void all_desktops_changed();
    void screen_geometry_changed();
    void item_changed();
    void no_modifier_grab_changed();
    void compositing_changed();

private:
    QAbstractItemModel* m_model;
    QObject* m_item;
    bool m_visible;
    bool m_all_desktops;
    int m_current_index;
    QMetaObject::Connection m_selected_index_connection;
    bool m_no_modifier_grab = false;
};

inline QAbstractItemModel* tabbox_switcher_item::model() const
{
    return m_model;
}

inline bool tabbox_switcher_item::is_visible() const
{
    return m_visible;
}

inline bool tabbox_switcher_item::is_all_desktops() const
{
    return m_all_desktops;
}

inline int tabbox_switcher_item::current_index() const
{
    return m_current_index;
}

inline QObject* tabbox_switcher_item::item() const
{
    return m_item;
}

} // win
} // KWin

#endif // KWIN_TABBOX_SWITCHERITEM_H
