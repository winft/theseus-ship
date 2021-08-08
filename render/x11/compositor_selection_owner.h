/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KSelectionOwner>
#include <QObject>

namespace KWin::render::x11
{

class compositor_selection_owner : public KSelectionOwner
{
    Q_OBJECT
public:
    explicit compositor_selection_owner(char const* selection);
    bool owning() const;
    void setOwning(bool own);

private:
    bool m_owning;
};

}
