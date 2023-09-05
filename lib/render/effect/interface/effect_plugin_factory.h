/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/config-kwin.h>
#include <kwin_export.h>

#include <KPluginFactory>

namespace KWin
{

class Effect;

/**
 * Prefer the KWIN_EFFECT_FACTORY macros.
 */
class KWIN_EXPORT EffectPluginFactory : public KPluginFactory
{
    Q_OBJECT
public:
    EffectPluginFactory();
    ~EffectPluginFactory() override;
    /**
     * Returns whether the Effect is supported.
     *
     * An Effect can implement this method to determine at runtime whether the Effect is supported.
     *
     * If the current compositing backend is not supported it should return @c false.
     *
     * This method is optional, by default @c true is returned.
     */
    virtual bool isSupported() const;
    /**
     * Returns whether the Effect should get enabled by default.
     *
     * This function provides a way for an effect to override the default at runtime,
     * e.g. based on the capabilities of the hardware.
     *
     * This method is optional; the effect doesn't have to provide it.
     *
     * Note that this function is only called if the supported() function returns true,
     * and if X-KDE-PluginInfo-EnabledByDefault is set to true in the .desktop file.
     *
     * This method is optional, by default @c true is returned.
     */
    virtual bool enabledByDefault() const;
    /**
     * This method returns the created Effect.
     */
    virtual KWin::Effect* createEffect() const = 0;
};

#define EffectPluginFactory_iid "org.kde.kwin.EffectPluginFactory" KWIN_VERSION_STRING
#define KWIN_PLUGIN_FACTORY_NAME KPLUGINFACTORY_PLUGIN_CLASS_INTERNAL_NAME

/**
 * Defines an EffectPluginFactory sub class with customized isSupported and enabledByDefault
 * methods.
 *
 * If the Effect to be created does not need the isSupported or enabledByDefault methods prefer
 * the simplified KWIN_EFFECT_FACTORY, KWIN_EFFECT_FACTORY_SUPPORTED or KWIN_EFFECT_FACTORY_ENABLED
 * macros which create an EffectPluginFactory with a useable default value.
 *
 * This API is not providing binary compatibility and thus the effect plugin must be compiled
 * against the same kwineffects library version as KWin.
 *
 * @param factoryName The name to be used for the EffectPluginFactory
 * @param className The class name of the Effect sub class which is to be created by the factory
 * @param jsonFile Name of the json file to be compiled into the plugin as metadata
 * @param supported Source code to go into the isSupported() method, must return a boolean
 * @param enabled Source code to go into the enabledByDefault() method, must return a boolean
 */
#define KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(className, jsonFile, supported, enabled)             \
    class KWIN_PLUGIN_FACTORY_NAME : public KWin::EffectPluginFactory                              \
    {                                                                                              \
        Q_OBJECT                                                                                   \
        Q_PLUGIN_METADATA(IID EffectPluginFactory_iid FILE jsonFile)                               \
        Q_INTERFACES(KPluginFactory)                                                               \
    public:                                                                                        \
        explicit KWIN_PLUGIN_FACTORY_NAME()                                                        \
        {                                                                                          \
        }                                                                                          \
        ~KWIN_PLUGIN_FACTORY_NAME()                                                                \
        {                                                                                          \
        }                                                                                          \
        bool isSupported() const override                                                          \
        {                                                                                          \
            supported                                                                              \
        }                                                                                          \
        bool enabledByDefault() const override{                                                    \
            enabled} KWin::Effect* createEffect() const override                                   \
        {                                                                                          \
            return new className();                                                                \
        }                                                                                          \
    };

#define KWIN_EFFECT_FACTORY_ENABLED(className, jsonFile, enabled)                                  \
    KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(className, jsonFile, return true;, enabled)

#define KWIN_EFFECT_FACTORY_SUPPORTED(className, jsonFile, supported)                              \
    KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(className, jsonFile, supported, return true;)

#define KWIN_EFFECT_FACTORY(className, jsonFile)                                                   \
    KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(className, jsonFile, return true;, return true;)

}
