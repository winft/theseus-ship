/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "rules.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
#include <QTemporaryFile>
#include <kconfig.h>

#ifndef KCMRULES
#include "client_machine.h"
#include "screens.h"
#include "win/win.h"
#include "workspace.h"
#include "x11client.h"
#endif

#include "rule_book_settings.h"
#include "rule_settings.h"

namespace KWin
{

Rules::Rules()
    : temporary_state(0)
    , wmclasscomplete(UnimportantMatch)
    , types(NET::AllTypesMask)
    , placementrule(force_rule::unused)
    , positionrule(set_rule::unused)
    , sizerule(set_rule::unused)
    , minsizerule(force_rule::unused)
    , maxsizerule(force_rule::unused)
    , opacityactiverule(force_rule::unused)
    , opacityinactiverule(force_rule::unused)
    , ignoregeometryrule(set_rule::unused)
    , desktoprule(set_rule::unused)
    , screenrule(set_rule::unused)
    , activityrule(set_rule::unused)
    , typerule(force_rule::unused)
    , maximizevertrule(set_rule::unused)
    , maximizehorizrule(set_rule::unused)
    , minimizerule(set_rule::unused)
    , shaderule(set_rule::unused)
    , skiptaskbarrule(set_rule::unused)
    , skippagerrule(set_rule::unused)
    , skipswitcherrule(set_rule::unused)
    , aboverule(set_rule::unused)
    , belowrule(set_rule::unused)
    , fullscreenrule(set_rule::unused)
    , noborderrule(set_rule::unused)
    , decocolorrule(force_rule::unused)
    , blockcompositingrule(force_rule::unused)
    , fsplevelrule(force_rule::unused)
    , fpplevelrule(force_rule::unused)
    , acceptfocusrule(force_rule::unused)
    , closeablerule(force_rule::unused)
    , autogrouprule(force_rule::unused)
    , autogroupfgrule(force_rule::unused)
    , autogroupidrule(force_rule::unused)
    , strictgeometryrule(force_rule::unused)
    , shortcutrule(set_rule::unused)
    , disableglobalshortcutsrule(force_rule::unused)
    , desktopfilerule(set_rule::unused)
{
}

Rules::Rules(const QString& str, bool temporary)
    : temporary_state(temporary ? 2 : 0)
{
    QTemporaryFile file;
    if (file.open()) {
        QByteArray s = str.toUtf8();
        file.write(s.data(), s.length());
    }
    file.flush();
    auto cfg = KSharedConfig::openConfig(file.fileName(), KConfig::SimpleConfig);
    RuleSettings settings(cfg, QString());
    readFromSettings(&settings);
    if (description.isEmpty())
        description = QStringLiteral("temporary");
}

#define READ_SET_RULE(var)                                                                         \
    var = settings->var();                                                                         \
    var##rule = static_cast<set_rule>(settings->var##rule())

#define READ_FORCE_RULE(var, func)                                                                 \
    var = func(settings->var());                                                                   \
    var##rule = convertForceRule(settings->var##rule())

Rules::Rules(const RuleSettings* settings)
    : temporary_state(0)
{
    readFromSettings(settings);
}

void Rules::readFromSettings(const RuleSettings* settings)
{
    description = settings->description();
    if (description.isEmpty()) {
        description = settings->descriptionLegacy();
    }

    auto read_bytes_match = [](auto const& data, auto const& match) {
        bytes_match bytes;
        bytes.data = data.toLower().toLatin1();
        bytes.match = static_cast<StringMatch>(match);
        return bytes;
    };

    auto read_string_match = [](auto const& data, auto const& match) {
        string_match str;
        str.data = data;
        str.match = static_cast<StringMatch>(match);
        return str;
    };

    wmclass = read_bytes_match(settings->wmclass(), settings->wmclassmatch());
    wmclasscomplete = settings->wmclasscomplete();
    windowrole = read_bytes_match(settings->windowrole(), settings->windowrolematch());
    clientmachine = read_bytes_match(settings->clientmachine(), settings->clientmachinematch());
    title = read_string_match(settings->title(), settings->titlematch());

    types = NET::WindowTypeMask(settings->types());
    READ_FORCE_RULE(placement, );
    READ_SET_RULE(position);
    READ_SET_RULE(size);
    if (size.isEmpty() && sizerule != static_cast<set_rule>(Remember))
        sizerule = set_rule::unused;
    READ_FORCE_RULE(minsize, );
    if (!minsize.isValid())
        minsize = QSize(1, 1);
    READ_FORCE_RULE(maxsize, );
    if (maxsize.isEmpty())
        maxsize = QSize(32767, 32767);
    READ_FORCE_RULE(opacityactive, );
    READ_FORCE_RULE(opacityinactive, );
    READ_SET_RULE(ignoregeometry);
    READ_SET_RULE(desktop);
    READ_SET_RULE(screen);
    READ_SET_RULE(activity);
    READ_FORCE_RULE(type, static_cast<NET::WindowType>);
    if (type == NET::Unknown)
        typerule = force_rule::unused;
    READ_SET_RULE(maximizevert);
    READ_SET_RULE(maximizehoriz);
    READ_SET_RULE(minimize);
    READ_SET_RULE(shade);
    READ_SET_RULE(skiptaskbar);
    READ_SET_RULE(skippager);
    READ_SET_RULE(skipswitcher);
    READ_SET_RULE(above);
    READ_SET_RULE(below);
    READ_SET_RULE(fullscreen);
    READ_SET_RULE(noborder);

    READ_FORCE_RULE(decocolor, getDecoColor);
    if (decocolor.isEmpty())
        decocolorrule = force_rule::unused;

    READ_FORCE_RULE(blockcompositing, );
    READ_FORCE_RULE(fsplevel, );
    READ_FORCE_RULE(fpplevel, );
    READ_FORCE_RULE(acceptfocus, );
    READ_FORCE_RULE(closeable, );
    READ_FORCE_RULE(autogroup, );
    READ_FORCE_RULE(autogroupfg, );
    READ_FORCE_RULE(autogroupid, );
    READ_FORCE_RULE(strictgeometry, );
    READ_SET_RULE(shortcut);
    READ_FORCE_RULE(disableglobalshortcuts, );
    READ_SET_RULE(desktopfile);
}

#undef READ_SET_RULE
#undef READ_FORCE_RULE
#undef READ_FORCE_RULE2

#define WRITE_SET_RULE(var, capital, func)                                                         \
    settings->set##capital##rule(static_cast<int>(var##rule));                                     \
    if (var##rule != set_rule::unused) {                                                           \
        settings->set##capital(func(var));                                                         \
    }

#define WRITE_FORCE_RULE(var, capital, func)                                                       \
    settings->set##capital##rule(static_cast<int>(var##rule));                                     \
    if (var##rule != force_rule::unused) {                                                         \
        settings->set##capital(func(var));                                                         \
    }

void Rules::write(RuleSettings* settings) const
{
    auto write_string
        = [&settings](auto const& str, auto data_writer, auto match_writer, bool force = false) {
              std::invoke(match_writer, settings, str.match);
              if (!str.data.isEmpty() || force) {
                  std::invoke(data_writer, settings, str.data);
              }
          };

    settings->setDescription(description);

    // Always write wmclass.
    write_string(wmclass, &RuleSettings::setWmclass, &RuleSettings::setWmclassmatch, true);
    settings->setWmclasscomplete(wmclasscomplete);
    write_string(windowrole, &RuleSettings::setWindowrole, &RuleSettings::setWindowrolematch);
    write_string(title, &RuleSettings::setTitle, &RuleSettings::setTitlematch);
    write_string(
        clientmachine, &RuleSettings::setClientmachine, &RuleSettings::setClientmachinematch);

    settings->setTypes(types);
    WRITE_FORCE_RULE(placement, Placement, );
    WRITE_SET_RULE(position, Position, );
    WRITE_SET_RULE(size, Size, );
    WRITE_FORCE_RULE(minsize, Minsize, );
    WRITE_FORCE_RULE(maxsize, Maxsize, );
    WRITE_FORCE_RULE(opacityactive, Opacityactive, );
    WRITE_FORCE_RULE(opacityinactive, Opacityinactive, );
    WRITE_SET_RULE(ignoregeometry, Ignoregeometry, );
    WRITE_SET_RULE(desktop, Desktop, );
    WRITE_SET_RULE(screen, Screen, );
    WRITE_SET_RULE(activity, Activity, );
    WRITE_FORCE_RULE(type, Type, );
    WRITE_SET_RULE(maximizevert, Maximizevert, );
    WRITE_SET_RULE(maximizehoriz, Maximizehoriz, );
    WRITE_SET_RULE(minimize, Minimize, );
    WRITE_SET_RULE(shade, Shade, );
    WRITE_SET_RULE(skiptaskbar, Skiptaskbar, );
    WRITE_SET_RULE(skippager, Skippager, );
    WRITE_SET_RULE(skipswitcher, Skipswitcher, );
    WRITE_SET_RULE(above, Above, );
    WRITE_SET_RULE(below, Below, );
    WRITE_SET_RULE(fullscreen, Fullscreen, );
    WRITE_SET_RULE(noborder, Noborder, );
    auto colorToString = [](const QString& value) -> QString {
        if (value.endsWith(QLatin1String(".colors"))) {
            return QFileInfo(value).baseName();
        } else {
            return value;
        }
    };
    WRITE_FORCE_RULE(decocolor, Decocolor, colorToString);
    WRITE_FORCE_RULE(blockcompositing, Blockcompositing, );
    WRITE_FORCE_RULE(fsplevel, Fsplevel, );
    WRITE_FORCE_RULE(fpplevel, Fpplevel, );
    WRITE_FORCE_RULE(acceptfocus, Acceptfocus, );
    WRITE_FORCE_RULE(closeable, Closeable, );
    WRITE_FORCE_RULE(autogroup, Autogroup, );
    WRITE_FORCE_RULE(autogroupfg, Autogroupfg, );
    WRITE_FORCE_RULE(autogroupid, Autogroupid, );
    WRITE_FORCE_RULE(strictgeometry, Strictgeometry, );
    WRITE_SET_RULE(shortcut, Shortcut, );
    WRITE_FORCE_RULE(disableglobalshortcuts, Disableglobalshortcuts, );
    WRITE_SET_RULE(desktopfile, Desktopfile, );
}

#undef WRITE_SET_RULE
#undef WRITE_FORCE_RULE

// returns true if it doesn't affect anything
bool Rules::isEmpty() const
{
    return (placementrule == force_rule::unused && positionrule == set_rule::unused
            && sizerule == set_rule::unused && minsizerule == force_rule::unused
            && maxsizerule == force_rule::unused && opacityactiverule == force_rule::unused
            && opacityinactiverule == force_rule::unused && ignoregeometryrule == set_rule::unused
            && desktoprule == set_rule::unused && screenrule == set_rule::unused
            && activityrule == set_rule::unused && typerule == force_rule::unused
            && maximizevertrule == set_rule::unused && maximizehorizrule == set_rule::unused
            && minimizerule == set_rule::unused && shaderule == set_rule::unused
            && skiptaskbarrule == set_rule::unused && skippagerrule == set_rule::unused
            && skipswitcherrule == set_rule::unused && aboverule == set_rule::unused
            && belowrule == set_rule::unused && fullscreenrule == set_rule::unused
            && noborderrule == set_rule::unused && decocolorrule == force_rule::unused
            && blockcompositingrule == force_rule::unused && fsplevelrule == force_rule::unused
            && fpplevelrule == force_rule::unused && acceptfocusrule == force_rule::unused
            && closeablerule == force_rule::unused && autogrouprule == force_rule::unused
            && autogroupfgrule == force_rule::unused && autogroupidrule == force_rule::unused
            && strictgeometryrule == force_rule::unused && shortcutrule == set_rule::unused
            && disableglobalshortcutsrule == force_rule::unused
            && desktopfilerule == set_rule::unused);
}

force_rule Rules::convertForceRule(int v)
{
    if (v == DontAffect || v == Force || v == ForceTemporarily)
        return static_cast<force_rule>(v);
    return force_rule::unused;
}

QString Rules::getDecoColor(const QString& themeName)
{
    if (themeName.isEmpty()) {
        return QString();
    }
    // find the actual scheme file
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                  QLatin1String("color-schemes/") + themeName
                                      + QLatin1String(".colors"));
}

