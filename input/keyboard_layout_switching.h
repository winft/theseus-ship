/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KConfigGroup>
#include <QObject>
#include <unordered_map>

namespace KWin
{
class Toplevel;
class VirtualDesktop;

namespace input
{
namespace xkb
{
class layout_manager;
}

namespace keyboard_layout_switching
{

class policy : public QObject
{
    Q_OBJECT
public:
    ~policy() override;

    virtual QString name() const = 0;

    static policy*
    create(xkb::layout_manager* manager, KConfigGroup const& config, QString const& policy);

protected:
    explicit policy(xkb::layout_manager* manager, KConfigGroup const& config = KConfigGroup());

    virtual void clear_cache() = 0;
    virtual void handle_layout_change(uint index) = 0;

    void set_layout(uint index);

    virtual QString const default_layout_entry_key() const;
    void clear_layouts();

    KConfigGroup config;
    static const char default_layout_entry_key_prefix[];

private:
    xkb::layout_manager* manager;
};

class global_policy : public policy
{
    Q_OBJECT
public:
    global_policy(xkb::layout_manager* manager, KConfigGroup const& config);

    QString name() const override
    {
        return QStringLiteral("Global");
    }

protected:
    void clear_cache() override
    {
    }
    void handle_layout_change(uint index) override
    {
        Q_UNUSED(index)
    }

private:
    QString const default_layout_entry_key() const override;
};

class virtual_desktop_policy : public policy
{
    Q_OBJECT
public:
    virtual_desktop_policy(xkb::layout_manager* manager, KConfigGroup const& config);

    QString name() const override
    {
        return QStringLiteral("Desktop");
    }

protected:
    void clear_cache() override;
    void handle_layout_change(uint index) override;

private:
    void handle_desktop_change();

    std::unordered_map<VirtualDesktop*, uint32_t> layouts;
};

class window_policy : public policy
{
    Q_OBJECT
public:
    explicit window_policy(xkb::layout_manager* manager);

    QString name() const override
    {
        return QStringLiteral("Window");
    }

protected:
    void clear_cache() override;
    void handle_layout_change(uint index) override;

private:
    std::unordered_map<Toplevel*, uint32_t> layouts;
};

class application_policy : public policy
{
    Q_OBJECT
public:
    application_policy(xkb::layout_manager* manager, KConfigGroup const& config);

    QString name() const override
    {
        return QStringLiteral("WinClass");
    }

protected:
    void clear_cache() override;
    void handle_layout_change(uint index) override;

private:
    void handle_client_activated(Toplevel* window);

    std::unordered_map<Toplevel*, uint32_t> layouts;
    std::unordered_map<QByteArray, uint32_t> restored_layouts;
};

}
}
}
