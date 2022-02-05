/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <xcb/shm.h>
#include <xcb/xcb.h>

namespace KWin::base::x11::xcb
{

class shm
{
public:
    shm();
    ~shm();

    int id() const;
    void* buffer() const;
    xcb_shm_seg_t segment() const;
    bool is_valid() const;
    uint8_t pixmap_format() const;

private:
    bool init();

    int m_shmId;
    void* m_buffer;
    xcb_shm_seg_t m_segment;
    bool m_valid;
    uint8_t m_pixmap_format;
};

inline void* shm::buffer() const
{
    return m_buffer;
}

inline bool shm::is_valid() const
{
    return m_valid;
}

inline xcb_shm_seg_t shm::segment() const
{
    return m_segment;
}

inline int shm::id() const
{
    return m_shmId;
}

inline uint8_t shm::pixmap_format() const
{
    return m_pixmap_format;
}

}
