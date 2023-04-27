/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <Wrapland/Server/data_source.h>
#include <Wrapland/Server/primary_selection.h>

namespace KWin::xwl
{

class KWIN_EXPORT data_source_ext : public Wrapland::Server::data_source_ext
{
    Q_OBJECT
public:
    data_source_ext();

    void accept(std::string const& mime_type) override;
    void request_data(std::string const& mime_type, int32_t fd) override;
    void cancel() override;

    void send_dnd_drop_performed() override;
    void send_dnd_finished() override;
    void send_action(Wrapland::Server::dnd_action action) override;

    Wrapland::Server::dnd_action action{Wrapland::Server::dnd_action::none};

Q_SIGNALS:
    void data_requested(std::string const& mime_type, int32_t fd);
    void accepted(std::string const& mime_type);
    void cancelled();
    void dropped();
    void finished();
};

class KWIN_EXPORT primary_selection_source_ext
    : public Wrapland::Server::primary_selection_source_ext
{
    Q_OBJECT
public:
    primary_selection_source_ext();

    void request_data(std::string const& mime_type, int32_t fd) override;
    void cancel() override;

Q_SIGNALS:
    void data_requested(std::string const& mime_type, int32_t fd);
};

}
