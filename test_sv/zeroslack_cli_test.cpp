#include "pinloomcodelinkstore.h"
#include "semanticstableidentity.h"
#include "slangmanager.h"
#include "suitecontextcatalog.h"
#include "zeroslackcli.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <algorithm>

class ZeroSlackCliTest : public QObject
{
    Q_OBJECT

private:
    struct Fixture {
        std::unique_ptr<QTemporaryDir> workspace;
        std::unique_ptr<QTemporaryDir> cache;
        QString topFile;
        QString childFile;
        QString waveFile;
        QString regMapFile;
        QString suiteReferencesFile;
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
        fixture.waveFile = QDir(root).filePath(
            QStringLiteral("timing/dma.wave.json"));
        fixture.regMapFile = QDir(root).filePath(
            QStringLiteral("registers/dma.regmap.yaml"));
        fixture.suiteReferencesFile = QDir(root).filePath(
            QStringLiteral(".zeroslack/suite-references.json"));
        fixture.originalTop = QStringLiteral(
            "module top(input logic clk, input logic d, output logic q);\n"
            "  child u_child(.d(d), .q(q));\n"
            "  always_ff @(posedge clk) q <= d;\n"
            "endmodule\n");
        const QString child = QStringLiteral(
            "module child(input logic d, output logic q);\n"
            "  assign q = d;\n"
            "endmodule\n");
        const QString wave = QStringLiteral(
            "{\"schemaVersion\":1,\"projectId\":\"dma-wave\","
            "\"name\":\"DMA timing\",\"scenarios\":[{"
            "\"id\":\"scenario-main\",\"name\":\"Main\","
            "\"durationTick\":\"100\",\"lanes\":[{"
            "\"id\":\"lane-q\",\"name\":\"q\"}]}]}\n");
        const QString regMap = QStringLiteral(
            "schema_version: 2\n"
            "workspace:\n"
            "  id: dma-regmap\n"
            "  name: DMA Registers\n"
            "  address_spaces:\n"
            "    - id: space-main\n"
            "      name: CSR\n"
            "      blocks:\n"
            "        - id: block-control\n"
            "          name: CONTROL\n"
            "          registers:\n"
            "            - id: reg-q\n"
            "              name: q\n"
            "              fields:\n"
            "                - id: field-enable\n"
            "                  name: ENABLE\n");
        const QString suiteReferences = QStringLiteral(
            "{\"schema\":\"zeroslack.suite-references/v1\","
            "\"resources\":["
            "{\"id\":\"dma-wave\",\"provider\":\"wave\","
            "\"file\":\"timing/dma.wave.json\",\"symbols\":[\"q\"]},"
            "{\"id\":\"dma-regmap\",\"provider\":\"regmap\","
            "\"file\":\"registers/dma.regmap.yaml\","
            "\"objectId\":\"reg-q\",\"symbols\":[\"q\"]}]}\n");
        if (!writeText(fixture.topFile, fixture.originalTop)
            || !writeText(fixture.childFile, child)
            || !writeText(fixture.waveFile, wave)
            || !writeText(fixture.regMapFile, regMap)
            || !writeText(fixture.suiteReferencesFile,
                          suiteReferences)) {
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

    void suiteContextAggregatesExplicitReferencesWithinBudget()
    {
        Fixture fixture = makeFixture();
        QVERIFY(fixture.workspace && fixture.cache);
        const QString waveHash = fileHash(fixture.waveFile);
        const QString regMapHash = fileHash(fixture.regMapFile);
        const QString referencesHash = fileHash(
            fixture.suiteReferencesFile);

        ZeroSlackCliRequest request;
        request.command = QStringLiteral("suite-context");
        request.workspaceRoot = fixture.workspace->path();
        request.cacheDirectory = fixture.cache->path();
        request.filePath = QStringLiteral("rtl/top.sv");
        request.line = 3;
        request.symbol = QStringLiteral("q");
        request.maxTokens = 900;
        const ZeroSlackCliResult result =
            ZeroSlackCliService().execute(request);
        QCOMPARE(result.exitCode, 0);
        const QJsonObject data = result.envelope.value(
            QStringLiteral("data")).toObject();
        QCOMPARE(data.value(QStringLiteral("source")).toObject().value(
                     QStringLiteral("symbol")).toString(),
                 QStringLiteral("q"));
        const QJsonObject budget = data.value(
            QStringLiteral("budget")).toObject();
        QVERIFY(budget.value(QStringLiteral("estimatedTokens")).toInt()
                <= budget.value(QStringLiteral("maxTokens")).toInt());
        const QJsonArray providers = data.value(
            QStringLiteral("payload")).toObject().value(
                QStringLiteral("providers")).toArray();
        QCOMPARE(providers.size(), 3);
        QSet<QString> providerIds;
        int explicitItems = 0;
        for (const QJsonValue& value : providers) {
            const QJsonObject provider = value.toObject();
            providerIds.insert(provider.value(
                QStringLiteral("id")).toString());
            for (const QJsonValue& itemValue : provider.value(
                     QStringLiteral("items")).toArray()) {
                const QJsonObject item = itemValue.toObject();
                if (item.value(QStringLiteral("origin")).toString()
                    == QStringLiteral("suite-references")) {
                    ++explicitItems;
                }
                QVERIFY(!item.contains(QStringLiteral("content")));
                QVERIFY(!item.contains(QStringLiteral("base64")));
            }
        }
        QCOMPARE(providerIds,
                 QSet<QString>({QStringLiteral("pinloom"),
                                QStringLiteral("wave"),
                                QStringLiteral("regmap")}));
        QVERIFY(explicitItems >= 1);

        ZeroSlackCliRequest waveOnly = request;
        waveOnly.includedProviders = {QStringLiteral("wave")};
        const ZeroSlackCliResult filtered =
            ZeroSlackCliService().execute(waveOnly);
        QCOMPARE(filtered.exitCode, 0);
        const QJsonArray filteredProviders = filtered.envelope.value(
            QStringLiteral("data")).toObject().value(
                QStringLiteral("payload")).toObject().value(
                    QStringLiteral("providers")).toArray();
        QCOMPARE(filteredProviders.size(), 1);
        QCOMPARE(filteredProviders.at(0).toObject().value(
                     QStringLiteral("id")).toString(),
                 QStringLiteral("wave"));

        QCOMPARE(fileHash(fixture.waveFile), waveHash);
        QCOMPARE(fileHash(fixture.regMapFile), regMapHash);
        QCOMPARE(fileHash(fixture.suiteReferencesFile), referencesHash);
    }

    void suiteContextRevisionTracksSuiteFilesIndependently()
    {
        Fixture fixture = makeFixture();
        QVERIFY(fixture.workspace && fixture.cache);
        ZeroSlackCliRequest request;
        request.command = QStringLiteral("suite-context");
        request.workspaceRoot = fixture.workspace->path();
        request.cacheDirectory = fixture.cache->path();
        request.filePath = QStringLiteral("rtl/top.sv");
        request.line = 2;
        request.maxTokens = 900;

        const ZeroSlackCliResult first =
            ZeroSlackCliService().execute(request);
        QCOMPARE(first.exitCode, 0);
        const QString workspaceRevision = first.envelope.value(
            QStringLiteral("workspaceRevision")).toString();
        const QString firstSuiteRevision = first.envelope.value(
            QStringLiteral("data")).toObject().value(
                QStringLiteral("suiteRevision")).toString();
        QVERIFY(firstSuiteRevision.startsWith(QStringLiteral("sha256:")));

        QVERIFY(writeText(fixture.waveFile,
            QStringLiteral("{\"schemaVersion\":1,\"projectId\":\"dma-wave\","
                           "\"name\":\"DMA timing updated\","
                           "\"scenarios\":[]}\n")));
        const ZeroSlackCliResult second =
            ZeroSlackCliService().execute(request);
        QCOMPARE(second.exitCode, 0);
        QCOMPARE(second.envelope.value(
                     QStringLiteral("workspaceRevision")).toString(),
                 workspaceRevision);
        const QString secondSuiteRevision = second.envelope.value(
            QStringLiteral("data")).toObject().value(
                QStringLiteral("suiteRevision")).toString();
        QVERIFY(secondSuiteRevision.startsWith(QStringLiteral("sha256:")));
        QVERIFY(secondSuiteRevision != firstSuiteRevision);
    }

    void suiteContextCountsReferencesBeyondInspectionLimit()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QJsonArray references;
        for (int index = 0;
             index < SuiteContextCatalog::kMaximumReferences + 1;
             ++index) {
            references.append(QJsonObject{
                {QStringLiteral("id"),
                 QStringLiteral("wave-%1").arg(index)},
                {QStringLiteral("provider"), QStringLiteral("wave")},
                {QStringLiteral("file"),
                 QStringLiteral("wave/missing-%1.wave.json").arg(index)},
            });
        }
        const QString manifest = QDir(workspace.path()).filePath(
            QStringLiteral(".zeroslack/suite-references.json"));
        QVERIFY(writeText(manifest,
            QString::fromUtf8(QJsonDocument(QJsonObject{
                {QStringLiteral("schema"),
                 QStringLiteral("zeroslack.suite-references/v1")},
                {QStringLiteral("resources"), references},
            }).toJson(QJsonDocument::Compact))));

        SuiteContextCatalogRequest request;
        request.workspaceRoot = workspace.path();
        request.includedProviders = {QStringLiteral("wave")};
        request.maxItemsPerProvider = 64;
        const SuiteContextSnapshot snapshot =
            SuiteContextCatalog::inspect(request);
        QCOMPARE(snapshot.discoveredCounts.value(
                     QStringLiteral("wave")),
                 SuiteContextCatalog::kMaximumReferences + 1);
        QCOMPARE(snapshot.resources.size(), 64);
        QCOMPARE(snapshot.catalogOmittedCount, 1);
        QVERIFY(std::any_of(
            snapshot.diagnostics.cbegin(), snapshot.diagnostics.cend(),
            [](const SuiteContextDiagnostic& diagnostic) {
                return diagnostic.code
                    == QStringLiteral("reference_limit_exceeded");
            }));
    }

