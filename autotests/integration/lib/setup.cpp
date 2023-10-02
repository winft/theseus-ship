/*´
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "setup.h"

#include "base/config.h"
#include "base/seat/backend/wlroots/session.h"
#include "input/backend/wlroots/platform.h"
#include "render/backend/wlroots/platform.h"
#include "render/shortcuts_init.h"
#include "win/shortcuts_init.h"

extern "C" {
#include <wlr/backend/headless.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_touch.h>
}

#include <iostream>

namespace KWin::detail::test
{

static setup* current_setup{nullptr};

setup::setup(std::string const& test_name)
    : setup(test_name, base::operation_mode::wayland, base::wayland::start_options::none)
{
}

setup::setup(std::string const& test_name, base::operation_mode mode)
    : setup(test_name, mode, base::wayland::start_options::none)
{
}

setup::setup(std::string const& test_name,
             base::operation_mode mode,
             base::wayland::start_options flags)
{
    current_setup = this;

    auto const socket_name = create_socket_name(test_name);

    auto rm_config = [](auto name) {
        auto const path = QStandardPaths::locate(QStandardPaths::ConfigLocation, name);
        if (!path.isEmpty()) {
            QFile{path}.remove();
        }
    };

    rm_config("kcminputrc");
    rm_config("kxkbrc");
    rm_config("kglobalshortcutsrc");

    QIcon::setThemeName(QStringLiteral("breeze"));

    qunsetenv("XKB_DEFAULT_RULES");
    qunsetenv("XKB_DEFAULT_MODEL");
    qunsetenv("XKB_DEFAULT_LAYOUT");
    qunsetenv("XKB_DEFAULT_VARIANT");
    qunsetenv("XKB_DEFAULT_OPTIONS");

    base = std::make_unique<base_t>(base::config(KConfig::OpenFlag::SimpleConfig, ""),
                                    socket_name,
                                    flags,
                                    base::backend::wlroots::start_options::headless);
    base->operation_mode = mode;

    auto headless_backend = base::backend::wlroots::get_headless_backend(base->backend);
    auto out = wlr_headless_add_output(headless_backend, 1280, 1024);
    wlr_output_enable(out, true);

    base->session = std::make_unique<base::seat::backend::wlroots::session>(base->wlroots_session,
                                                                            headless_backend);
    base->render = std::make_unique<render::backend::wlroots::platform<base_t>>(*base);

    base->process_environment.insert(QStringLiteral("WAYLAND_DISPLAY"), socket_name.c_str());
    prepare_sys_env(socket_name);
}

setup::~setup()
{
    assert(keyboard);
    assert(pointer);
    assert(touch);
    wlr_keyboard_finish(keyboard);
    wlr_pointer_finish(pointer);
    wlr_touch_finish(touch);

    // TODO(romangg): can this be done in the end?
    clients.clear();

    // need to unload all effects prior to destroying X connection as they might do X calls
    // also before destroy Workspace, as effects might call into Workspace
    if (effects) {
        base->render->compositor->effects->unloadAllEffects();
    }

    // Kill Xwayland before terminating its connection.
    base->xwayland.reset();
    base->server->terminateClientConnections();

    // Block compositor to prevent further compositing from crashing with a null workspace.
    // TODO(romangg): Instead we should kill the compositor before that or remove all outputs.
    base->render->compositor->lock();

    base->space.reset();
    base->render->compositor.reset();

    current_setup = nullptr;
}

void setup::start()
{
    base->options = base::create_options(base->operation_mode, base->config.main);
    base->input = std::make_unique<input::backend::wlroots::platform<base_t>>(
        *base, base->backend, input::config(KConfig::SimpleConfig));
    base->input->install_shortcuts();

    keyboard = static_cast<wlr_keyboard*>(calloc(1, sizeof(wlr_keyboard)));
    pointer = static_cast<wlr_pointer*>(calloc(1, sizeof(wlr_pointer)));
    touch = static_cast<wlr_touch*>(calloc(1, sizeof(wlr_touch)));
    assert(keyboard);
    assert(pointer);
    assert(touch);

    try {
        static_cast<render::backend::wlroots::platform<base_t>&>(*base->render).init();
    } catch (std::exception const&) {
        std::cerr << "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
        return;
    }

    wlr_keyboard_init(keyboard, nullptr, "headless-keyboard");
    wlr_pointer_init(pointer, nullptr, "headless-pointer");
    wlr_touch_init(touch, nullptr, "headless-touch");

    wlr_signal_emit_safe(&base->backend->events.new_input, keyboard);
    wlr_signal_emit_safe(&base->backend->events.new_input, pointer);
    wlr_signal_emit_safe(&base->backend->events.new_input, touch);

    // Must set physical size for calculation of screen edges corner offset.
    // TODO(romangg): Make the corner offset calculation not depend on that.
    auto out = base->outputs.at(0);
    auto metadata = out->wrapland_output()->get_metadata();
    metadata.physical_size = {1280, 1024};
    out->wrapland_output()->set_metadata(metadata);

    try {
        base->render->compositor = std::make_unique<base_t::render_t::compositor_t>(*base->render);
    } catch (std::system_error const& exc) {
        std::cerr << "FATAL ERROR: compositor creation failed: " << exc.what() << std::endl;
        return;
    }

    base->space = std::make_unique<base_t::space_t>(*base->render, *base->input);
    input::wayland::add_dbus(base->input.get());
    win::init_shortcuts(*base->space);
    render::init_shortcuts(*base->render);
    base->script = std::make_unique<scripting::platform<base_t::space_t>>(*base->space);

    base->render->compositor->start(*base->space);
    base->server->create_addons([this] { handle_server_addons_created(); });

    TRY_REQUIRE_WITH_TIMEOUT(ready, 10000);
}

void setup::set_outputs(size_t count)
{
    auto outputs = std::vector<output>();
    auto const size = QSize(1280, 1024);
    auto width = 0;

    for (size_t i = 0; i < count; i++) {
        auto const out = output({QPoint(width, 0), size});
        outputs.push_back(out);
        width += size.width();
    }

    set_outputs(outputs);
}

void setup::set_outputs(std::vector<QRect> const& geometries)
{
    auto outputs = std::vector<output>();
    for (auto&& geo : geometries) {
        auto const out = output(geo);
        outputs.push_back(out);
    }
    set_outputs(outputs);
}

void setup::set_outputs(std::vector<output> const& outputs)
{
    auto outputs_copy = base->all_outputs;
    for (auto output : outputs_copy) {
        delete output;
    }

    for (auto&& output : outputs) {
        auto const size = output.geometry.size() * output.scale;

        auto out = wlr_headless_add_output(base->backend, size.width(), size.height());
        wlr_output_enable(out, true);
        base->all_outputs.back()->force_geometry(output.geometry);
    }

    base::update_output_topology(*base);
}

void setup::add_client(global_selection globals)
{
    clients.emplace_back(globals);
}

void setup::handle_server_addons_created()
{
    if (base->operation_mode == base::operation_mode::xwayland) {
        create_xwayland();
        return;
    }

    ready = true;
}

void setup::create_xwayland()
{
    auto status_callback = [this](auto error) {
        if (error) {
            std::cerr << "Xwayland had a critical error. Going to exit now." << std::endl;
        }
        ready = !error;
    };

    try {
        base->xwayland
            = std::make_unique<xwl::xwayland<base_t::space_t>>(*base->space, status_callback);
    } catch (std::system_error const& exc) {
        std::cerr << "System error creating Xwayland: " << exc.what() << std::endl;
    } catch (std::exception const& exc) {
        std::cerr << "Exception creating Xwayland: " << exc.what() << std::endl;
    }
}

setup* app()
{
    return current_setup;
}

}
