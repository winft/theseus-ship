/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QSet>
#include <Wrapland/Server/filtered_display.h>

namespace KWin::base::wayland
{

class KWIN_EXPORT filtered_display : public Wrapland::Server::FilteredDisplay
{
    Q_OBJECT
public:
    filtered_display();

    bool allowInterface(Wrapland::Server::Client* client, QByteArray const& interfaceName) override;

private:
    QSet<QString> reported;
};

}
