/*
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: MIT
*/
#pragma once

#include <memory>

namespace KWin
{

/**
 * Helper class to acquire and release a lock inside a scope.
 */
template<typename BasicLockable>
class blocker
{
public:
    explicit blocker(BasicLockable* lock)
        : p(lock)
    {
        p->lock();
    }
    explicit blocker(std::unique_ptr<BasicLockable>& lock)
        : p(lock.get())
    {
        p->lock();
    }
    explicit blocker(BasicLockable& lock)
        : p(&lock)
    {
        p->lock();
    }
    blocker(blocker const& other)
    {
        p = other.p;
        p->lock();
    }
    blocker& operator=(blocker const& other)
    {
        blocker tmp(other);
        std::swap(*this, tmp);
        return *this;
    }
    blocker(blocker&& other) noexcept
    {
        p = other.p;
        other.p = nullptr;
    }
    blocker& operator=(blocker&& other) noexcept
    {
        p = std::move(other.p);
        return *this;
    }
    ~blocker()
    {
        if (p) {
            p->unlock();
        }
    }

private:
    BasicLockable* p;
};

}
