/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect/basic_effect_loader.h"

#include "kwin_export.h"
#include "script/effect_loader.h"

#include <KPluginMetaData>
#include <QPair>
#include <QQueue>

namespace KWin
{

class EffectPluginFactory;
class EffectsHandler;

namespace render
{

/**
 * @brief Helper class to queue the loading of Effects.
 *
 * Loading an Effect has to be done in the compositor thread and thus the Compositor is blocked
 * while the Effect loads. To not block the compositor for several frames the loading of all
 * Effects need to be queued. By invoking the slot dequeue() through a QueuedConnection the queue
 * can ensure that events are processed between the loading of two Effects and thus the compositor
 * doesn't block.
 *
 * As it needs to be a slot, the queue must subclass QObject, but it also needs to be templated as
 * the information to load an Effect is specific to the Effect Loader. Thus there is the
 * basic_effect_load_queue providing the slots as pure virtual functions and the templated
 * effect_load_queue inheriting from basic_effect_load_queue.
 *
 * The queue operates like a normal queue providing enqueue and a scheduleDequeue instead of
 * dequeue.
 *
 */
class basic_effect_load_queue : public QObject
{
public:
    explicit basic_effect_load_queue(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    virtual void dequeue() = 0;
};

template<typename Loader, typename QueueType>
class effect_load_queue : public basic_effect_load_queue
{
public:
    explicit effect_load_queue(Loader* parent)
        : basic_effect_load_queue(parent)
        , m_effectLoader(parent)
        , m_dequeueScheduled(false)
    {
    }
    void enqueue(const QPair<QueueType, load_effect_flags> value)
    {
        m_queue.enqueue(value);
        scheduleDequeue();
    }
    void clear()
    {
        m_queue.clear();
        m_dequeueScheduled = false;
    }

protected:
    void dequeue() override
    {
        if (m_queue.isEmpty()) {
            return;
        }
        m_dequeueScheduled = false;
        const auto pair = m_queue.dequeue();
        m_effectLoader->loadEffect(pair.first, pair.second);
        scheduleDequeue();
    }

private:
    void scheduleDequeue()
    {
        if (m_queue.isEmpty() || m_dequeueScheduled) {
            return;
        }
        m_dequeueScheduled = true;
        QMetaObject::invokeMethod(
            this, [this] { dequeue(); }, Qt::QueuedConnection);
    }
    Loader* m_effectLoader;
    bool m_dequeueScheduled;
    QQueue<QPair<QueueType, load_effect_flags>> m_queue;
};

class KWIN_EXPORT plugin_effect_loader : public basic_effect_loader
{
public:
    explicit plugin_effect_loader(QObject* parent = nullptr);
    ~plugin_effect_loader() override;

    bool hasEffect(const QString& name) const override;
    bool isEffectSupported(const QString& name) const override;
    QStringList listOfKnownEffects() const override;

    void clear() override;
    void queryAndLoadAll() override;
    bool loadEffect(const QString& name) override;
    bool loadEffect(const KPluginMetaData& info, load_effect_flags load_flags);

    void setPluginSubDirectory(const QString& directory);

private:
    QVector<KPluginMetaData> findAllEffects() const;
    KPluginMetaData findEffect(const QString& name) const;
    EffectPluginFactory* factory(const KPluginMetaData& info) const;
    QStringList m_loadedEffects;
    QString m_pluginSubDirectory;
};

class KWIN_EXPORT effect_loader : public basic_effect_loader
{
public:
    template<typename Compositor>
    explicit effect_loader(EffectsHandler& effects,
                           Compositor& compositor,
                           QObject* parent = nullptr)
        : basic_effect_loader(parent)
    {
        m_loaders << new scripting::effect_loader(effects, compositor, this)
                  << new plugin_effect_loader(this);
        for (auto it = m_loaders.constBegin(); it != m_loaders.constEnd(); ++it) {
            connect(
                *it, &basic_effect_loader::effectLoaded, this, &basic_effect_loader::effectLoaded);
        }
    }

    ~effect_loader() override;
    bool hasEffect(const QString& name) const override;
    bool isEffectSupported(const QString& name) const override;
    QStringList listOfKnownEffects() const override;
    bool loadEffect(const QString& name) override;
    void queryAndLoadAll() override;
    void setConfig(KSharedConfig::Ptr config) override;
    void clear() override;

private:
    QList<basic_effect_loader*> m_loaders;
};

}
}
