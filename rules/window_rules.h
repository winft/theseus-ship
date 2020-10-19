/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_RULES_WINDOW_RULES_H
#define KWIN_RULES_WINDOW_RULES_H


#include <netwm_def.h>
#include <QRect>
#include <QVector>

#include "placement.h"
#include "options.h"
#include "utils.h"

class QDebug;
class KConfig;
class KXMessages;

namespace KWin
{

class AbstractClient;
class Rules;
class RuleSettings;

namespace win
{
enum class maximize_mode;
}

#ifndef KCMRULES // only for kwin core

class WindowRules
{
public:
    explicit WindowRules(const QVector< Rules* >& rules);
    WindowRules();
    void update(AbstractClient*, int selection);
    void discardTemporary();
    bool contains(const Rules* rule) const;
    void remove(Rules* rule);
    Placement::Policy checkPlacement(Placement::Policy placement) const;
    QRect checkGeometry(QRect rect, bool init = false) const;
    // use 'invalidPoint' with checkPosition, unlike QSize() and QRect(), QPoint() is a valid point
    QPoint checkPosition(QPoint pos, bool init = false) const;
    QSize checkSize(QSize s, bool init = false) const;
    QSize checkMinSize(QSize s) const;
    QSize checkMaxSize(QSize s) const;
    int checkOpacityActive(int s) const;
    int checkOpacityInactive(int s) const;
    bool checkIgnoreGeometry(bool ignore, bool init = false) const;
    int checkDesktop(int desktop, bool init = false) const;
    int checkScreen(int screen, bool init = false) const;
    QString checkActivity(QString activity, bool init = false) const;
    NET::WindowType checkType(NET::WindowType type) const;
    KWin::win::maximize_mode checkMaximize(win::maximize_mode mode, bool init = false) const;
    bool checkMinimize(bool minimized, bool init = false) const;
    ShadeMode checkShade(ShadeMode shade, bool init = false) const;
    bool checkSkipTaskbar(bool skip, bool init = false) const;
    bool checkSkipPager(bool skip, bool init = false) const;
    bool checkSkipSwitcher(bool skip, bool init = false) const;
    bool checkKeepAbove(bool above, bool init = false) const;
    bool checkKeepBelow(bool below, bool init = false) const;
    bool checkFullScreen(bool fs, bool init = false) const;
    bool checkNoBorder(bool noborder, bool init = false) const;
    QString checkDecoColor(QString schemeFile) const;
    bool checkBlockCompositing(bool block) const;
    int checkFSP(int fsp) const;
    int checkFPP(int fpp) const;
    bool checkAcceptFocus(bool focus) const;
    bool checkCloseable(bool closeable) const;
    bool checkAutogrouping(bool autogroup) const;
    bool checkAutogroupInForeground(bool fg) const;
    QString checkAutogroupById(QString id) const;
    bool checkStrictGeometry(bool strict) const;
    QString checkShortcut(QString s, bool init = false) const;
    bool checkDisableGlobalShortcuts(bool disable) const;
    QString checkDesktopFile(QString desktopFile, bool init = false) const;
private:
    MaximizeMode checkMaximizeVert(MaximizeMode mode, bool init) const;
    MaximizeMode checkMaximizeHoriz(MaximizeMode mode, bool init) const;
    QVector< Rules* > rules;
};

#endif

}

#endif
