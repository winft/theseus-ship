/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

class QTemporaryFile;

namespace KWin::xwl
{

KWIN_EXPORT bool xauthority_generate_file(int display, QTemporaryFile& dest);

}
