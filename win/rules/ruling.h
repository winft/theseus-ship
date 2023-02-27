/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include <QRect>
#include <netwm_def.h>

#include "base/options.h"
#include "win/types.h"
#include "win/virtual_desktops.h"

class QDebug;

namespace KWin::win::rules
{

class settings;

template<typename T>
struct set_ruler {
    T data;
    set_rule rule{set_rule::unused};
};

template<typename T>
struct force_ruler {
    T data;
    force_rule rule{force_rule::unused};
};

class KWIN_EXPORT ruling
{
public:
    ruling();
    explicit ruling(settings const*);

    void write(settings*) const;
    bool isEmpty() const;
    bool discardUsed(bool withdrawn);

    bool applyPlacement(win::placement& placement) const;
    bool applyGeometry(QRect& rect, bool init) const;
    // use 'invalidPoint' with applyPosition, unlike QSize() and QRect(), QPoint() is a valid point
    bool applyPosition(QPoint& pos, bool init) const;
    bool applySize(QSize& s, bool init) const;
    bool applyMinSize(QSize& size) const;
    bool applyMaxSize(QSize& size) const;
    bool applyOpacityActive(int& s) const;
    bool applyOpacityInactive(int& s) const;
    bool applyIgnoreGeometry(bool& ignore, bool init) const;
    bool applyDesktops(virtual_desktop_manager const& manager,
                       QVector<virtual_desktop*>& vds,
                       bool init) const;
    bool applyScreen(int& screen, bool init) const;
    bool applyType(NET::WindowType& type) const;
    bool applyMaximizeVert(win::maximize_mode& mode, bool init) const;
    bool applyMaximizeHoriz(win::maximize_mode& mode, bool init) const;
    bool applyMinimize(bool& minimize, bool init) const;
    bool applySkipTaskbar(bool& skip, bool init) const;
    bool applySkipPager(bool& skip, bool init) const;
    bool applySkipSwitcher(bool& skip, bool init) const;
    bool applyKeepAbove(bool& above, bool init) const;
    bool applyKeepBelow(bool& below, bool init) const;
    bool applyFullScreen(bool& fs, bool init) const;
    bool applyNoBorder(bool& noborder, bool init) const;
    bool applyDecoColor(QString& schemeFile) const;
    bool applyBlockCompositing(bool& block) const;
    bool applyFSP(win::fsp_level& fsp) const;
    bool applyFPP(win::fsp_level& fpp) const;
    bool applyAcceptFocus(bool& focus) const;
    bool applyCloseable(bool& closeable) const;
    bool applyAutogrouping(bool& autogroup) const;
    bool applyAutogroupInForeground(bool& fg) const;
    bool applyAutogroupById(QString& id) const;
    bool applyStrictGeometry(bool& strict) const;
    bool applyShortcut(QString& shortcut, bool init) const;
    bool applyDisableGlobalShortcuts(bool& disable) const;
    bool applyDesktopFile(QString& desktopFile, bool init) const;

    bool matchType(NET::WindowType match_type) const;
    bool matchWMClass(QByteArray const& match_class, QByteArray const& match_name) const;
    bool matchRole(QByteArray const& match_role) const;
    bool matchTitle(QString const& match_title) const;
    bool matchClientMachine(QByteArray const& match_machine, bool local) const;

    void readFromSettings(rules::settings const* settings);
    static force_rule convertForceRule(int v);
    static QString getDecoColor(QString const& themeName);
    static bool checkSetRule(set_rule rule, bool init);
    static bool checkForceRule(force_rule rule);
    static bool checkSetStop(set_rule rule);
    static bool checkForceStop(force_rule rule);

    template<typename T>
    bool apply_force_enum(force_ruler<int> const& ruler, T& apply, T min, T max) const;

    template<typename T>
    bool apply_set(T& target, set_ruler<T> const& ruler, bool init) const
    {
        if (checkSetRule(ruler.rule, init)) {
            target = ruler.data;
        }
        return checkSetStop(ruler.rule);
    }

    template<typename T>
    bool apply_force(T& target, force_ruler<T> const& ruler) const
    {
        if (checkForceRule(ruler.rule)) {
            target = ruler.data;
        }
        return checkForceStop(ruler.rule);
    }

    struct bytes_match {
        QByteArray data;
        name_match match{name_match::unimportant};
    };
    struct string_match {
        QString data;
        name_match match{name_match::unimportant};
    };

    bytes_match wmclass;
    bytes_match windowrole;
    bytes_match clientmachine;
    string_match title;

    QString description;
    bool wmclasscomplete;
    NET::WindowTypes types; // types for matching

    set_ruler<bool> above;
    set_ruler<bool> below;
    set_ruler<bool> ignoregeometry;
    set_ruler<QStringList> desktops;
    set_ruler<QString> desktopfile;
    set_ruler<bool> fullscreen;
    set_ruler<bool> maximizehoriz;
    set_ruler<bool> maximizevert;
    set_ruler<bool> minimize;
    set_ruler<bool> noborder;
    set_ruler<QPoint> position;
    set_ruler<int> screen;
    set_ruler<QString> shortcut;
    set_ruler<QSize> size;
    set_ruler<bool> skippager;
    set_ruler<bool> skipswitcher;
    set_ruler<bool> skiptaskbar;

    force_ruler<bool> acceptfocus;
    force_ruler<bool> autogroup;
    force_ruler<bool> autogroupfg;
    force_ruler<QString> autogroupid;
    force_ruler<bool> blockcompositing;
    force_ruler<bool> closeable;
    force_ruler<QString> decocolor;
    force_ruler<bool> disableglobalshortcuts;
    force_ruler<int> fpplevel;
    force_ruler<int> fsplevel;
    force_ruler<QSize> maxsize;
    force_ruler<QSize> minsize;
    force_ruler<int> opacityactive;
    force_ruler<int> opacityinactive;
    force_ruler<int> placement;
    force_ruler<bool> strictgeometry;
    force_ruler<NET::WindowType> type;

    friend QDebug& operator<<(QDebug& stream, ruling const*);
};

KWIN_EXPORT QDebug& operator<<(QDebug& stream, ruling const*);

}
