#include "pinloomcodelinkstore.h"
#include "semanticstableidentity.h"
#include "slangmanager.h"
#include "zeroslackcli.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class ZeroSlackCliTest : public QObject
{
    Q_OBJECT

private:
    struct Fixture {
        std::unique_ptr<QTemporaryDir> workspace;
        std::unique_ptr<QTemporaryDir> cache;
        QString topFile;
        QString childFile;
        QString originalTop;
    };

    static bool writeText(const QString& path, const QString& text)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Text)
            && file.write(text.toUtf8()) == text.toUtf8().size()
            && file.commit();
    }

    static QString fileHash(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QString::fromLatin1(QCryptographicHash::hash(
            file.readAll(), QCryptographicHash::Sha256).toHex());
    }

    static SemanticSymbolRecord recordNamed(
        const QList<SemanticSymbolRecord>& records,
        const QString& name)
    {
        for (const SemanticSymbolRecord& record : records) {
            if (record.name == name)
                return record;
        }
        return {};
    }

    static Fixture makeFixture()
    {
        Fixture fixture;
        fixture.workspace = std::make_unique<QTemporaryDir>();
        fixture.cache = std::make_unique<QTemporaryDir>();
        const QString root = fixture.workspace->path();
        fixture.topFile = QDir(root).filePath(QStringLiteral("rtl/top.sv"));
        fixture.childFile = QDir(root).filePath(QStringLiteral("rtl/child.sv"));
        fixture.originalTop = QStringLiteral(
            "module top(input logic clk, input logic d, output logic q);\n"
            "  child u_child(.d(d), .q(q));\n"
            "  always_ff @(posedge clk) q <= d;\n"
            "endmodule\n");
        const QString child = QStringLiteral(
            "module child(input logic d, output logic q);\n"
            "  assign q = d;\n"
            "endmodule\n");
        if (!writeText(fixture.topFile, fixture.originalTop)
            || !writeText(fixture.childFile, child)) {
            fixture.workspace.reset();
            return fixture;
        }

        SlangManager slang;
        const SemanticSymbolRecord symbol = recordNamed(
            slang.extractSymbolRecords(fixture.topFile, fixture.originalTop),
            QStringLiteral("q"));
        PinloomSourceSelection source =
            PinloomSourceSelection::fromSemanticSymbol(
                root, fixture.originalTop, symbol);
        PinloomCodeLinkStore store;
        store.setWorkspaceRoot(root);
        QString failure;
        if (!source.isValid()
            || !store.addLink(source,
                              QUrl(QStringLiteral("pinloom://anchor/demo-q")),
                              QStringLiteral("DMA q anchor"),
                              {{QStringLiteral("anchorId"),
                                QStringLiteral("demo-q")}},
                              &failure)) {
            fixture.workspace.reset();
        }
        return fixture;
    }

