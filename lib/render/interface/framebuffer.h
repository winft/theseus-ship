/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QSize>
#include <cassert>
#include <stack>

namespace KWin::render
{

class framebuffer
{
public:
    virtual ~framebuffer() = default;

    virtual QSize size() const
    {
        return {};
    }

    virtual void bind()
    {
    }
};

template<typename Data>
void push_framebuffer(Data& data, framebuffer* target)
{
    target->bind();
    data.targets.push(target);
}

template<typename Data, typename Framebuffer>
void push_framebuffers(Data& data, std::stack<Framebuffer*> targets)
{
    static_assert(std::is_convertible_v<Framebuffer*, framebuffer*>,
                  "Framebuffer must inherit framebuffer");

    targets.top()->bind();

    // TODO(romangg): With C++23 use push_range() instead.
    std::stack<render::framebuffer*> temp;
    while (!targets.empty()) {
        auto next = targets.top();
        targets.pop();
        temp.push(next);
    }
    while (!temp.empty()) {
        auto next = temp.top();
        temp.pop();
        data.targets.push(next);
    }
}

template<typename Data>
render::framebuffer* pop_framebuffer(Data& data)
{
    assert(!data.targets.empty());

    auto target = data.targets.top();
    data.targets.pop();

    if (!data.targets.empty()) {
        data.targets.top()->bind();
    }

    return target;
}

}
