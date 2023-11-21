// SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

effects.windowAdded.connect(function (window) {
    if (effect.grab(window, Effect.WindowAddedGrabRole)) {
        sendTestResponse('ok');
    } else {
        sendTestResponse('fail');
    }

    window.minimizedChanged.connect(() => {
        if (!window.minimized) {
            return;
        }
        if (effect.ungrab(window, Effect.WindowAddedGrabRole)) {
            sendTestResponse('ok');
        } else {
            sendTestResponse('fail');
        }
    });
});
