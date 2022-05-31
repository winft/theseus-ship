/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QHash>
#include <QObject>

#include <chrono>
#include <deque>
#include <memory>
#include <time.h>

#include <Wrapland/Server/presentation_time.h>

class QElapsedTimer;

namespace Wrapland::Server
{
class PresentationFeedback;
class PresentationManager;
class Surface;
}

namespace KWin
{

class Toplevel;

namespace render::wayland
{
class output;
struct presentation_data;

enum class presentation_kind {
    none = 0,
    vsync = 1 << 0,
    hw_clock = 1 << 1,
    hw_completion = 1 << 2,
    zero_copy = 1 << 3,
};
Q_DECLARE_FLAGS(presentation_kinds, presentation_kind)

struct presentation_data {
    uint32_t commit_seq;
    std::chrono::nanoseconds when;
    unsigned seq;
    std::chrono::nanoseconds refresh;
    presentation_kinds flags;
};

class KWIN_EXPORT presentation : public QObject
{
    Q_OBJECT
public:
    presentation();

    bool init_clock(clockid_t clockid);

    void frame(render::wayland::output* output, std::deque<Toplevel*> const& windows);
    void lock(render::wayland::output* output, std::deque<Toplevel*> const& windows);
    void presented(render::wayland::output* output, presentation_data const& data);

private:
    QHash<uint32_t, Wrapland::Server::Surface*> surfaces;

    clockid_t clockid;

    std::unique_ptr<Wrapland::Server::PresentationManager> presentation_manager;
};

}
}

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::render::wayland::presentation_kinds)
