// Checks archives in Folix: listing with folder sizes, extract, add, new folder, delete, passwords,
// and copying through the transfer code. Run: cmake --build build --target tst_archive && build/tst_archive
#include "ArchiveBackend.h"
#include "Archives.h"
#include "JobDialog.h"
#include "LocalBackend.h"
#include "Operations.h"

#include "../third_party/Packix/core/Archiver.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <cstdio>

static int fails = 0;
#define CHECK(c, msg) do { if (c) std::printf("ok   %s\n", msg); else { std::printf("FAIL %s\n", msg); ++fails; } } while (0)

static void write(const QString& path, int kb)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    QByteArray data(kb * 1024, Qt::Uninitialized);
    for (int i = 0; i < data.size(); ++i)
        data[i] = char((i * 31) ^ (i >> 7));   // not compressible to nothing
    f.write(data);
}

static std::shared_ptr<ArchiveBackend> openArchive(const QString& path, const QString& password = {})
{
    packix::ArchiveInfo info;
    if (!packix::Archiver::open(path, password, info).ok())
        return nullptr;
    return std::make_shared<ArchiveBackend>(path, info, password);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("FolixTest");
    Archives::init();
    QTemporaryDir tmp;
    const QString t = tmp.path();

    CHECK(Archives::isArchive("Фото.zip") && Archives::isArchive("a.tar.gz") && Archives::isArchive("Игра.7z.002"),
          "zip, tar.gz and volume parts are archives");
    CHECK(!Archives::isArchive("game.apk") && !Archives::isArchive("disk.iso") && !Archives::isArchive("notes.txt"),
          "apk, iso, txt are not");

    // Source tree -> zip through Packix's core
    write(t + "/src/Документы/отчёт.bin", 100);
    write(t + "/src/Документы/Вложено/b.bin", 50);
    write(t + "/src/фото.bin", 30);
    packix::Archiver::CreateRequest create;
    create.sources = {t + "/src/Документы", t + "/src/фото.bin"};
    create.destinationDir = t;
    create.fileName = "Архив.zip";
    create.formatId = "zip";
    create.levelId = "normal";
    QString made;
    CHECK(packix::Archiver::create(create, nullptr, &made).ok(), "zip created");

    auto a = openArchive(t + "/Архив.zip");
    CHECK(a != nullptr, "archive opens");
    if (!a) return 1;
    QList<Entry> root = a->list("/");
    bool dirOk = false, fileOk = false;
    for (const Entry& e : root) {
        if (e.name == "Документы" && e.isDir && e.size == 150 * 1024) dirOk = true;
        if (e.name == "фото.bin" && !e.isDir && e.size == 30 * 1024) fileOk = true;
    }
    CHECK(dirOk, "folder listed with its full size (150 KB)");
    CHECK(fileOk, "file listed with its size");
    CHECK(a->list("/Документы/Вложено").size() == 1, "nested folder lists");
    const SizeMap sizes = a->scanTree("/", nullptr, nullptr);
    CHECK(sizes.value("/") == 180 * 1024 && sizes.value("/Документы/Вложено") == 50 * 1024, "size map from the listing");

    a->download("/Документы/отчёт.bin", t + "/out.bin", nullptr);
    CHECK(QFileInfo(t + "/out.bin").size() == 100 * 1024, "single file extracted");
    CHECK(a->read("/фото.bin", 1000).size() == 1000, "partial read for previews");

    write(t + "/new.bin", 20);
    a->upload(t + "/new.bin", "/Документы/добавлен.bin", nullptr);
    CHECK(a->list("/Документы").size() == 3, "file added under a new name");
    a->mkdir("/Пустая");
    bool empty = false;
    for (const Entry& e : a->list("/")) empty |= e.name == "Пустая" && e.isDir;
    CHECK(empty, "new folder inside the archive");
    a->removeRecursive("/Документы/Вложено");
    CHECK(a->list("/Документы").size() == 2, "folder removed from the archive");
    bool renameRefused = false;
    try { a->rename("/фото.bin", "/x.bin"); } catch (const FsError&) { renameRefused = true; }
    CHECK(renameRefused, "rename inside an archive is refused with a message");

    // Transfer code: archive -> Mac and Mac -> archive in one 7-Zip run each
    auto local = std::make_shared<LocalBackend>();
    QDir().mkpath(t + "/dst");
    JobState st;
    Entry docs; docs.name = "Документы"; docs.isDir = true;
    runTransfer(st, TransferRequest{a, "/", {docs}, local, t + "/dst", false, {}});
    CHECK(st.snapshot().errors.isEmpty() && QFileInfo::exists(t + "/dst/Документы/отчёт.bin"), "folder copied out of the archive");
    write(t + "/more/m.bin", 10);
    Entry more; more.name = "more"; more.isDir = true;
    JobState st2;
    runTransfer(st2, TransferRequest{local, t, {more}, a, "/Пустая", false, {}});
    CHECK(st2.snapshot().errors.isEmpty() && a->list("/Пустая/more").size() == 1, "folder copied into the archive");

    // Password-protected zip
    packix::Archiver::CreateRequest secret = create;
    secret.fileName = "Секрет.zip";
    secret.password = "pass123";
    CHECK(packix::Archiver::create(secret).ok(), "encrypted zip created");
    auto s = openArchive(t + "/Секрет.zip", "pass123");
    CHECK(s != nullptr, "encrypted zip opens");
    if (s) {
        s->download("/фото.bin", t + "/s.bin", nullptr);
        CHECK(QFileInfo(t + "/s.bin").size() == 30 * 1024, "file from the encrypted zip");
    }
    auto wrong = openArchive(t + "/Секрет.zip", "");
    bool refused = false;
    if (wrong) { try { wrong->download("/фото.bin", t + "/w.bin", nullptr); } catch (const FsError& e) { refused = true; std::printf("     msg: %s\n", e.what()); } }
    CHECK(refused, "no password: readable error, nothing extracted");

    // Read-only formats refuse changes
    packix::Archiver::CreateRequest tar = create;
    tar.fileName = "a.tar.gz"; tar.formatId = "tar.gz";
    if (packix::Archiver::create(tar).ok()) {
        auto g = openArchive(t + "/a.tar.gz");
        bool ro = false;
        if (g && !g->canModify()) { try { g->mkdir("/x"); } catch (const FsError&) { ro = true; } }
        CHECK(!g || g->canModify() || ro, "read-only formats say so");
    }
    // A running Packix app answers on the address Folix uses (skipped when it is not running).
    if (Archives::packixInstalled()) {
        QLocalSocket sock;
        sock.connectToServer(QStringLiteral("Packix-") + qEnvironmentVariable("USER"));
        const bool up = sock.waitForConnected(500);
        std::printf("     Packix installed at %s, reachable: %s\n", qPrintable(Archives::packixApp()), up ? "yes" : "no");
        sock.abort();
    }
    Archives::shutdown();
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASSED");
    return fails;
}
