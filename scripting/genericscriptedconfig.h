/*
 *  KWin - the KDE window manager
 *  This file is part of the KDE project.
 *
 * Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
    Q_PLUGIN_METADATA(IID "org.kde.KPluginFactory" FILE "genericscriptedconfig.json")
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
