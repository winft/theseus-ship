/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019-2021 Roman Gilg <subdiff@gmail.com>

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
#pragma once

#include "types.h"

#include <QObject>
#include <memory>
#include <vector>
#include <xcb/xfixes.h>

class QSocketNotifier;

struct xcb_selection_request_event_t;
struct xcb_xfixes_selection_notify_event_t;

namespace KWin::xwl
{

/*
 * QObject attribute of a wl_source.
 * This is a hack around having a template QObject.
 */
class q_wl_source : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

Q_SIGNALS:
    void transfer_ready(xcb_selection_request_event_t* event, qint32 fd);
};

/**
 * Representing a Wayland native data source.
 */
template<typename ServerSource>
class wl_source
{
public:
    wl_source(ServerSource* source, x11_data const& x11)
        : server_source{source}
        , x11{x11}
        , qobject{std::make_unique<q_wl_source>()}
    {
        assert(source);

        for (auto const& mime : source->mime_types()) {
            offers.emplace_back(mime);
        }

        QObject::connect(source,
                         &ServerSource::mime_type_offered,
                         get_qobject(),
                         [this](auto mime) { offers.emplace_back(mime); });
    }

    q_wl_source* get_qobject() const
    {
        return qobject.get();
    }

    ServerSource* server_source = nullptr;
    x11_data const& x11;
    std::vector<std::string> offers;
    xcb_timestamp_t timestamp{XCB_CURRENT_TIME};

private:
    std::unique_ptr<q_wl_source> qobject;

    Q_DISABLE_COPY(wl_source)
};

/*
 * QObject attribute of a x11_source.
 * This is a hack around having a template QObject.
 */
class q_x11_source : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

Q_SIGNALS:
    void offers_changed(std::vector<std::string> const& added,
                        std::vector<std::string> const& removed);
    void transfer_ready(xcb_atom_t target, qint32 fd);
};

/**
 * Representing an X data source.
 */
template<typename InternalSource>
class x11_source
{
public:
    x11_source(xcb_xfixes_selection_notify_event_t* event, x11_data const& x11)
        : x11{x11}
        , timestamp{event->timestamp}
        , qobject{std::make_unique<q_x11_source>()}
    {
    }
    ~x11_source() = default;

    /**
     * Does not take ownership of @param src in general, but if the function is called again, it
     * will delete the previous data source.
     */
    void set_source(InternalSource* src)
    {
        Q_ASSERT(src);
        if (source) {
            delete source;
        }

        source = src;

        for (auto const& offer : offers) {
            src->offer(offer.id);
        }

        QObject::connect(src,
                         &InternalSource::data_requested,
                         get_qobject(),
                         [this](auto const& mimeName, auto fd) {
                             selection_x11_start_transfer(this, mimeName, fd);
                         });
    }

    InternalSource* get_source() const
    {
        return source;
    }

    q_x11_source* get_qobject() const
    {
        return qobject.get();
    }

    x11_data const& x11;
    mime_atoms offers;
    xcb_timestamp_t timestamp;

private:
    InternalSource* source = nullptr;
    std::unique_ptr<q_x11_source> qobject;

    Q_DISABLE_COPY(x11_source)
};

}

Q_DECLARE_METATYPE(std::vector<std::string>)
