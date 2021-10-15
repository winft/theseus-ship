/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "sources_ext.h"

namespace KWin::xwl
{

data_source_ext::data_source_ext()
    : Wrapland::Server::data_source_ext()
{
}

void data_source_ext::accept(std::string const& mime_type)
{
    Q_EMIT accepted(mime_type);
}

void data_source_ext::request_data(std::string const& mime_type, int32_t fd)
{
    Q_EMIT data_requested(mime_type, fd);
}

void data_source_ext::cancel()
{
    Q_EMIT cancelled();
}

void data_source_ext::send_dnd_drop_performed()
{
    Q_EMIT dropped();
}

void data_source_ext::send_dnd_finished()
{
    Q_EMIT finished();
}

void data_source_ext::send_action(Wrapland::Server::dnd_action action)
{
    this->action = action;
}

primary_selection_source_ext::primary_selection_source_ext()
    : Wrapland::Server::primary_selection_source_ext()
{
}

void primary_selection_source_ext::request_data(std::string const& mime_type, int32_t fd)
{
    Q_EMIT data_requested(mime_type, fd);
}

void primary_selection_source_ext::cancel()
{
}

}
