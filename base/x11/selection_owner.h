/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QObject>
#include <memory>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

namespace KWin::base::x11
{

class KWIN_EXPORT selection_owner : public QObject
{
    Q_OBJECT
public:
    selection_owner(xcb_atom_t selection, int screen);
    selection_owner(char const* selection, int screen);
    selection_owner(xcb_atom_t selection, xcb_connection_t* c, xcb_window_t root);
    selection_owner(char const* selection, xcb_connection_t* c, xcb_window_t root);
    ~selection_owner() override;

    void claim(bool force, bool force_kill = true);
    void release();

    // None if not owning the selection
    xcb_window_t ownerWindow() const;

    // internal
    bool filterEvent(void* ev_P);
    void timerEvent(QTimerEvent* event) override;

Q_SIGNALS:
    void lostOwnership();
    void claimedOwnership();
    void failedToClaimOwnership();

protected:
    virtual bool genericReply(xcb_atom_t target, xcb_atom_t property, xcb_window_t requestor);
    virtual void replyTargets(xcb_atom_t property, xcb_window_t requestor);
    virtual void getAtoms();
    void setData(uint32_t extra1, uint32_t extra2);

private:
    void filter_selection_request(void* ev_P);
    bool handle_selection(xcb_atom_t target_P, xcb_atom_t property_P, xcb_window_t requestor_P);

    class Private;
    std::unique_ptr<Private> const d_ptr;
};

}
