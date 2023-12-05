/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "ruling.h"

#include "base/output.h"
#include "base/output_helpers.h"
#include "utils/algorithm.h"
#include "utils/geo.h"
#include "win/setup.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <kconfig.h>

#include "book_settings.h"
#include "rules_settings.h"

namespace KWin::win::rules
{

ruling::ruling()
    : wmclasscomplete(enum_index(name_match::unimportant))
    , types(window_type_mask::all_types)
{
}

ruling::ruling(rules::settings const* settings)
{
    readFromSettings(settings);
}

void ruling::readFromSettings(rules::settings const* settings)
{
    description = settings->description();
    if (description.isEmpty()) {
        description = settings->descriptionLegacy();
    }

    auto read_bytes_match = [](auto const& data, auto const& match) {
        bytes_match bytes;
        bytes.data = data.toLatin1();
        bytes.match = static_cast<name_match>(match);
        return bytes;
    };

    auto read_string_match = [](auto const& data, auto const& match) {
        string_match str;
        str.data = data;
        str.match = static_cast<name_match>(match);
        return str;
    };

    wmclass = read_bytes_match(settings->wmclass(), settings->wmclassmatch());
    wmclasscomplete = settings->wmclasscomplete();
    windowrole = read_bytes_match(settings->windowrole(), settings->windowrolematch());
    clientmachine
        = read_bytes_match(settings->clientmachine().toLower(), settings->clientmachinematch());
    title = read_string_match(settings->title(), settings->titlematch());

    types = window_type_mask(settings->types());

    auto read_set_rule = [](auto const& data, auto const& rule) {
        set_ruler<std::decay_t<decltype(data)>> set;
        set.data = data;
        set.rule = static_cast<set_rule>(rule);
        return set;
    };

    above = read_set_rule(settings->above(), settings->aboverule());
    below = read_set_rule(settings->below(), settings->belowrule());
    desktops = read_set_rule(settings->desktops(), settings->desktopsrule());
    desktopfile = read_set_rule(settings->desktopfile(), settings->desktopfilerule());
    fullscreen = read_set_rule(settings->fullscreen(), settings->fullscreenrule());
    ignoregeometry = read_set_rule(settings->ignoregeometry(), settings->ignoregeometryrule());
    maximizehoriz = read_set_rule(settings->maximizehoriz(), settings->maximizehorizrule());
    maximizevert = read_set_rule(settings->maximizevert(), settings->maximizevertrule());
    minimize = read_set_rule(settings->minimize(), settings->minimizerule());
    noborder = read_set_rule(settings->noborder(), settings->noborderrule());
    position = read_set_rule(settings->position(), settings->positionrule());
    screen = read_set_rule(settings->screen(), settings->screenrule());
    shortcut = read_set_rule(settings->shortcut(), settings->shortcutrule());
    size = read_set_rule(settings->size(), settings->sizerule());
    if (size.data.isEmpty() && size.rule != static_cast<set_rule>(action::remember)) {
        size.rule = set_rule::unused;
    }

    skippager = read_set_rule(settings->skippager(), settings->skippagerrule());
    skipswitcher = read_set_rule(settings->skipswitcher(), settings->skipswitcherrule());
    skiptaskbar = read_set_rule(settings->skiptaskbar(), settings->skiptaskbarrule());

    auto read_force_rule = [](auto const& data, auto const& rule) {
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

    type = read_force_rule(static_cast<win_type>(settings->type()), settings->typerule());
    if (type.data == win_type::unknown) {
        type.rule = force_rule::unused;
    }
}

void ruling::write(rules::settings* settings) const
{
    auto write_string
        = [&settings](auto const& str, auto data_writer, auto match_writer, bool force = false) {
              std::invoke(match_writer, settings, enum_index(str.match));
              if (!str.data.isEmpty() || force) {
                  std::invoke(data_writer, settings, str.data);
              }
          };

    settings->setDescription(description);

    // Always write wmclass.
    write_string(wmclass, &settings::setWmclass, &settings::setWmclassmatch, true);
    settings->setWmclasscomplete(wmclasscomplete);
    write_string(windowrole, &settings::setWindowrole, &settings::setWindowrolematch);
    write_string(title, &settings::setTitle, &settings::setTitlematch);
    write_string(clientmachine, &settings::setClientmachine, &settings::setClientmachinematch);

    settings->setTypes(static_cast<unsigned int>(types));

    auto write_set = [&settings](auto const& ruler, auto rule_writer, auto data_writer) {
        std::invoke(rule_writer, settings, static_cast<int>(ruler.rule));
        if (ruler.rule != set_rule::unused) {
            std::invoke(data_writer, settings, ruler.data);
        }
    };

    write_set(above, &settings::setAboverule, &settings::setAbove);
    write_set(below, &settings::setBelowrule, &settings::setBelow);
    write_set(desktops, &settings::setDesktopsrule, &settings::setDesktops);
    write_set(desktopfile, &settings::setDesktopfilerule, &settings::setDesktopfile);
    write_set(fullscreen, &settings::setFullscreenrule, &settings::setFullscreen);
    write_set(ignoregeometry, &settings::setIgnoregeometryrule, &settings::setIgnoregeometry);
    write_set(maximizehoriz, &settings::setMaximizehorizrule, &settings::setMaximizehoriz);
    write_set(maximizevert, &settings::setMaximizevertrule, &settings::setMaximizevert);
    write_set(minimize, &settings::setMinimizerule, &settings::setMinimize);
    write_set(noborder, &settings::setNoborderrule, &settings::setNoborder);
    write_set(position, &settings::setPositionrule, &settings::setPosition);
    write_set(screen, &settings::setScreenrule, &settings::setScreen);
    write_set(shortcut, &settings::setShortcutrule, &settings::setShortcut);
    write_set(size, &settings::setSizerule, &settings::setSize);
    write_set(skippager, &settings::setSkippagerrule, &settings::setSkippager);
    write_set(skipswitcher, &settings::setSkipswitcherrule, &settings::setSkipswitcher);
    write_set(skiptaskbar, &settings::setSkiptaskbarrule, &settings::setSkiptaskbar);

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

    write_force(acceptfocus, &settings::setAcceptfocusrule, &settings::setAcceptfocus);
    write_force(autogroup, &settings::setAutogrouprule, &settings::setAutogroup);
    write_force(autogroupfg, &settings::setAutogroupfgrule, &settings::setAutogroupfg);
    write_force(autogroupid, &settings::setAutogroupidrule, &settings::setAutogroupid);
    write_force(
        blockcompositing, &settings::setBlockcompositingrule, &settings::setBlockcompositing);
    write_force(closeable, &settings::setCloseablerule, &settings::setCloseable);
    write_force(disableglobalshortcuts,
                &settings::setDisableglobalshortcutsrule,
                &settings::setDisableglobalshortcuts);
    write_force(fpplevel, &settings::setFpplevelrule, &settings::setFpplevel);
    write_force(fsplevel, &settings::setFsplevelrule, &settings::setFsplevel);

    auto colorToString = [](auto const& value) -> QString {
        if (value.endsWith(QLatin1String(".colors"))) {
            return QFileInfo(value).baseName();
        } else {
            return value;
        }
    };
    convert_write_force(
        decocolor, &settings::setDecocolorrule, &settings::setDecocolor, colorToString);

    write_force(maxsize, &settings::setMaxsizerule, &settings::setMaxsize);
    write_force(minsize, &settings::setMinsizerule, &settings::setMinsize);
    write_force(opacityactive, &settings::setOpacityactiverule, &settings::setOpacityactive);
    write_force(opacityinactive, &settings::setOpacityinactiverule, &settings::setOpacityinactive);
    write_force(placement, &settings::setPlacementrule, &settings::setPlacement);
    write_force(strictgeometry, &settings::setStrictgeometryrule, &settings::setStrictgeometry);
    convert_write_force(type,
                        &settings::setTyperule,
                        &settings::setType,
                        [](auto const& value) -> int { return static_cast<int>(value); });
}

// returns true if it doesn't affect anything
bool ruling::isEmpty() const
{
    auto unused_s = [](auto rule) { return rule == set_rule::unused; };
    auto unused_f = [](auto rule) { return rule == force_rule::unused; };

    return unused_s(position.rule) && unused_s(size.rule) && unused_s(desktopfile.rule)
        && unused_s(ignoregeometry.rule) && unused_s(desktops.rule) && unused_s(screen.rule)
        && unused_s(maximizevert.rule) && unused_s(maximizehoriz.rule) && unused_s(minimize.rule)
        && unused_s(skiptaskbar.rule) && unused_s(skippager.rule) && unused_s(skipswitcher.rule)
        && unused_s(above.rule) && unused_s(below.rule) && unused_s(fullscreen.rule)
        && unused_s(noborder.rule) && unused_f(decocolor.rule) && unused_f(blockcompositing.rule)
        && unused_f(fsplevel.rule) && unused_f(fpplevel.rule) && unused_f(acceptfocus.rule)
        && unused_f(closeable.rule) && unused_f(autogroup.rule) && unused_f(autogroupfg.rule)
        && unused_f(autogroupid.rule) && unused_f(strictgeometry.rule) && unused_s(shortcut.rule)
        && unused_f(disableglobalshortcuts.rule) && unused_f(minsize.rule) && unused_f(maxsize.rule)
        && unused_f(opacityactive.rule) && unused_f(opacityinactive.rule)
        && unused_f(placement.rule) && unused_f(type.rule);
}

force_rule ruling::convertForceRule(int v)
{
    if (v == enum_index(action::dont_affect) || v == enum_index(action::force)
        || v == enum_index(action::force_temporarily)) {
        return static_cast<force_rule>(v);
    }
    return force_rule::unused;
}

QString ruling::getDecoColor(QString const& themeName)
{
    if (themeName.isEmpty()) {
        return QString();
    }
    // find the actual scheme file
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                  QLatin1String("color-schemes/") + themeName
                                      + QLatin1String(".colors"));
}

bool ruling::matchType(win_type match_type) const
{
    if (types != window_type_mask::all_types) {
        if (match_type == win_type::unknown) {
            // win_type::Unknown->win_type::Normal is only here for matching
            match_type = win_type::normal;
        }
        if (!win::type_matches_mask(match_type, types)) {
            return false;
        }
    }
    return true;
}

bool ruling::matchWMClass(QByteArray const& match_class, QByteArray const& match_name) const
{
    if (wmclass.match != name_match::unimportant) {
        // TODO optimize?
        QByteArray cwmclass;
        if (wmclasscomplete) {
            cwmclass.append(match_name);
            cwmclass.append(' ');
        }
        cwmclass.append(match_class);

        if (wmclass.match == name_match::regex
            && !QRegularExpression(QString::fromUtf8(wmclass.data))
                    .match(QString::fromUtf8(cwmclass))
                    .hasMatch()) {
            return false;
        }
        if (wmclass.match == name_match::exact && wmclass.data != cwmclass)
            return false;
        if (wmclass.match == name_match::substring && !cwmclass.contains(wmclass.data))
            return false;
    }
    return true;
}

bool ruling::matchRole(QByteArray const& match_role) const
{
    if (windowrole.match != name_match::unimportant) {
        if (windowrole.match == name_match::regex
            && !QRegularExpression(QString::fromUtf8(windowrole.data))
                    .match(QString::fromUtf8(match_role))
                    .hasMatch()) {
            return false;
        }
        if (windowrole.match == name_match::exact && windowrole.data != match_role)
            return false;
        if (windowrole.match == name_match::substring && !match_role.contains(windowrole.data))
            return false;
    }
    return true;
}

bool ruling::matchTitle(QString const& match_title) const
{
    if (title.match != name_match::unimportant) {
        if (title.match == name_match::regex
            && !QRegularExpression(title.data).match(match_title).hasMatch()) {
            return false;
        }
        if (title.match == name_match::exact && title.data != match_title)
            return false;
        if (title.match == name_match::substring && !match_title.contains(title.data))
            return false;
    }
    return true;
}

bool ruling::matchClientMachine(QByteArray const& match_machine, bool local) const
{
    if (clientmachine.match != name_match::unimportant) {
        // if it's localhost, check also "localhost" before checking hostname
        if (match_machine != "localhost" && local && matchClientMachine("localhost", true))
            return true;
        if (clientmachine.match == name_match::regex
            && !QRegularExpression(QString::fromUtf8(clientmachine.data))
                    .match(QString::fromUtf8(match_machine))
                    .hasMatch()) {
            return false;
        }
        if (clientmachine.match == name_match::exact && clientmachine.data != match_machine)
            return false;
        if (clientmachine.match == name_match::substring
            && !match_machine.contains(clientmachine.data))
            return false;
    }
    return true;
}

bool ruling::checkSetRule(set_rule rule, bool init)
{
    if (rule > static_cast<set_rule>(action::dont_affect)) {
        // Unused or DontAffect
        if (rule == static_cast<set_rule>(action::force)
            || rule == static_cast<set_rule>(action::apply_now)
            || rule == static_cast<set_rule>(action::force_temporarily) || init) {
            return true;
        }
    }
    return false;
}

bool ruling::checkForceRule(force_rule rule)
{
    return rule == static_cast<force_rule>(action::force)
        || rule == static_cast<force_rule>(action::force_temporarily);
}

bool ruling::checkSetStop(set_rule rule)
{
    return rule != set_rule::unused;
}

bool ruling::checkForceStop(force_rule rule)
{
    return rule != force_rule::unused;
}

bool ruling::applyGeometry(QRect& rect, bool init) const
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

bool ruling::applyPosition(QPoint& pos, bool init) const
{
    if (this->position.data != geo::invalid_point && checkSetRule(position.rule, init)) {
        pos = this->position.data;
    }
    return checkSetStop(position.rule);
}

bool ruling::applySize(QSize& s, bool init) const
{
    if (this->size.data.isValid() && checkSetRule(size.rule, init)) {
        s = this->size.data;
    }
    return checkSetStop(size.rule);
}

bool ruling::applyMinimize(bool& minimize, bool init) const
{
    return apply_set(minimize, this->minimize, init);
}

bool ruling::applySkipTaskbar(bool& skip, bool init) const
{
    return apply_set(skip, this->skiptaskbar, init);
}

bool ruling::applySkipPager(bool& skip, bool init) const
{
    return apply_set(skip, this->skippager, init);
}

bool ruling::applySkipSwitcher(bool& skip, bool init) const
{
    return apply_set(skip, this->skipswitcher, init);
}

bool ruling::applyKeepAbove(bool& above, bool init) const
{
    return apply_set(above, this->above, init);
}

bool ruling::applyKeepBelow(bool& below, bool init) const
{
    return apply_set(below, this->below, init);
}

bool ruling::applyFullScreen(bool& fs, bool init) const
{
    return apply_set(fs, this->fullscreen, init);
}

bool ruling::applyScreen(int& screen, bool init) const
{
    return apply_set(screen, this->screen, init);
}

bool ruling::applyNoBorder(bool& noborder, bool init) const
{
    return apply_set(noborder, this->noborder, init);
}

bool ruling::applyShortcut(QString& shortcut, bool init) const
{
    return apply_set(shortcut, this->shortcut, init);
}

bool ruling::applyDesktopFile(QString& desktopFile, bool init) const
{
    return apply_set(desktopFile, this->desktopfile, init);
}

bool ruling::applyIgnoreGeometry(bool& ignore, bool init) const
{
    return apply_set(ignore, this->ignoregeometry, init);
}

bool ruling::applyPlacement(win::placement& placement) const
{
    auto setting = static_cast<int>(placement);
    if (!apply_force(setting, this->placement)) {
        return false;
    }

    if (setting < 0 || setting >= static_cast<int>(win::placement::count)) {
        // Loaded value is out of bounds.
        return false;
    }

    placement = static_cast<win::placement>(setting);
    return true;
}

bool ruling::applyMinSize(QSize& size) const
{
    return apply_force(size, this->minsize);
}

bool ruling::applyMaxSize(QSize& size) const
{
    return apply_force(size, this->maxsize);
}

bool ruling::applyOpacityActive(int& s) const
{
    return apply_force(s, this->opacityactive);
}

bool ruling::applyOpacityInactive(int& s) const
{
    return apply_force(s, this->opacityinactive);
}

bool ruling::applyType(win_type& type) const
{
    return apply_force(type, this->type);
}

bool ruling::applyDecoColor(QString& schemeFile) const
{
    return apply_force(schemeFile, this->decocolor);
}

bool ruling::applyBlockCompositing(bool& block) const
{
    return apply_force(block, this->blockcompositing);
}

template<typename T>
bool ruling::apply_force_enum(force_ruler<int> const& ruler, T& apply, T min, T max) const
{
    auto setting = static_cast<int>(apply);
    if (!apply_force(setting, ruler)) {
        return false;
    }

    // Note: this does include the max item, so doesn't work for enums with "count" as last
    // element.
    if (setting < enum_index(min) || setting > enum_index(max)) {
        // Loaded value is out of bounds.
        return false;
    }

    apply = static_cast<win::fsp_level>(setting);
    return true;
}

bool ruling::applyFSP(win::fsp_level& fsp) const
{
    return apply_force_enum(fsplevel, fsp, win::fsp_level::none, win::fsp_level::extreme);
}

bool ruling::applyFPP(win::fsp_level& fpp) const
{
    return apply_force_enum(fpplevel, fpp, win::fsp_level::none, win::fsp_level::extreme);
}

bool ruling::applyAcceptFocus(bool& focus) const
{
    return apply_force(focus, this->acceptfocus);
}

bool ruling::applyCloseable(bool& closeable) const
{
    return apply_force(closeable, this->closeable);
}

bool ruling::applyAutogrouping(bool& autogroup) const
{
    return apply_force(autogroup, this->autogroup);
}

bool ruling::applyAutogroupInForeground(bool& fg) const
{
    return apply_force(fg, this->autogroupfg);
}

bool ruling::applyAutogroupById(QString& id) const
{
    return apply_force(id, this->autogroupid);
}

bool ruling::applyStrictGeometry(bool& strict) const
{
    return apply_force(strict, this->strictgeometry);
}

bool ruling::applyDisableGlobalShortcuts(bool& disable) const
{
    return apply_force(disable, this->disableglobalshortcuts);
}

bool ruling::applyMaximizeHoriz(win::maximize_mode& mode, bool init) const
{
    if (checkSetRule(maximizehoriz.rule, init)) {
        if (maximizehoriz.data) {
            mode |= win::maximize_mode::horizontal;
        }
    }
    return checkSetStop(maximizehoriz.rule);
}

bool ruling::applyMaximizeVert(win::maximize_mode& mode, bool init) const
{
    if (checkSetRule(maximizevert.rule, init)) {
        if (maximizevert.data) {
            mode |= win::maximize_mode::vertical;
        }
    }
    return checkSetStop(maximizevert.rule);
}

bool ruling::discardUsed(bool withdrawn)
{
    bool changed = false;

    auto discard_used_set = [withdrawn, &changed](auto& ruler) {
        auto const apply_now = ruler.rule == static_cast<set_rule>(action::apply_now);
        auto const is_temp = ruler.rule == static_cast<set_rule>(action::force_temporarily);

        if (apply_now || (is_temp && withdrawn)) {
            ruler.rule = set_rule::unused;
            changed = true;
        }
    };

    discard_used_set(above);
    discard_used_set(below);
    discard_used_set(desktops);
    discard_used_set(desktopfile);
    discard_used_set(fullscreen);
    discard_used_set(ignoregeometry);
    discard_used_set(maximizehoriz);
    discard_used_set(maximizevert);
    discard_used_set(minimize);
    discard_used_set(noborder);
    discard_used_set(position);
    discard_used_set(screen);
    discard_used_set(shortcut);
    discard_used_set(size);
    discard_used_set(skippager);
    discard_used_set(skipswitcher);
    discard_used_set(skiptaskbar);

    auto discard_used_force = [withdrawn, &changed](auto& ruler) {
        auto const is_temp = ruler.rule == static_cast<force_rule>(action::force_temporarily);
        if (withdrawn && is_temp) {
            ruler.rule = force_rule::unused;
            changed = true;
        }
    };

    discard_used_force(acceptfocus);
    discard_used_force(autogroup);
    discard_used_force(autogroupfg);
    discard_used_force(autogroupid);
    discard_used_force(blockcompositing);
    discard_used_force(closeable);
    discard_used_force(decocolor);
    discard_used_force(disableglobalshortcuts);
    discard_used_force(fpplevel);
    discard_used_force(fsplevel);
    discard_used_force(maxsize);
    discard_used_force(minsize);
    discard_used_force(opacityactive);
    discard_used_force(opacityinactive);
    discard_used_force(placement);
    discard_used_force(strictgeometry);
    discard_used_force(type);

    return changed;
}

QDebug& operator<<(QDebug& stream, ruling const* r)
{
    return stream << "[" << r->description << ":" << r->wmclass.data << "]";
}

}
