#include "AdbBackend.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>

namespace {

QString q(const QString& s) { return Tools::shellQuote(s); }

QString errorText(const Tools::Result& r)
{
    QString msg = QString::fromUtf8(r.err).trimmed();
    if (msg.isEmpty())
        msg = QString::fromUtf8(r.out).trimmed();
    if (r.timedOut)
        msg = QStringLiteral("Телефон не отвечает");
    if (msg.contains(QLatin1String("not found")) && msg.contains(QLatin1String("device")))
        msg = QStringLiteral("Телефон отключён. Проверьте кабель и отладку по USB.");
    return msg.isEmpty() ? QStringLiteral("Ошибка adb") : msg;
}

void requireAdb()
{
    if (Tools::adb().isEmpty())
        throw FsError(QStringLiteral("Не найден adb. Установите его командой: brew install android-platform-tools"));
}

} // namespace

QList<AdbBackend::Device> AdbBackend::devices()
{
    QList<Device> result;
    if (Tools::adb().isEmpty())
        return result;
    const auto r = Tools::run(Tools::adb(), {QStringLiteral("devices"), QStringLiteral("-l")}, 15000);
    const auto lines = QString::fromUtf8(r.out).split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('*')) || line.startsWith(QLatin1String("List of")))
            continue;
        const auto parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;
        Device d{parts[0], {}, parts[1]};
        for (const QString& p : parts) {
            if (p.startsWith(QLatin1String("model:")))
                d.model = p.mid(6).replace(QLatin1Char('_'), QLatin1Char(' '));
        }
        result.append(d);
    }
    return result;
}

AdbBackend::AdbBackend(const QString& serial, const QString& model)
    : m_serial(serial), m_model(model)
{
}

Tools::Result AdbBackend::shell(const QString& cmd, int timeoutMs) const
{
    requireAdb();
    return Tools::run(Tools::adb(), {QStringLiteral("-s"), m_serial, QStringLiteral("shell"), cmd}, timeoutMs);
}

void AdbBackend::shellChecked(const QString& cmd, int timeoutMs) const
{
    const auto r = shell(cmd, timeoutMs);
    if (r.code != 0)
        throw FsError(errorText(r));
}

qint64 AdbBackend::remoteSize(const QString& path) const
{
    const auto r = shell(QStringLiteral("stat -L -c %s ") + q(path), 15000);
    bool ok = false;
    const qint64 size = r.out.trimmed().toLongLong(&ok);
    if (r.code != 0 || !ok)
        throw FsError(QStringLiteral("Файл не найден: %1").arg(path));
    return size;
}

QList<Entry> AdbBackend::list(const QString& dir)
{
    const QString cmd = QStringLiteral("cd ") + q(dir)
        + QStringLiteral(" || exit 3; stat -L -c '%F|%s|%Y|%n' -- * .* 2>/dev/null; exit 0");
    const auto r = shell(cmd, 60000);
    if (r.code == 3)
        throw FsError(QStringLiteral("Нет доступа к папке %1").arg(dir));
    if (r.code != 0)
        throw FsError(errorText(r));

    QList<Entry> result;
    const auto lines = QString::fromUtf8(r.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString type = line.section(QLatin1Char('|'), 0, 0);
        const QString name = line.section(QLatin1Char('|'), 3);
        if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
            continue;
        Entry e;
        e.name = name;
        e.isDir = type == QLatin1String("directory");
        e.hidden = name.startsWith(QLatin1Char('.'));
        e.size = e.isDir ? -1 : line.section(QLatin1Char('|'), 1, 1).toLongLong();
        e.mtime = QDateTime::fromSecsSinceEpoch(line.section(QLatin1Char('|'), 2, 2).toLongLong());
        result.append(e);
    }
    return result;
}

void AdbBackend::mkdir(const QString& path) { shellChecked(QStringLiteral("mkdir -p ") + q(path)); }
void AdbBackend::removeFile(const QString& path) { shellChecked(QStringLiteral("rm -f ") + q(path)); }
void AdbBackend::removeDir(const QString& path) { shellChecked(QStringLiteral("rmdir ") + q(path)); }
void AdbBackend::removeRecursive(const QString& path) { shellChecked(QStringLiteral("rm -rf ") + q(path), 600000); }

void AdbBackend::rename(const QString& from, const QString& to)
{
    shellChecked(QStringLiteral("mv ") + q(from) + QLatin1Char(' ') + q(to), 600000);
}

bool AdbBackend::copyWithin(const QString& from, const QString& to)
{
    shellChecked(QStringLiteral("cp -r ") + q(from) + QLatin1Char(' ') + q(to), 3600000);
    return true;
}

void AdbBackend::download(const QString& path, const QString& localFile, const Progress& cb, bool append)
{
    requireAdb();
    if (append) {
        // Pull into a temp file, then append it.
        const QString tmp = Tools::cacheDir(QStringLiteral("transfer")) + QLatin1Char('/')
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        download(path, tmp, cb, false);
        QFile in(tmp), out(localFile);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::Append))
            throw FsError(QStringLiteral("Не удалось записать %1").arg(localFile));
        QByteArray buf(4 << 20, Qt::Uninitialized);
        qint64 n;
        while ((n = in.read(buf.data(), buf.size())) > 0)
            out.write(buf.constData(), n);
        in.close();
        QFile::remove(tmp);
        return;
    }

    const qint64 expected = remoteSize(path);
    QProcess p;
    p.start(Tools::adb(), {QStringLiteral("-s"), m_serial, QStringLiteral("pull"), path, localFile});
    if (!p.waitForStarted(5000))
        throw FsError(QStringLiteral("Не удалось запустить adb"));
    while (!p.waitForFinished(300)) {
        const qint64 now = QFileInfo(localFile).size();
        if (cb && !cb(now, expected)) {
            p.kill();
            p.waitForFinished(3000);
            QFile::remove(localFile);
            throw Cancelled();
        }
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        Tools::Result r;
        r.err = p.readAllStandardError();
        r.out = p.readAllStandardOutput();
        QFile::remove(localFile);
        throw FsError(errorText(r));
    }
    if (cb)
        cb(expected, expected);
}

