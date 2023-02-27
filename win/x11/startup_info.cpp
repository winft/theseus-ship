/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "startup_info.h"

#include "net/win_info.h"

#include "base/logging.h"

#include <QDateTime>

#include <sys/time.h>
#include <unistd.h>

#include <QTimer>
#include <stdlib.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <private/qtx11extras_p.h>
#else
#include <QX11Info>
#endif

#include <QCoreApplication>
#include <QDebug>
#include <QStandardPaths>
#include <signal.h>

namespace KWin::win::x11
{
static const char NET_STARTUP_MSG[] = "_NET_STARTUP_INFO";

// DESKTOP_STARTUP_ID is used also in kinit/wrapper.c ,
// kdesu in both kdelibs and kdebase and who knows where else
static const char NET_STARTUP_ENV[] = "DESKTOP_STARTUP_ID";

static QByteArray s_startup_id;

static long get_num(const QString& item_P);
static QString get_str(const QString& item_P);
static QByteArray get_cstr(const QString& item_P);
static QStringList get_fields(const QString& txt_P);
static QString escape_str(const QString& str_P);

class Q_DECL_HIDDEN startup_info::Data : public startup_info_data
{
public:
    Data()
        : age(0)
    {
    } // just because it's in a QMap
    Data(const QString& txt_P)
        : startup_info_data(txt_P)
        , age(0)
    {
    }
    unsigned int age;
};

struct Q_DECL_HIDDEN startup_info_id::Private {
    Private()
        : id("")
    {
    }

    QString to_text() const;

    QByteArray id; // id
};

struct Q_DECL_HIDDEN startup_info_data::Private {
    Private()
        : desktop(0)
        , wmclass("")
        , hostname("")
        , silent(startup_info_data::Unknown)
        , screen(-1)
        , xinerama(-1)
    {
    }

    QString to_text() const;
    void remove_pid(pid_t pid);

    QString bin;
    QString name;
    QString description;
    QString icon;
    int desktop;
    QList<pid_t> pids;
    QByteArray wmclass;
    QByteArray hostname;
    startup_info_data::TriState silent;
    int screen;
    int xinerama;
    QString application_id;
};

class Q_DECL_HIDDEN startup_info::Private
{
public:
    void init(int flags);

    startup_t check_startup_internal(WId w, startup_info_id* id, startup_info_data* data);

    static QString
    check_required_startup_fields(const QString& msg, const startup_info_data& data, int screen);

    startup_info* q;
    unsigned int timeout;
    QMap<startup_info_id, startup_info::Data> startups;
    // contains silenced ASN's only if !AnnounceSilencedChanges
    QMap<startup_info_id, startup_info::Data> silent_startups;
    // contains ASN's that had change: but no new: yet
    QMap<startup_info_id, startup_info::Data> uninited_startups;
    //    KXMessages msgs;
    QTimer* cleanup;
    int flags;

