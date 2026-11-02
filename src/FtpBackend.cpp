#include "FtpBackend.h"

#include <QFile>
#include <QRegularExpression>
#include <QThread>
#include <QWaitCondition>

#include <thread>
#include <QUrl>

#include <curl/curl.h>

class CurlHandle {
public:
    explicit CurlHandle(FtpBackend* b) : m_b(b), m_h(b->acquire()) {}
    ~CurlHandle() { m_b->release(m_h); }
    operator CURL*() const { return m_h; }

    // Runs the transfer and throws a readable error on failure.
    void perform(const QString& what)
    {
        char err[CURL_ERROR_SIZE] = {0};
        curl_easy_setopt(m_h, CURLOPT_ERRORBUFFER, err);
        const CURLcode rc = curl_easy_perform(m_h);
        curl_easy_setopt(m_h, CURLOPT_ERRORBUFFER, nullptr);
        if (rc == CURLE_OK)
            return;
        if (rc == CURLE_ABORTED_BY_CALLBACK)
            throw Cancelled();

        const FtpProfile& p = m_b->profile();
        QString msg;
        switch (rc) {
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_HOST:
            msg = QStringLiteral("Не удалось подключиться к %1:%2. Проверьте, что FTP-сервер на устройстве "
                                 "запущен, а IP-адрес и порт указаны верно.").arg(p.host).arg(p.port);
            break;
        case CURLE_OPERATION_TIMEDOUT:
            msg = QStringLiteral("Устройство не отвечает (%1:%2).").arg(p.host).arg(p.port);
            break;
        case CURLE_LOGIN_DENIED:
            msg = QStringLiteral("Неверный логин или пароль.");
            break;
        case CURLE_REMOTE_FILE_NOT_FOUND:
            msg = QStringLiteral("Файл или папка не найдены.");
            break;
        case CURLE_REMOTE_ACCESS_DENIED:
            msg = QStringLiteral("Сервер запретил доступ.");
            break;
        case CURLE_QUOTE_ERROR:
            msg = QStringLiteral("Сервер отказал в выполнении команды.");
            break;
        case CURLE_REMOTE_DISK_FULL:
            msg = QStringLiteral("На устройстве закончилось место.");
            break;
        default:
            msg = QString::fromUtf8(err[0] ? err : curl_easy_strerror(rc));
        }
        throw FsError(what.isEmpty() ? msg : what + QStringLiteral(": ") + msg);
    }

private:
    FtpBackend* m_b;
    CURL* m_h;
};

