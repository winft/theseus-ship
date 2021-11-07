/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "xkb.h"

#include "utils.h"

#include <KConfigGroup>

#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

#include <QTemporaryFile>
#include <QtXkbCommonSupport/private/qxkbcommon_p.h>

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <bitset>
#include <sstream>
#include <sys/mman.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(KWIN_XKB, "kwin_xkbcommon", QtWarningMsg)

namespace KWin::input
{

static void xkbLogHandler([[maybe_unused]] xkb_context* context,
                          xkb_log_level priority,
                          char const* format,
                          va_list args)
{
    char buf[1024];
    int length = std::vsnprintf(buf, 1023, format, args);

    while (length > 0 && std::isspace(buf[length - 1])) {
        --length;
    }

    if (length <= 0) {
        return;
    }

    switch (priority) {
    case XKB_LOG_LEVEL_DEBUG:
        qCDebug(KWIN_XKB, "XKB: %.*s", length, buf);
        break;
    case XKB_LOG_LEVEL_INFO:
        qCInfo(KWIN_XKB, "XKB: %.*s", length, buf);
        break;
    case XKB_LOG_LEVEL_WARNING:
        qCWarning(KWIN_XKB, "XKB: %.*s", length, buf);
        break;
    case XKB_LOG_LEVEL_ERROR:
    case XKB_LOG_LEVEL_CRITICAL:
    default:
        qCCritical(KWIN_XKB, "XKB: %.*s", length, buf);
        break;
    }
}

xkb::xkb()
    : context(xkb_context_new(XKB_CONTEXT_NO_FLAGS))
{
    qRegisterMetaType<KWin::input::keyboard_leds>();

    if (!context) {
        // TODO(romangg): throw instead
        qCCritical(KWIN_XKB) << "Could not create xkb context";
        QCoreApplication::exit(1);
    }
    xkb_context_set_log_level(context, XKB_LOG_LEVEL_DEBUG);
    xkb_context_set_log_fn(context, &xkbLogHandler);

    // Get locale as described in xkbcommon doc, cannot use QLocale as it drops the modifier part.
    QByteArray locale = qgetenv("LC_ALL");
    if (locale.isEmpty()) {
        locale = qgetenv("LC_CTYPE");
    }
    if (locale.isEmpty()) {
        locale = qgetenv("LANG");
    }
    if (locale.isEmpty()) {
        locale = QByteArrayLiteral("C");
    }

    compose.table = xkb_compose_table_new_from_locale(
        context, locale.constData(), XKB_COMPOSE_COMPILE_NO_FLAGS);

    if (compose.table) {
        compose.state = xkb_compose_state_new(compose.table, XKB_COMPOSE_STATE_NO_FLAGS);
    }
}

xkb::~xkb()
{
    xkb_compose_state_unref(compose.state);
    xkb_compose_table_unref(compose.table);
    xkb_state_unref(m_state);
    xkb_keymap_unref(m_keymap);
    xkb_context_unref(context);
}

void xkb::setConfig(const KSharedConfigPtr& config)
{
    m_configGroup = config->group("Layout");
}

void xkb::setNumLockConfig(const KSharedConfigPtr& config)
{
    m_numLockConfig = config;
}

void xkb::reconfigure()
{
    xkb_keymap* keymap{nullptr};
    if (!qEnvironmentVariableIsSet("KWIN_XKB_DEFAULT_KEYMAP")) {
        keymap = loadKeymapFromConfig();
    }
    if (!keymap) {
        qCDebug(KWIN_XKB) << "Could not create xkb keymap from configuration";
        keymap = loadDefaultKeymap();
    }
    if (keymap) {
        updateKeymap(keymap);
    } else {
        qCDebug(KWIN_XKB) << "Could not create default xkb keymap";
    }
}

static bool stringIsEmptyOrNull(const char* str)
{
    return str == nullptr || str[0] == '\0';
}

/**
 * libxkbcommon uses secure_getenv to read the XKB_DEFAULT_* variables.
 * As kwin_wayland may have the CAP_SET_NICE capability, it returns nullptr
 * so we need to do it ourselves (see xkb_context_sanitize_rule_names).
 **/
void xkb::apply_environment_rules(xkb_rule_names& ruleNames,
                                  std::vector<std::string>& layouts) const
{
    if (stringIsEmptyOrNull(ruleNames.rules)) {
        ruleNames.rules = getenv("XKB_DEFAULT_RULES");
    }

    if (stringIsEmptyOrNull(ruleNames.model)) {
        ruleNames.model = getenv("XKB_DEFAULT_MODEL");
    }

    if (stringIsEmptyOrNull(ruleNames.layout)) {
        ruleNames.layout = getenv("XKB_DEFAULT_LAYOUT");
        ruleNames.variant = getenv("XKB_DEFAULT_VARIANT");
    }

    if (ruleNames.options == nullptr) {
        ruleNames.options = getenv("XKB_DEFAULT_OPTIONS");
    }

    layouts.clear();

    if (ruleNames.layout) {
        auto layout_stream = std::stringstream(ruleNames.layout);
        while (layout_stream.good()) {
            std::string layout;
            getline(layout_stream, layout, ',');
            layouts.push_back(layout);
        }
    }
}

xkb_keymap* xkb::loadKeymapFromConfig()
{
    // load config
    if (!m_configGroup.isValid()) {
        return nullptr;
    }

    QByteArray const model = m_configGroup.readEntry("Model", "pc104").toLatin1();
    QByteArray const layout = m_configGroup.readEntry("LayoutList").toLatin1();
    QByteArray const variant = m_configGroup.readEntry("VariantList").toLatin1();
    QByteArray const options = m_configGroup.readEntry("Options").toLatin1();

    xkb_rule_names ruleNames = {.rules = nullptr,
                                .model = model.constData(),
                                .layout = layout.constData(),
                                .variant = variant.constData(),
                                .options = options.constData()};

    apply_environment_rules(ruleNames, m_layoutList);

    return xkb_keymap_new_from_names(context, &ruleNames, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

xkb_keymap* xkb::loadDefaultKeymap()
{
    xkb_rule_names ruleNames = {};
    apply_environment_rules(ruleNames, m_layoutList);
    return xkb_keymap_new_from_names(context, &ruleNames, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

void xkb::installKeymap(int fd, uint32_t size)
{
    char* map = reinterpret_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
    if (map == MAP_FAILED) {
        return;
    }
    xkb_keymap* keymap = xkb_keymap_new_from_string(
        context, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_MAP_COMPILE_PLACEHOLDER);
    munmap(map, size);
    if (!keymap) {
        qCDebug(KWIN_XKB) << "Could not map keymap from file";
        return;
    }
    m_ownership = Ownership::Client;
    updateKeymap(keymap);
}

void xkb::updateKeymap(xkb_keymap* keymap)
{
    Q_ASSERT(keymap);

    auto state = xkb_state_new(keymap);
    if (!state) {
        qCDebug(KWIN_XKB) << "Could not create XKB state";
        xkb_keymap_unref(keymap);
        return;
    }

    // now release the old ones
    xkb_state_unref(m_state);
    xkb_keymap_unref(m_keymap);

    m_keymap = keymap;
    m_state = state;

    m_shiftModifier = xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_SHIFT);
    m_capsModifier = xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_CAPS);
    m_controlModifier = xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_CTRL);
    m_altModifier = xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_ALT);
    m_metaModifier = xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_LOGO);
    m_numModifier = xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_NUM);

    m_numLock = xkb_keymap_led_get_index(m_keymap, XKB_LED_NAME_NUM);
    m_capsLock = xkb_keymap_led_get_index(m_keymap, XKB_LED_NAME_CAPS);
    m_scrollLock = xkb_keymap_led_get_index(m_keymap, XKB_LED_NAME_SCROLL);

    m_currentLayout = xkb_state_serialize_layout(m_state, XKB_STATE_LAYOUT_EFFECTIVE);

    m_modifierState.depressed
        = xkb_state_serialize_mods(m_state, xkb_state_component(XKB_STATE_MODS_DEPRESSED));
    m_modifierState.latched
        = xkb_state_serialize_mods(m_state, xkb_state_component(XKB_STATE_MODS_LATCHED));
    m_modifierState.locked
        = xkb_state_serialize_mods(m_state, xkb_state_component(XKB_STATE_MODS_LOCKED));

    evaluate_startup_num_lock();
    createKeymapFile();
    updateModifiers();
    forwardModifiers();
}

