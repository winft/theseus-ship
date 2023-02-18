// SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

uniform float u_alpha;

varying vec2 texcoord0;

void main()
{
    gl_FragColor = vec4(0.0, 0.0, 0.0, u_alpha*texcoord0.s);
}