namespace {

size_t appendCb(char* ptr, size_t size, size_t n, void* ud)
{
    static_cast<QByteArray*>(ud)->append(ptr, qsizetype(size * n));
    return size * n;
}

struct LimitedBuffer {
    QByteArray data;
    qint64 max = 0;
};

size_t limitedCb(char* ptr, size_t size, size_t n, void* ud)
{
    auto* b = static_cast<LimitedBuffer*>(ud);
    const qint64 room = b->max - b->data.size();
    if (room <= 0)
        return 0;   // stop the transfer
    b->data.append(ptr, qsizetype(qMin<qint64>(room, qint64(size * n))));
    return b->data.size() >= b->max ? 0 : size * n;
}

size_t fileWriteCb(char* ptr, size_t size, size_t n, void* ud)
{
    return size_t(static_cast<QFile*>(ud)->write(ptr, qint64(size * n)));
}

struct ReadCtx {
    QFile* file;
    qint64 remaining;
};

size_t fileReadCb(char* buf, size_t size, size_t n, void* ud)
{
    auto* c = static_cast<ReadCtx*>(ud);
    const qint64 want = qMin<qint64>(qint64(size * n), c->remaining);
    if (want <= 0)
        return 0;
    const qint64 got = c->file->read(buf, want);
    if (got < 0)
        return CURL_READFUNC_ABORT;
    c->remaining -= got;
    return size_t(got);
}

struct XferCtx {
    const Progress* cb;
    bool upload;
};

int xferCb(void* ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    auto* c = static_cast<XferCtx*>(ud);
    if (!c->cb || !*c->cb)
        return 0;
    const qint64 done = c->upload ? ulnow : dlnow;
    const qint64 total = c->upload ? ultotal : dltotal;
    return (*c->cb)(done, total > 0 ? total : -1) ? 0 : 1;
}

QByteArray encodeSegment(const QString& s)
{
    return QUrl::toPercentEncoding(s);
}

const QStringList kMonths = {
    QStringLiteral("jan"), QStringLiteral("feb"), QStringLiteral("mar"), QStringLiteral("apr"),
    QStringLiteral("may"), QStringLiteral("jun"), QStringLiteral("jul"), QStringLiteral("aug"),
    QStringLiteral("sep"), QStringLiteral("oct"), QStringLiteral("nov"), QStringLiteral("dec"),
};

// "drwxr-xr-x 1 owner group 4096 Jan 01 12:00 name with spaces"
bool parseUnixLine(const QString& line, Entry& e)
{
    if (line.size() < 10 || !QStringLiteral("-dlbcps").contains(line[0]))
        return false;

    struct Tok { int start; int len; };
    QList<Tok> toks;
    for (int i = 0, n = int(line.size()); i < n;) {
        while (i < n && line[i].isSpace())
            ++i;
        if (i >= n)
            break;
        const int s = i;
        while (i < n && !line[i].isSpace())
            ++i;
        toks.append({s, i - s});
    }
    auto tok = [&](int k) { return line.mid(toks[k].start, toks[k].len); };

    for (int k = 2; k + 3 < toks.size(); ++k) {
        const QString mon = tok(k).toLower();
        const int month = kMonths.indexOf(mon.left(3));
        if (month < 0 || mon.size() != 3)
            continue;
        bool okSize = false, okDay = false;
        const qint64 size = tok(k - 1).toLongLong(&okSize);
        const int day = tok(k + 1).toInt(&okDay);
        if (!okSize || !okDay)
            continue;

        const QString timeOrYear = tok(k + 2);
        const QDate today = QDate::currentDate();
        QDate date;
        QTime time(0, 0);
        if (timeOrYear.contains(QLatin1Char(':'))) {
            time = QTime::fromString(timeOrYear, QStringLiteral("H:mm"));
            date = QDate(today.year(), month + 1, day);
            if (date > today.addDays(2))
                date = date.addYears(-1);
        } else {
            date = QDate(timeOrYear.toInt(), month + 1, day);
        }

        QString name = line.mid(toks[k + 3].start);
        e.isDir = line[0] == QLatin1Char('d') || line[0] == QLatin1Char('l');
        e.isLink = line[0] == QLatin1Char('l');
        if (line[0] == QLatin1Char('l')) {
            const int arrow = int(name.indexOf(QLatin1String(" -> ")));
            if (arrow > 0)
                name = name.left(arrow);
        }
        e.name = name;
        e.size = e.isDir ? -1 : size;
        e.mtime = QDateTime(date, time.isValid() ? time : QTime(0, 0));
        e.hidden = name.startsWith(QLatin1Char('.'));
        return true;
    }
    return false;
}

// "01-31-24  10:15AM  <DIR>  name"
bool parseDosLine(const QString& line, Entry& e)
{
    static const QRegularExpression re(QStringLiteral(
        "^(\\d{2})-(\\d{2})-(\\d{2,4})\\s+(\\d{1,2}):(\\d{2})([AaPp][Mm])?\\s+(<DIR>|\\d+)\\s+(.+)$"));
    const auto m = re.match(line);
    if (!m.hasMatch())
        return false;
    int year = m.captured(3).toInt();
    if (year < 100)
        year += year < 70 ? 2000 : 1900;
    int hour = m.captured(4).toInt();
    const QString ampm = m.captured(6).toUpper();
    if (ampm == QLatin1String("PM") && hour < 12)
        hour += 12;
    if (ampm == QLatin1String("AM") && hour == 12)
        hour = 0;
    e.isDir = m.captured(7) == QLatin1String("<DIR>");
    e.size = e.isDir ? -1 : m.captured(7).toLongLong();
    e.name = m.captured(8);
    e.mtime = QDateTime(QDate(year, m.captured(1).toInt(), m.captured(2).toInt()), QTime(hour, m.captured(5).toInt()));
    e.hidden = e.name.startsWith(QLatin1Char('.'));
    return true;
}

} // namespace

