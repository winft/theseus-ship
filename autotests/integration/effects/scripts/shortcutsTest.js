// SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

registerShortcut("testShortcut", "Test Shortcut", "Meta+Shift+Y", function() {
    sendTestResponse("shortcutTriggered");
});
