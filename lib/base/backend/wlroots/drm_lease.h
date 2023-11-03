/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"
#include "kwin_export.h"

#include <QObject>
#include <memory>
#include <vector>

extern "C" {
#define static
#include <wlr/backend/drm.h>
#undef static
}

#include <Wrapland/Server/drm_lease_v1.h>

namespace KWin::base::backend::wlroots
{

class non_desktop_output_wrap;

class KWIN_EXPORT drm_lease : public QObject
{
    Q_OBJECT
public:
    drm_lease(Wrapland::Server::drm_lease_v1* lease,
              std::vector<non_desktop_output_wrap*> const& outputs);

    drm_lease(drm_lease const&) = delete;
    drm_lease& operator=(drm_lease const&) = delete;
    drm_lease(drm_lease&& other) noexcept;
    drm_lease& operator=(drm_lease&& other) noexcept;
    ~drm_lease() override;

    Wrapland::Server::drm_lease_v1* lease;
    wlr_drm_lease* wlr_lease{nullptr};

    std::vector<non_desktop_output_wrap*> outputs;

Q_SIGNALS:
    void finished();

private:
    std::unique_ptr<event_receiver<drm_lease>> destroyed;
};

}
