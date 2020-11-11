/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "remnant.h"

#include "control.h"
#include "meta.h"
#include "win.h"

#include "decorations/decorationrenderer.h"
#include "x11client.h"
#include "xdgshellclient.h"

#include <cassert>

namespace KWin::win
{

remnant::remnant(Toplevel* win, Toplevel* source)
    : win{win}
{
    assert(!win->remnant());

    //        win->copyToDeleted(source);

    buffer_geometry = source->bufferGeometry();
    buffer_margins = source->bufferMargins();
    frame_margins = source->frameMargins();
    buffer_scale = source->bufferScale();
    desk = source->desktop();
    activities = source->activities();
    contents_rect = QRect(source->clientPos(), source->clientSize());
    content_pos = source->clientContentPos();
    transparent_rect = source->transparentRect();
    frame = source->frameId();
    opacity = source->opacity();
    window_type = source->windowType();
    window_role = source->windowRole();

    if (source->control()) {
        no_border = source->noBorder();
        if (!no_border) {
            source->layoutDecorationRects(
                decoration_left, decoration_top, decoration_right, decoration_bottom);
            if (win::decoration(source)) {
                if (auto renderer = source->control()->deco().client->renderer()) {
                    decoration_renderer = renderer;
                    decoration_renderer->reparent(win);
                }
            }
        }
        minimized = source->control()->minimized();
        modal = source->control()->modal();

        for (auto const& mc : source->mainClients()) {
            main_clients.push_back(mc);
        }
        for (auto lead : main_clients) {
            leads.push_back(lead);
            QObject::connect(lead, &Toplevel::windowClosed, win, [this](auto window, auto deleted) {
                lead_closed(window, deleted);
            });
        }

        fullscreen = source->control()->fullscreen();
        keep_above = source->control()->keep_above();
        keep_below = source->control()->keep_below();
        caption = win::caption(source);

        was_active = source->control()->active();

        was_group_transient = source->groupTransient();
    }

    for (auto vd : win->desktops()) {
        QObject::connect(vd, &QObject::destroyed, win, [=] {
            auto desks = win->desktops();
            desks.removeOne(vd);
            win->set_desktops(desks);
        });
    }

    was_wayland_client = qobject_cast<XdgShellClient*>(source) != nullptr;
    was_x11_client = qobject_cast<X11Client*>(source) != nullptr;
    was_popup_window = win::is_popup(source);
    was_outline = source->isOutline();

    if (source->control()) {
        control = std::make_unique<win::control>(win);
        control->set_modal(source->control()->modal());
    }
}

remnant::~remnant()
{
    if (refcount != 0) {
        qCCritical(KWIN_CORE) << "Deleted client has non-zero reference count (" << refcount << ")";
    }
    assert(refcount == 0);

    if (workspace()) {
        workspace()->removeDeleted(win);
    }
    for (auto lead : leads) {
        if (auto remnant = lead->remnant()) {
            remove_all(remnant->transients, win);
        }
    }
    for (auto transient : transients) {
        remove_all(transient->remnant()->leads, win);
    }
    win->deleteEffectWindow();
}

void remnant::ref()
{
    ++refcount;
}

void remnant::unref()
{
    if (--refcount > 0) {
        return;
    }

    // needs to be delayed
    // a) when calling from effects, otherwise it'd be rather complicated to handle the case of the
    // window going away during a painting pass
    // b) to prevent dangeling pointers in the stacking order, see bug #317765
    win->deleteLater();
}

void remnant::discard()
{
    refcount = 0;
    delete win;
}

bool remnant::was_transient() const
{
    return !leads.empty();
}

bool remnant::has_lead(Toplevel const* toplevel) const
{
    return contains(leads, toplevel);
}

void remnant::lead_closed(Toplevel* window, Toplevel* deleted)
{
    if (window->control()) {
        remove_all(main_clients, window);
    }

    if (deleted == nullptr) {
        remove_all(leads, window);
        return;
    }

    auto const index = index_of(leads, window);
    if (index == -1) {
        return;
    }

    leads[index] = deleted;
    deleted->remnant()->transients.push_back(win);
}

void remnant::layout_decoration_rects(QRect& left, QRect& top, QRect& right, QRect& bottom) const
{
    left = decoration_left;
    top = decoration_top;
    right = decoration_right;
    bottom = decoration_bottom;
}

}
