/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_rules.h"

#include <kconfig.h>

#ifndef KCMRULES
#include "x11client.h"
#include "client_machine.h"
#include "screens.h"
#include "win/win.h"
#include "workspace.h"
#endif

#include "rule_book.h"
#include "rule_settings.h"
#include "rule_book_settings.h"

namespace KWin
{

WindowRules::WindowRules(const QVector< Rules* >& r)
    : rules(r)
{
}

WindowRules::WindowRules()
{
}

bool WindowRules::contains(const Rules* rule) const
{
    return rules.contains(const_cast<Rules *>(rule));
}

void WindowRules::remove(Rules* rule)
{
    rules.removeOne(rule);
}

#ifndef KCMRULES
void WindowRules::discardTemporary()
{
    QVector< Rules* >::Iterator it2 = rules.begin();
    for (QVector< Rules* >::Iterator it = rules.begin();
            it != rules.end();
       ) {
        if ((*it)->discardTemporary(true))
            ++it;
        else {
            *it2++ = *it++;
        }
    }
    rules.erase(it2, rules.end());
}

void WindowRules::update(AbstractClient* c, int selection)
{
    bool updated = false;
    for (QVector< Rules* >::ConstIterator it = rules.constBegin();
            it != rules.constEnd();
            ++it)
        if ((*it)->update(c, selection))    // no short-circuiting here
            updated = true;
    if (updated)
        RuleBook::self()->requestDiskStorage();
}

#define CHECK_RULE( rule, type ) \
    type WindowRules::check##rule( type arg, bool init ) const \
    { \
        if ( rules.count() == 0 ) \
            return arg; \
        type ret = arg; \
        for ( QVector< Rules* >::ConstIterator it = rules.constBegin(); \
                it != rules.constEnd(); \
                ++it ) \
        { \
            if ( (*it)->apply##rule( ret, init )) \
                break; \
        } \
        return ret; \
    }

#define CHECK_FORCE_RULE( rule, type ) \
    type WindowRules::check##rule( type arg ) const \
    { \
        if ( rules.count() == 0 ) \
            return arg; \
        type ret = arg; \
        for ( QVector< Rules* >::ConstIterator it = rules.begin(); \
                it != rules.end(); \
                ++it ) \
        { \
            if ( (*it)->apply##rule( ret )) \
                break; \
        } \
        return ret; \
    }

CHECK_FORCE_RULE(Placement, Placement::Policy)

QRect WindowRules::checkGeometry(QRect rect, bool init) const
{
    return QRect(checkPosition(rect.topLeft(), init), checkSize(rect.size(), init));
}

CHECK_RULE(Position, QPoint)
CHECK_RULE(Size, QSize)
CHECK_FORCE_RULE(MinSize, QSize)
CHECK_FORCE_RULE(MaxSize, QSize)
CHECK_FORCE_RULE(OpacityActive, int)
CHECK_FORCE_RULE(OpacityInactive, int)
CHECK_RULE(IgnoreGeometry, bool)

CHECK_RULE(Desktop, int)
CHECK_RULE(Activity, QString)
CHECK_FORCE_RULE(Type, NET::WindowType)
CHECK_RULE(MaximizeVert, MaximizeMode)
CHECK_RULE(MaximizeHoriz, MaximizeMode)

win::maximize_mode WindowRules::checkMaximize(win::maximize_mode mode, bool init) const
{
    auto vert = get_maximize_mode(checkMaximizeVert(get_MaximizeMode(mode), init))
        & win::maximize_mode::vertical;
    auto horiz = get_maximize_mode(checkMaximizeHoriz(get_MaximizeMode(mode), init))
        & win::maximize_mode::horizontal;
    return vert | horiz;
}

int WindowRules::checkScreen(int screen, bool init) const
{
    if ( rules.count() == 0 )
        return screen;
    int ret = screen;
    for ( QVector< Rules* >::ConstIterator it = rules.constBegin(); it != rules.constEnd(); ++it ) {
        if ( (*it)->applyScreen( ret, init ))
            break;
    }
    if (ret >= Screens::self()->count())
        ret = screen;
    return ret;
}

CHECK_RULE(Minimize, bool)
CHECK_RULE(Shade, ShadeMode)
CHECK_RULE(SkipTaskbar, bool)
CHECK_RULE(SkipPager, bool)
CHECK_RULE(SkipSwitcher, bool)
CHECK_RULE(KeepAbove, bool)
CHECK_RULE(KeepBelow, bool)
CHECK_RULE(FullScreen, bool)
CHECK_RULE(NoBorder, bool)
CHECK_FORCE_RULE(DecoColor, QString)
CHECK_FORCE_RULE(BlockCompositing, bool)
CHECK_FORCE_RULE(FSP, int)
CHECK_FORCE_RULE(FPP, int)
CHECK_FORCE_RULE(AcceptFocus, bool)
CHECK_FORCE_RULE(Closeable, bool)
CHECK_FORCE_RULE(Autogrouping, bool)
CHECK_FORCE_RULE(AutogroupInForeground, bool)
CHECK_FORCE_RULE(AutogroupById, QString)
CHECK_FORCE_RULE(StrictGeometry, bool)
CHECK_RULE(Shortcut, QString)
CHECK_FORCE_RULE(DisableGlobalShortcuts, bool)
CHECK_RULE(DesktopFile, QString)

#undef CHECK_RULE
#undef CHECK_FORCE_RULE

// Client

void AbstractClient::setupWindowRules(bool ignore_temporary)
{
    disconnect(this, &AbstractClient::captionChanged, this, &AbstractClient::evaluateWindowRules);
    m_rules = RuleBook::self()->find(this, ignore_temporary);
    // check only after getting the rules, because there may be a rule forcing window type
}

// Applies Force, ForceTemporarily and ApplyNow rules
// Used e.g. after the rules have been modified using the kcm.
void AbstractClient::applyWindowRules()
{
    // apply force rules
    // Placement - does need explicit update, just like some others below
    // Geometry : setGeometry() doesn't check rules
    auto client_rules = rules();
    QRect orig_geom = QRect(pos(), sizeForClientSize(clientSize()));   // handle shading
    QRect geom = client_rules->checkGeometry(orig_geom);
    if (geom != orig_geom)
        setFrameGeometry(geom);
    // MinSize, MaxSize handled by Geometry
    // IgnoreGeometry
    win::set_desktop(this, desktop());
    workspace()->sendClientToScreen(this, screen());
    setOnActivities(activities());
    // Type
    win::maximize(this, maximizeMode());
    // Minimize : functions don't check, and there are two functions
    if (client_rules->checkMinimize(isMinimized()))
        minimize();
    else
        unminimize();
    setShade(shadeMode());
    setOriginalSkipTaskbar(skipTaskbar());
    setSkipPager(skipPager());
    setSkipSwitcher(skipSwitcher());
    setKeepAbove(keepAbove());
    setKeepBelow(keepBelow());
    setFullScreen(isFullScreen(), true);
    setNoBorder(noBorder());
    updateColorScheme();
    // FSP
    // AcceptFocus :
    if (workspace()->mostRecentlyActivatedClient() == this
            && !client_rules->checkAcceptFocus(true))
        workspace()->activateNextClient(this);
    // Closeable
    auto s = win::adjusted_size(this);
    if (s != size() && s.isValid())
        resizeWithChecks(s);
    // Autogrouping : Only checked on window manage
    // AutogroupInForeground : Only checked on window manage
    // AutogroupById : Only checked on window manage
    // StrictGeometry
    setShortcut(rules()->checkShortcut(shortcut().toString()));
    // see also X11Client::setActive()
    if (isActive()) {
        setOpacity(rules()->checkOpacityActive(qRound(opacity() * 100.0)) / 100.0);
        workspace()->disableGlobalShortcutsForClient(rules()->checkDisableGlobalShortcuts(false));
    } else
        setOpacity(rules()->checkOpacityInactive(qRound(opacity() * 100.0)) / 100.0);
    setDesktopFileName(rules()->checkDesktopFile(desktopFileName()).toUtf8());
}

void X11Client::updateWindowRules(Rules::Types selection)
{
    if (!isManaged())  // not fully setup yet
        return;
    AbstractClient::updateWindowRules(selection);
}

void AbstractClient::updateWindowRules(Rules::Types selection)
{
    if (RuleBook::self()->areUpdatesDisabled())
        return;
    m_rules.update(this, selection);
}

void AbstractClient::finishWindowRules()
{
    updateWindowRules(Rules::All);
    m_rules = WindowRules();
}

#endif

}
