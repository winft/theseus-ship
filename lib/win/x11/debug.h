/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/x11/xcb/extensions.h>
#include <render/gl/interface/platform.h>

#include <QString>

namespace KWin::win::x11
{

template<typename Space>
void debug_support_info(Space const& space, QString& support)
{
    if (!space.base.x11_data.connection) {
        return;
    }

    auto x11setup = xcb_get_setup(space.base.x11_data.connection);

    auto get_xserver_version = [](auto setup) {
        int64_t major = 0;
        int64_t minor = 0;
        int64_t patch = 0;

        QByteArray const vendorName(xcb_setup_vendor(setup), xcb_setup_vendor_length(setup));

        if (vendorName.contains("X.Org")) {
            int const release = setup->release_number;
            major = (release / 10000000);
            minor = (release / 100000) % 100;
            patch = (release / 1000) % 100;
        }

        return GLPlatform::versionToString(kVersionNumber(major, minor, patch));
    };

    support.append(QStringLiteral("X11\n"));
    support.append(QStringLiteral("===\n"));
    support.append(QStringLiteral("Vendor: %1\n")
                       .arg(QString::fromUtf8(QByteArray::fromRawData(
                           xcb_setup_vendor(x11setup), xcb_setup_vendor_length(x11setup)))));
    support.append(QStringLiteral("Vendor Release: %1\n").arg(x11setup->release_number));
    support.append(QStringLiteral("Server version: %1\n").arg(get_xserver_version(x11setup)));
    support.append(QStringLiteral("Protocol Version/Revision: %1/%2\n")
                       .arg(x11setup->protocol_major_version)
                       .arg(x11setup->protocol_minor_version));

    auto const extensions = base::x11::xcb::extensions::self()->get_data();
    for (const auto& ext : extensions) {
        support.append(QStringLiteral("%1: %2; Version: 0x%3\n")
                           .arg(QString::fromUtf8(ext.name))
                           .arg(ext.present ? "yes" : "no")
                           .arg(QString::number(ext.version, 16)));
    }

    support.append(QStringLiteral("\n"));
}

}
