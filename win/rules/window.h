/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/options.h"
#include "base/output_helpers.h"
#include "kwin_export.h"
#include "ruling.h"
#include "win/types.h"
#include "win/virtual_desktops.h"

#include <QRect>
#include <functional>
#include <vector>

class QDebug;
class KConfig;
class KXMessages;

namespace KWin
{

namespace base
{
class output;
}

namespace win::rules
{

class ruling;

class KWIN_EXPORT window
{
public:
    explicit window(std::vector<ruling*> const& rules);
    window();

    bool contains(ruling const* rule) const;
    void remove(ruling* rule);
    placement checkPlacement(win::placement placement) const;
    QRect checkGeometry(QRect rect, bool init = false) const;
    // use 'invalidPoint' with checkPosition, unlike QSize() and QRect(), QPoint() is a valid point
    QPoint checkPosition(QPoint pos, bool init = false) const;
    QSize checkSize(QSize s, bool init = false) const;
    QSize checkMinSize(QSize s) const;
    QSize checkMaxSize(QSize s) const;
    int checkOpacityActive(int s) const;
    int checkOpacityInactive(int s) const;
    bool checkIgnoreGeometry(bool ignore, bool init = false) const;
    QVector<win::virtual_desktop*> checkDesktops(virtual_desktop_manager const& manager,
                                                 QVector<virtual_desktop*> vds,
                                                 bool init = false) const
    {
        for (auto&& rule : rules) {
            if (rule->applyDesktops(manager, vds, init)) {
                break;
            }
        }

        return vds;
    }

    template<typename Base, typename Output>
    Output const* checkScreen(Base& base, Output const* output, bool init = false) const
    {
        if (rules.size() == 0) {
            return output;
        }

        auto const& outputs = base.outputs;
        int index = output ? base::get_output_index(outputs, *output) : 0;

        for (auto&& rule : rules) {
            if (rule->applyScreen(index, init))
                break;
        }

        return base::get_output(outputs, index);
    }

    win_type checkType(win_type type) const;
    maximize_mode checkMaximize(maximize_mode mode, bool init = false) const;
    bool checkMinimize(bool minimized, bool init = false) const;
    bool checkSkipTaskbar(bool skip, bool init = false) const;
    bool checkSkipPager(bool skip, bool init = false) const;
    bool checkSkipSwitcher(bool skip, bool init = false) const;
    bool checkKeepAbove(bool above, bool init = false) const;
    bool checkKeepBelow(bool below, bool init = false) const;
    bool checkFullScreen(bool fs, bool init = false) const;
    bool checkNoBorder(bool noborder, bool init = false) const;
    QString checkDecoColor(QString schemeFile) const;
    bool checkBlockCompositing(bool block) const;
    fsp_level checkFSP(fsp_level fsp) const;
    fsp_level checkFPP(fsp_level fpp) const;
    bool checkAcceptFocus(bool focus) const;
    bool checkCloseable(bool closeable) const;
    bool checkAutogrouping(bool autogroup) const;
    bool checkAutogroupInForeground(bool fg) const;
    QString checkAutogroupById(QString id) const;
    bool checkStrictGeometry(bool strict) const;
    QString checkShortcut(QString s, bool init = false) const;
    bool checkDisableGlobalShortcuts(bool disable) const;
    QString checkDesktopFile(QString desktopFile, bool init = false) const;

    std::vector<ruling*> rules;

private:
    maximize_mode checkMaximizeVert(maximize_mode mode, bool init) const;
    maximize_mode checkMaximizeHoriz(maximize_mode mode, bool init) const;

    template<typename T, typename F>
    T check_set(T data, bool init, F apply_call) const
    {
        if (rules.size() == 0) {
            return data;
        }
        for (auto&& rule : rules) {
            if (std::invoke(apply_call, rule, data, init)) {
                break;
            }
        }
        return data;
    }

    template<typename T, typename F>
    T check_force(T data, F apply_call) const
    {
        if (rules.size() == 0) {
            return data;
        }
        for (auto&& rule : rules) {
            if (std::invoke(apply_call, rule, data)) {
                break;
            }
        }
        return data;
    }
};

}
}
