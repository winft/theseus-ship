// SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

uniform sampler2D sampler;
uniform vec4 geometryColor;

varying vec2 texcoord0;

void main()
{
    vec4 tex = texture2D(sampler, texcoord0);
    if (tex.a != 1.0) {
        tex = geometryColor;
    }
    gl_FragColor = tex;
}