FtpBackend::FtpBackend(const FtpProfile& profile) : m_p(profile)
{
    if (m_p.user.isEmpty())
        m_p.user = QStringLiteral("anonymous");
}

FtpBackend::~FtpBackend()
{
    for (CURL* h : std::as_const(m_pool))
        curl_easy_cleanup(h);
}

CURL* FtpBackend::acquire()
{
    CURL* h = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_pool.isEmpty())
            h = m_pool.takeLast();
    }
    if (h)
        curl_easy_reset(h);   // keeps the live connection
    else
        h = curl_easy_init();

    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(h, CURLOPT_SERVER_RESPONSE_TIMEOUT, 30L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(h, CURLOPT_FTP_USE_EPSV, 0L);   // console servers often break on EPSV
    curl_easy_setopt(h, CURLOPT_FTP_FILEMETHOD, long(CURLFTPMETHOD_SINGLECWD));
    curl_easy_setopt(h, CURLOPT_USERNAME, m_p.user.toUtf8().constData());
    curl_easy_setopt(h, CURLOPT_PASSWORD, m_p.password.toUtf8().constData());
    return h;
}

void FtpBackend::release(CURL* h)
{
    QMutexLocker lock(&m_mutex);
    if (m_pool.size() < 4)
        m_pool.append(h);
    else
        curl_easy_cleanup(h);
}

QByteArray FtpBackend::urlFor(const QString& path, bool isDir) const
{
    // "%2F" makes the path absolute instead of relative to the login folder.
    QByteArray url = "ftp://" + m_p.host.toUtf8() + ':' + QByteArray::number(m_p.port) + "/%2F";
    QByteArrayList segs;
    for (const QString& s : path.split(QLatin1Char('/'), Qt::SkipEmptyParts))
        segs.append(encodeSegment(s));
    url += segs.join('/');
    if (isDir)
        url += '/';
    return url;
}

QString FtpBackend::mediaUrl(const QString& path) const
{
    // ffmpeg does not percent-decode FTP paths, so the path goes in as is.
    // It also fails on an empty password, so anonymous logins are left out entirely.
    QString url = QStringLiteral("ftp://");
    if (m_p.user != QLatin1String("anonymous") || !m_p.password.isEmpty()) {
        url += QString::fromUtf8(QUrl::toPercentEncoding(m_p.user));
        if (!m_p.password.isEmpty())
            url += QLatin1Char(':') + QString::fromUtf8(QUrl::toPercentEncoding(m_p.password));
        url += QLatin1Char('@');
    }
    url += m_p.host + QLatin1Char(':') + QString::number(m_p.port);
    if (!path.startsWith(QLatin1Char('/')))
        url += QLatin1Char('/');
    return url + path;
}

QList<Entry> FtpBackend::parseListing(const QByteArray& data)
{
    QList<Entry> result;
    const auto lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.trimmed().isEmpty() || line.startsWith(QLatin1String("total ")))
            continue;
        Entry e;
        if (!parseUnixLine(line, e) && !parseDosLine(line, e))
            continue;
        if (e.name == QLatin1String(".") || e.name == QLatin1String(".."))
            continue;
        result.append(e);
    }
    return result;
}

QList<Entry> FtpBackend::list(const QString& dir)
{
    CurlHandle h(this);
    QByteArray data;
    const QByteArray url = urlFor(dir, true);
    curl_easy_setopt(h, CURLOPT_URL, url.constData());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, appendCb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &data);
    h.perform(QString());
    return parseListing(data);
}

