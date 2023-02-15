// SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick 2.0;
import org.kde.kwin 3.0;

ScreenEdgeHandler {
    edge: ScreenEdgeHandler.LeftEdge
    mode: ScreenEdgeHandler.Touch
    onActivated: {
        Workspace.slotToggleShowDesktop();
    }
}
