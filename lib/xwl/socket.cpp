/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "socket.h"

#include <base/logging.h>

#include <QCoreApplication>
#include <QFile>
#include <QScopeGuard>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

namespace KWin::xwl
{

struct socket_address {
    sockaddr const* data() const
    {
        return reinterpret_cast<sockaddr const*>(buffer.data());
    }
    int size() const
    {
        return buffer.size();
    }

protected:
    socket_address(std::string const& socket_path)
    {
        int byte_count = offsetof(sockaddr_un, sun_path) + socket_path.size() + 1;
        buffer.resize(byte_count);

        auto address = reinterpret_cast<sockaddr_un*>(buffer.data());
        address->sun_family = AF_UNIX;
    }

    std::vector<std::byte> buffer;
};

struct unix_socket_address : public socket_address {
    unix_socket_address(std::string const& socket_path)
        : socket_address(socket_path)
    {
        auto address = reinterpret_cast<sockaddr_un*>(buffer.data());

        memcpy(address->sun_path, socket_path.c_str(), socket_path.size());
        address->sun_path[socket_path.size()] = '\0';
    }
};

struct abstract_socket_address : public socket_address {
    abstract_socket_address(std::string const& socket_path)
        : socket_address(socket_path)
    {
        auto address = reinterpret_cast<sockaddr_un*>(buffer.data());

        // Abstract domain socket does not need the NUL-termination byte.
        *address->sun_path = '\0';
        memcpy(address->sun_path + 1, socket_path.c_str(), socket_path.size());
    }
};

static std::string lock_file_name_for_display(int display)
{
    return "/tmp/.X" + std::to_string(display) + "-lock";
}

static std::string socket_file_name_for_display(int display)
{
    return "/tmp/.X11-unix/X" + std::to_string(display);
}

static bool try_lock_file(std::string const& file_name)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        QFile lock_file(file_name.c_str());

        if (lock_file.open(QFile::WriteOnly | QFile::NewOnly)) {
            char buffer[12];
            snprintf(buffer, sizeof(buffer), "%10lld\n", QCoreApplication::applicationPid());
            if (lock_file.write(buffer, sizeof(buffer) - 1) != sizeof(buffer) - 1) {
                qCWarning(KWIN_CORE)
                    << "Failed to write pid to lock file:" << lock_file.errorString();
                lock_file.remove();
                return false;
            }
            return true;
        }

        if (lock_file.open(QFile::ReadOnly)) {
            auto const lock_pid = lock_file.readLine().trimmed().toInt();
            if (!lock_pid) {
                return false;
            }
            if (kill(lock_pid, 0) < 0 && errno == ESRCH) {
                // Try to grab the lock file in the next loop iteration.
                lock_file.remove();
            } else {
                return false;
            }
        }
    }

    return false;
}

static int get_socket_fd(socket_address const& address, socket::mode mode)
{
    int socketFlags = SOCK_STREAM;
    if (mode == socket::mode::close_fds_on_exec) {
        socketFlags |= SOCK_CLOEXEC;
    }

    int fd = ::socket(AF_UNIX, socketFlags, 0);
    if (fd == -1) {
        return -1;
    }

    if (bind(fd, address.data(), address.size()) == -1) {
        close(fd);
        return -1;
    }

    if (listen(fd, 1) == -1) {
        close(fd);
        return -1;
    }

    return fd;
}

static bool check_sockets_directory()
{
    struct stat info;
    char const* path = "/tmp/.X11-unix";

    if (lstat(path, &info) != 0) {
        if (errno == ENOENT) {
            qCWarning(KWIN_CORE) << path << "does not exist. Please check your installation";
            return false;
        }

        qCWarning(KWIN_CORE, "Failed to stat %s: %s", path, strerror(errno));
        return false;
    }

    if (!S_ISDIR(info.st_mode)) {
        qCWarning(KWIN_CORE) << path << "is not a directory. Broken system?";
        return false;
    }
    if (info.st_uid != 0 && info.st_uid != getuid()) {
        qCWarning(KWIN_CORE) << path << "is not owned by root or us";
        return false;
    }
    if (!(info.st_mode & S_ISVTX)) {
        qCWarning(KWIN_CORE) << path << "has no sticky bit on. Your system might be compromised!";
        return false;
    }

    return true;
}

socket::socket(socket::mode mode)
{
    if (!check_sockets_directory()) {
        return;
    }

    for (int display = 0; display < 100; ++display) {
        auto const socket_file_path = socket_file_name_for_display(display);
        auto const lock_file_path = lock_file_name_for_display(display);

        if (!try_lock_file(lock_file_path)) {
            continue;
        }

        std::vector<int> file_descriptors;
        auto socketCleanup = qScopeGuard([&file_descriptors]() {
            for (auto const& fd : std::as_const(file_descriptors)) {
                close(fd);
            }
        });

        QFile::remove(socket_file_path.c_str());
        auto fd = get_socket_fd(unix_socket_address(socket_file_path), mode);
        if (fd == -1) {
            QFile::remove(lock_file_path.c_str());
            continue;
        }

        file_descriptors.push_back(fd);

#if defined(Q_OS_LINUX)
        fd = get_socket_fd(abstract_socket_address(socket_file_path), mode);
        if (fd == -1) {
            QFile::remove(lock_file_path.c_str());
            QFile::remove(socket_file_path.c_str());
            continue;
        }

        file_descriptors.push_back(fd);
#endif

        this->file_descriptors = file_descriptors;
        socketCleanup.dismiss();

        this->socket_file_path = socket_file_path;
        this->lock_file_path = lock_file_path;
        this->display = display;
        return;
    }

    qCWarning(KWIN_CORE) << "Failed to find free X11 connection socket";
}

socket::~socket()
{
    for (auto const& fd : file_descriptors) {
        close(fd);
    }

    if (!socket_file_path.empty()) {
        QFile::remove(socket_file_path.c_str());
    }
    if (!lock_file_path.empty()) {
        QFile::remove(lock_file_path.c_str());
    }
}

}