    void suiteContextRevisionIncludesReferencesBeyondEmissionLimit()
    {
        Fixture fixture = makeFixture();
        QVERIFY(fixture.workspace && fixture.cache);
        QJsonArray references;
        for (int index = 0; index < 70; ++index) {
            references.append(QJsonObject{
                {QStringLiteral("id"),
                 QStringLiteral("wave-%1").arg(index)},
                {QStringLiteral("provider"), QStringLiteral("wave")},
                {QStringLiteral("file"),
                 QStringLiteral("timing/revision-%1.wave.json").arg(index)},
            });
        }
        QVERIFY(writeText(fixture.suiteReferencesFile,
            QString::fromUtf8(QJsonDocument(QJsonObject{
                {QStringLiteral("schema"),
                 QStringLiteral("zeroslack.suite-references/v1")},
                {QStringLiteral("resources"), references},
            }).toJson(QJsonDocument::Compact))));

        ZeroSlackCliRequest request;
        request.command = QStringLiteral("suite-context");
        request.workspaceRoot = fixture.workspace->path();
        request.cacheDirectory = fixture.cache->path();
        request.includedProviders = {QStringLiteral("wave")};
        request.maxTokens = 4096;
        const ZeroSlackCliResult first =
            ZeroSlackCliService().execute(request);
        QCOMPARE(first.exitCode, 0);
        const QJsonObject firstData = first.envelope.value(
            QStringLiteral("data")).toObject();
        QVERIFY(firstData.value(QStringLiteral("budget")).toObject().value(
            QStringLiteral("truncated")).toBool());
        const QString firstRevision = firstData.value(
            QStringLiteral("suiteRevision")).toString();

        const QString lastFile = QDir(fixture.workspace->path()).filePath(
            QStringLiteral("timing/revision-69.wave.json"));
        QVERIFY(writeText(lastFile,
            QStringLiteral("{\"schemaVersion\":1,\"projectId\":\"late\","
                           "\"name\":\"Late reference\","
                           "\"scenarios\":[]}\n")));
        const ZeroSlackCliResult second =
            ZeroSlackCliService().execute(request);
        QCOMPARE(second.exitCode, 0);
        const QString secondRevision = second.envelope.value(
            QStringLiteral("data")).toObject().value(
                QStringLiteral("suiteRevision")).toString();
        QVERIFY(firstRevision != secondRevision);
    }

