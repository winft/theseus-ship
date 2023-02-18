// SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick 2.0;
import org.kde.kwin 2.0;

ScreenEdgeItem {
    edge: ScreenEdgeItem.LeftEdge
    mode: ScreenEdgeItem.Touch
    onActivated: {
        workspace.slotToggleShowDesktop();
    }
}