void xkb::evaluate_startup_num_lock()
{
    if (startup_num_lock_done) {
        return;
    }
    startup_num_lock_done = true;

    if (m_ownership == Ownership::Client || m_numModifier == XKB_MOD_INVALID || !m_numLockConfig) {
        return;
    }

    auto const setting = read_startup_num_lock_config();
    if (setting == latched_key_change::unchanged) {
        // We keep the current state.
        return;
    }

    auto num_lock_is_active
        = xkb_state_mod_index_is_active(m_state, m_numModifier, XKB_STATE_MODS_LOCKED);
    if (num_lock_is_active < 0) {
        // Index not available
        return;
    }

    auto num_lock_current = num_lock_is_active ? latched_key_change::on : latched_key_change::off;

    if (setting == num_lock_current) {
        // Nothing to change.
        return;
    }

    auto mask = std::bitset<sizeof(xkb_mod_mask_t) * 8>{m_modifierState.locked};

    if (mask.size() <= m_numModifier) {
        // Not enough space in the mask for the num lock.
        return;
    }

    mask[m_numModifier] = (setting == latched_key_change::on);
    m_modifierState.locked = mask.to_ulong();

    xkb_state_update_mask(m_state,
                          m_modifierState.depressed,
                          m_modifierState.latched,
                          m_modifierState.locked,
                          0,
                          0,
                          m_currentLayout);

    m_modifierState.locked
        = xkb_state_serialize_mods(m_state, xkb_state_component(XKB_STATE_MODS_LOCKED));
}

