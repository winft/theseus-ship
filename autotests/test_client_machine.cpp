/*
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "integration/lib/setup.h"

#include "base/x11/xcb/window.h"
#include "win/x11/client_machine.h"

#include <catch2/generators/catch_generators.hpp>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace KWin::detail::test
{

TEST_CASE("client machine", "[win],[xwl]")
{
    test::setup setup("client-machine", base::operation_mode::xwayland);
    setup.start();

#ifdef HOST_NAME_MAX
    char hostnamebuf[HOST_NAME_MAX];
#else
    char hostnamebuf[256];
#endif

    QByteArray host_name;
    QByteArray fqdn;

    if (gethostname(hostnamebuf, sizeof hostnamebuf) >= 0) {
        hostnamebuf[sizeof(hostnamebuf) - 1] = 0;
        host_name = hostnamebuf;
    }

    addrinfo* res;
    addrinfo addressHints;
    memset(&addressHints, 0, sizeof(addressHints));
    addressHints.ai_family = PF_UNSPEC;
    addressHints.ai_socktype = SOCK_STREAM;
    addressHints.ai_flags |= AI_CANONNAME;
    if (getaddrinfo(host_name.constData(), nullptr, &addressHints, &res) == 0) {
        if (res->ai_canonname) {
            fqdn = QByteArray(res->ai_canonname);
        }
    }

    freeaddrinfo(res);

    auto setClientMachineProperty = [&](xcb_window_t window, const QByteArray& hostname) {
        xcb_change_property(setup.base->x11_data.connection,
                            XCB_PROP_MODE_REPLACE,
                            window,
                            XCB_ATOM_WM_CLIENT_MACHINE,
                            XCB_ATOM_STRING,
                            8,
                            hostname.length(),
                            hostname.constData());
    };

    SECTION("host name")
    {
        struct data {
            std::string host_name;
            std::string expected_host;
            bool local;
        };

        auto cutted_host_name = host_name;
        auto cutted_fqdn = fqdn;
        cutted_host_name.remove(0, 1);
        cutted_fqdn.remove(0, 1);

        auto test_data = GENERATE_COPY(
            data{{}, "localhost", true},
            data{"localhost", "localhost", true},
            data{host_name.toStdString(), host_name.toStdString(), true},
            data{host_name.toUpper().toStdString(), host_name.toUpper().toStdString(), true},
            data{cutted_host_name.toStdString(), cutted_host_name.toStdString(), false},
            data{"random.name.not.exist.tld", "random.name.not.exist.tld", false},
            data{fqdn.toStdString(), fqdn.toStdString(), true},
            data{fqdn.toUpper().toStdString(), fqdn.toUpper().toStdString(), true},
            data{cutted_fqdn.toStdString(), cutted_fqdn.toStdString(), false});

        QRect const geometry(0, 0, 10, 10);
        uint32_t const values[] = {true};
        base::x11::xcb::window window(setup.base->x11_data.connection,
                                      setup.base->x11_data.root_window,
                                      geometry,
                                      XCB_WINDOW_CLASS_INPUT_ONLY,
                                      XCB_CW_OVERRIDE_REDIRECT,
                                      values);
        setClientMachineProperty(window, test_data.host_name.c_str());

        win::x11::client_machine clientMachine;
        QSignalSpy spy(&clientMachine, &win::x11::client_machine::localhostChanged);

        base::x11::data data;
        data.connection = setup.base->x11_data.connection;
        data.root_window = setup.base->x11_data.root_window;
        clientMachine.resolve(data, window, XCB_WINDOW_NONE);
        REQUIRE(clientMachine.hostname() == QByteArray::fromStdString(test_data.expected_host));

        int i = 0;
        while (clientMachine.is_resolving() && i++ < 50) {
            // name is being resolved in an external thread, so let's wait a little bit
            QTest::qWait(250);
        }

        QCOMPARE(clientMachine.is_local(), test_data.local);
        QCOMPARE(spy.isEmpty(), !test_data.local);
    };

    SECTION("empty host name")
    {
        QRect const geometry(0, 0, 10, 10);
        uint32_t const values[] = {true};
        base::x11::xcb::window window(setup.base->x11_data.connection,
                                      setup.base->x11_data.root_window,
                                      geometry,
                                      XCB_WINDOW_CLASS_INPUT_ONLY,
                                      XCB_CW_OVERRIDE_REDIRECT,
                                      values);
        win::x11::client_machine clientMachine;
        QSignalSpy spy(&clientMachine, &win::x11::client_machine::localhostChanged);

        base::x11::data data;
        data.connection = setup.base->x11_data.connection;
        data.root_window = setup.base->x11_data.root_window;
        clientMachine.resolve(data, window, XCB_WINDOW_NONE);
        QCOMPARE(clientMachine.hostname(), win::x11::client_machine::localhost());
        QVERIFY(clientMachine.is_local());
        // should be local
        QCOMPARE(spy.isEmpty(), false);
    }
}

}
