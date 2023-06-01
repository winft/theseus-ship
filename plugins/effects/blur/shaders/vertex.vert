// SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

uniform mat4 modelViewProjectionMatrix;
attribute vec4 vertex;

void main(void)
{
    gl_Position = modelViewProjectionMatrix * vertex;
}
