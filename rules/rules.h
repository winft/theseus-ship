/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_RULES_H
#define KWIN_RULES_H

#include <QRect>
#include <netwm_def.h>

#include "options.h"
#include "win/types.h"

class QDebug;

namespace KWin
{
class RuleSettings;
class Toplevel;

enum class set_rule {
    unused = 0,
    dummy = 256 // so that it's at least short int
};
enum class force_rule {
    unused = 0,
    dummy = 256 // so that it's at least short int
};

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

class Rules
{
public:
    Rules();
    explicit Rules(const RuleSettings*);
    Rules(const QString&, bool temporary);

    enum Type {
        Position = 1 << 0,
        Size = 1 << 1,
        Desktop = 1 << 2,
        MaximizeVert = 1 << 3,
        MaximizeHoriz = 1 << 4,
        Minimize = 1 << 5,
        Shade = 1 << 6, // Deprecated
        SkipTaskbar = 1 << 7,
        SkipPager = 1 << 8,
        SkipSwitcher = 1 << 9,
        Above = 1 << 10,
        Below = 1 << 11,
        Fullscreen = 1 << 12,
        NoBorder = 1 << 13,
        OpacityActive = 1 << 14,
        OpacityInactive = 1 << 15,
        Activity = 1 << 16, // Deprecated
        Screen = 1 << 17,
        DesktopFile = 1 << 18,
        All = 0xffffffff
    };
    Q_DECLARE_FLAGS(Types, Type)
    // All these values are saved to the cfg file, and are also used in kstart!
    enum {
        Unused = 0,
        DontAffect,      // use the default value
        Force,           // force the given value
        Apply,           // apply only after initial mapping
        Remember,        // like apply, and remember the value when the window is withdrawn
        ApplyNow,        // apply immediatelly, then forget the setting
        ForceTemporarily // apply and force until the window is withdrawn
    };
    enum StringMatch {
        FirstStringMatch,
        UnimportantMatch = FirstStringMatch,
        ExactMatch,
        SubstringMatch,
        RegExpMatch,
        LastStringMatch = RegExpMatch
    };

    void write(RuleSettings*) const;
    bool isEmpty() const;

#ifndef KCMRULES
    bool discardUsed(bool withdrawn);
    bool match(Toplevel const* window) const;
    bool update(Toplevel* window, int selection);
    bool isTemporary() const;
    bool discardTemporary(bool force); // removes if temporary and forced or too old

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
    bool applyDesktop(int& desktop, bool init) const;
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
    bool applyFSP(int& fsp) const;
    bool applyFPP(int& fpp) const;
    bool applyAcceptFocus(bool& focus) const;
    bool applyCloseable(bool& closeable) const;
    bool applyAutogrouping(bool& autogroup) const;
    bool applyAutogroupInForeground(bool& fg) const;
    bool applyAutogroupById(QString& id) const;
    bool applyStrictGeometry(bool& strict) const;
    bool applyShortcut(QString& shortcut, bool init) const;
    bool applyDisableGlobalShortcuts(bool& disable) const;
    bool applyDesktopFile(QString& desktopFile, bool init) const;

private:
#endif
    bool matchType(NET::WindowType match_type) const;
    bool matchWMClass(const QByteArray& match_class, const QByteArray& match_name) const;
    bool matchRole(const QByteArray& match_role) const;
    bool matchTitle(const QString& match_title) const;
    bool matchClientMachine(const QByteArray& match_machine, bool local) const;
    void readFromSettings(const RuleSettings* settings);
    static force_rule convertForceRule(int v);
    static QString getDecoColor(const QString& themeName);
#ifndef KCMRULES
    static bool checkSetRule(set_rule rule, bool init);
    static bool checkForceRule(force_rule rule);
    static bool checkSetStop(set_rule rule);
    static bool checkForceStop(force_rule rule);
#endif

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
        StringMatch match{UnimportantMatch};
    };
    struct string_match {
        QString data;
        StringMatch match{UnimportantMatch};
    };

    bytes_match wmclass;
    bytes_match windowrole;
    bytes_match clientmachine;
    string_match title;

    int temporary_state; // e.g. for kstart
    QString description;
    bool wmclasscomplete;
    NET::WindowTypes types; // types for matching

    set_ruler<bool> above;
    set_ruler<bool> below;
    set_ruler<bool> ignoregeometry;
    set_ruler<int> desktop;
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

    friend QDebug& operator<<(QDebug& stream, const Rules*);
};

QDebug& operator<<(QDebug& stream, const Rules*);

} // namespace

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::Rules::Types)

#endif
