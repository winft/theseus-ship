/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KConfigGroup>
#include <QHash>
#include <QObject>

namespace KWin
{
class Toplevel;
class VirtualDesktop;

namespace input
{
class keyboard_layout_spy;
class xkb;

namespace KeyboardLayoutSwitching
{

class Policy : public QObject
{
    Q_OBJECT
public:
    ~Policy() override;

    virtual QString name() const = 0;

    static Policy* create(xkb* xkb,
                          keyboard_layout_spy* layout,
                          const KConfigGroup& config,
                          const QString& policy);

protected:
    explicit Policy(xkb* xkb,
                    keyboard_layout_spy* layout,
                    const KConfigGroup& config = KConfigGroup());
    virtual void clearCache() = 0;
    virtual void layoutChanged(uint index) = 0;

    void setLayout(uint index);

    KConfigGroup m_config;
    virtual const QString defaultLayoutEntryKey() const;
    void clearLayouts();

    static const char defaultLayoutEntryKeyPrefix[];
    xkb* m_xkb;

private:
    keyboard_layout_spy* m_layout;
};

class GlobalPolicy : public Policy
{
    Q_OBJECT
public:
    explicit GlobalPolicy(xkb* xkb, keyboard_layout_spy* layout, const KConfigGroup& config);
    ~GlobalPolicy() override;

    QString name() const override
    {
        return QStringLiteral("Global");
    }

protected:
    void clearCache() override
    {
    }
    void layoutChanged(uint index) override
    {
        Q_UNUSED(index)
    }

private:
    const QString defaultLayoutEntryKey() const override;
};

class VirtualDesktopPolicy : public Policy
{
    Q_OBJECT
public:
    explicit VirtualDesktopPolicy(xkb* xkb,
                                  keyboard_layout_spy* layout,
                                  const KConfigGroup& config);
    ~VirtualDesktopPolicy() override;

    QString name() const override
    {
        return QStringLiteral("Desktop");
    }

protected:
    void clearCache() override;
    void layoutChanged(uint index) override;

private:
    void desktopChanged();
    QHash<VirtualDesktop*, quint32> m_layouts;
};

class WindowPolicy : public Policy
{
    Q_OBJECT
public:
    explicit WindowPolicy(xkb* xkb, keyboard_layout_spy* layout);
    ~WindowPolicy() override;

    QString name() const override
    {
        return QStringLiteral("Window");
    }

protected:
    void clearCache() override;
    void layoutChanged(uint index) override;

private:
    QHash<Toplevel*, quint32> m_layouts;
};

class ApplicationPolicy : public Policy
{
    Q_OBJECT
public:
    explicit ApplicationPolicy(xkb* xkb, keyboard_layout_spy* layout, const KConfigGroup& config);
    ~ApplicationPolicy() override;

    QString name() const override
    {
        return QStringLiteral("WinClass");
    }

protected:
    void clearCache() override;
    void layoutChanged(uint index) override;

private:
    void clientActivated(Toplevel* window);
    QHash<Toplevel*, quint32> m_layouts;
    QHash<QByteArray, quint32> m_layoutsRestored;
};

}
}
}
