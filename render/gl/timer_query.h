/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <chrono>
#include <epoxy/gl.h>

namespace KWin::render::gl
{

class timer_query
{
public:
    timer_query()
    {
        glGenQueries(1, &query);
        glQueryCounter(query, GL_TIMESTAMP);
        glGetInteger64v(GL_TIMESTAMP, &start_time);
    }
    timer_query(timer_query const&) = delete;
    timer_query& operator=(timer_query const&) = delete;
    timer_query(timer_query&&) noexcept = default;
    timer_query& operator=(timer_query&&) noexcept = default;

    bool get_query()
    {
        if (!done) {
            glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &done);
            if (done) {
                glGetQueryObjectui64v(query, GL_QUERY_RESULT, &end_time);
            }
        }
        return done;
    }

    std::chrono::nanoseconds time() const
    {
        if (!done) {
            return {};
        }
        return std::chrono::nanoseconds{end_time - start_time};
    }

private:
    GLuint64 end_time;
    GLint64 start_time;
    unsigned int query;
    int done{0};
};

}