bool Rules::matchType(NET::WindowType match_type) const
{
    if (types != NET::AllTypesMask) {
        if (match_type == NET::Unknown)
            match_type = NET::Normal; // NET::Unknown->NET::Normal is only here for matching
        if (!NET::typeMatchesMask(match_type, types))
            return false;
    }
    return true;
}

bool Rules::matchWMClass(const QByteArray& match_class, const QByteArray& match_name) const
{
    if (wmclass.match != UnimportantMatch) {
        // TODO optimize?
        QByteArray cwmclass = wmclasscomplete ? match_name + ' ' + match_class : match_class;
        if (wmclass.match == RegExpMatch
            && QRegExp(QString::fromUtf8(wmclass.data)).indexIn(QString::fromUtf8(cwmclass)) == -1)
            return false;
        if (wmclass.match == ExactMatch && wmclass.data != cwmclass)
            return false;
        if (wmclass.match == SubstringMatch && !cwmclass.contains(wmclass.data))
            return false;
    }
    return true;
}

bool Rules::matchRole(const QByteArray& match_role) const
{
    if (windowrole.match != UnimportantMatch) {
        if (windowrole.match == RegExpMatch
            && QRegExp(QString::fromUtf8(windowrole.data)).indexIn(QString::fromUtf8(match_role))
                == -1)
            return false;
        if (windowrole.match == ExactMatch && windowrole.data != match_role)
            return false;
        if (windowrole.match == SubstringMatch && !match_role.contains(windowrole.data))
            return false;
    }
    return true;
}

