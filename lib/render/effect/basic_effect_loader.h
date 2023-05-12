/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "render/types.h"
#include "utils/flags.h"

#include <KSharedConfig>
#include <QObject>

namespace KWin
{

class Effect;

namespace render
{

/**
 * @brief Interface to describe how an effect loader has to function.
 *
 * The basic_effect_loader specifies the methods a concrete loader has to implement and how
 * those methods are expected to perform. Also it provides an interface to the outside world
 * (that is render::effects_handler_impl).
 *
 * The abstraction is used because there are multiple types of Effects which need to be loaded:
 * @li Static Effects
 * @li Scripted Effects
 * @li Binary Plugin Effects
 *
 * Serving all of them with one Effect Loader is rather complex given that different stores need
 * to be queried at the same time. Thus the idea is to have one implementation per type and one
 * implementation which makes use of all of them and combines the loading.
 */
class KWIN_EXPORT basic_effect_loader : public QObject
{
    Q_OBJECT
public:
    ~basic_effect_loader() override;

    /**
     * @brief The KSharedConfig this effect loader should operate on.
     *
     * Important: a valid KSharedConfig must be provided before trying to load any effects!
     *
     * @param config
     * @internal
     */
    virtual void setConfig(KSharedConfig::Ptr config);

    /**
     * @brief Whether this Effect Loader can load the Effect with the given @p name.
     *
     * The Effect Loader determines whether it knows or can find an Effect called @p name,
     * and thus whether it can attempt to load the Effect.
     *
     * @param name The name of the Effect to look for.
     * @return bool @c true if the Effect Loader knows this effect, false otherwise
     */
    virtual bool hasEffect(QString const& name) const = 0;

    /**
     * @brief All the Effects this loader knows of.
     *
     * The implementation should re-query its store whenever this method is invoked.
     * It's possible that the store of effects changed (e.g. a new one got installed)
     *
     * @return QStringList The internal names of the known Effects
     */
    virtual QStringList listOfKnownEffects() const = 0;

    /**
     * @brief Synchronous loading of the Effect with the given @p name.
     *
     * Loads the Effect without checking any configuration value or any enabled by default
     * function provided by the Effect.
     *
     * The loader is expected to apply the following checks:
     * If the Effect is already loaded, the Effect should not get loaded again. Thus the loader
     * is expected to track which Effects it has loaded, and which of those have been destroyed.
     * The loader should check whether the Effect is supported. If the Effect indicates it is
     * not supported, it should not get loaded.
     *
     * If the Effect loaded successfully the signal effectLoaded(KWin::Effect*,QString const&)
     * must be emitted. Otherwise the user of the loader is not able to get the loaded Effect.
     * It's not returning the Effect as queryAndLoadAll() is working async and thus the users
     * of the loader are expected to be prepared for async loading.
     *
     * @param name The internal name of the Effect which should be loaded
     * @return bool @c true if the effect could be loaded, @c false in error case
     * @see queryAndLoadAll()
     * @see effectLoaded(KWin::Effect*,QString const&)
     */
    virtual bool loadEffect(QString const& name) = 0;

    /**
     * @brief The Effect Loader should query its store for all available effects and try to load
     * them.
     *
     * The Effect  Loader is supposed to perform this operation in a highly async way. If there is
     * IO which needs to be performed this should be done in a background thread and a queue should
     * be used to load the effects. The loader should make sure to not load more than one Effect
     * in one event cycle. Loading the Effect has to be performed in the Compositor thread and
     * thus blocks the Compositor. Therefore after loading one Effect all events should get
     * processed first, so that the Compositor can perform a painting pass if needed. To simplify
     * this operation one can use the effect_load_queue. This requires to add another loadEffect
     * method with the custom loader specific type to refer to an Effect and load_effect_flags.
     *
     * The load_effect_flags have to be determined by querying the configuration with readConfig().
     * If the Load flag is set the loading can proceed and all the checks from
     * loadEffect(QString const &) have to be applied.
     * In addition if the CheckDefaultFunction flag is set and the Effect provides such a method,
     * it should be queried to determine whether the Effect is enabled by default. If such a method
     * returns @c false the Effect should not get loaded. If the Effect does not provide a way to
     * query whether it's enabled by default at runtime the flag can get ignored.
     *
     * If the Effect loaded successfully the signal effectLoaded(KWin::Effect*,QString const&)
     * must be emitted.
     *
     * @see loadEffect(QString const &)
     * @see effectLoaded(KWin::Effect*,QString const&)
     */
    virtual void queryAndLoadAll() = 0;

    /**
     * @brief Whether the Effect with the given @p name is supported by the compositing backend.
     *
     * @param name The name of the Effect to check.
     * @return bool @c true if it is supported, @c false otherwise
     */
    virtual bool isEffectSupported(QString const& name) const = 0;

    /**
     * @brief Clears the load queue, that is all scheduled Effects are discarded from loading.
     */
    virtual void clear() = 0;

Q_SIGNALS:
    /**
     * @brief The loader emits this signal when it successfully loaded an effect.
     *
     * @param effect The created Effect
     * @param name The internal name of the loaded Effect
     * @return void
     */
    void effectLoaded(KWin::Effect* effect, QString const& name);

protected:
    explicit basic_effect_loader(QObject* parent = nullptr);
    /**
     * @brief Checks the configuration for the Effect identified by @p effectName.
     *
     * For each Effect there could be a key called "<effectName>Enabled". If there is such a key
     * the returned flags will contain Load in case it's @c true. If the key does not exist the
     * @p defaultValue determines whether the Effect should be loaded. A value of @c true means
     * that Load | CheckDefaultFunction is returned, in case of @c false no Load flags are returned.
     *
     * @param effectName The name of the Effect to look for in the configuration
     * @param defaultValue Whether the Effect is enabled by default or not.
     * @returns Flags indicating whether the Effect should be loaded and how it should be loaded
     */
    load_effect_flags readConfig(QString const& effectName, bool defaultValue) const;

private:
    KSharedConfig::Ptr m_config;
};

}
}
