/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Needs to be included before KKeyServer, because KKeyServer includes XLib whose macros collide
// with Qt declarations in QDBus, in particular the "True" and "False" names.
#include <QtCore>

#include <KKeyServer>
