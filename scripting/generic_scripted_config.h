/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KCModule>
#include <KConfigGroup>
#include <KPluginFactory>

class KLocalizedTranslator;

namespace KWin::scripting
{

class generic_scripted_config_factory : public KPluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.KPluginFactory" FILE "generic_scripted_config.json")
    Q_INTERFACES(KPluginFactory)

protected:
    QObject* create(const char* iface,
                    QWidget* parentWidget,
                    QObject* parent,
                    const QVariantList& args,
                    const QString& keyword) override;
};

class generic_scripted_config : public KCModule
{
    Q_OBJECT

public:
    generic_scripted_config(const QString& keyword, QWidget* parent, const QVariantList& args);
    ~generic_scripted_config() override;

public Q_SLOTS:
    void save() override;

protected:
    const QString& packageName() const;
    void createUi();
    virtual QString typeName() const = 0;
    virtual KConfigGroup configGroup() = 0;
    virtual void reload();

private:
    QString m_packageName;
    KLocalizedTranslator* m_translator;
};

class scripted_effect_config : public generic_scripted_config
{
    Q_OBJECT
public:
    scripted_effect_config(const QString& keyword, QWidget* parent, const QVariantList& args);
    ~scripted_effect_config() override;

protected:
    QString typeName() const override;
    KConfigGroup configGroup() override;
    void reload() override;
};

class scripting_config : public generic_scripted_config
{
    Q_OBJECT
public:
    scripting_config(const QString& keyword, QWidget* parent, const QVariantList& args);
    ~scripting_config() override;

protected:
    QString typeName() const override;
    KConfigGroup configGroup() override;
    void reload() override;
};

inline const QString& generic_scripted_config::packageName() const
{
    return m_packageName;
}

}
