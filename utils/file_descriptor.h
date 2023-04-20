/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: MIT
*/
#pragma once

#include <unistd.h>
#include <utility>

namespace KWin
{

/**
 * Helper struct to manage access to file descriptors via RAII.
 */
struct file_descriptor {
    file_descriptor() = default;

    explicit file_descriptor(int fd)
        : fd{fd}
    {
    }

    file_descriptor(file_descriptor&& other)
        : fd(std::exchange(other.fd, -1))
    {
    }

    file_descriptor& operator=(file_descriptor&& other)
    {
        if (fd != -1) {
            ::close(fd);
        }

        fd = std::exchange(other.fd, -1);
        return *this;
    }

    ~file_descriptor()
    {
        if (fd != -1) {
            ::close(fd);
        }
    }

    bool is_valid() const
    {
        return fd != -1;
    }

    int take()
    {
        return std::exchange(fd, -1);
    }

    file_descriptor duplicate() const
    {
        if (fd == -1) {
            return {};
        }
        return file_descriptor{dup(fd)};
    }

    int fd{-1};
};

}
