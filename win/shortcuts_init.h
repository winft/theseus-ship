/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "activation.h"
#include "active_window.h"
#include "kill_window.h"
#include "types.h"

#include "main.h"
#include "render/platform.h"
#include "render/post/night_color_manager.h"
#include "render/post/night_color_setup.h"

#include <QAction>
#include <QKeySequence>
#include <QString>
#include <QVariant>

#include <KGlobalAccel>
#include <KLazyLocalizedString>
#include <KLocalizedString>

namespace KWin::win
{

template<typename Space>
QAction* prepare_shortcut_action(Space& space,
                                 QString const& actionName,
                                 QString const& description,
                                 QKeySequence const& shortcut,
                                 QVariant const& data)
{
    auto action = new QAction(space.qobject.get());
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(actionName);
    action->setText(description);

    if (data.isValid()) {
        action->setData(data);
    }

    KGlobalAccel::self()->setDefaultShortcut(action, QList<QKeySequence>() << shortcut);
    KGlobalAccel::self()->setShortcut(action, QList<QKeySequence>() << shortcut);
    return action;
}

template<typename Space, typename T, typename Slot>
void init_shortcut(Space& space,
                   QString const& actionName,
                   QString const& description,
                   const QKeySequence& shortcut,
                   T* receiver,
                   Slot slot,
                   const QVariant& data = QVariant())
{
    auto action = prepare_shortcut_action(space, actionName, description, shortcut, data);
    kwinApp()->input->registerShortcut(shortcut, action, receiver, slot);
}

template<typename Space, typename Slot>
void init_shortcut(Space& space,
                   QString const& actionName,
                   QString const& description,
                   const QKeySequence& shortcut,
                   Slot slot,
                   const QVariant& data = QVariant())
{
    init_shortcut(space, actionName, description, shortcut, space.qobject.get(), slot, data);
}

template<typename Space, typename T, typename Slot>
void init_shortcut_with_action_arg(Space& space,
                                   QString const& actionName,
                                   QString const& description,
                                   const QKeySequence& shortcut,
                                   T* receiver,
                                   Slot slot,
                                   QVariant const& data)
{
    auto action = prepare_shortcut_action(space, actionName, description, shortcut, data);
    kwinApp()->input->registerShortcut(
        shortcut, action, receiver, [action, &slot] { slot(action); });
}

/**
 * Creates the global accel object \c keys.
 */
template<typename Space>
void init_shortcuts(Space& space)
{
    // Some shortcuts have Tarzan-speech like names, they need extra
    // normal human descriptions with def2() the others can use def()
    // new def3 allows to pass data to the action, replacing the %1 argument in the name

    auto def2 = [&](auto name, auto descr, auto key, auto functor) {
        init_shortcut(
            space, QString(name), descr.toString(), key, [&, functor] { functor(space); });
    };

    auto def = [&](auto name, auto key, auto functor) {
        init_shortcut(
            space, QString::fromUtf8(name.untranslatedText()), name.toString(), key, [&, functor] {
                functor(space);
            });
    };

    auto def3 = [&](auto name, auto key, auto functor, auto value) {
        init_shortcut_with_action_arg(
            space,
            QString::fromUtf8(name.untranslatedText()).arg(value),
            name.subs(value).toString(),
            key,
            space.qobject.get(),
            [&, functor](QAction* action) { functor(space, action); },
            value);
    };

    auto def4 = [&](auto name, auto descr, auto key, auto functor) {
        init_shortcut(space, QString(name), descr.toString(), key, functor);
    };

    auto def5 = [&](auto name, auto key, auto functor, auto value) {
        init_shortcut(space,
                      QString::fromUtf8(name.untranslatedText()).arg(value),
                      name.subs(value).toString(),
                      key,
                      functor,
                      value);
    };

    auto def6 = [&](auto name, auto key, auto target, auto functor) {
        init_shortcut(space,
                      QString::fromUtf8(name.untranslatedText()),
                      name.toString(),
                      key,
                      target,
                      functor);
    };

    def(kli18n("Window Operations Menu"),
        Qt::ALT + Qt::Key_F3,
        active_window_show_operations_popup<Space>);
    def2("Window Close", kli18n("Close Window"), Qt::ALT + Qt::Key_F4, active_window_close<Space>);
    def2("Window Maximize",
         kli18n("Maximize Window"),
         Qt::META + Qt::Key_PageUp,
         active_window_maximize<Space>);
    def2("Window Maximize Vertical",
         kli18n("Maximize Window Vertically"),
         0,
         active_window_maximize_vertical<Space>);
    def2("Window Maximize Horizontal",
         kli18n("Maximize Window Horizontally"),
         0,
         active_window_maximize_horizontal<Space>);
    def2("Window Minimize",
         kli18n("Minimize Window"),
         Qt::META + Qt::Key_PageDown,
         active_window_minimize<Space>);
    def2("Window Move", kli18n("Move Window"), 0, active_window_move<Space>);
    def2("Window Resize", kli18n("Resize Window"), 0, active_window_resize<Space>);
    def2("Window Raise", kli18n("Raise Window"), 0, active_window_raise<Space>);
    def2("Window Lower", kli18n("Lower Window"), 0, active_window_lower<Space>);
    def(kli18n("Toggle Window Raise/Lower"), 0, active_window_raise_or_lower<Space>);
    def2("Window Fullscreen",
         kli18n("Make Window Fullscreen"),
         0,
         active_window_set_fullscreen<Space>);
    def2("Window No Border", kli18n("Hide Window Border"), 0, active_window_set_no_border<Space>);
    def2("Window Above Other Windows",
         kli18n("Keep Window Above Others"),
         0,
         active_window_set_keep_above<Space>);
    def2("Window Below Other Windows",
         kli18n("Keep Window Below Others"),
         0,
         active_window_set_keep_below<Space>);

    def(kli18n("Activate Window Demanding Attention"),
        Qt::META | Qt::CTRL | Qt::Key_A,
        activate_attention_window<Space>);
    def(kli18n("Setup Window Shortcut"), 0, active_window_setup_window_shortcut<Space>);
    def2("Window Pack Right",
         kli18n("Pack Window to the Right"),
         0,
         active_window_pack_right<Space>);
    def2("Window Pack Left", kli18n("Pack Window to the Left"), 0, active_window_pack_left<Space>);
    def2("Window Pack Up", kli18n("Pack Window Up"), 0, active_window_pack_up<Space>);
    def2("Window Pack Down", kli18n("Pack Window Down"), 0, active_window_pack_down<Space>);
    def2("Window Grow Horizontal",
         kli18n("Pack Grow Window Horizontally"),
         0,
         active_window_grow_horizontal<Space>);
    def2("Window Grow Vertical",
         kli18n("Pack Grow Window Vertically"),
         0,
         active_window_grow_vertical<Space>);
    def2("Window Shrink Horizontal",
         kli18n("Pack Shrink Window Horizontally"),
         0,
         active_window_shrink_horizontal<Space>);
    def2("Window Shrink Vertical",
         kli18n("Pack Shrink Window Vertically"),
         0,
         active_window_shrink_vertical<Space>);
    def4("Window Quick Tile Left",
         kli18n("Quick Tile Window to the Left"),
         Qt::META + Qt::Key_Left,
         [&space] { active_window_quicktile(space, quicktiles::left); });
    def4("Window Quick Tile Right",
         kli18n("Quick Tile Window to the Right"),
         Qt::META + Qt::Key_Right,
         [&space] { active_window_quicktile(space, quicktiles::right); });
    def4("Window Quick Tile Top",
         kli18n("Quick Tile Window to the Top"),
         Qt::META + Qt::Key_Up,
         [&space] { active_window_quicktile(space, quicktiles::top); });
    def4("Window Quick Tile Bottom",
         kli18n("Quick Tile Window to the Bottom"),
         Qt::META + Qt::Key_Down,
         [&space] { active_window_quicktile(space, quicktiles::bottom); });
    def4("Window Quick Tile Top Left", kli18n("Quick Tile Window to the Top Left"), 0, [&space] {
        active_window_quicktile(space, quicktiles::top | quicktiles::left);
    });
    def4("Window Quick Tile Bottom Left",
         kli18n("Quick Tile Window to the Bottom Left"),
         0,
         [&space] { active_window_quicktile(space, quicktiles::bottom | quicktiles::left); });
    def4("Window Quick Tile Top Right", kli18n("Quick Tile Window to the Top Right"), 0, [&space] {
        active_window_quicktile(space, quicktiles::top | quicktiles::right);
    });
    def4("Window Quick Tile Bottom Right",
         kli18n("Quick Tile Window to the Bottom Right"),
         0,
         [&space] { active_window_quicktile(space, quicktiles::bottom | quicktiles::right); });
    def4("Switch Window Up",
         kli18n("Switch to Window Above"),
         Qt::META + Qt::ALT + Qt::Key_Up,
         [&space] { activate_window_direction(space, direction::north); });
    def4("Switch Window Down",
         kli18n("Switch to Window Below"),
         Qt::META + Qt::ALT + Qt::Key_Down,
         [&space] { activate_window_direction(space, direction::south); });
    def4("Switch Window Right",
         kli18n("Switch to Window to the Right"),
         Qt::META + Qt::ALT + Qt::Key_Right,
         [&space] { activate_window_direction(space, direction::east); });
    def4("Switch Window Left",
         kli18n("Switch to Window to the Left"),
         Qt::META + Qt::ALT + Qt::Key_Left,
         [&space] { activate_window_direction(space, direction::west); });
    def2("Increase Opacity",
         kli18n("Increase Opacity of Active Window by 5 %"),
         0,
         active_window_increase_opacity<Space>);
    def2("Decrease Opacity",
         kli18n("Decrease Opacity of Active Window by 5 %"),
         0,
         active_window_lower_opacity<Space>);

    def2("Window On All Desktops",
         kli18n("Keep Window on All Desktops"),
         0,
         active_window_set_on_all_desktops<Space>);

    for (int i = 1; i < 21; ++i) {
        def5(
            kli18n("Window to Desktop %1"),
            0,
            [&space, i] { active_window_to_desktop(space, i); },
            i);
    }

    def(kli18n("Window to Next Desktop"), 0, active_window_to_next_desktop<Space>);
    def(kli18n("Window to Previous Desktop"), 0, active_window_to_prev_desktop<Space>);
    def(kli18n("Window One Desktop to the Right"), 0, active_window_to_right_desktop<Space>);
    def(kli18n("Window One Desktop to the Left"), 0, active_window_to_left_desktop<Space>);
    def(kli18n("Window One Desktop Up"), 0, active_window_to_above_desktop<Space>);
    def(kli18n("Window One Desktop Down"), 0, active_window_to_below_desktop<Space>);

    for (int i = 0; i < 8; ++i) {
        def3(kli18n("Window to Screen %1"), 0, active_window_to_output<Space>, i);
    }
    def(kli18n("Window to Next Screen"), 0, active_window_to_next_output<Space>);
    def(kli18n("Window to Previous Screen"), 0, active_window_to_prev_output<Space>);
    def(kli18n("Show Desktop"), Qt::META + Qt::Key_D, toggle_show_desktop<Space>);

    for (int i = 0; i < 8; ++i) {
        def3(kli18n("Switch to Screen %1"), 0, switch_to_output<Space>, i);
    }

    def(kli18n("Switch to Next Screen"), 0, switch_to_next_output<Space>);
    def(kli18n("Switch to Previous Screen"), 0, switch_to_prev_output<Space>);

    def(kli18n("Kill Window"),
        Qt::META | Qt::CTRL | Qt::Key_Escape,
        start_window_killer<win::space>);
    def6(kli18n("Suspend Compositing"),
         Qt::SHIFT + Qt::ALT + Qt::Key_F12,
         &space.render,
         [render = &space.render] { render->toggleCompositing(); });
    def6(kli18n("Invert Screen Colors"), 0, &space.render, [render = &space.render] {
        render->platform.invertScreen();
    });

#if KWIN_BUILD_TABBOX
    space.tabbox->init_shortcuts();
#endif
    space.virtual_desktop_manager->initShortcuts();
    render::post::init_night_color_shortcuts(*kwinApp()->input,
                                             *kwinApp()->get_base().render->night_color);

    // so that it's recreated next time
    space.user_actions_menu->discard();
}

}
