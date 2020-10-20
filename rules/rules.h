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
#include "placement.h"
#include "utils.h"
#include "window_rules.h"

class QDebug;

namespace KWin
{

class AbstractClient;
class RuleSettings;

enum class set_rule {
    unused = 0,
    dummy = 256 // so that it's at least short int
};
enum class force_rule {
    unused = 0,
    dummy = 256 // so that it's at least short int
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
        Shade = 1 << 6,
        SkipTaskbar = 1 << 7,
        SkipPager = 1 << 8,
        SkipSwitcher = 1 << 9,
        Above = 1 << 10,
        Below = 1 << 11,
        Fullscreen = 1 << 12,
        NoBorder = 1 << 13,
        OpacityActive = 1 << 14,
        OpacityInactive = 1 << 15,
        Activity = 1 << 16,
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
    bool match(const AbstractClient* c) const;
    bool update(AbstractClient*, int selection);
    bool isTemporary() const;
    bool discardTemporary(bool force); // removes if temporary and forced or too old
    bool applyPlacement(Placement::Policy& placement) const;
    bool applyGeometry(QRect& rect, bool init) const;
    // use 'invalidPoint' with applyPosition, unlike QSize() and QRect(), QPoint() is a valid point
    bool applyPosition(QPoint& pos, bool init) const;
    bool applySize(QSize& s, bool init) const;
    bool applyMinSize(QSize& s) const;
    bool applyMaxSize(QSize& s) const;
    bool applyOpacityActive(int& s) const;
    bool applyOpacityInactive(int& s) const;
    bool applyIgnoreGeometry(bool& ignore, bool init) const;
    bool applyDesktop(int& desktop, bool init) const;
    bool applyScreen(int& desktop, bool init) const;
    bool applyActivity(QString& activity, bool init) const;
    bool applyType(NET::WindowType& type) const;
    bool applyMaximizeVert(MaximizeMode& mode, bool init) const;
    bool applyMaximizeHoriz(MaximizeMode& mode, bool init) const;
    bool applyMinimize(bool& minimized, bool init) const;
    bool applyShade(ShadeMode& shade, bool init) const;
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
    Placement::Policy placement;
    force_rule placementrule;
    QPoint position;
    set_rule positionrule;
    QSize size;
    set_rule sizerule;
    QSize minsize;
    force_rule minsizerule;
    QSize maxsize;
    force_rule maxsizerule;
    int opacityactive;
    force_rule opacityactiverule;
    int opacityinactive;
    force_rule opacityinactiverule;
    bool ignoregeometry;
    set_rule ignoregeometryrule;
    int desktop;
    set_rule desktoprule;
    int screen;
    set_rule screenrule;
    QString activity;
    set_rule activityrule;
    NET::WindowType type; // type for setting
    force_rule typerule;
    bool maximizevert;
    set_rule maximizevertrule;
    bool maximizehoriz;
    set_rule maximizehorizrule;
    bool minimize;
    set_rule minimizerule;
    bool shade;
    set_rule shaderule;
    bool skiptaskbar;
    set_rule skiptaskbarrule;
    bool skippager;
    set_rule skippagerrule;
    bool skipswitcher;
    set_rule skipswitcherrule;
    bool above;
    set_rule aboverule;
    bool below;
    set_rule belowrule;
    bool fullscreen;
    set_rule fullscreenrule;
    bool noborder;
    set_rule noborderrule;
    QString decocolor;
    force_rule decocolorrule;
    bool blockcompositing;
    force_rule blockcompositingrule;
    int fsplevel;
    int fpplevel;
    force_rule fsplevelrule;
    force_rule fpplevelrule;
    bool acceptfocus;
    force_rule acceptfocusrule;
    bool closeable;
    force_rule closeablerule;
    bool autogroup;
    force_rule autogrouprule;
    bool autogroupfg;
    force_rule autogroupfgrule;
    QString autogroupid;
    force_rule autogroupidrule;
    bool strictgeometry;
    force_rule strictgeometryrule;
    QString shortcut;
    set_rule shortcutrule;
    bool disableglobalshortcuts;
    force_rule disableglobalshortcutsrule;
    QString desktopfile;
    set_rule desktopfilerule;
    friend QDebug& operator<<(QDebug& stream, const Rules*);
};

QDebug& operator<<(QDebug& stream, const Rules*);

} // namespace

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::Rules::Types)

#endif