void FtpBackend::quote(const QStringList& commands)
{
    CurlHandle h(this);
    curl_slist* cmds = nullptr;
    for (const QString& c : commands)
        cmds = curl_slist_append(cmds, c.toUtf8().constData());
    const QByteArray url = urlFor(QStringLiteral("/"), true);
    curl_easy_setopt(h, CURLOPT_URL, url.constData());
    curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(h, CURLOPT_QUOTE, cmds);
    try {
        h.perform(QString());
    } catch (...) {
        curl_easy_setopt(h, CURLOPT_QUOTE, nullptr);
        curl_slist_free_all(cmds);
        throw;
    }
    curl_easy_setopt(h, CURLOPT_QUOTE, nullptr);
    curl_slist_free_all(cmds);
}

void FtpBackend::mkdir(const QString& path) { quote({QStringLiteral("MKD ") + path}); }
void FtpBackend::removeFile(const QString& path) { quote({QStringLiteral("DELE ") + path}); }
void FtpBackend::removeDir(const QString& path) { quote({QStringLiteral("RMD ") + path}); }

void FtpBackend::rename(const QString& from, const QString& to)
{
    quote({QStringLiteral("RNFR ") + from, QStringLiteral("RNTO ") + to});
}

void FtpBackend::removeRecursive(const QString& path)
{
    // Collect everything first, then delete in batches over a single connection.
    QStringList commands;
    std::function<void(const QString&)> walk = [&](const QString& dir) {
        for (const Entry& e : list(dir)) {
            const QString child = joinPath(dir, e.name);
            if (e.isDir)
                walk(child);
            else
                commands.append(QStringLiteral("DELE ") + child);
        }
        commands.append(QStringLiteral("RMD ") + dir);
    };
    walk(path);
    for (int i = 0; i < commands.size(); i += 40)
        quote(commands.mid(i, 40));
}

void FtpBackend::download(const QString& path, const QString& localFile, const Progress& cb, bool append)
{
    QFile f(localFile);
    if (!f.open(append ? QIODevice::Append : (QIODevice::WriteOnly | QIODevice::Truncate)))
        throw FsError(QStringLiteral("Не удалось создать %1: %2").arg(localFile, f.errorString()));

    CurlHandle h(this);
    XferCtx ctx{&cb, false};
    const QByteArray url = urlFor(path, false);
    curl_easy_setopt(h, CURLOPT_URL, url.constData());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, fileWriteCb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &f);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xferCb);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &ctx);
    try {
        h.perform(QString());
    } catch (...) {
        f.close();
        if (!append)
            f.remove();
        throw;
    }
}

void FtpBackend::upload(const QString& localFile, const QString& path, const Progress& cb,
                        qint64 offset, qint64 length)
{
    QFile f(localFile);
    if (!f.open(QIODevice::ReadOnly))
        throw FsError(QStringLiteral("Не удалось открыть %1: %2").arg(localFile, f.errorString()));
    if (offset > 0)
        f.seek(offset);
    const qint64 total = length < 0 ? f.size() - offset : length;

    CurlHandle h(this);
    ReadCtx rctx{&f, total};
    XferCtx xctx{&cb, true};
    const QByteArray url = urlFor(path, false);
    curl_easy_setopt(h, CURLOPT_URL, url.constData());
    curl_easy_setopt(h, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(h, CURLOPT_INFILESIZE_LARGE, curl_off_t(total));
    curl_easy_setopt(h, CURLOPT_READFUNCTION, fileReadCb);
    curl_easy_setopt(h, CURLOPT_READDATA, &rctx);
    curl_easy_setopt(h, CURLOPT_FTP_CREATE_MISSING_DIRS, long(CURLFTP_CREATE_DIR));
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xferCb);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &xctx);
    h.perform(QString());
}

QByteArray FtpBackend::read(const QString& path, qint64 maxBytes)
{
    CurlHandle h(this);
    LimitedBuffer buf;
    buf.max = maxBytes;
    const QByteArray url = urlFor(path, false);
    curl_easy_setopt(h, CURLOPT_URL, url.constData());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, limitedCb);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &buf);
    try {
        h.perform(QString());
    } catch (const Cancelled&) {
        throw;
    } catch (const FsError&) {
        if (buf.data.size() < buf.max)
            throw;
    }
    return buf.data;
}

