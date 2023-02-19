#version 140
// SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

uniform float u_alpha;

in vec2 texcoord0;

out vec4 fragColor;

void main()
{
    fragColor = vec4(0.0, 0.0, 0.0, u_alpha*texcoord0.s);
}
