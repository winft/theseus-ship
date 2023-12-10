/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#cmakedefine01 KWIN_BUILD_DECORATIONS
#cmakedefine01 KWIN_BUILD_TABBOX
#define KWIN_NAME "${KWIN_NAME}"
#define KWIN_CONFIG "${KWIN_NAME}rc"
#define KWIN_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
#define XCB_VERSION_STRING "${XCB_VERSION}"
#define KWIN_KILLER_BIN "${CMAKE_INSTALL_FULL_LIBEXECDIR}/kwin_killer_helper"
#cmakedefine01 HAVE_PERF
#cmakedefine01 HAVE_BREEZE_DECO
#cmakedefine01 HAVE_SCHED_RESET_ON_FORK
#cmakedefine01 HAVE_ACCESSIBILITY
#cmakedefine01 HAVE_EPOXY_GLX
#cmakedefine01 WLR_HAVE_SESSION_CREATE_LOOP

#if HAVE_BREEZE_DECO
#define BREEZE_KDECORATION_PLUGIN_ID "${BREEZE_KDECORATION_PLUGIN_ID}"
#endif
