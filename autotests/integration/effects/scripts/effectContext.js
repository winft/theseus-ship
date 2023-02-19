// SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

sendTestResponse(displayWidth() + "x" + displayHeight());
sendTestResponse(animationTime(100));

//test enums for Effect / QEasingCurve
sendTestResponse(Effect.Saturation)
sendTestResponse(QEasingCurve.Linear)
