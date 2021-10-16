/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "kwin_wayland_test.h"

#include "../../abstract_wayland_output.h"
#include "../../debug/wayland_console.h"
#include "../../effects.h"
#include "../../input/backend/wlroots/platform.h"
#include "../../input/wayland/cursor.h"
#include "../../input/wayland/redirect.h"
#include "../../platform.h"
#include "../../render/wayland/compositor.h"
#include "../../seat/backend/wlroots/session.h"
#include "../../wayland_server.h"
#include "../../workspace.h"
#include "../../xcbutils.h"
#include "../../xwl/xwayland.h"

#include <KCrash>
#include <KPluginMetaData>

#include <QAbstractEventDispatcher>
#include <QPluginLoader>
#include <QSocketNotifier>
#include <QStyle>
#include <QThread>
#include <QtConcurrentRun>

extern "C" {
#include <wlr/backend/headless.h>
}

#include <Wrapland/Server/display.h>

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

Q_IMPORT_PLUGIN(KWinIntegrationPlugin)
Q_IMPORT_PLUGIN(KGlobalAccelImpl)
Q_IMPORT_PLUGIN(KWindowSystemKWinPlugin)
Q_IMPORT_PLUGIN(KWinIdleTimePoller)

namespace KWin
{

void disable_dr_konqi()
{
    KCrash::setDrKonqiEnabled(false);
}
Q_CONSTRUCTOR_FUNCTION(disable_dr_konqi)

WaylandTestApplication::WaylandTestApplication(OperationMode mode,
                                               std::string const& socket_name,
                                               WaylandServer::InitializationFlags flags,
                                               int& argc,
                                               char** argv)
    : ApplicationWaylandAbstract(mode, argc, argv)
    , base{new platform_base::wlroots()}
{
    // TODO: add a test move to kglobalaccel instead?
    QFile{QStandardPaths::locate(QStandardPaths::ConfigLocation,
                                 QStringLiteral("kglobalshortcutsrc"))}
        .remove();

    QIcon::setThemeName(QStringLiteral("breeze"));

#ifdef KWIN_BUILD_ACTIVITIES
    setUseKActivities(false);
#endif

    qunsetenv("XKB_DEFAULT_RULES");
    qunsetenv("XKB_DEFAULT_MODEL");
    qunsetenv("XKB_DEFAULT_LAYOUT");
    qunsetenv("XKB_DEFAULT_VARIANT");
    qunsetenv("XKB_DEFAULT_OPTIONS");

    const auto ownPath = libraryPaths().last();
    removeLibraryPath(ownPath);
    addLibraryPath(ownPath);

    server.reset(new WaylandServer(socket_name, flags));

    render.reset(new render::backend::wlroots::backend(base.get(), this));
    platform = render.get();

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("WAYLAND_DISPLAY"), socket_name.c_str());
    setProcessStartupEnvironment(environment);
}

WaylandTestApplication::~WaylandTestApplication()
{
    setTerminating();

    // need to unload all effects prior to destroying X connection as they might do X calls
    // also before destroy Workspace, as effects might call into Workspace
    if (effects) {
        static_cast<EffectsHandlerImpl*>(effects)->unloadAllEffects();
    }

    // Kill Xwayland before terminating its connection.
    xwayland.reset();

    if (QStyle* s = style()) {
        // Unpolish style before terminating internal connection.
        s->unpolish(this);
    }

    waylandServer()->terminateClientConnections();

    // Block compositor to prevent further compositing from crashing with a null workspace.
    // TODO(romangg): Instead we should kill the compositor before that or remove all outputs.
    static_cast<render::wayland::compositor*>(compositor.get())->lock();

    workspace.reset();
    compositor.reset();
}

debug::console* WaylandTestApplication::create_debug_console()
{
    return new debug::wayland_console;
}

void WaylandTestApplication::start()
{
    prepare_start();

    auto headless_backend = wlr_headless_backend_create(waylandServer()->display()->native());
    wlr_headless_add_output(headless_backend, 1280, 1024);
    base->init(headless_backend);
    input.reset(new input::backend::wlroots::platform(base.get()));
    input::add_dbus(input.get());

    createOptions();

    session.reset(new seat::backend::wlroots::session(headless_backend));
    input::add_redirect(input.get(), std::make_unique<input::wayland::redirect>());
    input->cursor.reset(new input::wayland::cursor);
    input->redirect->set_platform(input.get());

    keyboard = wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_KEYBOARD);
    pointer = wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_POINTER);
    touch = wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_TOUCH);

    try {
        render->init();
    } catch (std::exception const&) {
        std::cerr << "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
        ::exit(1);
    }

    // Must set physical size for calculation of screen edges corner offset.
    // TODO(romangg): Make the corner offset calculation not depend on that.
    auto out = dynamic_cast<AbstractWaylandOutput*>(kwinApp()->platform->enabledOutputs().at(0));
    out->output()->set_physical_size(QSize(1280, 1024));

    compositor = std::make_unique<render::wayland::compositor>();
    workspace = std::make_unique<Workspace>();
    Q_EMIT workspaceCreated();

    waylandServer()->create_addons([this] { handle_server_addons_created(); });
}

void WaylandTestApplication::handle_server_addons_created()
{
    if (operationMode() == OperationModeXwayland) {
        create_xwayland();
        return;
    }

    Q_EMIT startup_finished();
}

void WaylandTestApplication::create_xwayland()
{
    auto status_callback = [this](auto error) {
        if (error) {
            // we currently exit on Xwayland errors always directly
            // TODO: restart Xwayland
            std::cerr << "Xwayland had a critical error. Going to exit now." << std::endl;
            exit(error);
        }
        Q_EMIT startup_finished();
    };

    try {
        xwayland.reset(new xwl::xwayland(this, status_callback));
    } catch (std::system_error const& exc) {
        std::cerr << "FATAL ERROR creating Xwayland: " << exc.what() << std::endl;
        exit(exc.code().value());
    } catch (std::exception const& exc) {
        std::cerr << "FATAL ERROR creating Xwayland: " << exc.what() << std::endl;
        exit(1);
    }
}

}
