/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "shm.h"

#include "utils.h"
#include "utils/memory.h"

#include <sys/shm.h>
#include <sys/types.h>

namespace KWin::base::x11::xcb
{

shm::shm()
    : m_shmId(-1)
    , m_buffer(nullptr)
    , m_segment(XCB_NONE)
    , m_valid(false)
    , m_pixmap_format(XCB_IMAGE_FORMAT_XY_BITMAP)
{
    m_valid = init();
}

shm::~shm()
{
    if (m_valid) {
        xcb_shm_detach(connection(), m_segment);
        shmdt(m_buffer);
    }
}

bool shm::init()
{
    auto ext = xcb_get_extension_data(connection(), &xcb_shm_id);
    if (!ext || !ext->present) {
        qCDebug(KWIN_CORE) << "SHM extension not available";
        return false;
    }

    unique_cptr<xcb_shm_query_version_reply_t> version(xcb_shm_query_version_reply(
        connection(), xcb_shm_query_version_unchecked(connection()), nullptr));
    if (!version) {
        qCDebug(KWIN_CORE) << "Failed to get SHM extension version information";
        return false;
    }
    m_pixmap_format = version->pixmap_format;

    // TODO check there are not larger windows
    int const MAXSIZE = 4096 * 2048 * 4;
    m_shmId = shmget(IPC_PRIVATE, MAXSIZE, IPC_CREAT | 0600);

    if (m_shmId < 0) {
        qCDebug(KWIN_CORE) << "Failed to allocate SHM segment";
        return false;
    }

    m_buffer = shmat(m_shmId, nullptr, 0 /*read/write*/);
    if (-1 == reinterpret_cast<long>(m_buffer)) {
        qCDebug(KWIN_CORE) << "Failed to attach SHM segment";
        shmctl(m_shmId, IPC_RMID, nullptr);
        return false;
    }

    shmctl(m_shmId, IPC_RMID, nullptr);
    m_segment = xcb_generate_id(connection());

    auto const cookie = xcb_shm_attach_checked(connection(), m_segment, m_shmId, false);
    unique_cptr<xcb_generic_error_t> error(xcb_request_check(connection(), cookie));
    if (error) {
        qCDebug(KWIN_CORE) << "xcb_shm_attach error: " << error->error_code;
        shmdt(m_buffer);
        return false;
    }

    return true;
}

}
