/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "debug/console/console.h"

namespace KWin
{

namespace win::wayland
{
class window;
}

namespace debug
{

class input_filter;

class KWIN_EXPORT wayland_console : public console
{
    Q_OBJECT
public:
    wayland_console();
    ~wayland_console();

private:
    void update_keyboard_tab();

    QScopedPointer<input_filter> m_inputFilter;
};

class KWIN_EXPORT wayland_console_model : public console_model
{
    Q_OBJECT
public:
    explicit wayland_console_model(QObject* parent = nullptr);

protected:
    bool get_client_count(int parent_id, int& count) const override;
    bool get_property_count(QModelIndex const& parent, int& count) const override;

    bool get_client_index(int row, int column, int parent_id, QModelIndex& index) const override;
    bool get_property_index(int row,
                            int column,
                            QModelIndex const& parent,
                            QModelIndex& index) const override;

    QVariant get_client_data(QModelIndex const& index, int role) const override;
    QVariant get_client_property_data(QModelIndex const& index, int role) const override;

    int topLevelRowCount() const override;

private:
    win::wayland::window* shellClient(const QModelIndex& index) const;

    QVector<win::wayland::window*> m_shellClients;
};

class KWIN_EXPORT wayland_console_delegate : public console_delegate
{
    Q_OBJECT
public:
    explicit wayland_console_delegate(QObject* parent = nullptr);

    QString displayText(const QVariant& value, const QLocale& locale) const override;
};

}
}