    Private(int flags_P, startup_info* qq)
        : q(qq)
        , timeout(60)
        , cleanup(nullptr)
        , flags(flags_P)
    {
    }
};

startup_info::startup_info(int flags_P, QObject* parent_P)
    : QObject(parent_P)
    , d(new Private(flags_P, this))
{
    //    d->createConnections();
}

startup_info::~startup_info()
{
    delete d;
}

// if the application stops responding for a while, KWindowSystem may get
// the information about the already mapped window before KXMessages
// actually gets the info about the started application (depends
// on their order in the native x11 event filter)
// simply delay info from KWindowSystem a bit
// SELI???
namespace
{

class DelayedWindowEvent : public QEvent
{
public:
    DelayedWindowEvent(WId w_P)
        : QEvent(uniqueType())
        , w(w_P)
    {
    }
    WId w;
    static Type uniqueType()
    {
        return Type(QEvent::User + 15);
    }
};

}

QString startup_info::Private::check_required_startup_fields(const QString& msg,
                                                             const startup_info_data& data_P,
                                                             int screen)
{
    QString ret = msg;
    if (data_P.name().isEmpty()) {
        //        qWarning() << "NAME not specified in initial startup message";
        QString name = data_P.bin();
        if (name.isEmpty()) {
            name = QStringLiteral("UNKNOWN");
        }
        ret += QStringLiteral(" NAME=\"%1\"").arg(escape_str(name));
    }
    if (data_P.screen() == -1) { // add automatically if needed
        ret += QStringLiteral(" SCREEN=%1").arg(screen);
    }
    return ret;
}

startup_info::startup_t
startup_info::checkStartup(WId w_P, startup_info_id& id_O, startup_info_data& data_O)
{
    return d->check_startup_internal(w_P, &id_O, &data_O);
}

startup_info::startup_t startup_info::Private::check_startup_internal(WId /*w_P*/,
                                                                      startup_info_id* /*id_O*/,
                                                                      startup_info_data* /*data_O*/)
{
    // TODO(romangg): Implement?
    return NoMatch;
}

QByteArray startup_info::windowStartupId(WId w_P)
{
    if (!QX11Info::isPlatformX11()) {
        return QByteArray();
    }
    net::win_info info(QX11Info::connection(),
                       w_P,
                       QX11Info::appRootWindow(),
                       net::Properties(),
                       net::WM2StartupId | net::WM2GroupLeader);
    QByteArray ret = info.startupId();
    if (ret.isEmpty() && info.groupLeader() != XCB_WINDOW_NONE) {
        // retry with window group leader, as the spec says
        net::win_info groupLeaderInfo(QX11Info::connection(),
                                      info.groupLeader(),
                                      QX11Info::appRootWindow(),
                                      net::Properties(),
                                      net::Properties2());
        ret = groupLeaderInfo.startupId();
    }
    return ret;
}

const QByteArray& startup_info_id::id() const
{
    return d->id;
}

QString startup_info_id::Private::to_text() const
{
    return QStringLiteral(" ID=\"%1\" ").arg(escape_str(id));
}

startup_info_id::startup_info_id(const QString& txt_P)
    : d(new Private)
{
    const QStringList items = get_fields(txt_P);
    for (QStringList::ConstIterator it = items.begin(); it != items.end(); ++it) {
        if ((*it).startsWith(QLatin1String("ID="))) {
            d->id = get_cstr(*it);
        }
    }
}

startup_info_id::startup_info_id()
    : d(new Private)
{
}

startup_info_id::~startup_info_id()
{
    delete d;
}

startup_info_id::startup_info_id(const startup_info_id& id_P)
    : d(new Private(*id_P.d))
{
}

startup_info_id& startup_info_id::operator=(const startup_info_id& id_P)
{
    if (&id_P == this) {
        return *this;
    }
    *d = *id_P.d;
    return *this;
}

bool startup_info_id::operator==(const startup_info_id& id_P) const
{
    return id() == id_P.id();
}

bool startup_info_id::operator!=(const startup_info_id& id_P) const
{
    return !(*this == id_P);
}

// needed for QMap
bool startup_info_id::operator<(const startup_info_id& id_P) const
{
    return id() < id_P.id();
}

bool startup_info_id::isNull() const
{
    return d->id.isEmpty() || d->id == "0";
}

unsigned long startup_info_id::timestamp() const
{
    if (isNull()) {
        return 0;
    }
    // As per the spec, the ID must contain the _TIME followed by the timestamp
    int pos = d->id.lastIndexOf("_TIME");
    if (pos >= 0) {
        bool ok;
        unsigned long time = QString(d->id.mid(pos + 5)).toULong(&ok);
        if (!ok && d->id[pos + 5] == '-') { // try if it's as a negative signed number perhaps
            time = QString(d->id.mid(pos + 5)).toLong(&ok);
        }
        if (ok) {
            return time;
        }
    }
    return 0;
}

QString startup_info_data::Private::to_text() const
{
    QString ret;
    // prepare some space which should be always enough.
    // No need to squeze at the end, as the result is only used as intermediate string
    ret.reserve(256);
    if (!bin.isEmpty()) {
        ret += QStringLiteral(" BIN=\"%1\"").arg(escape_str(bin));
    }
    if (!name.isEmpty()) {
        ret += QStringLiteral(" NAME=\"%1\"").arg(escape_str(name));
    }
    if (!description.isEmpty()) {
        ret += QStringLiteral(" DESCRIPTION=\"%1\"").arg(escape_str(description));
    }
    if (!icon.isEmpty()) {
        ret += QStringLiteral(" ICON=\"%1\"").arg(icon);
    }
    if (desktop != 0) {
        ret += QStringLiteral(" DESKTOP=%1")
                   .arg(desktop == net::OnAllDesktops ? net::OnAllDesktops
                                                      : desktop - 1); // spec counts from 0
    }
    if (!wmclass.isEmpty()) {
        ret += QStringLiteral(" WMCLASS=\"%1\"").arg(QString(wmclass));
    }
    if (!hostname.isEmpty()) {
        ret += QStringLiteral(" HOSTNAME=%1").arg(QString(hostname));
    }
    for (QList<pid_t>::ConstIterator it = pids.begin(); it != pids.end(); ++it) {
        ret += QStringLiteral(" PID=%1").arg(*it);
    }
    if (silent != startup_info_data::Unknown) {
        ret += QStringLiteral(" SILENT=%1").arg(silent == startup_info_data::Yes ? 1 : 0);
    }
    if (screen != -1) {
        ret += QStringLiteral(" SCREEN=%1").arg(screen);
    }
    if (xinerama != -1) {
        ret += QStringLiteral(" XINERAMA=%1").arg(xinerama);
    }
    if (!application_id.isEmpty()) {
        ret += QStringLiteral(" APPLICATION_ID=\"%1\"").arg(application_id);
    }
    return ret;
}

startup_info_data::startup_info_data(const QString& txt_P)
    : d(new Private)
{
    const QStringList items = get_fields(txt_P);
    for (QStringList::ConstIterator it = items.begin(); it != items.end(); ++it) {
        if ((*it).startsWith(QLatin1String("BIN="))) {
            d->bin = get_str(*it);
        } else if ((*it).startsWith(QLatin1String("NAME="))) {
            d->name = get_str(*it);
        } else if ((*it).startsWith(QLatin1String("DESCRIPTION="))) {
            d->description = get_str(*it);
        } else if ((*it).startsWith(QLatin1String("ICON="))) {
            d->icon = get_str(*it);
        } else if ((*it).startsWith(QLatin1String("DESKTOP="))) {
            d->desktop = get_num(*it);
            if (d->desktop != net::OnAllDesktops) {
                ++d->desktop; // spec counts from 0
            }
        } else if ((*it).startsWith(QLatin1String("WMCLASS="))) {
            d->wmclass = get_cstr(*it);
        } else if ((*it).startsWith(QLatin1String("HOSTNAME="))) { // added to version 1 (2014)
            d->hostname = get_cstr(*it);
        } else if ((*it).startsWith(QLatin1String("PID="))) { // added to version 1 (2014)
            addPid(get_num(*it));
        } else if ((*it).startsWith(QLatin1String("SILENT="))) {
            d->silent = get_num(*it) != 0 ? Yes : No;
        } else if ((*it).startsWith(QLatin1String("SCREEN="))) {
            d->screen = get_num(*it);
        } else if ((*it).startsWith(QLatin1String("XINERAMA="))) {
            d->xinerama = get_num(*it);
        } else if ((*it).startsWith(QLatin1String("APPLICATION_ID="))) {
            d->application_id = get_str(*it);
        }
    }
}

startup_info_data::startup_info_data(const startup_info_data& data)
    : d(new Private(*data.d))
{
}

startup_info_data& startup_info_data::operator=(const startup_info_data& data)
{
    if (&data == this) {
        return *this;
    }
    *d = *data.d;
    return *this;
}

void startup_info_data::update(const startup_info_data& data_P)
{
    if (!data_P.bin().isEmpty()) {
        d->bin = data_P.bin();
    }
    if (!data_P.name().isEmpty() && name().isEmpty()) { // don't overwrite
        d->name = data_P.name();
    }
    if (!data_P.description().isEmpty() && description().isEmpty()) { // don't overwrite
        d->description = data_P.description();
    }
    if (!data_P.icon().isEmpty() && icon().isEmpty()) { // don't overwrite
        d->icon = data_P.icon();
    }
    if (data_P.desktop() != 0 && desktop() == 0) { // don't overwrite
        d->desktop = data_P.desktop();
    }
    if (!data_P.d->wmclass.isEmpty()) {
        d->wmclass = data_P.d->wmclass;
    }
    if (!data_P.d->hostname.isEmpty()) {
        d->hostname = data_P.d->hostname;
    }
    for (QList<pid_t>::ConstIterator it = data_P.d->pids.constBegin();
         it != data_P.d->pids.constEnd();
         ++it) {
        addPid(*it);
    }
    if (data_P.silent() != Unknown) {
        d->silent = data_P.silent();
    }
    if (data_P.screen() != -1) {
        d->screen = data_P.screen();
    }
    if (data_P.xinerama() != -1 && xinerama() != -1) { // don't overwrite
        d->xinerama = data_P.xinerama();
    }
    if (!data_P.applicationId().isEmpty() && applicationId().isEmpty()) { // don't overwrite
        d->application_id = data_P.applicationId();
    }
}

startup_info_data::startup_info_data()
    : d(new Private)
{
}

startup_info_data::~startup_info_data()
{
    delete d;
}

const QString& startup_info_data::bin() const
{
    return d->bin;
}

const QString& startup_info_data::name() const
{
    return d->name;
}

const QString& startup_info_data::description() const
{
    return d->description;
}

const QString& startup_info_data::icon() const
{
    return d->icon;
}

int startup_info_data::desktop() const
{
    return d->desktop;
}

QByteArray startup_info_data::WMClass() const
{
    return d->wmclass;
}

QByteArray startup_info_data::hostname() const
{
    return d->hostname;
}

void startup_info_data::addPid(pid_t pid_P)
{
    if (!d->pids.contains(pid_P)) {
        d->pids.append(pid_P);
    }
}

void startup_info_data::Private::remove_pid(pid_t pid_P)
{
    pids.removeAll(pid_P);
}

QList<pid_t> startup_info_data::pids() const
{
    return d->pids;
}

bool startup_info_data::is_pid(pid_t pid_P) const
{
    return d->pids.contains(pid_P);
}

startup_info_data::TriState startup_info_data::silent() const
{
    return d->silent;
}

int startup_info_data::screen() const
{
    return d->screen;
}

int startup_info_data::xinerama() const
{
    return d->xinerama;
}

QString startup_info_data::applicationId() const
{
    return d->application_id;
}

static long get_num(const QString& item_P)
{
    unsigned int pos = item_P.indexOf(QLatin1Char('='));
    return item_P.mid(pos + 1).toLong();
}

static QString get_str(const QString& item_P)
{
    int pos = item_P.indexOf(QLatin1Char('='));
    return item_P.mid(pos + 1);
}

static QByteArray get_cstr(const QString& item_P)
{
    return get_str(item_P).toUtf8();
}

static QStringList get_fields(const QString& txt_P)
{
    QString txt = txt_P.simplified();
    QStringList ret;
    QString item;
    bool in = false;
    bool escape = false;
    for (int pos = 0; pos < txt.length(); ++pos) {
        if (escape) {
            item += txt[pos];
            escape = false;
        } else if (txt[pos] == QLatin1Char('\\')) {
            escape = true;
        } else if (txt[pos] == QLatin1Char('\"')) {
            in = !in;
        } else if (txt[pos] == QLatin1Char(' ') && !in) {
            ret.append(item);
            item = QString();
        } else {
            item += txt[pos];
        }
    }
    ret.append(item);
    return ret;
}

static QString escape_str(const QString& str_P)
{
    QString ret;
    // prepare some space which should be always enough.
    // No need to squeze at the end, as the result is only used as intermediate string
    ret.reserve(str_P.size() * 2);
    for (int pos = 0; pos < str_P.length(); ++pos) {
        if (str_P[pos] == QLatin1Char('\\') || str_P[pos] == QLatin1Char('"')) {
            ret += QLatin1Char('\\');
        }
        ret += str_P[pos];
    }
    return ret;
}

}

#include "moc_startup_info.cpp"