void AdbBackend::upload(const QString& localFile, const QString& path, const Progress& cb,
                        qint64 offset, qint64 length)
{
    requireAdb();
    if (offset != 0 || length >= 0)
        throw FsError(QStringLiteral("Частичная загрузка на Android не поддерживается"));

    const qint64 total = QFileInfo(localFile).size();
    QProcess p;
    p.start(Tools::adb(), {QStringLiteral("-s"), m_serial, QStringLiteral("push"), localFile, path});
    if (!p.waitForStarted(5000))
        throw FsError(QStringLiteral("Не удалось запустить adb"));

    QElapsedTimer poll;
    poll.start();
    qint64 last = 0;
    while (!p.waitForFinished(250)) {
        if (cb && poll.elapsed() > 700) {
            poll.restart();
            const auto r = shell(QStringLiteral("stat -c %s ") + q(path) + QStringLiteral(" 2>/dev/null"), 5000);
            bool ok = false;
            const qint64 now = r.out.trimmed().toLongLong(&ok);
            if (ok)
                last = now;
        }
        if (cb && !cb(last, total)) {
            p.kill();
            p.waitForFinished(3000);
            shell(QStringLiteral("rm -f ") + q(path), 10000);
            throw Cancelled();
        }
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        Tools::Result r;
        r.err = p.readAllStandardError();
        r.out = p.readAllStandardOutput();
        throw FsError(errorText(r));
    }
    if (cb)
        cb(total, total);
}

QByteArray AdbBackend::read(const QString& path, qint64 maxBytes)
{
    requireAdb();
    const auto r = Tools::run(Tools::adb(),
        {QStringLiteral("-s"), m_serial, QStringLiteral("exec-out"),
         QStringLiteral("head -c %1 ").arg(maxBytes) + q(path) + QStringLiteral(" 2>/dev/null")},
        60000);
    if (r.code != 0 || r.out.isEmpty())
        throw FsError(QStringLiteral("Не удалось прочитать %1").arg(path));
    return r.out;
}

qint64 AdbBackend::dirSize(const QString& path, const std::atomic_bool*)
{
    const auto r = shell(QStringLiteral("find ") + q(path) + QStringLiteral(" -type f -exec stat -c %s {} + 2>/dev/null"),
                         600000);
    if (r.timedOut)
        throw FsError(QStringLiteral("Подсчёт занял слишком много времени"));
    qint64 total = 0;
    for (const QByteArray& line : r.out.split('\n'))
        total += line.trimmed().toLongLong();
    return total;
}

qint64 AdbBackend::freeSpace(const QString& path)
{
    const auto r = shell(QStringLiteral("df -k ") + q(path), 10000);
    const auto lines = QString::fromUtf8(r.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (r.code != 0 || lines.size() < 2)
        return -1;
    const auto cols = lines[1].split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (cols.size() < 4)
        return -1;
    return cols[3].toLongLong() * 1024;
}

SizeMap AdbBackend::scanTree(const QString& root, const std::atomic_bool* cancel, const ScanProgress& progress)
{
    // One command on the phone lists every entry with its type and size; the Mac only adds them up.
    requireAdb();
    QProcess p;
    p.start(Tools::adb(), {QStringLiteral("-s"), m_serial, QStringLiteral("shell"),
                           QStringLiteral("find ") + q(root)
                               + QStringLiteral(" -mindepth 1 -exec stat -c '%f|%s|%n' {} + 2>/dev/null")});
    if (!p.waitForStarted(5000))
        throw FsError(QStringLiteral("Не удалось запустить adb"));

    SizeMap own;
    own.insert(root, 0);
    qint64 files = 0;
    auto handle = [&](QByteArray line) {
        while (line.endsWith('\n') || line.endsWith('\r'))
            line.chop(1);
        const int a = int(line.indexOf('|'));
        const int b = a < 0 ? -1 : int(line.indexOf('|', a + 1));
        if (b < 0)
            return;
        bool ok = false;
        const uint mode = line.left(a).toUInt(&ok, 16);
        if (!ok)
            return;
        const QString name = QString::fromUtf8(line.mid(b + 1));
        const uint type = mode & 0170000;
        if (type == 0040000) {
            if (!own.contains(name))
                own.insert(name, 0);
        } else if (type == 0100000) {
            own[parentPath(name)] += line.mid(a + 1, b - a - 1).toLongLong();
            ++files;
        }
    };

    for (;;) {
        if (cancel && *cancel) {
            p.kill();
            p.waitForFinished(2000);
            throw Cancelled();
        }
        while (p.canReadLine())
            handle(p.readLine());
        if (p.state() == QProcess::NotRunning) {
            while (p.canReadLine())
                handle(p.readLine());
            const QByteArray rest = p.readAll();
            if (!rest.isEmpty())
                handle(rest);
            break;
        }
        p.waitForReadyRead(200);
        if (progress)
            progress(files);
    }
    if (files == 0 && own.size() == 1 && p.exitCode() != 0) {
        Tools::Result r;
        r.err = p.readAllStandardError();
        throw FsError(errorText(r));
    }
    return aggregateSizes(root, own);
}
