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

    auto read_set_rule = [&settings](auto const& data, auto const& rule) {
        set_ruler<std::decay_t<decltype(data)>> set;
        set.data = data;
        set.rule = static_cast<set_rule>(rule);
        return set;
    };

    activity = read_set_rule(settings->activity(), settings->activityrule());
    above = read_set_rule(settings->above(), settings->aboverule());
    below = read_set_rule(settings->below(), settings->belowrule());
    desktop = read_set_rule(settings->desktop(), settings->desktoprule());
    desktopfile = read_set_rule(settings->desktopfile(), settings->desktopfilerule());
    fullscreen = read_set_rule(settings->fullscreen(), settings->fullscreenrule());
    ignoregeometry = read_set_rule(settings->ignoregeometry(), settings->ignoregeometryrule());
    maximizehoriz = read_set_rule(settings->maximizehoriz(), settings->maximizehorizrule());
    maximizevert = read_set_rule(settings->maximizevert(), settings->maximizevertrule());
    minimize = read_set_rule(settings->minimize(), settings->minimizerule());
    noborder = read_set_rule(settings->noborder(), settings->noborderrule());
    position = read_set_rule(settings->position(), settings->positionrule());
    screen = read_set_rule(settings->screen(), settings->screenrule());
    shade = read_set_rule(settings->shade(), settings->shaderule());
    shortcut = read_set_rule(settings->shortcut(), settings->shortcutrule());
    size = read_set_rule(settings->size(), settings->sizerule());
    if (size.data.isEmpty() && size.rule != static_cast<set_rule>(Remember)) {
        size.rule = set_rule::unused;
    }

    skippager = read_set_rule(settings->skippager(), settings->skippagerrule());
    skipswitcher = read_set_rule(settings->skipswitcher(), settings->skipswitcherrule());
    skiptaskbar = read_set_rule(settings->skiptaskbar(), settings->skiptaskbarrule());

    auto read_force_rule = [&settings](auto const& data, auto const& rule) {
        force_ruler<std::decay_t<decltype(data)>> ruler;

        ruler.data = data;
        ruler.rule = static_cast<force_rule>(rule);
        return ruler;
    };

    acceptfocus = read_force_rule(settings->acceptfocus(), settings->acceptfocusrule());
    autogroup = read_force_rule(settings->autogroup(), settings->autogrouprule());
    autogroupfg = read_force_rule(settings->autogroupfg(), settings->autogroupfgrule());
    autogroupid = read_force_rule(settings->autogroupid(), settings->autogroupidrule());
    blockcompositing
        = read_force_rule(settings->blockcompositing(), settings->blockcompositingrule());

    closeable = read_force_rule(settings->closeable(), settings->closeablerule());

    decocolor = read_force_rule(getDecoColor(settings->decocolor()), settings->decocolorrule());
    if (decocolor.data.isEmpty()) {
        decocolor.rule = force_rule::unused;
    }

    disableglobalshortcuts = read_force_rule(settings->disableglobalshortcuts(),
                                             settings->disableglobalshortcutsrule());
    fpplevel = read_force_rule(settings->fpplevel(), settings->fpplevelrule());
    fsplevel = read_force_rule(settings->fsplevel(), settings->fsplevelrule());

    maxsize = read_force_rule(settings->maxsize(), settings->maxsizerule());
    if (maxsize.data.isEmpty()) {
        maxsize.data = QSize(32767, 32767);
    }
    minsize = read_force_rule(settings->minsize(), settings->minsizerule());
    if (!minsize.data.isValid()) {
        minsize.data = QSize(1, 1);
    }

    opacityactive = read_force_rule(settings->opacityactive(), settings->opacityactiverule());
    opacityinactive = read_force_rule(settings->opacityinactive(), settings->opacityinactiverule());
    placement = read_force_rule(settings->placement(), settings->placementrule());
    strictgeometry = read_force_rule(settings->strictgeometry(), settings->strictgeometryrule());

    type = read_force_rule(static_cast<NET::WindowType>(settings->type()), settings->typerule());
    if (type.data == NET::Unknown) {
        type.rule = force_rule::unused;
    }
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

    auto write_set = [&settings](auto const& ruler, auto rule_writer, auto data_writer) {
        std::invoke(rule_writer, settings, static_cast<int>(ruler.rule));
        if (ruler.rule != set_rule::unused) {
            std::invoke(data_writer, settings, ruler.data);
        }
    };

    write_set(above, &RuleSettings::setAboverule, &RuleSettings::setAbove);
    write_set(activity, &RuleSettings::setActivityrule, &RuleSettings::setActivity);
    write_set(below, &RuleSettings::setBelowrule, &RuleSettings::setBelow);
    write_set(desktop, &RuleSettings::setDesktoprule, &RuleSettings::setDesktop);
    write_set(desktopfile, &RuleSettings::setDesktopfilerule, &RuleSettings::setDesktopfile);
    write_set(fullscreen, &RuleSettings::setFullscreenrule, &RuleSettings::setFullscreen);
    write_set(
        ignoregeometry, &RuleSettings::setIgnoregeometryrule, &RuleSettings::setIgnoregeometry);
    write_set(maximizehoriz, &RuleSettings::setMaximizehorizrule, &RuleSettings::setMaximizehoriz);
    write_set(maximizevert, &RuleSettings::setMaximizevertrule, &RuleSettings::setMaximizevert);
    write_set(minimize, &RuleSettings::setMinimizerule, &RuleSettings::setMinimize);
    write_set(noborder, &RuleSettings::setNoborderrule, &RuleSettings::setNoborder);
    write_set(position, &RuleSettings::setPositionrule, &RuleSettings::setPosition);
    write_set(screen, &RuleSettings::setScreenrule, &RuleSettings::setScreen);
    write_set(shade, &RuleSettings::setShaderule, &RuleSettings::setShade);
    write_set(shortcut, &RuleSettings::setShortcutrule, &RuleSettings::setShortcut);
    write_set(size, &RuleSettings::setSizerule, &RuleSettings::setSize);
    write_set(skippager, &RuleSettings::setSkippagerrule, &RuleSettings::setSkippager);
    write_set(skipswitcher, &RuleSettings::setSkipswitcherrule, &RuleSettings::setSkipswitcher);
    write_set(skiptaskbar, &RuleSettings::setSkiptaskbarrule, &RuleSettings::setSkiptaskbar);

    auto write_force = [&settings](auto const& ruler, auto rule_writer, auto data_writer) {
        std::invoke(rule_writer, settings, static_cast<int>(ruler.rule));
        if (ruler.rule != force_rule::unused) {
            std::invoke(data_writer, settings, ruler.data);
        }
    };

    // TODO: Integrate this with above labmda once we can use lambda template parameters in C++20.
    auto convert_write_force
        = [&settings](auto const& ruler, auto rule_writer, auto data_writer, auto converter) {
              std::invoke(rule_writer, settings, static_cast<int>(ruler.rule));
              if (ruler.rule != force_rule::unused) {
                  std::invoke(data_writer, settings, converter(ruler.data));
              }
          };

    write_force(acceptfocus, &RuleSettings::setAcceptfocusrule, &RuleSettings::setAcceptfocus);
    write_force(autogroup, &RuleSettings::setAutogrouprule, &RuleSettings::setAutogroup);
    write_force(autogroupfg, &RuleSettings::setAutogroupfgrule, &RuleSettings::setAutogroupfg);
    write_force(autogroupid, &RuleSettings::setAutogroupidrule, &RuleSettings::setAutogroupid);
    write_force(blockcompositing,
                &RuleSettings::setBlockcompositingrule,
                &RuleSettings::setBlockcompositing);
    write_force(closeable, &RuleSettings::setCloseablerule, &RuleSettings::setCloseable);
    write_force(disableglobalshortcuts,
                &RuleSettings::setDisableglobalshortcutsrule,
                &RuleSettings::setDisableglobalshortcuts);
    write_force(fpplevel, &RuleSettings::setFpplevelrule, &RuleSettings::setFpplevel);
    write_force(fsplevel, &RuleSettings::setFsplevelrule, &RuleSettings::setFsplevel);

    auto colorToString = [](const QString& value) -> QString {
        if (value.endsWith(QLatin1String(".colors"))) {
            return QFileInfo(value).baseName();
        } else {
            return value;
        }
    };
    convert_write_force(
        decocolor, &RuleSettings::setDecocolorrule, &RuleSettings::setDecocolor, colorToString);

    write_force(maxsize, &RuleSettings::setMaxsizerule, &RuleSettings::setMaxsize);
    write_force(minsize, &RuleSettings::setMinsizerule, &RuleSettings::setMinsize);
    write_force(
        opacityactive, &RuleSettings::setOpacityactiverule, &RuleSettings::setOpacityactive);
    write_force(
        opacityinactive, &RuleSettings::setOpacityinactiverule, &RuleSettings::setOpacityinactive);
    write_force(placement, &RuleSettings::setPlacementrule, &RuleSettings::setPlacement);
    write_force(
        strictgeometry, &RuleSettings::setStrictgeometryrule, &RuleSettings::setStrictgeometry);
    write_force(type, &RuleSettings::setTyperule, &RuleSettings::setType);
}

