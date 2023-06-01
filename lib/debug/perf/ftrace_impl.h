/*
SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QFile>
#include <QString>
#include <memory>

namespace KWin
{
namespace Perf
{

/**
 * Provides an interface to mark the Ftrace output for debugging.
 */
class FtraceImpl
{
public:
    static FtraceImpl& instance();

    /**
     * @brief Enables or disables the marker
     *
     * @param enable The enablement state to set
     * @return True if setting enablement succeeded, else false
     */
    bool setEnabled(bool enable);
    void print(const QString& message);
    void printBegin(const QString& message, ulong ctx);
    void printEnd(const QString& message, ulong ctx);

private:
    FtraceImpl() = default;
    bool findFile();

    std::unique_ptr<QFile> m_file;
};

}
}