bool Rules::matchTitle(const QString& match_title) const
{
    if (title.match != UnimportantMatch) {
        if (title.match == RegExpMatch && QRegExp(title.data).indexIn(match_title) == -1)
            return false;
        if (title.match == ExactMatch && title.data != match_title)
            return false;
        if (title.match == SubstringMatch && !match_title.contains(title.data))
            return false;
    }
    return true;
}

bool Rules::matchClientMachine(const QByteArray& match_machine, bool local) const
{
    if (clientmachine.match != UnimportantMatch) {
        // if it's localhost, check also "localhost" before checking hostname
        if (match_machine != "localhost" && local && matchClientMachine("localhost", true))
            return true;
        if (clientmachine.match == RegExpMatch
            && QRegExp(QString::fromUtf8(clientmachine.data))
                    .indexIn(QString::fromUtf8(match_machine))
                == -1)
            return false;
        if (clientmachine.match == ExactMatch && clientmachine.data != match_machine)
            return false;
        if (clientmachine.match == SubstringMatch && !match_machine.contains(clientmachine.data))
            return false;
    }
    return true;
}

#ifndef KCMRULES
bool Rules::match(const AbstractClient* c) const
{
    if (!matchType(c->windowType(true)))
        return false;
    if (!matchWMClass(c->resourceClass(), c->resourceName()))
        return false;
    if (!matchRole(c->windowRole().toLower()))
        return false;
    if (!matchClientMachine(c->clientMachine()->hostName(), c->clientMachine()->isLocal()))
        return false;
    if (title.match != UnimportantMatch) // track title changes to rematch rules
        QObject::connect(
            c,
            &AbstractClient::captionChanged,
            c,
            &AbstractClient::evaluateWindowRules,
            // QueuedConnection, because title may change before
            // the client is ready (could segfault!)
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    if (!matchTitle(c->captionNormal()))
        return false;
    return true;
}

bool Rules::checkSetRule(set_rule rule, bool init)
{
    if (rule > static_cast<set_rule>(DontAffect)) { // Unused or DontAffect
        if (rule == (set_rule)Force || rule == (set_rule)ApplyNow
            || rule == (set_rule)ForceTemporarily || init)
            return true;
    }
    return false;
}

bool Rules::checkForceRule(force_rule rule)
{
    return rule == static_cast<force_rule>(Force)
        || rule == static_cast<force_rule>(ForceTemporarily);
}

bool Rules::checkSetStop(set_rule rule)
{
    return rule != set_rule::unused;
}

bool Rules::checkForceStop(force_rule rule)
{
    return rule != force_rule::unused;
}

#define NOW_REMEMBER(_T_, _V_) ((selection & _T_) && (_V_##rule == static_cast<set_rule>(Remember)))

bool Rules::update(AbstractClient* c, int selection)
{
    // TODO check this setting is for this client ?
    bool updated = false;
    if NOW_REMEMBER (Position, position) {
        if (!c->isFullScreen()) {
            QPoint new_pos = position;
            // don't use the position in the direction which is maximized
            if (!win::flags(c->maximizeMode() & win::maximize_mode::horizontal))
                new_pos.setX(c->pos().x());
            if (!win::flags(c->maximizeMode() & win::maximize_mode::vertical))
                new_pos.setY(c->pos().y());
            updated = updated || position != new_pos;
            position = new_pos;
        }
    }
    if NOW_REMEMBER (Size, size) {
        if (!c->isFullScreen()) {
            QSize new_size = size;
            // don't use the position in the direction which is maximized
            if (!win::flags(c->maximizeMode() & win::maximize_mode::horizontal))
                new_size.setWidth(c->size().width());
            if (!win::flags(c->maximizeMode() & win::maximize_mode::vertical))
                new_size.setHeight(c->size().height());
            updated = updated || size != new_size;
            size = new_size;
        }
    }
    if NOW_REMEMBER (Desktop, desktop) {
        updated = updated || desktop != c->desktop();
        desktop = c->desktop();
    }
    if NOW_REMEMBER (Screen, screen) {
        updated = updated || screen != c->screen();
        screen = c->screen();
    }
    if NOW_REMEMBER (Activity, activity) {
        // TODO: ivan - multiple activities support
        const QString& joinedActivities = c->activities().join(QStringLiteral(","));
        updated = updated || activity != joinedActivities;
        activity = joinedActivities;
    }
    if NOW_REMEMBER (MaximizeVert, maximizevert) {
        updated = updated || maximizevert != bool(c->maximizeMode() & win::maximize_mode::vertical);
        maximizevert = win::flags(c->maximizeMode() & win::maximize_mode::vertical);
    }
    if NOW_REMEMBER (MaximizeHoriz, maximizehoriz) {
        updated
            = updated || maximizehoriz != bool(c->maximizeMode() & win::maximize_mode::horizontal);
        maximizehoriz = win::flags(c->maximizeMode() & win::maximize_mode::horizontal);
    }
    if NOW_REMEMBER (Minimize, minimize) {
        updated = updated || minimize != c->isMinimized();
        minimize = c->isMinimized();
    }
    if NOW_REMEMBER (Shade, shade) {
        updated = updated || (shade != (c->shadeMode() != ShadeNone));
        shade = c->shadeMode() != ShadeNone;
    }
    if NOW_REMEMBER (SkipTaskbar, skiptaskbar) {
        updated = updated || skiptaskbar != c->skipTaskbar();
        skiptaskbar = c->skipTaskbar();
    }
    if NOW_REMEMBER (SkipPager, skippager) {
        updated = updated || skippager != c->skipPager();
        skippager = c->skipPager();
    }
    if NOW_REMEMBER (SkipSwitcher, skipswitcher) {
        updated = updated || skipswitcher != c->skipSwitcher();
        skipswitcher = c->skipSwitcher();
    }
    if NOW_REMEMBER (Above, above) {
        updated = updated || above != c->keepAbove();
        above = c->keepAbove();
    }
    if NOW_REMEMBER (Below, below) {
        updated = updated || below != c->keepBelow();
        below = c->keepBelow();
    }
    if NOW_REMEMBER (Fullscreen, fullscreen) {
        updated = updated || fullscreen != c->isFullScreen();
        fullscreen = c->isFullScreen();
    }
    if NOW_REMEMBER (NoBorder, noborder) {
        updated = updated || noborder != c->noBorder();
        noborder = c->noBorder();
    }
    if NOW_REMEMBER (DesktopFile, desktopfile) {
        updated = updated || desktopfile != c->desktopFileName();
        desktopfile = c->desktopFileName();
    }
    return updated;
}

#undef NOW_REMEMBER

#define APPLY_RULE(var, name, type)                                                                \
    bool Rules::apply##name(type& arg, bool init) const                                            \
    {                                                                                              \
        if (checkSetRule(var##rule, init))                                                         \
            arg = this->var;                                                                       \
        return checkSetStop(var##rule);                                                            \
    }

#define APPLY_FORCE_RULE(var, name, type)                                                          \
    bool Rules::apply##name(type& arg) const                                                       \
    {                                                                                              \
        if (checkForceRule(var##rule))                                                             \
            arg = this->var;                                                                       \
        return checkForceStop(var##rule);                                                          \
    }

APPLY_FORCE_RULE(placement, Placement, Placement::Policy)

bool Rules::applyGeometry(QRect& rect, bool init) const
{
    QPoint p = rect.topLeft();
    QSize s = rect.size();
    bool ret = false; // no short-circuiting
    if (applyPosition(p, init)) {
        rect.moveTopLeft(p);
        ret = true;
    }
    if (applySize(s, init)) {
        rect.setSize(s);
        ret = true;
    }
    return ret;
}

bool Rules::applyPosition(QPoint& pos, bool init) const
{
    if (this->position != invalidPoint && checkSetRule(positionrule, init))
        pos = this->position;
    return checkSetStop(positionrule);
}

bool Rules::applySize(QSize& s, bool init) const
{
    if (this->size.isValid() && checkSetRule(sizerule, init))
        s = this->size;
    return checkSetStop(sizerule);
}

APPLY_FORCE_RULE(minsize, MinSize, QSize)
APPLY_FORCE_RULE(maxsize, MaxSize, QSize)
APPLY_FORCE_RULE(opacityactive, OpacityActive, int)
APPLY_FORCE_RULE(opacityinactive, OpacityInactive, int)
APPLY_RULE(ignoregeometry, IgnoreGeometry, bool)

APPLY_RULE(desktop, Desktop, int)
APPLY_RULE(screen, Screen, int)
APPLY_RULE(activity, Activity, QString)
APPLY_FORCE_RULE(type, Type, NET::WindowType)

bool Rules::applyMaximizeHoriz(MaximizeMode& mode, bool init) const
{
    if (checkSetRule(maximizehorizrule, init))
        mode = static_cast<MaximizeMode>((maximizehoriz ? MaximizeHorizontal : 0)
                                         | (mode & MaximizeVertical));
    return checkSetStop(maximizehorizrule);
}

bool Rules::applyMaximizeVert(MaximizeMode& mode, bool init) const
{
    if (checkSetRule(maximizevertrule, init))
        mode = static_cast<MaximizeMode>((maximizevert ? MaximizeVertical : 0)
                                         | (mode & MaximizeHorizontal));
    return checkSetStop(maximizevertrule);
}

APPLY_RULE(minimize, Minimize, bool)

bool Rules::applyShade(ShadeMode& sh, bool init) const
{
    if (checkSetRule(shaderule, init)) {
        if (!this->shade)
            sh = ShadeNone;
        if (this->shade && sh == ShadeNone)
            sh = ShadeNormal;
    }
    return checkSetStop(shaderule);
}

APPLY_RULE(skiptaskbar, SkipTaskbar, bool)
APPLY_RULE(skippager, SkipPager, bool)
APPLY_RULE(skipswitcher, SkipSwitcher, bool)
APPLY_RULE(above, KeepAbove, bool)
APPLY_RULE(below, KeepBelow, bool)
APPLY_RULE(fullscreen, FullScreen, bool)
APPLY_RULE(noborder, NoBorder, bool)
APPLY_FORCE_RULE(decocolor, DecoColor, QString)
APPLY_FORCE_RULE(blockcompositing, BlockCompositing, bool)
APPLY_FORCE_RULE(fsplevel, FSP, int)
APPLY_FORCE_RULE(fpplevel, FPP, int)
APPLY_FORCE_RULE(acceptfocus, AcceptFocus, bool)
APPLY_FORCE_RULE(closeable, Closeable, bool)
APPLY_FORCE_RULE(autogroup, Autogrouping, bool)
APPLY_FORCE_RULE(autogroupfg, AutogroupInForeground, bool)
APPLY_FORCE_RULE(autogroupid, AutogroupById, QString)
APPLY_FORCE_RULE(strictgeometry, StrictGeometry, bool)
APPLY_RULE(shortcut, Shortcut, QString)
APPLY_FORCE_RULE(disableglobalshortcuts, DisableGlobalShortcuts, bool)
APPLY_RULE(desktopfile, DesktopFile, QString)

#undef APPLY_RULE
#undef APPLY_FORCE_RULE

bool Rules::isTemporary() const
{
    return temporary_state > 0;
}

bool Rules::discardTemporary(bool force)
{
    if (temporary_state == 0) // not temporary
        return false;
    if (force || --temporary_state == 0) { // too old
        delete this;
        return true;
    }
    return false;
}

#define DISCARD_USED_SET_RULE(var)                                                                 \
    do {                                                                                           \
        if (var##rule == static_cast<set_rule>(ApplyNow)                                           \
            || (withdrawn && var##rule == (set_rule)ForceTemporarily)) {                           \
            var##rule = set_rule::unused;                                                          \
            changed = true;                                                                        \
        }                                                                                          \
    } while (false)
#define DISCARD_USED_FORCE_RULE(var)                                                               \
    do {                                                                                           \
        if (withdrawn && var##rule == static_cast<force_rule>(ForceTemporarily)) {                 \
            var##rule = force_rule::unused;                                                        \
            changed = true;                                                                        \
        }                                                                                          \
    } while (false)

bool Rules::discardUsed(bool withdrawn)
{
    bool changed = false;
    DISCARD_USED_FORCE_RULE(placement);
    DISCARD_USED_SET_RULE(position);
    DISCARD_USED_SET_RULE(size);
    DISCARD_USED_FORCE_RULE(minsize);
    DISCARD_USED_FORCE_RULE(maxsize);
    DISCARD_USED_FORCE_RULE(opacityactive);
    DISCARD_USED_FORCE_RULE(opacityinactive);
    DISCARD_USED_SET_RULE(ignoregeometry);
    DISCARD_USED_SET_RULE(desktop);
    DISCARD_USED_SET_RULE(screen);
    DISCARD_USED_SET_RULE(activity);
    DISCARD_USED_FORCE_RULE(type);
    DISCARD_USED_SET_RULE(maximizevert);
    DISCARD_USED_SET_RULE(maximizehoriz);
    DISCARD_USED_SET_RULE(minimize);
    DISCARD_USED_SET_RULE(shade);
    DISCARD_USED_SET_RULE(skiptaskbar);
    DISCARD_USED_SET_RULE(skippager);
    DISCARD_USED_SET_RULE(skipswitcher);
    DISCARD_USED_SET_RULE(above);
    DISCARD_USED_SET_RULE(below);
    DISCARD_USED_SET_RULE(fullscreen);
    DISCARD_USED_SET_RULE(noborder);
    DISCARD_USED_FORCE_RULE(decocolor);
    DISCARD_USED_FORCE_RULE(blockcompositing);
    DISCARD_USED_FORCE_RULE(fsplevel);
    DISCARD_USED_FORCE_RULE(fpplevel);
    DISCARD_USED_FORCE_RULE(acceptfocus);
    DISCARD_USED_FORCE_RULE(closeable);
    DISCARD_USED_FORCE_RULE(autogroup);
    DISCARD_USED_FORCE_RULE(autogroupfg);
    DISCARD_USED_FORCE_RULE(autogroupid);
    DISCARD_USED_FORCE_RULE(strictgeometry);
    DISCARD_USED_SET_RULE(shortcut);
    DISCARD_USED_FORCE_RULE(disableglobalshortcuts);
    DISCARD_USED_SET_RULE(desktopfile);

    return changed;
}
#undef DISCARD_USED_SET_RULE
#undef DISCARD_USED_FORCE_RULE

#endif

QDebug& operator<<(QDebug& stream, const Rules* r)
{
    return stream << "[" << r->description << ":" << r->wmclass.data << "]";
}

}