// returns true if it doesn't affect anything
bool Rules::isEmpty() const
{
    auto unused_s = [](auto rule) { return rule == set_rule::unused; };
    auto unused_f = [](auto rule) { return rule == force_rule::unused; };

    return unused_s(position.rule) && unused_s(size.rule) && unused_s(desktopfile.rule)
        && unused_s(ignoregeometry.rule) && unused_s(desktop.rule) && unused_s(screen.rule)
        && unused_s(activity.rule) && unused_s(maximizevert.rule) && unused_s(maximizehoriz.rule)
        && unused_s(minimize.rule) && unused_s(shade.rule) && unused_s(skiptaskbar.rule)
        && unused_s(skippager.rule) && unused_s(skipswitcher.rule) && unused_s(above.rule)
        && unused_s(below.rule) && unused_s(fullscreen.rule) && unused_s(noborder.rule)
        && unused_f(decocolor.rule) && unused_f(blockcompositing.rule) && unused_f(fsplevel.rule)
        && unused_f(fpplevel.rule) && unused_f(acceptfocus.rule) && unused_f(closeable.rule)
        && unused_f(autogroup.rule) && unused_f(autogroupfg.rule) && unused_f(autogroupid.rule)
        && unused_f(strictgeometry.rule) && unused_s(shortcut.rule)
        && unused_f(disableglobalshortcuts.rule) && unused_f(minsize.rule) && unused_f(maxsize.rule)
        && unused_f(opacityactive.rule) && unused_f(opacityinactive.rule)
        && unused_f(placement.rule) && unused_f(type.rule);
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

bool Rules::update(AbstractClient* c, int selection)
{
    // TODO check this setting is for this client ?
    bool updated = false;

    auto remember = [selection](auto const& ruler, auto type) {
        return (selection & type) && ruler.rule == static_cast<set_rule>(Remember);
    };

    if (remember(above, Above)) {
        updated = updated || above.data != c->keepAbove();
        above.data = c->keepAbove();
    }
    if (remember(activity, Activity)) {
        // TODO: ivan - multiple activities support
        const QString& joinedActivities = c->activities().join(QStringLiteral(","));
        updated = updated || activity.data != joinedActivities;
        activity.data = joinedActivities;
    }
    if (remember(below, Below)) {
        updated = updated || below.data != c->keepBelow();
        below.data = c->keepBelow();
    }
    if (remember(desktop, Desktop)) {
        updated = updated || desktop.data != c->desktop();
        desktop.data = c->desktop();
    }
    if (remember(desktopfile, DesktopFile)) {
        updated = updated || desktopfile.data != c->desktopFileName();
        desktopfile.data = c->desktopFileName();
    }
    if (remember(fullscreen, Fullscreen)) {
        updated = updated || fullscreen.data != c->isFullScreen();
        fullscreen.data = c->isFullScreen();
    }

    if (remember(maximizehoriz, MaximizeHoriz)) {
        updated = updated
            || maximizehoriz.data != bool(c->maximizeMode() & win::maximize_mode::horizontal);
        maximizehoriz.data = win::flags(c->maximizeMode() & win::maximize_mode::horizontal);
    }
    if (remember(maximizevert, MaximizeVert)) {
        updated = updated
            || maximizevert.data != bool(c->maximizeMode() & win::maximize_mode::vertical);
        maximizevert.data = win::flags(c->maximizeMode() & win::maximize_mode::vertical);
    }
    if (remember(minimize, Minimize)) {
        updated = updated || minimize.data != c->isMinimized();
        minimize.data = c->isMinimized();
    }
    if (remember(noborder, NoBorder)) {
        updated = updated || noborder.data != c->noBorder();
        noborder.data = c->noBorder();
    }

    if (remember(position, Position)) {
        if (!c->isFullScreen()) {
            QPoint new_pos = position.data;

            // Don't use the position in the direction which is maximized.
            if (!win::flags(c->maximizeMode() & win::maximize_mode::horizontal)) {
                new_pos.setX(c->pos().x());
            }
            if (!win::flags(c->maximizeMode() & win::maximize_mode::vertical)) {
                new_pos.setY(c->pos().y());
            }
            updated = updated || position.data != new_pos;
            position.data = new_pos;
        }
    }

    if (remember(screen, Screen)) {
        updated = updated || screen.data != c->screen();
        screen.data = c->screen();
    }
    if (remember(shade, Shade)) {
        updated = updated || (shade.data != (c->shadeMode() != ShadeNone));
        shade.data = c->shadeMode() != ShadeNone;
    }
    if (remember(size, Size)) {
        if (!c->isFullScreen()) {
            QSize new_size = size.data;
            // don't use the position in the direction which is maximized
            if (!win::flags(c->maximizeMode() & win::maximize_mode::horizontal))
                new_size.setWidth(c->size().width());
            if (!win::flags(c->maximizeMode() & win::maximize_mode::vertical))
                new_size.setHeight(c->size().height());
            updated = updated || size.data != new_size;
            size.data = new_size;
        }
    }
    if (remember(skippager, SkipPager)) {
        updated = updated || skippager.data != c->skipPager();
        skippager.data = c->skipPager();
    }
    if (remember(skipswitcher, SkipSwitcher)) {
        updated = updated || skipswitcher.data != c->skipSwitcher();
        skipswitcher.data = c->skipSwitcher();
    }
    if (remember(skiptaskbar, SkipTaskbar)) {
        updated = updated || skiptaskbar.data != c->skipTaskbar();
        skiptaskbar.data = c->skipTaskbar();
    }

    return updated;
}

#define APPLY_RULE(var, name, type)                                                                \
    bool Rules::apply##name(type& arg, bool init) const                                            \
    {                                                                                              \
        if (checkSetRule(var.rule, init))                                                          \
            arg = this->var.data;                                                                  \
        return checkSetStop(var.rule);                                                             \
    }

#define APPLY_FORCE_RULE(var, name, type)                                                          \
    bool Rules::apply##name(type& arg) const                                                       \
    {                                                                                              \
        if (checkForceRule(var.rule))                                                              \
            arg = this->var.data;                                                                  \
        return checkForceStop(var.rule);                                                           \
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
    if (this->position.data != invalidPoint && checkSetRule(position.rule, init)) {
        pos = this->position.data;
    }
    return checkSetStop(position.rule);
}

bool Rules::applySize(QSize& s, bool init) const
{
    if (this->size.data.isValid() && checkSetRule(size.rule, init)) {
        s = this->size.data;
    }
    return checkSetStop(size.rule);
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
    if (checkSetRule(maximizehoriz.rule, init))
        mode = static_cast<MaximizeMode>((maximizehoriz.data ? MaximizeHorizontal : 0)
                                         | (mode & MaximizeVertical));
    return checkSetStop(maximizehoriz.rule);
}

bool Rules::applyMaximizeVert(MaximizeMode& mode, bool init) const
{
    if (checkSetRule(maximizevert.rule, init)) {
        mode = static_cast<MaximizeMode>((maximizevert.data ? MaximizeVertical : 0)
                                         | (mode & MaximizeHorizontal));
    }
    return checkSetStop(maximizevert.rule);
}

APPLY_RULE(minimize, Minimize, bool)

bool Rules::applyShade(ShadeMode& sh, bool init) const
{
    if (checkSetRule(shade.rule, init)) {
        if (!this->shade.data) {
            sh = ShadeNone;
        }
        if (this->shade.data && sh == ShadeNone) {
            sh = ShadeNormal;
        }
    }
    return checkSetStop(shade.rule);
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
        if (var.rule == static_cast<set_rule>(ApplyNow)                                            \
            || (withdrawn && var.rule == (set_rule)ForceTemporarily)) {                            \
            var.rule = set_rule::unused;                                                           \
            changed = true;                                                                        \
        }                                                                                          \
    } while (false)
#define DISCARD_USED_FORCE_RULE(var)                                                               \
    do {                                                                                           \
        if (withdrawn && var.rule == static_cast<force_rule>(ForceTemporarily)) {                  \
            var.rule = force_rule::unused;                                                         \
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