bool FtpBackend::skipInScan(const QString& path) const
{
    static const QStringList ps3 = {
        QStringLiteral("/dev_flash"), QStringLiteral("/dev_blind"), QStringLiteral("/app_home"),
        QStringLiteral("/host_root"), QStringLiteral("/dev_bdvd"), QStringLiteral("/dev_hdd1"),
        QStringLiteral("/dev_ps2disc"),
    };
    static const QStringList vita = {
        QStringLiteral("/vs0:"), QStringLiteral("/os0:"), QStringLiteral("/sa0:"), QStringLiteral("/pd0:"),
        QStringLiteral("/tm0:"), QStringLiteral("/vd0:"), QStringLiteral("/ud0:"), QStringLiteral("/gro0:"),
        QStringLiteral("/grw0:"), QStringLiteral("/host0:"),
    };
    const QStringList* rules = m_p.kind == DeviceKind::PS3 ? &ps3 : (m_p.kind == DeviceKind::Vita ? &vita : nullptr);
    if (path.count(QLatin1Char('/')) > 48)
        return true;   // runaway depth, most likely a link loop
    if (!rules)
        return false;
    for (const QString& r : *rules) {
        if (path.startsWith(r, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

SizeMap FtpBackend::scanTree(const QString& root, const std::atomic_bool* cancel, const ScanProgress& progress)
{
    // Several workers share one queue of folders; each lists over its own pooled connection.
    struct Shared {
        QMutex mutex;
        QWaitCondition wake;
        QStringList queue;
        int active = 0;
        SizeMap own;
        qint64 files = 0;
        QString rootError;
    } s;
    s.own.insert(root, 0);
    s.queue << root;
    auto cancelled = [cancel] { return cancel && cancel->load(); };

    auto work = [&] {
        for (;;) {
            QString dir;
            {
                QMutexLocker lock(&s.mutex);
                while (s.queue.isEmpty() && s.active > 0 && !cancelled())
                    s.wake.wait(&s.mutex, QDeadlineTimer(200));
                if (cancelled() || s.queue.isEmpty()) {
                    s.wake.wakeAll();
                    return;
                }
                dir = s.queue.takeLast();
                ++s.active;
            }

            QList<Entry> entries;
            QString error;
            for (int attempt = 0; attempt < 3 && !cancelled(); ++attempt) {
                try {
                    entries = list(dir);
                    error.clear();
                    break;
                } catch (const FsError& e) {
                    error = e.message();
                    QThread::msleep(300);
                }
            }

            qint64 sum = 0;
            int files = 0;
            QStringList subdirs;
            for (const Entry& e : std::as_const(entries)) {
                if (e.isLink)
                    continue;
                const QString path = joinPath(dir, e.name);
                if (e.isDir) {
                    if (!skipInScan(path))
                        subdirs << path;
                } else {
                    sum += qMax<qint64>(0, e.size);
                    ++files;
                }
            }

            qint64 filesNow = 0;
            {
                QMutexLocker lock(&s.mutex);
                if (!error.isEmpty() && dir == root)
                    s.rootError = error;
                s.own[dir] += sum;
                for (const QString& p : std::as_const(subdirs)) {
                    s.own.insert(p, 0);
                    s.queue << p;
                }
                s.files += files;
                filesNow = s.files;
                --s.active;
                s.wake.wakeAll();
            }
            if (progress)
                progress(filesNow);
        }
    };

    const int workers = m_p.kind == DeviceKind::PS3 ? 2 : 3;
    std::vector<std::thread> threads;
    for (int i = 0; i < workers; ++i)
        threads.emplace_back(work);
    for (std::thread& t : threads)
        t.join();

    if (cancelled())
        throw Cancelled();
    if (!s.rootError.isEmpty())
        throw FsError(s.rootError);
    return aggregateSizes(root, s.own);
}
