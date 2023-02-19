// SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

function init() {
    const edge = readConfig("Edge", 1);
    if (readConfig("mode", "") == "unregister") {
        unregisterScreenEdge(edge);
    } else {
        registerScreenEdge(edge, workspace.slotToggleShowDesktop);
    }
}
options.configChanged.connect(init);

init();

