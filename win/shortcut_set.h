/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "activation.h"
#include "meta.h"
#include "shortcut_dialog.h"
#include "shortcut_set.h"

#include <KGlobalAccel>
#include <QRegularExpression>

namespace KWin::win
{

template<typename Space>
bool shortcut_available(Space& space, const QKeySequence& cut, typename Space::window_t* ignore)
{
    if (ignore && cut == ignore->control->shortcut) {
        return true;
    }

    // Check if the shortcut is already registered.
    auto const registeredShortcuts = KGlobalAccel::globalShortcutsByKey(cut);
    for (auto const& shortcut : registeredShortcuts) {
        // Only return "not available" if is not a client activation shortcut, as it may be no
        // longer valid.
        if (!shortcut.uniqueName().startsWith(QStringLiteral("_k_session:"))) {
            return false;
        }
    }

    // Check now conflicts with activation shortcuts for current clients.
    for (auto const win : space.windows) {
        if (win != ignore && win->control && win->control->shortcut == cut) {
            return false;
        }
    }

    return true;
}

template<typename Win>
void set_shortcut(Win* win, QString const& shortcut)
{
    auto update_shortcut = [&win](QKeySequence const& cut = QKeySequence()) {
        if (win->control->shortcut == cut) {
            return;
        }
        win->control->set_shortcut(cut.toString());
        win->setShortcutInternal();
    };

    auto cut = win->control->rules.checkShortcut(shortcut);
    if (cut.isEmpty()) {
        update_shortcut();
        return;
    }
    if (cut == win->control->shortcut.toString()) {
        // No change
        return;
    }

    // Format:
    //       base+(abcdef)<space>base+(abcdef)
    //   Alt+Ctrl+(ABCDEF);Meta+X,Meta+(ABCDEF)
    //
    if (!cut.contains(QLatin1Char('(')) && !cut.contains(QLatin1Char(')'))
        && !cut.contains(QLatin1String(" - "))) {
        if (shortcut_available(win->space, cut, win)) {
            update_shortcut(QKeySequence(cut));
        } else {
            update_shortcut();
        }
        return;
    }

    QRegularExpression const reg(QStringLiteral("(.*\\+)\\((.*)\\)"));
    QList<QKeySequence> keys;
    QStringList groups = cut.split(QStringLiteral(" - "));
    for (QStringList::ConstIterator it = groups.constBegin(); it != groups.constEnd(); ++it) {
        auto const match = reg.match(*it);
        if (match.hasMatch()) {
            auto const base = match.captured(1);
            auto const list = match.captured(2);

            for (int i = 0; i < list.length(); ++i) {
                QKeySequence c(base + list[i]);
                if (!c.isEmpty()) {
                    keys.append(c);
                }
            }
        } else {
            // The regexp doesn't match, so it should be a normal shortcut.
            QKeySequence c(*it);
            if (!c.isEmpty()) {
                keys.append(c);
            }
        }
    }

    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
        if (win->control->shortcut == *it) {
            // Current one is in the list.
            return;
        }
    }
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
        if (shortcut_available(win->space, *it, win)) {
            update_shortcut(*it);
            return;
        }
    }
    update_shortcut();
}

template<typename Space>
void setup_window_shortcut_done(Space& space, bool ok)
{
    //    keys->setEnabled( true );
    //    disable_shortcuts_keys->setEnabled( true );
    //    client_keys->setEnabled( true );
    if (ok) {
        set_shortcut(space.client_keys_client, space.client_keys_dialog->shortcut().toString());
    }

    close_active_popup(space);

    space.client_keys_dialog->deleteLater();
    space.client_keys_dialog = nullptr;
    space.client_keys_client = nullptr;

    if (space.stacking.active) {
        space.stacking.active->takeFocus();
    }
}

template<typename Space, typename Win>
void setup_window_shortcut(Space& space, Win* window)
{
    assert(!space.client_keys_dialog);

    // TODO: PORT ME (KGlobalAccel related)
    // keys->setEnabled( false );
    // disable_shortcuts_keys->setEnabled( false );
    // client_keys->setEnabled( false );
    space.client_keys_dialog = new shortcut_dialog(window->control->shortcut);
    space.client_keys_client = window;

    QObject::connect(space.client_keys_dialog,
                     &win::shortcut_dialog::dialogDone,
                     space.qobject.get(),
                     [&space](auto&& ok) { setup_window_shortcut_done(space, ok); });

    auto area = space_window_area(space, ScreenArea, window);
    auto size = space.client_keys_dialog->sizeHint();

    auto pos = win::frame_to_client_pos(window, window->geo.pos());
    if (pos.x() + size.width() >= area.right()) {
        pos.setX(area.right() - size.width());
    }
    if (pos.y() + size.height() >= area.bottom()) {
        pos.setY(area.bottom() - size.height());
    }

    space.client_keys_dialog->move(pos);
    space.client_keys_dialog->show();
    space.active_popup = space.client_keys_dialog;
    space.active_popup_client = window;
}

template<typename Space>
void window_shortcut_updated(Space& space, typename Space::window_t* window)
{
    QString key = QStringLiteral("_k_session:%1").arg(window->xcb_window);
    auto action = space.qobject->template findChild<QAction*>(key);

    if (!window->control->shortcut.isEmpty()) {
        if (action == nullptr) {
            // new shortcut
            action = new QAction(space.qobject.get());
            space.base.input->setup_action_for_global_accel(action);
            action->setProperty("componentName", QStringLiteral(KWIN_NAME));
            action->setObjectName(key);
            action->setText(i18n("Activate Window (%1)", win::caption(window)));
            QObject::connect(action, &QAction::triggered, window->qobject.get(), [&space, window] {
                force_activate_window(space, window);
            });
        }

        // no autoloading, since it's configured explicitly here and is not meant to be reused
        // (the key is the window id anyway, which is kind of random)
        KGlobalAccel::self()->setShortcut(action,
                                          QList<QKeySequence>() << window->control->shortcut,
                                          KGlobalAccel::NoAutoloading);
        action->setEnabled(true);
    } else {
        KGlobalAccel::self()->removeAllShortcuts(action);
        delete action;
    }
}

}