latched_key_change xkb::read_startup_num_lock_config()
{
    // STATE_ON = 0,  STATE_OFF = 1, STATE_UNCHANGED = 2, see plasma-desktop/kcms/keyboard/kcmmisc.h
    auto const config = m_numLockConfig->group("Keyboard");
    auto setting = config.readEntry("NumLock", 2);

    if (setting == 0) {
        return latched_key_change::on;
    }
    if (setting == 1) {
        return latched_key_change::off;
    }

    return latched_key_change::unchanged;
}

void xkb::createKeymapFile()
{
    if (!m_seat) {
        return;
    }
    // TODO: uninstall keymap on server?
    if (!m_keymap) {
        return;
    }

    ScopedCPointer<char> keymapString(
        xkb_keymap_get_as_string(m_keymap, XKB_KEYMAP_FORMAT_TEXT_V1));
    if (keymapString.isNull()) {
        return;
    }

    m_seat->keyboards().set_keymap(keymapString.data());
}

void xkb::updateModifiers(uint32_t modsDepressed,
                          uint32_t modsLatched,
                          uint32_t modsLocked,
                          uint32_t group)
{
    if (!m_keymap || !m_state) {
        return;
    }
    xkb_state_update_mask(m_state, modsDepressed, modsLatched, modsLocked, 0, 0, group);
    updateModifiers();
    forwardModifiers();
}

