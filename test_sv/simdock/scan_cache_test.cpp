#include "../../src/simulation/simdock/core/workspace.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
using namespace simdock;

class ScanCacheTest : public QObject {
    Q_OBJECT
    static bool write(const QString& path, const QByteArray& text) {
        QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(text) == text.size();
    }
private slots:
    void contentAndContextInvalidation() {
        QTemporaryDir root, other;
        QVERIFY(write(root.filePath("a.sv"), "module aaa; endmodule"));
        QVERIFY(write(root.filePath("b.svh"), "typedef logic [7:0] byte_t;"));
        SourceCache cache; ScanMetrics stats;
        auto scan = scanWorkspace(root.path(), nullptr, &stats, &cache);
        QCOMPARE(stats.parsedFiles, 2); QCOMPARE(scan.files.size(), 2);
        scan = scanWorkspace(root.path(), nullptr, &stats, &cache);
        QCOMPARE(stats.readFiles, 2); QCOMPARE(stats.parsedFiles, 0); QCOMPARE(stats.reusedFiles, 2);
        QFile file(root.filePath("a.sv"));
        QVERIFY(file.open(QIODevice::ReadWrite));
        const auto modified = file.fileTime(QFileDevice::FileModificationTime);
        QCOMPARE(file.write("module bbb; endmodule"), 21);
        QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime)); file.close();
        scan = scanWorkspace(root.path(), nullptr, &stats, &cache);
        QCOMPARE(stats.parsedFiles, 1); QCOMPARE(stats.reusedFiles, 1);
        QCOMPARE(scan.files.first().modules.first().name, QStringLiteral("bbb"));
        QVERIFY(QFile::remove(root.filePath("b.svh")));
        QVERIFY(write(root.filePath("c.sv"), "module broken ("));
        scan = scanWorkspace(root.path(), nullptr, &stats, &cache);
        QCOMPARE(scan.files.size(), 2); QVERIFY(!scan.messages.isEmpty());
        QVERIFY(!cache.files.contains("b.svh"));
        const auto issues = scan.messages;
        scan = scanWorkspace(root.path(), nullptr, &stats, &cache);
        QCOMPARE(stats.parsedFiles, 0); QCOMPARE(scan.messages, issues);
        QVERIFY(write(other.filePath("a.sv"), "module ccc; endmodule"));
        scan = scanWorkspace(other.path(), nullptr, &stats, &cache);
        QCOMPARE(stats.parsedFiles, 1); QCOMPARE(scan.files.size(), 1);
        QCOMPARE(scan.files.first().modules.first().name, QStringLiteral("ccc"));
        std::atomic_bool cancelled(true);
        scan = scanWorkspace(root.path(), &cancelled, &stats, &cache);
        QVERIFY(scan.files.isEmpty()); QCOMPARE(stats.parsedFiles, 0);
        scan = scanWorkspace(root.path(), nullptr, &stats, &cache);
        QCOMPARE(scan.files.size(), 2); QCOMPARE(stats.parsedFiles, 2);
    }
    void boundedCacheDoesNotDropFiles() {
        QTemporaryDir root;
        QVERIFY(write(root.filePath("a.sv"), "module aaa; endmodule"));
        QVERIFY(write(root.filePath("b.sv"), "module bbb; endmodule"));
        SourceCache cache; cache.files.setMaxCost(1); ScanMetrics stats;
        scanWorkspace(root.path(), nullptr, &stats, &cache);
        QCOMPARE(cache.files.size(), 1);
        const auto cached = scanWorkspace(root.path(), nullptr, &stats, &cache);
        const auto full = scanWorkspace(root.path());
        QCOMPARE(cached.files.size(), full.files.size());
        for (int i = 0; i < cached.files.size(); ++i) {
            QCOMPARE(cached.files[i].content, full.files[i].content);
            QCOMPARE(cached.files[i].declaredUnits, full.files[i].declaredUnits);
        }
    }
};
QTEST_GUILESS_MAIN(ScanCacheTest)
#include "scan_cache_test.moc"
