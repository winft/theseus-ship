/********************************************************************
 This file is part of the KDE project.

 Copyright (C) 2012 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

"use strict";

class MaximizeEffect {
    constructor() {
        effect.configChanged.connect(this.loadConfig.bind(this));
        effects.windowFrameGeometryChanged.connect(
                this.onWindowFrameGeometryChanged.bind(this));
        effects.windowMaximizedStateChanged.connect(
                this.onWindowMaximizedStateChanged.bind(this));
        effect.animationEnded.connect(this.restoreForceBlurState.bind(this));

        this.loadConfig();
    }

    loadConfig() {
        this.duration = animationTime(250);
    }

    onWindowMaximizedStateChanged(window) {
        if (!window.oldGeometry) {
            return;
        }
        window.setData(Effect.WindowForceBlurRole, true);
        let oldGeometry = window.oldGeometry;
        const newGeometry = window.geometry;
        if (oldGeometry.width == newGeometry.width && oldGeometry.height == newGeometry.height)
            oldGeometry = window.olderGeometry;
        window.olderGeometry = Object.assign({}, window.oldGeometry);
        window.oldGeometry = Object.assign({}, newGeometry);
        window.maximizeAnimation1 = animate({
            window: window,
            duration: this.duration,
            animations: [{
                type: Effect.Size,
                to: {
                    value1: newGeometry.width,
                    value2: newGeometry.height
                },
                from: {
                    value1: oldGeometry.width,
                    value2: oldGeometry.height
                },
                curve: QEasingCurve.OutCubic
            }, {
                type: Effect.Translation,
                to: {
                    value1: 0,
                    value2: 0
                },
                from: {
                    value1: oldGeometry.x - newGeometry.x - (newGeometry.width / 2 - oldGeometry.width / 2),
                    value2: oldGeometry.y - newGeometry.y - (newGeometry.height / 2 - oldGeometry.height / 2)
                },
                curve: QEasingCurve.OutCubic
            }]
        });
        if (!window.resize) {
            window.maximizeAnimation2 =animate({
                window: window,
                duration: this.duration,
                animations: [{
                    type: Effect.CrossFadePrevious,
                    to: 1.0,
                    from: 0.0,
                    curve: QEasingCurve.OutCubic
                }]
            });
        }
    }

    restoreForceBlurState(window) {
        window.setData(Effect.WindowForceBlurRole, null);
    }

    onWindowFrameGeometryChanged(window, oldGeometry) {
        if (window.maximizeAnimation1) {
            if (window.geometry.width != window.oldGeometry.width ||
                window.geometry.height != window.oldGeometry.height) {
                cancel(window.maximizeAnimation1);
                delete window.maximizeAnimation1;
                if (window.maximizeAnimation2) {
                    cancel(window.maximizeAnimation2);
                    delete window.maximizeAnimation2;
                }
            }
        }
        window.oldGeometry = Object.assign({}, window.geometry);
        window.olderGeometry = Object.assign({}, oldGeometry);
    }
}

new MaximizeEffect();
