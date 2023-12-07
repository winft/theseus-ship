/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "xauthority.h"

#include <QDataStream>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryFile>
#include <string>

namespace KWin::xwl
{

namespace
{

void xauthority_write_entry(QDataStream& stream,
                            quint16 family,
                            std::string const& address,
                            std::string const& display,
                            std::string const& name,
                            std::string const& cookie)
{
    stream << quint16(family);

    auto write_array = [&stream](auto const& str) {
        stream << static_cast<quint16>(str.size());
        stream.writeRawData(str.data(), str.size());
    };

    write_array(address);
    write_array(display);
    write_array(name);
    write_array(cookie);
}

std::string xauthority_generate_cookie()
{
    std::string cookie;

    // Cookie must be 128bits
    cookie.resize(16);

    auto generator = QRandomGenerator::system();

    for (auto i = 0u; i < cookie.size(); ++i) {
        cookie.at(i) = static_cast<char>(generator->bounded(256));
    }

    return cookie;
}

}

bool xauthority_generate_file(int display, QTemporaryFile& dest)
{
    const QString runtimeDirectory
        = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);

    dest.setFileTemplate(runtimeDirectory + QStringLiteral("/xauth_XXXXXX"));
    if (!dest.open()) {
        return false;
    }

    auto hostname = QSysInfo::machineHostName().toStdString();
    auto displayName = std::to_string(display);
    auto name = std::string("MIT-MAGIC-COOKIE-1");
    auto cookie = xauthority_generate_cookie();

    QDataStream stream(&dest);
    stream.setByteOrder(QDataStream::BigEndian);

    // Write entry with FamilyLocal and the host name as address
    xauthority_write_entry(stream, 256 /* FamilyLocal */, hostname, displayName, name, cookie);

    // Write entry with FamilyWild, no address
    xauthority_write_entry(stream, 65535 /* FamilyWild */, {}, displayName, name, cookie);

    if (stream.status() != QDataStream::Ok || !dest.flush()) {
        dest.remove();
        return false;
    }

    return true;
}

}