void xkb::updateKey(uint32_t key, key_state state)
{
    if (!m_keymap || !m_state) {
        return;
    }
    xkb_state_update_key(m_state, key + 8, static_cast<xkb_key_direction>(state));
    if (state == key_state::pressed) {
        const auto sym = toKeysym(key);
        if (compose.state
            && xkb_compose_state_feed(compose.state, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
            switch (xkb_compose_state_get_status(compose.state)) {
            case XKB_COMPOSE_NOTHING:
                m_keysym = sym;
                break;
            case XKB_COMPOSE_COMPOSED:
                m_keysym = xkb_compose_state_get_one_sym(compose.state);
                break;
            default:
                m_keysym = XKB_KEY_NoSymbol;
                break;
            }
        } else {
            m_keysym = sym;
        }
    }
    updateModifiers();
    updateConsumedModifiers(key);
}

void xkb::updateModifiers()
{
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (xkb_state_mod_index_is_active(m_state, m_shiftModifier, XKB_STATE_MODS_EFFECTIVE) == 1
        || xkb_state_mod_index_is_active(m_state, m_capsModifier, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::ShiftModifier;
    }
    if (xkb_state_mod_index_is_active(m_state, m_altModifier, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::AltModifier;
    }
    if (xkb_state_mod_index_is_active(m_state, m_controlModifier, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::ControlModifier;
    }
    if (xkb_state_mod_index_is_active(m_state, m_metaModifier, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::MetaModifier;
    }
    if (m_keysym >= XKB_KEY_KP_Space && m_keysym <= XKB_KEY_KP_9) {
        mods |= Qt::KeypadModifier;
    }
    m_modifiers = mods;

    // update LEDs
    auto leds{keyboard_leds::none};
    if (xkb_state_led_index_is_active(m_state, m_numLock) == 1) {
        leds = leds | keyboard_leds::num_lock;
    }
    if (xkb_state_led_index_is_active(m_state, m_capsLock) == 1) {
        leds = leds | keyboard_leds::caps_lock;
    }
    if (xkb_state_led_index_is_active(m_state, m_scrollLock) == 1) {
        leds = leds | keyboard_leds::scroll_lock;
    }
    if (m_leds != leds) {
        m_leds = leds;
        emit ledsChanged(m_leds);
    }

    m_currentLayout = xkb_state_serialize_layout(m_state, XKB_STATE_LAYOUT_EFFECTIVE);
    m_modifierState.depressed
        = xkb_state_serialize_mods(m_state, xkb_state_component(XKB_STATE_MODS_DEPRESSED));
    m_modifierState.latched
        = xkb_state_serialize_mods(m_state, xkb_state_component(XKB_STATE_MODS_LATCHED));
    m_modifierState.locked
        = xkb_state_serialize_mods(m_state, xkb_state_component(XKB_STATE_MODS_LOCKED));
}

void xkb::forwardModifiers()
{
    if (!m_seat) {
        return;
    }
    m_seat->keyboards().update_modifiers(m_modifierState.depressed,
                                         m_modifierState.latched,
                                         m_modifierState.locked,
                                         m_currentLayout);
}

std::string xkb::layoutName(xkb_layout_index_t index) const
{
    if (!m_keymap) {
        return {};
    }
    return std::string(xkb_keymap_layout_get_name(m_keymap, index));
}

std::string xkb::layoutName() const
{
    return layoutName(m_currentLayout);
}

std::string const& xkb::layoutShortName(int index) const
{
    return m_layoutList.at(index);
}

void xkb::updateConsumedModifiers(uint32_t key)
{
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (xkb_state_mod_index_is_consumed2(m_state, key + 8, m_shiftModifier, XKB_CONSUMED_MODE_GTK)
        == 1) {
        mods |= Qt::ShiftModifier;
    }
    if (xkb_state_mod_index_is_consumed2(m_state, key + 8, m_altModifier, XKB_CONSUMED_MODE_GTK)
        == 1) {
        mods |= Qt::AltModifier;
    }
    if (xkb_state_mod_index_is_consumed2(m_state, key + 8, m_controlModifier, XKB_CONSUMED_MODE_GTK)
        == 1) {
        mods |= Qt::ControlModifier;
    }
    if (xkb_state_mod_index_is_consumed2(m_state, key + 8, m_metaModifier, XKB_CONSUMED_MODE_GTK)
        == 1) {
        mods |= Qt::MetaModifier;
    }
    m_consumedModifiers = mods;
}

Qt::KeyboardModifiers xkb::modifiersRelevantForGlobalShortcuts(uint32_t scanCode) const
{
    if (!m_state) {
        return Qt::NoModifier;
    }
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (xkb_state_mod_index_is_active(m_state, m_shiftModifier, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::ShiftModifier;
    }
    if (xkb_state_mod_index_is_active(m_state, m_altModifier, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::AltModifier;
    }
    if (xkb_state_mod_index_is_active(m_state, m_controlModifier, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::ControlModifier;
    }
    if (xkb_state_mod_index_is_active(m_state, m_metaModifier, XKB_STATE_MODS_EFFECTIVE) == 1) {
        mods |= Qt::MetaModifier;
    }

    Qt::KeyboardModifiers consumedMods = m_consumedModifiers;
    if ((mods & Qt::ShiftModifier) && (consumedMods == Qt::ShiftModifier)) {
        // test whether current keysym is a letter
        // in that case the shift should be removed from the consumed modifiers again
        // otherwise it would not be possible to trigger e.g. Shift+W as a shortcut
        // see BUG: 370341
        if (QChar(toQtKey(m_keysym, scanCode, Qt::ControlModifier)).isLetter()) {
            consumedMods = Qt::KeyboardModifiers();
        }
    }

    return mods & ~consumedMods;
}

xkb_keysym_t xkb::toKeysym(uint32_t key)
{
    if (!m_state) {
        return XKB_KEY_NoSymbol;
    }
    return xkb_state_key_get_one_sym(m_state, key + 8);
}

std::string xkb::toString(xkb_keysym_t keysym)
{
    if (!m_state || keysym == XKB_KEY_NoSymbol) {
        return {};
    }
    QByteArray byteArray(7, 0);
    int ok = xkb_keysym_to_utf8(keysym, byteArray.data(), byteArray.size());
    if (ok == -1 || ok == 0) {
        return {};
    }
    return std::string(byteArray.constData());
}

Qt::Key xkb::toQtKey(xkb_keysym_t keySym,
                     uint32_t scanCode,
                     Qt::KeyboardModifiers modifiers,
                     bool superAsMeta) const
{
    // FIXME: passing superAsMeta doesn't have impact due to bug in the Qt function, so handle it
    // below
    Qt::Key qtKey
        = Qt::Key(QXkbCommon::keysymToQtKey(keySym, modifiers, m_state, scanCode + 8, superAsMeta));

    // FIXME: workarounds for symbols currently wrong/not mappable via keysymToQtKey()
    if (superAsMeta && (qtKey == Qt::Key_Super_L || qtKey == Qt::Key_Super_R)) {
        // translate Super/Hyper keys to Meta if we're using them as the MetaModifier
        qtKey = Qt::Key_Meta;
    } else if (qtKey > 0xff && keySym <= 0xff) {
        // XKB_KEY_mu, XKB_KEY_ydiaeresis go here
        qtKey = Qt::Key(keySym);
#if QT_VERSION_MAJOR < 6 // since Qt 5 LTS is frozen
    } else if (keySym == XKB_KEY_Sys_Req) {
        // fixed in QTBUG-92087
        qtKey = Qt::Key_SysReq;
#endif
    }
    return qtKey;
}

bool xkb::shouldKeyRepeat(quint32 key) const
{
    if (!m_keymap) {
        return false;
    }
    return xkb_keymap_key_repeats(m_keymap, key + 8) != 0;
}

void xkb::switchToNextLayout()
{
    if (!m_keymap || !m_state) {
        return;
    }
    const xkb_layout_index_t numLayouts = xkb_keymap_num_layouts(m_keymap);
    const xkb_layout_index_t nextLayout
        = (xkb_state_serialize_layout(m_state, XKB_STATE_LAYOUT_EFFECTIVE) + 1) % numLayouts;
    switchToLayout(nextLayout);
}

void xkb::switchToPreviousLayout()
{
    if (!m_keymap || !m_state) {
        return;
    }
    const xkb_layout_index_t previousLayout
        = m_currentLayout == 0 ? numberOfLayouts() - 1 : m_currentLayout - 1;
    switchToLayout(previousLayout);
}

bool xkb::switchToLayout(xkb_layout_index_t layout)
{
    if (!m_keymap || !m_state || layout >= numberOfLayouts()) {
        return false;
    }
    const xkb_mod_mask_t depressed
        = xkb_state_serialize_mods(m_state, xkb_state_component(XKB_STATE_MODS_DEPRESSED));
    const xkb_mod_mask_t latched
        = xkb_state_serialize_mods(m_state, xkb_state_component(XKB_STATE_MODS_LATCHED));
    const xkb_mod_mask_t locked
        = xkb_state_serialize_mods(m_state, xkb_state_component(XKB_STATE_MODS_LOCKED));
    xkb_state_update_mask(m_state, depressed, latched, locked, 0, 0, layout);
    updateModifiers();
    forwardModifiers();
    return true;
}

quint32 xkb::numberOfLayouts() const
{
    if (!m_keymap) {
        return 0;
    }
    return xkb_keymap_num_layouts(m_keymap);
}

void xkb::setSeat(Wrapland::Server::Seat* seat)
{
    m_seat = QPointer<Wrapland::Server::Seat>(seat);
}

}