private slots:
    void stableIdentitySurvivesLineMovement()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString file = QDir(directory.path()).filePath(
            QStringLiteral("stable.sv"));
        const QString original = QStringLiteral(
            "module stable; logic payload; endmodule\n");
        const QString moved = QStringLiteral(
            "\n\nmodule stable; logic payload; endmodule\n");
        SlangManager slang;
        const SemanticSymbolRecord before = recordNamed(
            slang.extractSymbolRecords(file, original),
            QStringLiteral("payload"));
        const SemanticSymbolRecord after = recordNamed(
            slang.extractSymbolRecords(file, moved),
            QStringLiteral("payload"));
        QVERIFY(before.isValid());
        QVERIFY(after.isValid());
        const SemanticStableIdentity beforeId =
            semanticStableIdentity(before, directory.path());
        const SemanticStableIdentity afterId =
            semanticStableIdentity(after, directory.path());
        QCOMPARE(beforeId.stableId, afterId.stableId);
        QVERIFY(beforeId.exactId != afterId.exactId);
        QCOMPARE(beforeId.stability, QStringLiteral("semantic"));
    }

    void scanQueryCacheAndReadOnlyContract()
    {
        Fixture fixture = makeFixture();
        QVERIFY(fixture.workspace);
        QVERIFY(fixture.workspace->isValid());
        QVERIFY(fixture.cache && fixture.cache->isValid());
        const QString root = fixture.workspace->path();
        const QString topHashBefore = fileHash(fixture.topFile);
        const QString childHashBefore = fileHash(fixture.childFile);
        const QString linksPath = QDir(root).filePath(
            QStringLiteral(".zeroslack/pinloom-links.json"));
        const QString linksHashBefore = fileHash(linksPath);

        ZeroSlackCliService service;
        ZeroSlackCliRequest scan;
        scan.command = QStringLiteral("scan");
        scan.workspaceRoot = root;
        scan.cacheDirectory = fixture.cache->path();
        const ZeroSlackCliResult scanResult = service.execute(scan);
        QCOMPARE(scanResult.exitCode, 0);
        QVERIFY(scanResult.envelope.value(QStringLiteral("ok")).toBool());
        QCOMPARE(scanResult.envelope.value(QStringLiteral("schema")).toString(),
                 QStringLiteral("zeroslack.cli/v1"));
        const QJsonObject scanData = scanResult.envelope.value(
            QStringLiteral("data")).toObject();
        QVERIFY(scanData.value(QStringLiteral("files")).toInt() >= 2);
        QVERIFY(scanData.value(QStringLiteral("symbols")).toInt() > 0);
        const QString cachePath = scanResult.envelope.value(
            QStringLiteral("cachePath")).toString();
        QVERIFY(QFileInfo::exists(cachePath));
        QVERIFY(!cachePath.startsWith(root, Qt::CaseInsensitive));

        QCOMPARE(fileHash(fixture.topFile), topHashBefore);
        QCOMPARE(fileHash(fixture.childFile), childHashBefore);
        QCOMPARE(fileHash(linksPath), linksHashBefore);

        ZeroSlackCliRequest summary = scan;
        summary.command = QStringLiteral("summary");
        const ZeroSlackCliResult summaryResult = service.execute(summary);
        QCOMPARE(summaryResult.exitCode, 0);
        QVERIFY(!summaryResult.envelope.value(
            QStringLiteral("cacheRebuilt")).toBool());
        QVERIFY(summaryResult.envelope.value(QStringLiteral("data"))
                    .toObject().value(QStringLiteral("anchorCount")).toInt() > 0);

        ZeroSlackCliRequest context = scan;
        context.command = QStringLiteral("context");
        context.filePath = QStringLiteral("rtl/top.sv");
        context.line = 3;
        const ZeroSlackCliResult contextResult = service.execute(context);
        QCOMPARE(contextResult.exitCode, 0);
        const QJsonObject contextData = contextResult.envelope.value(
            QStringLiteral("data")).toObject();
        QVERIFY(contextData.value(QStringLiteral("snippet"))
                    .toString().contains(QStringLiteral("always_ff")));
        QVERIFY(!contextData.value(QStringLiteral("symbols")).toArray().isEmpty());

        ZeroSlackCliRequest anchors = scan;
        anchors.command = QStringLiteral("anchors");
        const ZeroSlackCliResult anchorsResult = service.execute(anchors);
        QCOMPARE(anchorsResult.exitCode, 0);
        const QJsonArray anchorItems = anchorsResult.envelope.value(
            QStringLiteral("data")).toObject().value(
                QStringLiteral("anchors")).toArray();
        QVERIFY(!anchorItems.isEmpty());
        const QJsonObject anchor = anchorItems.at(0).toObject();
        QVERIFY(!anchor.contains(QStringLiteral("selectedText")));
        QVERIFY(anchor.value(QStringLiteral("linkCount")).toInt() > 0);

        ZeroSlackCliRequest symbol = scan;
        symbol.command = QStringLiteral("symbol");
        symbol.symbol = QStringLiteral("q");
        const ZeroSlackCliResult symbolResult = service.execute(symbol);
        QCOMPARE(symbolResult.exitCode, 0);
        const QJsonArray symbolMatches = symbolResult.envelope.value(
            QStringLiteral("data")).toObject().value(
                QStringLiteral("matches")).toArray();
        QVERIFY(!symbolMatches.isEmpty());
        QVERIFY(symbolMatches.at(0).toObject().value(
            QStringLiteral("uri")).toString().startsWith(
                QStringLiteral("zeroslack://symbol/")));

        ZeroSlackCliRequest impact = scan;
        impact.command = QStringLiteral("impact");
        impact.symbol = QStringLiteral("q");
        impact.depth = 2;
        const ZeroSlackCliResult impactResult = service.execute(impact);
        QCOMPARE(impactResult.exitCode, 0);
        QVERIFY(!impactResult.envelope.value(QStringLiteral("data"))
                     .toObject().value(QStringLiteral("symbols"))
                     .toArray().isEmpty());

        ZeroSlackCliRequest bundle = scan;
        bundle.command = QStringLiteral("bundle");
        bundle.query = QStringLiteral("q");
        bundle.maxTokens = 512;
        bundle.format = QStringLiteral("markdown");
        const ZeroSlackCliResult bundleResult = service.execute(bundle);
        QCOMPARE(bundleResult.exitCode, 0);
        const QJsonObject bundleData = bundleResult.envelope.value(
            QStringLiteral("data")).toObject();
        QVERIFY(bundleData.value(QStringLiteral("estimatedTokens")).toInt()
                <= 512);
        QVERIFY(bundleResult.rendered.contains("ZeroSlack context bundle"));

        ZeroSlackCliRequest status = scan;
        status.command = QStringLiteral("status");
        status.requireCurrent = true;
        const ZeroSlackCliResult current = service.execute(status);
        QCOMPARE(current.exitCode, 0);
        QVERIFY(current.envelope.value(QStringLiteral("data")).toObject()
                    .value(QStringLiteral("cacheCurrent")).toBool());

        QVERIFY(writeText(fixture.topFile,
                          QStringLiteral("\n") + fixture.originalTop));
        const ZeroSlackCliResult stale = service.execute(status);
        QCOMPARE(stale.exitCode, 4);
        QVERIFY(!stale.envelope.value(QStringLiteral("ok")).toBool());

        ZeroSlackCliRequest noRefresh = symbol;
        noRefresh.allowRefresh = false;
        const ZeroSlackCliResult rejected = service.execute(noRefresh);
        QCOMPARE(rejected.exitCode, 3);
        QVERIFY(rejected.envelope.value(QStringLiteral("error")).toObject()
                    .value(QStringLiteral("message")).toString()
                    .contains(QStringLiteral("stale"), Qt::CaseInsensitive));

        QCOMPARE(fileHash(fixture.childFile), childHashBefore);
        QCOMPARE(fileHash(linksPath), linksHashBefore);
    }

    void rendersJsonlAndRejectsOutsideFile()
    {
        Fixture fixture = makeFixture();
        QVERIFY(fixture.workspace && fixture.cache);
        ZeroSlackCliRequest scan;
        scan.command = QStringLiteral("scan");
        scan.workspaceRoot = fixture.workspace->path();
        scan.cacheDirectory = fixture.cache->path();
        scan.format = QStringLiteral("jsonl");
        const ZeroSlackCliResult scanResult = ZeroSlackCliService().execute(scan);
        QCOMPARE(scanResult.exitCode, 0);
        QVERIFY(scanResult.rendered.contains("\"recordType\":\"meta\""));

        ZeroSlackCliRequest context = scan;
        context.command = QStringLiteral("context");
        context.filePath = QDir::temp().filePath(QStringLiteral("outside.sv"));
        context.line = 1;
        const ZeroSlackCliResult rejected = ZeroSlackCliService().execute(context);
        QCOMPARE(rejected.exitCode, 5);
    }
};

QTEST_GUILESS_MAIN(ZeroSlackCliTest)

#include "zeroslack_cli_test.moc"
