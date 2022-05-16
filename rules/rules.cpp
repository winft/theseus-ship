/*
    SPDX-FileCopyrightText: 2004 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "rules.h"

#include "base/output.h"
#include "base/output_helpers.h"
#include "base/platform.h"
#include "main.h"
#include "utils/geo.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <kconfig.h>

#ifndef KCMRULES
#include "win/setup.h"
#include "win/space.h"
#include "win/types.h"
#include "win/x11/client_machine.h"
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

    auto read_set_rule = [](auto const& data, auto const& rule) {
        set_ruler<std::decay_t<decltype(data)>> set;
        set.data = data;
        set.rule = static_cast<set_rule>(rule);
        return set;
    };

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
    shortcut = read_set_rule(settings->shortcut(), settings->shortcutrule());
    size = read_set_rule(settings->size(), settings->sizerule());
    if (size.data.isEmpty() && size.rule != static_cast<set_rule>(Remember)) {
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
        QByteArray cwmclass;
        if (wmclasscomplete) {
            cwmclass.append(match_name);
            cwmclass.append(' ');
        }
        cwmclass.append(match_class);

        if (wmclass.match == RegExpMatch
            && !QRegularExpression(QString::fromUtf8(wmclass.data))
                    .match(QString::fromUtf8(cwmclass))
                    .hasMatch()) {
            return false;
        }
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
            && !QRegularExpression(QString::fromUtf8(windowrole.data))
                    .match(QString::fromUtf8(match_role))
                    .hasMatch()) {
            return false;
        }
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
        if (title.match == RegExpMatch
            && !QRegularExpression(title.data).match(match_title).hasMatch()) {
            return false;
        }
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
            && !QRegularExpression(QString::fromUtf8(clientmachine.data))
                    .match(QString::fromUtf8(match_machine))
                    .hasMatch()) {
            return false;
        }
        if (clientmachine.match == ExactMatch && clientmachine.data != match_machine)
            return false;
        if (clientmachine.match == SubstringMatch && !match_machine.contains(clientmachine.data))
            return false;
    }
    return true;
}

#ifndef KCMRULES
bool Rules::match(Toplevel const* window) const
{
    if (!matchType(window->windowType(true))) {
        return false;
    }
    if (!matchWMClass(window->resourceClass(), window->resourceName())) {
        return false;
    }
    if (!matchRole(window->windowRole().toLower())) {
        return false;
    }
    if (!matchClientMachine(window->clientMachine()->hostname(),
                            window->clientMachine()->is_local())) {
        return false;
    }

    if (title.match != UnimportantMatch) {
        // Track title changes to rematch rules.
        auto mutable_client = const_cast<Toplevel*>(window);
        QObject::connect(
            mutable_client,
            &Toplevel::captionChanged,
            mutable_client,
            [mutable_client] { win::evaluate_rules(mutable_client); },
            // QueuedConnection, because title may change before
            // the client is ready (could segfault!)
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    }
    if (!matchTitle(window->caption.normal))
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

bool Rules::update(Toplevel* window, int selection)
{
    // TODO check this setting is for this client ?
    bool updated = false;

    auto remember = [selection](auto const& ruler, auto type) {
        return (selection & type) && ruler.rule == static_cast<set_rule>(Remember);
    };

    if (remember(above, Above)) {
        updated = updated || above.data != window->control->keep_above();
        above.data = window->control->keep_above();
    }
    if (remember(below, Below)) {
        updated = updated || below.data != window->control->keep_below();
        below.data = window->control->keep_below();
    }
    if (remember(desktop, Desktop)) {
        updated = updated || desktop.data != window->desktop();
        desktop.data = window->desktop();
    }
    if (remember(desktopfile, DesktopFile)) {
        auto const name = window->control->desktop_file_name();
        updated = updated || desktopfile.data != name;
        desktopfile.data = name;
    }
    if (remember(fullscreen, Fullscreen)) {
        updated = updated || fullscreen.data != window->control->fullscreen();
        fullscreen.data = window->control->fullscreen();
    }

    if (remember(maximizehoriz, MaximizeHoriz)) {
        updated = updated
            || maximizehoriz.data != flags(window->maximizeMode() & win::maximize_mode::horizontal);
        maximizehoriz.data = flags(window->maximizeMode() & win::maximize_mode::horizontal);
    }
    if (remember(maximizevert, MaximizeVert)) {
        updated = updated
            || maximizevert.data != bool(window->maximizeMode() & win::maximize_mode::vertical);
        maximizevert.data = flags(window->maximizeMode() & win::maximize_mode::vertical);
    }
    if (remember(minimize, Minimize)) {
        updated = updated || minimize.data != window->control->minimized();
        minimize.data = window->control->minimized();
    }
    if (remember(noborder, NoBorder)) {
        updated = updated || noborder.data != window->noBorder();
        noborder.data = window->noBorder();
    }

    if (remember(position, Position)) {
        if (!window->control->fullscreen()) {
            QPoint new_pos = position.data;

            // Don't use the position in the direction which is maximized.
            if (!flags(window->maximizeMode() & win::maximize_mode::horizontal)) {
                new_pos.setX(window->pos().x());
            }
            if (!flags(window->maximizeMode() & win::maximize_mode::vertical)) {
                new_pos.setY(window->pos().y());
            }
            updated = updated || position.data != new_pos;
            position.data = new_pos;
        }
    }

    if (remember(screen, Screen)) {
        int output_index = window->central_output
            ? base::get_output_index(kwinApp()->get_base().get_outputs(), *window->central_output)
            : 0;
        updated = updated || screen.data != output_index;
        screen.data = output_index;
    }
    if (remember(size, Size)) {
        if (!window->control->fullscreen()) {
            QSize new_size = size.data;
            // don't use the position in the direction which is maximized
            if (!flags(window->maximizeMode() & win::maximize_mode::horizontal))
                new_size.setWidth(window->size().width());
            if (!flags(window->maximizeMode() & win::maximize_mode::vertical))
                new_size.setHeight(window->size().height());
            updated = updated || size.data != new_size;
            size.data = new_size;
        }
    }
    if (remember(skippager, SkipPager)) {
        updated = updated || skippager.data != window->control->skip_pager();
        skippager.data = window->control->skip_pager();
    }
    if (remember(skipswitcher, SkipSwitcher)) {
        updated = updated || skipswitcher.data != window->control->skip_switcher();
        skipswitcher.data = window->control->skip_switcher();
    }
    if (remember(skiptaskbar, SkipTaskbar)) {
        updated = updated || skiptaskbar.data != window->control->skip_taskbar();
        skiptaskbar.data = window->control->skip_taskbar();
    }

    return updated;
}

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
    if (this->position.data != geo::invalid_point && checkSetRule(position.rule, init)) {
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

bool Rules::applyMinimize(bool& minimize, bool init) const
{
    return apply_set(minimize, this->minimize, init);
}

bool Rules::applySkipTaskbar(bool& skip, bool init) const
{
    return apply_set(skip, this->skiptaskbar, init);
}

bool Rules::applySkipPager(bool& skip, bool init) const
{
    return apply_set(skip, this->skippager, init);
}

bool Rules::applySkipSwitcher(bool& skip, bool init) const
{
    return apply_set(skip, this->skipswitcher, init);
}

bool Rules::applyKeepAbove(bool& above, bool init) const
{
    return apply_set(above, this->above, init);
}

bool Rules::applyKeepBelow(bool& below, bool init) const
{
    return apply_set(below, this->below, init);
}

bool Rules::applyFullScreen(bool& fs, bool init) const
{
    return apply_set(fs, this->fullscreen, init);
}

bool Rules::applyDesktop(int& desktop, bool init) const
{
    return apply_set(desktop, this->desktop, init);
}

bool Rules::applyScreen(int& screen, bool init) const
{
    return apply_set(screen, this->screen, init);
}

bool Rules::applyNoBorder(bool& noborder, bool init) const
{
    return apply_set(noborder, this->noborder, init);
}

bool Rules::applyShortcut(QString& shortcut, bool init) const
{
    return apply_set(shortcut, this->shortcut, init);
}

bool Rules::applyDesktopFile(QString& desktopFile, bool init) const
{
    return apply_set(desktopFile, this->desktopfile, init);
}

bool Rules::applyIgnoreGeometry(bool& ignore, bool init) const
{
    return apply_set(ignore, this->ignoregeometry, init);
}

bool Rules::applyPlacement(win::placement& placement) const
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

bool Rules::applyMinSize(QSize& size) const
{
    return apply_force(size, this->minsize);
}

bool Rules::applyMaxSize(QSize& size) const
{
    return apply_force(size, this->maxsize);
}

bool Rules::applyOpacityActive(int& s) const
{
    return apply_force(s, this->opacityactive);
}

bool Rules::applyOpacityInactive(int& s) const
{
    return apply_force(s, this->opacityinactive);
}

bool Rules::applyType(NET::WindowType& type) const
{
    return apply_force(type, this->type);
}

bool Rules::applyDecoColor(QString& schemeFile) const
{
    return apply_force(schemeFile, this->decocolor);
}

bool Rules::applyBlockCompositing(bool& block) const
{
    return apply_force(block, this->blockcompositing);
}

bool Rules::applyFSP(int& fsp) const
{
    return apply_force(fsp, this->fsplevel);
}

bool Rules::applyFPP(int& fpp) const
{
    return apply_force(fpp, this->fpplevel);
}

bool Rules::applyAcceptFocus(bool& focus) const
{
    return apply_force(focus, this->acceptfocus);
}

bool Rules::applyCloseable(bool& closeable) const
{
    return apply_force(closeable, this->closeable);
}

bool Rules::applyAutogrouping(bool& autogroup) const
{
    return apply_force(autogroup, this->autogroup);
}

bool Rules::applyAutogroupInForeground(bool& fg) const
{
    return apply_force(fg, this->autogroupfg);
}

bool Rules::applyAutogroupById(QString& id) const
{
    return apply_force(id, this->autogroupid);
}

bool Rules::applyStrictGeometry(bool& strict) const
{
    return apply_force(strict, this->strictgeometry);
}

bool Rules::applyDisableGlobalShortcuts(bool& disable) const
{
    return apply_force(disable, this->disableglobalshortcuts);
}

bool Rules::applyMaximizeHoriz(win::maximize_mode& mode, bool init) const
{
    if (checkSetRule(maximizehoriz.rule, init)) {
        if (maximizehoriz.data) {
            mode |= win::maximize_mode::horizontal;
        }
    }
    return checkSetStop(maximizehoriz.rule);
}

bool Rules::applyMaximizeVert(win::maximize_mode& mode, bool init) const
{
    if (checkSetRule(maximizevert.rule, init)) {
        if (maximizevert.data) {
            mode |= win::maximize_mode::vertical;
        }
    }
    return checkSetStop(maximizevert.rule);
}

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

bool Rules::discardUsed(bool withdrawn)
{
    bool changed = false;

    auto discard_used_set = [withdrawn, &changed](auto& ruler) {
        auto const apply_now = ruler.rule == static_cast<set_rule>(ApplyNow);
        auto const is_temp = ruler.rule == (set_rule)ForceTemporarily;

        if (apply_now || (is_temp && withdrawn)) {
            ruler.rule = set_rule::unused;
            changed = true;
        }
    };

    discard_used_set(above);
    discard_used_set(below);
    discard_used_set(desktop);
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
        auto const is_temp = ruler.rule == (force_rule)ForceTemporarily;
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

#endif

QDebug& operator<<(QDebug& stream, const Rules* r)
{
    return stream << "[" << r->description << ":" << r->wmclass.data << "]";
}

}
