/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <KSelectionOwner>
#include <QObject>

namespace KWin::render::x11
{

class KWIN_EXPORT compositor_selection_owner : public KSelectionOwner
{
    Q_OBJECT
public:
    explicit compositor_selection_owner(char const* selection);

    bool is_owning() const;
    void own();
    void disown();

private:
    bool owning{false};
};

}
