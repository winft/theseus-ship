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
#include "../../composite.h"
#include "../../effects.h"
#include "../../platform.h"
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

WaylandTestApplication::WaylandTestApplication(OperationMode mode, int& argc, char** argv)
    : ApplicationWaylandAbstract(mode, argc, argv)
    , base{new platform_base::wlroots()}
{
    QStandardPaths::setTestModeEnabled(true);

    // TODO: add a test move to kglobalaccel instead?
    QFile{QStandardPaths::locate(QStandardPaths::ConfigLocation,
                                 QStringLiteral("kglobalshortcutsrc"))}
        .remove();

    QIcon::setThemeName(QStringLiteral("breeze"));

#ifdef KWIN_BUILD_ACTIVITIES
    setUseKActivities(false);
#endif

    qputenv("KWIN_WLR_OUTPUT_ALIGN_HORIZONTAL", QByteArrayLiteral("0"));
    qputenv("XDG_CURRENT_DESKTOP", QByteArrayLiteral("KDE"));
    qunsetenv("XKB_DEFAULT_RULES");
    qunsetenv("XKB_DEFAULT_MODEL");
    qunsetenv("XKB_DEFAULT_LAYOUT");
    qunsetenv("XKB_DEFAULT_VARIANT");
    qunsetenv("XKB_DEFAULT_OPTIONS");

    const auto ownPath = libraryPaths().last();
    removeLibraryPath(ownPath);
    addLibraryPath(ownPath);

    WaylandServer::create(this);
    init_wlroots_backend();

    setProcessStartupEnvironment(QProcessEnvironment::systemEnvironment());
}

WaylandTestApplication::~WaylandTestApplication()
{
    setTerminating();
    kwinApp()->platform()->setOutputsOn(false);

    // need to unload all effects prior to destroying X connection as they might do X calls
    // also before destroy Workspace, as effects might call into Workspace
    if (effects) {
        static_cast<EffectsHandlerImpl*>(effects)->unloadAllEffects();
    }

    if (m_xwayland) {
        // needs to be done before workspace gets destroyed
        m_xwayland->prepareDestroy();
    }

    destroyWorkspace();
    waylandServer()->dispatch();

    if (QStyle* s = style()) {
        s->unpolish(this);
    }

    // kill Xwayland before terminating its connection
    delete m_xwayland;
    waylandServer()->terminateClientConnections();
    destroyCompositor();
}

void WaylandTestApplication::init_wlroots_backend()
{
    render.reset(new render::backend::wlroots::backend(base.get(), this));
    set_platform(render.get());
}

void WaylandTestApplication::performStartup()
{
    auto headless_backend = wlr_headless_backend_create(waylandServer()->display()->native());
    wlr_headless_add_output(headless_backend, 1280, 1024);
    base->init(headless_backend);
    input.reset(new input::backend::wlroots::platform(base.get()));

    // first load options - done internally by a different thread
    createOptions();
    waylandServer()->createInternalConnection();

    // try creating the Wayland Backend
    createInput();
    input_redirect()->set_platform(input.get());

    keyboard = wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_KEYBOARD);
    pointer = wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_POINTER);
    wlr_headless_add_input_device(headless_backend, WLR_INPUT_DEVICE_TOUCH);

    createBackend();

    // Must set physical size for calculation of screen edges corner offset.
    // TODO(romangg): Make the corner offset calculation not depend on that.
    auto out = dynamic_cast<AbstractWaylandOutput*>(kwinApp()->platform()->enabledOutputs().at(0));
    out->output()->set_physical_size(QSize(1280, 1024));
}

void WaylandTestApplication::createBackend()
{
    auto platform = kwinApp()->platform();
    connect(platform, &Platform::initFailed, this, []() {
        std::cerr << "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
        ::exit(1);
    });
    platform->init();
}

void WaylandTestApplication::continueStartupWithCompositor()
{
    WaylandCompositor::create();
    connect(Compositor::self(),
            &Compositor::sceneCreated,
            this,
            &WaylandTestApplication::continueStartupWithScene);
}

void WaylandTestApplication::finalizeStartup()
{
    if (m_xwayland) {
        disconnect(m_xwayland,
                   &Xwl::Xwayland::initialized,
                   this,
                   &WaylandTestApplication::finalizeStartup);
    }
    createWorkspace();
}

void WaylandTestApplication::continueStartupWithScene()
{
    disconnect(Compositor::self(),
               &Compositor::sceneCreated,
               this,
               &WaylandTestApplication::continueStartupWithScene);

    if (operationMode() == OperationModeWaylandOnly) {
        finalizeStartup();
        return;
    }

    m_xwayland = new Xwl::Xwayland(this);
    connect(m_xwayland, &Xwl::Xwayland::criticalError, this, [](int code) {
        // we currently exit on Xwayland errors always directly
        // TODO: restart Xwayland
        std::cerr << "Xwayland had a critical error. Going to exit now." << std::endl;
        exit(code);
    });
    connect(
        m_xwayland, &Xwl::Xwayland::initialized, this, &WaylandTestApplication::finalizeStartup);
    m_xwayland->init();
}

}