    void pinloomReferencesFollowRelativePathAfterWorkspaceMove()
    {
        Fixture fixture = makeFixture();
        QVERIFY(fixture.workspace && fixture.cache);
        const QString linksPath = QDir(fixture.workspace->path()).filePath(
            QStringLiteral(".zeroslack/pinloom-links.json"));
        QFile links(linksPath);
        QVERIFY(links.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(
            links.readAll()).object();
        links.close();
        QJsonArray anchors = root.value(
            QStringLiteral("anchors")).toArray();
        QVERIFY(!anchors.isEmpty());
        QJsonObject anchor = anchors.at(0).toObject();
        QJsonObject source = anchor.value(
            QStringLiteral("source")).toObject();
        source.insert(QStringLiteral("workspaceRoot"),
                      QStringLiteral("Z:/retired/workspace"));
        source.insert(QStringLiteral("absoluteFilePath"),
                      QStringLiteral("Z:/retired/workspace/rtl/top.sv"));
        anchor.insert(QStringLiteral("source"), source);
        anchors[0] = anchor;
        root.insert(QStringLiteral("anchors"), anchors);
        QVERIFY(writeText(linksPath, QString::fromUtf8(
            QJsonDocument(root).toJson(QJsonDocument::Compact))));

        SuiteContextCatalogRequest request;
        request.workspaceRoot = fixture.workspace->path();
        request.includedProviders = {QStringLiteral("pinloom")};
        const SuiteContextSnapshot snapshot =
            SuiteContextCatalog::inspect(request);
        QCOMPARE(snapshot.resources.size(), 1);
        QCOMPARE(QFileInfo(snapshot.resources.first().filePath)
                     .canonicalFilePath(),
                 QFileInfo(fixture.topFile).canonicalFilePath());
    }

    void suiteContextRejectsInvalidProviderAndLineScope()
    {
        Fixture fixture = makeFixture();
        QVERIFY(fixture.workspace && fixture.cache);
        ZeroSlackCliRequest request;
        request.command = QStringLiteral("suite-context");
        request.workspaceRoot = fixture.workspace->path();
        request.cacheDirectory = fixture.cache->path();
        request.includedProviders = {QStringLiteral("unknown")};
        QCOMPARE(ZeroSlackCliService().execute(request).exitCode, 2);

        request.includedProviders.clear();
        request.line = 3;
        request.lineSpecified = true;
        QCOMPARE(ZeroSlackCliService().execute(request).exitCode, 5);

        request.filePath = QStringLiteral("rtl/top.sv");
        request.line = 999;
        QCOMPARE(ZeroSlackCliService().execute(request).exitCode, 5);

        request.line = 1;
        request.filePath = QStringLiteral("registers/dma.regmap.yaml");
        QCOMPARE(ZeroSlackCliService().execute(request).exitCode, 5);
    }
};

QTEST_GUILESS_MAIN(ZeroSlackCliTest)

#include "zeroslack_cli_test.moc"
