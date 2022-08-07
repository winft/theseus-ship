/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <memory>
#include <string>

namespace KWin::input
{

namespace control
{
class device_config;

class KWIN_EXPORT device : public QObject
{
    Q_OBJECT

public:
    ~device() override;

    struct {
        std::string name;
        std::string sys_name;
        uint32_t vendor_id{0};
        uint32_t product_id{0};
    } metadata;

    virtual bool supports_disable_events() const = 0;

    virtual bool is_enabled() const = 0;
    void set_enabled(bool enable);

    std::unique_ptr<device_config> config;

Q_SIGNALS:
    void enabled_changed();

protected:
    explicit device(device_config* config);
    virtual bool set_enabled_impl(bool enable) = 0;
};

}
}
