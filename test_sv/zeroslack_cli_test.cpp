#include <zeroslack/semantic/pinloomcodelinkstore.h>
#include <zeroslack/semantic/semanticstableidentity.h>
#include <zeroslack/semantic/slangmanager.h>
#include <zeroslack/semantic/suitecontextcatalog.h>
#include "zeroslackcli.h"
#include <zeroslack/semantic/workspaceconfigurationservice.h>
#include <QDateTime>

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QStringConverter>

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
    void cliDeclarationIdentity_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("header");
        QTest::addColumn<bool>("enumeration");
        const QString enumBody = "typedef enum logic { IDLE, RUN } state_t;\nstate_t state;\n";
        const QString structBody = "typedef struct packed { logic member; logic Member; } payload_t;\n"
            "typedef payload_t alias_t; typedef alias_t alias2_t; alias2_t item;\n";
        for (const bool enumeration : {true, false}) {
            const QString body = enumeration ? enumBody : structBody;
            const QString assignment = enumeration ? "state = IDLE" : "item.member = 1'b0";
            const QByteArray suffix = enumeration ? "enum" : "struct-alias-case";
            QTest::newRow(("modules-" + suffix).constData())
                << ("module top_a;\n" + body + "always_comb " + assignment + ";\nendmodule\n"
                    "module top_b;\n" + body + "always_comb " + assignment + ";\nendmodule\n")
                << QString() << enumeration;
            QTest::newRow(("packages-" + suffix).constData())
                << ("package pkg_a;\n" + body + "endpackage\npackage pkg_b;\n" + body + "endpackage\n"
                    "module top_a; import pkg_a::*; always_comb " + assignment + "; endmodule\n"
                    "module top_b; import pkg_b::*; always_comb " + assignment + "; endmodule\n")
                << QString() << enumeration;
            const QString accessA = enumeration ? "pkg_a::state = pkg_a::IDLE" : "pkg_a::item.member = 1'b0";
            const QString accessB = enumeration ? "pkg_b::state = pkg_b::IDLE" : "pkg_b::item.member = 1'b0";
            QTest::newRow(("qualified-packages-" + suffix).constData())
                << ("package pkg_a;\n" + body + "endpackage\npackage pkg_b;\n" + body + "endpackage\n"
                    "module top_a; always_comb " + accessA + "; endmodule\n"
                    "module top_b; always_comb " + accessB + "; endmodule\n")
                << QString() << enumeration;
            QTest::newRow(("shared-include-" + suffix).constData())
                << ("module top_a;\n`include \"shared.svh\"\nalways_comb " + assignment + ";\nendmodule\n"
                    "module top_b;\n`include \"shared.svh\"\nalways_comb " + assignment + ";\nendmodule\n")
                << body << enumeration;
        }
    }

    void cliDeclarationIdentity()
    {
        QFETCH(QString, source);
        QFETCH(QString, header);
        QFETCH(bool, enumeration);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("workspace");
        QVERIFY(writeText(root + "/types.sv", source));
        if (!header.isEmpty()) QVERIFY(writeText(root + "/shared.svh", header));
        WorkspaceConfigurationService configurationService;
        auto configuration = configurationService.defaultConfiguration(root);
        configuration.fileExtensions = {".sv"}; // Headers are includes, not independent compilation units.
        QVERIFY(configurationService.save(configuration));
        ZeroSlackCliRequest request;
        request.workspaceRoot = root;
        request.cacheDirectory = fixture.filePath("cache");
        // Run the shipped command-line entry point, not a substitute index.
        const auto execute = [&]() {
            QString binary = QCoreApplication::applicationDirPath() + "/zeroslack-cli";
#ifdef Q_OS_WIN
            binary += ".exe";
#endif
            QStringList args{request.command, request.workspaceRoot};
            if (request.command == "symbol") args.append(request.symbol);
            if (request.command == "impact") args.append({"--symbol", request.symbol, "--depth", "1"});
            args.append({"--cache-dir", request.cacheDirectory, "--format", "json"});
            if (!request.allowRefresh) args.append("--no-refresh");
            QProcess process;
            process.start(binary, args);
            ZeroSlackCliResult result;
            result.exitCode = -1;
            if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
                result.rendered = process.errorString().toUtf8();
                return result;
            }
            result.rendered = process.readAllStandardOutput();
            result.envelope = QJsonDocument::fromJson(result.rendered).object();
            if (process.exitStatus() == QProcess::NormalExit && !result.envelope.isEmpty())
                result.exitCode = process.exitCode();
            result.rendered += process.readAllStandardError();
            return result;
        };
        const auto matchesOf = [](const ZeroSlackCliResult& result) {
            return result.envelope.value("data").toObject().value("matches").toArray();
        };
        const auto idsOf = [](const QJsonArray& matches, const QString& key) {
            QSet<QString> ids;
            for (const auto& value : matches) ids.insert(value.toObject().value(key).toString());
            return ids;
        };
        request.command = "scan";
        auto result = execute();
        QVERIFY2(result.succeeded(), result.rendered.constData());
        request.command = "symbol";
        request.symbol = enumeration ? "IDLE" : "member";
        request.allowRefresh = false;
        result = execute();
        QVERIFY2(result.succeeded(), result.rendered.constData());
        QVERIFY(!result.envelope.value("cacheRebuilt").toBool());
        const auto matches = matchesOf(result);
        QCOMPARE(matches.size(), enumeration ? 2 : 4);
        const auto ids = idsOf(matches, "id");
        QCOMPARE(ids.size(), matches.size());
        QCOMPARE(idsOf(matches, "exactId").size(), matches.size());
        QCOMPARE(idsOf(matches, "uri").size(), matches.size());
        QJsonObject selected;
        for (const auto& value : matches) {
            const auto record = value.toObject();
            const auto id = record.value("id").toString();
            QVERIFY(id.startsWith("zsym-v2-"));
            QCOMPARE(QUrl(record.value("uri").toString()).path(), "/" + id);
            const auto owner = record.value("ownerKey").toString();
            if (record.value("name").toString() == request.symbol
                && (owner.contains("top_a") || owner.contains("pkg_a"))) selected = record;
        }
        QVERIFY(!selected.isEmpty());
        QSet<QString> foreignIds;
        for (const auto& value : matches)
            if (value.toObject().value("ownerKey") != selected.value("ownerKey"))
                foreignIds.insert(value.toObject().value("id").toString());
        const auto cachePath = ZeroSlackCliService().cachePathForWorkspace(root, request.cacheDirectory);
        const auto warmCacheHash = fileHash(cachePath);
        const int assignmentLine = source.left(source.indexOf("always_comb")).count('\n') + 1;
        for (const QString& key : {QString("id"), QString("exactId")}) {
            request.symbol = selected.value(key).toString();
            result = execute();
            QVERIFY2(result.succeeded(), result.rendered.constData());
            QCOMPARE(matchesOf(result).size(), 1);
            QCOMPARE(matchesOf(result).first().toObject().value("exactId"), selected.value("exactId"));
            const auto edges = result.envelope.value("data").toObject().value("relationships").toArray();
            if (enumeration) QCOMPARE(edges.size(), 2);
            for (const auto& value : edges) {
                const auto edge = value.toObject();
                QVERIFY(!foreignIds.contains(edge.value("from").toString()));
                QVERIFY(!foreignIds.contains(edge.value("to").toString()));
                if (enumeration) QCOMPARE(edge.value("range").toObject().value("line").toInt(), assignmentLine);
            }
        }
        request.command = "impact";
        result = execute();
        QVERIFY2(result.succeeded(), result.rendered.constData());
        const auto impact = result.envelope.value("data").toObject();
        QCOMPARE(impact.value("seeds").toArray().size(), 1);
        int selectedNameCount = 0;
        for (const auto& value : impact.value("symbols").toArray()) {
            const auto record = value.toObject();
            QVERIFY(!foreignIds.contains(record.value("id").toString()));
            const auto owner = record.value("ownerKey").toString();
            QVERIFY(owner.contains("top_a") || owner.contains("pkg_a"));
            if (record.value("name") == selected.value("name")) ++selectedNameCount;
        }
        QCOMPARE(selectedNameCount, 1);
        const auto impactEdges = impact.value("relationships").toArray();
        if (enumeration) QCOMPARE(impactEdges.size(), 2);
        for (const auto& value : impactEdges) {
            const auto edge = value.toObject();
            QVERIFY(!foreignIds.contains(edge.value("from").toString()));
            QVERIFY(!foreignIds.contains(edge.value("to").toString()));
            if (enumeration) QCOMPARE(edge.value("range").toObject().value("line").toInt(), assignmentLine);
        }
        QJsonObject counterpart;
        for (const auto& value : matches) {
            const auto record = value.toObject();
            if (record.value("name") == selected.value("name")
                && foreignIds.contains(record.value("id").toString())) counterpart = record;
        }
        QVERIFY(!counterpart.isEmpty());
        request.symbol = counterpart.value("exactId").toString();
        for (const QString command : {QString("symbol"), QString("impact")}) {
            request.command = command;
            result = execute();
            QVERIFY2(result.succeeded(), result.rendered.constData());
            const auto data = result.envelope.value("data").toObject();
            const auto seeds = data.value(command == "symbol" ? "matches" : "seeds").toArray();
            QCOMPARE(seeds.size(), 1);
            QCOMPARE(seeds.first().toObject().value("exactId"), counterpart.value("exactId"));
            for (const auto& value : data.value("symbols").toArray()) {
                const auto owner = value.toObject().value("ownerKey").toString();
                QVERIFY(owner.contains("top_b") || owner.contains("pkg_b"));
            }
            const auto edges = data.value("relationships").toArray();
            if (enumeration) QCOMPARE(edges.size(), 2);
            for (const auto& value : edges) {
                const auto edge = value.toObject();
                QVERIFY(edge.value("from") != selected.value("id"));
                QVERIFY(edge.value("to") != selected.value("id"));
                if (enumeration) QCOMPARE(edge.value("range").toObject().value("line").toInt(),
                    source.left(source.lastIndexOf("always_comb")).count('\n') + 1);
            }
        }
        request.command = "symbol";
        request.symbol = "zsym-v1-013b1865bb2a2ee820f204ff"; // Frozen REVIEW-01 collision.
        QVERIFY(!execute().succeeded());
        QCOMPARE(fileHash(cachePath), warmCacheHash);

        // Both declaration and owner positions move; only exact IDs may change.
        QVERIFY(writeText(root + "/types.sv", "\n\n" + source));
        if (!header.isEmpty()) QVERIFY(writeText(root + "/shared.svh", "\n\n" + header));
        request.symbol = enumeration ? "IDLE" : "member";
        QVERIFY(!execute().succeeded());
        request.allowRefresh = true;
        result = execute();
        QVERIFY2(result.succeeded(), result.rendered.constData());
        QVERIFY(result.envelope.value("cacheRebuilt").toBool());
        const auto moved = matchesOf(result);
        QCOMPARE(idsOf(moved, "id"), ids);
        QVERIFY(idsOf(moved, "exactId") != idsOf(matches, "exactId"));

        // Different absolute path lengths catch opaque nested-path hashing.
        request.workspaceRoot = fixture.filePath("different-length/relocated-workspace");
        QVERIFY(writeText(request.workspaceRoot + "/types.sv", "\n\n" + source));
        if (!header.isEmpty()) QVERIFY(writeText(request.workspaceRoot + "/shared.svh", "\n\n" + header));
        configuration = configurationService.defaultConfiguration(request.workspaceRoot);
        configuration.fileExtensions = {".sv"};
        QVERIFY(configurationService.save(configuration));
        result = execute();
        QVERIFY2(result.succeeded(), result.rendered.constData());
        QCOMPARE(idsOf(matchesOf(result), "id"), ids);
        QVERIFY(idsOf(matchesOf(result), "exactId") != idsOf(moved, "exactId"));
    }

    void cachePreservesDeclarationReferences() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("workspace");
        const QString source="module top_a; typedef struct packed { logic alpha; } payload_t; payload_t item; endmodule\n"
            "module top_b; typedef struct packed { logic beta; } payload_t; payload_t item; endmodule\n";
        QVERIFY(writeText(root+"/types.sv",source));
        ZeroSlackCliService service; ZeroSlackCliRequest request;
        request.workspaceRoot=root; request.cacheDirectory=fixture.filePath("cache"); request.command="scan";
        const auto scan=service.execute(request); QVERIFY2(scan.succeeded(),scan.rendered.constData());
        const auto path=service.cachePathForWorkspace(root,request.cacheDirectory);
        QFile cache(path); QVERIFY(cache.open(QIODevice::ReadOnly));
        auto json=QJsonDocument::fromJson(cache.readAll()).object(); cache.close();
        QCOMPARE(json.value("version").toInt(),5);
        QSet<QString> exactKeys, boundTypes, boundOwners;
        for(const auto& value:json.value("symbols").toArray()) exactKeys.insert(value.toObject().value("exactKey").toString());
        for(const auto& value:json.value("symbols").toArray()) {
            const auto record=value.toObject();
            if(record.value("name").toString()!="item") continue;
            const auto type=record.value("typeKey").toString(), owner=record.value("ownerKey").toString();
            QVERIFY(!type.isEmpty() && exactKeys.contains(type));
            QVERIFY(!owner.isEmpty() && exactKeys.contains(owner));
            boundTypes.insert(type); boundOwners.insert(owner);
        }
        QCOMPARE(boundTypes.size(),2); QCOMPARE(boundOwners.size(),2);
        request.command="symbol"; request.symbol="item"; request.allowRefresh=false;
        const auto hit=service.execute(request); QVERIFY(hit.succeeded()); QVERIFY(!hit.envelope.value("cacheRebuilt").toBool());
        for(const auto& value:hit.envelope.value("data").toObject().value("matches").toArray())
            QVERIFY(boundTypes.contains(value.toObject().value("typeKey").toString()));
        for (const int oldVersion : {3, 4}) {
            json.insert("version",oldVersion); QVERIFY(writeText(path,QString::fromUtf8(QJsonDocument(json).toJson())));
            request.allowRefresh=false;
            QVERIFY(!service.execute(request).succeeded()); // Old caches lack references or contain v1 IDs.
            request.allowRefresh=true; const auto rebuilt=service.execute(request);
            QVERIFY(rebuilt.succeeded()); QVERIFY(rebuilt.envelope.value("cacheRebuilt").toBool());
        }
    }

    void anchorsCaptureSourcesOutsideSemanticExtensions() {
        auto fixture = makeFixture(); QVERIFY(fixture.workspace && fixture.cache);
        const auto file = QDir(fixture.workspace->path()).filePath("notes.txt");
        const QString text = "module notes; logic selected; endmodule\n"; QVERIFY(writeText(file, text));
        PinloomCodeLinkStore links; links.setWorkspaceRoot(fixture.workspace->path());
        SlangManager slang;
        const auto source = PinloomSourceSelection::fromSemanticSymbol(fixture.workspace->path(), text,
            recordNamed(slang.extractSymbolRecords(file, text), "selected"));
        QVERIFY(links.addLink(source, QUrl("pinloom://anchor/notes"), "notes", {}));
        ZeroSlackCliRequest request; request.command = "anchors"; request.filePath = "notes.txt";
        request.workspaceRoot = fixture.workspace->path(); request.cacheDirectory = fixture.cache->path();
        const auto result = ZeroSlackCliService().execute(request); QVERIFY(result.succeeded());
        const auto anchors = result.envelope.value("data").toObject().value("anchors").toArray();
        QCOMPARE(anchors.size(), 1);
        QCOMPARE(anchors.first().toObject().value("resolution").toString(), QString("exact"));
        ZeroSlackCliService raced([&](auto stage) {
            if (stage == ZeroSlackCliService::ObservationStage::BeforeOutput)
                QVERIFY(writeText(file, "changed selection\n"));
        });
        QVERIFY(!raced.execute(request).succeeded());
    }

    void captureMismatchDoesNotPublishMixedInput_data() {
        QTest::addColumn<QString>("command");
        QTest::addColumn<bool>("late");
        for (const auto& command : {QString("context"), QString("bundle"), QString("anchors")})
            for (bool late : {false, true})
                QTest::newRow(qPrintable(command + (late ? "-before-output" : "-before-worker"))) << command << late;
    }
    void captureMismatchDoesNotPublishMixedInput() {
        QFETCH(QString, command); QFETCH(bool, late);
        auto fixture = makeFixture(); QVERIFY(fixture.workspace && fixture.cache);
        ZeroSlackCliRequest request; request.command = command;
        request.workspaceRoot = fixture.workspace->path(); request.cacheDirectory = fixture.cache->path();
        request.filePath = "rtl/top.sv"; request.query = "top"; request.line = 2;
        bool changed = false;
        ZeroSlackCliService interrupted([&](ZeroSlackCliService::ObservationStage stage) {
            if (stage == (late ? ZeroSlackCliService::ObservationStage::BeforeOutput
                               : ZeroSlackCliService::ObservationStage::WorkspaceCaptured)) {
                changed = writeText(fixture.topFile, QString("// new input\n")
                    + QString(fixture.originalTop).replace("module top(", "module top_new("));
            }
        });
        const auto result = interrupted.execute(request);
        QVERIFY(changed); QVERIFY(!result.succeeded());
        QVERIFY(!result.envelope.contains("data"));
        QVERIFY(!QFileInfo::exists(interrupted.cachePathForWorkspace(request.workspaceRoot, request.cacheDirectory)));
        ZeroSlackCliService service;
        const auto next = service.execute(request); QVERIFY2(next.succeeded(), next.rendered.constData());
        const auto cached = service.execute(request); QVERIFY(cached.succeeded());
        QVERIFY(!cached.envelope.value("cacheRebuilt").toBool());
        QCOMPARE(next.envelope.value("data"), cached.envelope.value("data"));
        request.command = "status"; request.requireCurrent = true;
        const auto status = service.execute(request); QVERIFY(status.succeeded());
        QVERIFY(status.envelope.value("data").toObject().value("cacheCurrent").toBool());
    }
    void rawEncodingAndLogicalSnippetShareInput_data() {
        QTest::addColumn<int>("encoding"); QTest::addColumn<QString>("ending");
        QTest::newRow("utf8-bom-crlf") << int(QStringConverter::Utf8) << QString("\r\n");
        QTest::newRow("utf16le-crlf") << int(QStringConverter::Utf16LE) << QString("\r\n");
        QTest::newRow("utf16be-cr") << int(QStringConverter::Utf16BE) << QString("\r");
        QTest::newRow("utf32le-lf") << int(QStringConverter::Utf32LE) << QString("\n");
    }
    void sameLogicalTextStillUsesRawCaptureIdentity() {
        auto fixture = makeFixture(); QVERIFY(fixture.workspace && fixture.cache);
        ZeroSlackCliRequest request; request.command = "context"; request.filePath = "rtl/top.sv"; request.line = 2;
        request.workspaceRoot = fixture.workspace->path(); request.cacheDirectory = fixture.cache->path();
        ZeroSlackCliService service;
        QVERIFY(service.execute(request).succeeded());
        const auto cachePath = service.cachePathForWorkspace(request.workspaceRoot, request.cacheDirectory);
        const auto oldCacheHash = fileHash(cachePath);
        request.forceRefresh = true;
        ZeroSlackCliService raced([&](auto stage) {
            if (stage != ZeroSlackCliService::ObservationStage::WorkspaceCaptured) return;
            QFile file(fixture.topFile); QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes = QByteArray::fromHex("efbbbf") + QString(fixture.originalTop).replace("\n", "\r\n").toUtf8();
            QCOMPARE(file.write(bytes), bytes.size());
        });
        const auto rejected = raced.execute(request);
        QVERIFY(!rejected.succeeded());
        QVERIFY(rejected.envelope.value("error").toObject().value("message").toString().contains("between"));
        QCOMPARE(fileHash(cachePath), oldCacheHash);
        request.forceRefresh = false; request.command = "status"; request.requireCurrent = true;
        QCOMPARE(service.execute(request).exitCode, 4);
        request.command = "context";
        QVERIFY(service.execute(request).succeeded());
        const auto newCacheHash = fileHash(cachePath);
        // A late change during a cache hit must also leave the coherent old cache intact.
        ZeroSlackCliService late([&](auto stage) {
            if (stage == ZeroSlackCliService::ObservationStage::BeforeOutput)
                QVERIFY(writeText(fixture.topFile, "// late\n" + fixture.originalTop));
        });
        QVERIFY(!late.execute(request).succeeded());
        QCOMPARE(fileHash(cachePath), newCacheHash);
    }
    void rawEncodingAndLogicalSnippetShareInput() {
        QFETCH(int, encoding); QFETCH(QString, ending);
        auto fixture = makeFixture(); QVERIFY(fixture.workspace && fixture.cache);
        const auto text = QString("// 中文 😀\n") + fixture.originalTop;
        QStringEncoder encoder(static_cast<QStringConverter::Encoding>(encoding), QStringConverter::Flag::WriteBom);
        const QByteArray bytes = encoder(QString(text).replace("\n", ending));
        { QFile file(fixture.topFile); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); }
        ZeroSlackCliRequest request; request.command = "context";
        request.workspaceRoot = fixture.workspace->path(); request.cacheDirectory = fixture.cache->path();
        request.filePath = "rtl/top.sv"; request.line = 3;
        const auto result = ZeroSlackCliService().execute(request);
        QVERIFY2(result.succeeded(), result.rendered.constData());
        const auto data = result.envelope.value("data").toObject();
        QVERIFY(result.rendered.contains(QString("中文 😀").toUtf8()));
        QVERIFY(!result.rendered.contains("\\r"));
        QFile cache(ZeroSlackCliService().cachePathForWorkspace(request.workspaceRoot, request.cacheDirectory));
        QVERIFY(cache.open(QIODevice::ReadOnly));
        const auto index = QJsonDocument::fromJson(cache.readAll()).object();
        bool matched = false;
        for (const auto& value : index.value("files").toArray()) {
            const auto file = value.toObject();
            if (file.value("path").toString() == "rtl/top.sv") {
                QCOMPARE(file.value("sha256").toString(), fileHash(fixture.topFile));
                QCOMPARE(file.value("size").toInt(), bytes.size()); matched = true;
            }
        }
        QVERIFY(matched);
        QVERIFY(!data.value("anchors").toArray().isEmpty());
    }
    void externalDependencyChangeBeforeOutputIsRejected() {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto root = fixture.filePath("root"), include = fixture.filePath("include");
        QVERIFY(writeText(root + "/top.sv", "`include \"def.svh\"\nmodule top; logic [`W-1:0] q; endmodule\n"));
        QVERIFY(writeText(include + "/def.svh", "`define W 8\n"));
        WorkspaceConfigurationService config;
        auto settings = config.load(root); settings.includeDirs = {include}; QVERIFY(config.save(settings));
        ZeroSlackCliRequest request; request.command = "bundle"; request.query = "q";
        request.workspaceRoot = root; request.cacheDirectory = fixture.filePath("cache");
        ZeroSlackCliService service([&](auto stage) {
            if (stage == ZeroSlackCliService::ObservationStage::BeforeOutput)
                QVERIFY(writeText(include + "/def.svh", "`define W 16\n"));
        });
        QVERIFY(!service.execute(request).succeeded());
        QVERIFY(!QFileInfo::exists(service.cachePathForWorkspace(root, request.cacheDirectory)));
        QVERIFY(ZeroSlackCliService().execute(request).succeeded());
        request.command = "status"; request.requireCurrent = true;
        QVERIFY(ZeroSlackCliService().execute(request).succeeded());
        QVERIFY(writeText(include + "/def.svh", "`define W 32\n"));
        QCOMPARE(ZeroSlackCliService().execute(request).exitCode, 4);
    }
    void statusValidatesFinalInput_data() {
        QTest::addColumn<QString>("scenario");
        QTest::addColumn<bool>("requireCurrent");
        for (const auto* scenario : {"current", "already-stale", "missing-cache", "unreadable-before",
                 "root-change", "root-delete", "root-added", "include-change", "include-delete",
                 "include-unreadable", "negative-created", "include-format"}) {
            for (bool required : {false, true}) {
                const auto label = QByteArray(scenario) + (required ? "-require-current" : "-report-only");
                QTest::newRow(label.constData()) << QString::fromLatin1(scenario) << required;
            }
        }
    }
    void statusValidatesFinalInput() {
        QFETCH(QString, scenario); QFETCH(bool, requireCurrent);
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("root"), first = fixture.filePath("first"), second = fixture.filePath("second");
        const QString top = root + "/top.sv", header = second + "/def.svh", missing = first + "/def.svh";
        const QString originalTop = "`include \"def.svh\"\nmodule top; logic [`W-1:0] q; endmodule\n";
        const QString originalHeader = "`define W 8\n";
        QVERIFY(writeText(top, originalTop)); QVERIFY(writeText(header, originalHeader));
        QVERIFY(QDir().mkpath(first));
        WorkspaceConfigurationService config;
        auto settings = config.load(root); settings.includeDirs = {first, second}; QVERIFY(config.save(settings));
        ZeroSlackCliRequest request; request.command = "scan";
        request.workspaceRoot = root; request.cacheDirectory = fixture.filePath("cache");
        ZeroSlackCliService stable; QVERIFY(stable.execute(request).succeeded());
        const auto cache = stable.cachePathForWorkspace(root, request.cacheDirectory);
        const auto scannedHash = fileHash(cache); QVERIFY(!scannedHash.isEmpty());
        if (scenario == "already-stale") QVERIFY(writeText(header, "`define W 16\n"));
        if (scenario == "missing-cache") QVERIFY(QFile::remove(cache));
        if (scenario == "unreadable-before") {
            QVERIFY(QFile::remove(header)); QVERIFY(QDir().mkdir(header));
        }
        const auto beforeHash = fileHash(cache);
        const auto beforeModified = QFileInfo(cache).lastModified();
        request.command = "status"; request.requireCurrent = requireCurrent;
        bool observed = false, changed = true;
        ZeroSlackCliService raced([&](auto stage) {
            if (stage != ZeroSlackCliService::ObservationStage::BeforeOutput) return;
            observed = true;
            if (scenario == "root-change") changed = writeText(top, originalTop + "// changed\n");
            if (scenario == "root-delete") changed = QFile::remove(top);
            if (scenario == "root-added") changed = writeText(root + "/added.sv", "module added; endmodule\n");
            if (scenario == "include-change") changed = writeText(header, "`define W 32\n");
            if (scenario == "include-delete") changed = QFile::remove(header);
            if (scenario == "include-unreadable") changed = QFile::remove(header) && QDir().mkdir(header);
            if (scenario == "negative-created") changed = writeText(missing, "`define W 64\n");
            if (scenario == "include-format") {
                QFile file(header);
                const auto bytes = QByteArray(QStringEncoder(QStringConverter::Utf16LE,
                    QStringConverter::Flag::WriteBom)(originalHeader));
                changed = file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
            }
        });
        const auto result = raced.execute(request);
        QVERIFY(observed); QVERIFY(changed);
        QCOMPARE(fileHash(cache), beforeHash);
        QCOMPARE(QFileInfo(cache).lastModified(), beforeModified);
        const bool initiallyStale = scenario == "already-stale" || scenario == "missing-cache" || scenario == "unreadable-before";
        const bool remainsCurrent = scenario == "current" || scenario == "include-format";
        const int expectedExit = initiallyStale ? (requireCurrent ? 4 : 0) : (remainsCurrent ? 0 : 3);
        QCOMPARE(result.exitCode, expectedExit);
        QVERIFY(!result.envelope.value("cacheRebuilt").toBool());
        if (expectedExit == 3) {
            QVERIFY(!result.envelope.contains("data"));
            QCOMPARE(result.envelope.value("error").toObject().value("code").toString(), QString("source_changed"));
        } else {
            const auto data = result.envelope.value("data").toObject();
            QCOMPARE(data.value("cacheCurrent").toBool(), remainsCurrent);
            QCOMPARE(data.value("cacheExists").toBool(), scenario != "missing-cache");
            if (expectedExit == 4)
                QCOMPARE(result.envelope.value("error").toObject().value("code").toString(), QString("cache_stale"));
        }
        request.requireCurrent = true;
        QCOMPARE(stable.execute(request).exitCode, remainsCurrent ? 0 : 4);
        QCOMPARE(fileHash(cache), beforeHash);
        if (!remainsCurrent) {
            if (QFileInfo(header).isDir()) QVERIFY(QDir().rmdir(header));
            QVERIFY(writeText(header, originalHeader)); QVERIFY(writeText(top, originalTop));
            if (QFileInfo::exists(missing)) QVERIFY(QFile::remove(missing));
            if (QFileInfo::exists(root + "/added.sv")) QVERIFY(QFile::remove(root + "/added.sv"));
            request.command = "scan"; QVERIFY(stable.execute(request).succeeded());
            request.command = "status"; QVERIFY(stable.execute(request).succeeded());
            QVERIFY(!fileHash(cache).isEmpty());
        }
    }
    void bundleCountsCompleteMarkdown() {
        auto fixture = makeFixture(); QVERIFY(fixture.workspace && fixture.cache);
        ZeroSlackCliRequest request; request.command = "bundle"; request.query = QString(700, 'x');
        request.workspaceRoot = fixture.workspace->path(); request.cacheDirectory = fixture.cache->path();
        ZeroSlackCliService service;
        for (int i = 0; i < 3; ++i) {
            const auto result = service.execute(request); QVERIFY(result.succeeded());
            request.maxTokens = result.envelope.value("data").toObject().value("estimatedTokens").toInt();
        }
        auto result = service.execute(request); QVERIFY(result.succeeded());
        auto data = result.envelope.value("data").toObject();
        QCOMPARE(data.value("estimatedTokens").toInt(), request.maxTokens);
        request.query += QString::fromUtf8("😀");
        QVERIFY(!service.execute(request).succeeded());
        request.query = QString(2000, 'x'); request.maxTokens = 128;
        QVERIFY(!service.execute(request).succeeded());

        request.query = QString(330, 'x') + QString::fromUtf8("中文😀"); request.maxTokens = 4000;
        PinloomCodeLinkStore links; links.setWorkspaceRoot(fixture.workspace->path());
        SlangManager slang;
        const auto source = PinloomSourceSelection::fromSemanticSymbol(fixture.workspace->path(),
            fixture.originalTop, recordNamed(slang.extractSymbolRecords(fixture.topFile, fixture.originalTop), "q"));
        QVERIFY(links.addLink(source, QUrl("pinloom://anchor/boundary"), request.query, {}));
        result = service.execute(request); QVERIFY(result.succeeded());
        data = result.envelope.value("data").toObject();
        QVERIFY(data.value("symbols").toArray().isEmpty());
        QCOMPARE(data.value("anchors").toArray().size(), 1);
        // Find the exact three-digit threshold so header's printed budget is stable.
        int threshold = 0;
        for (int budget = 128; budget < 300; ++budget) {
            request.maxTokens = budget;
            const auto bounded = service.execute(request);
            if (!bounded.succeeded()) {
                QVERIFY(bounded.envelope.value("error").toObject().value("message").toString().contains("header"));
                continue;
            }
            const auto payload = bounded.envelope.value("data").toObject();
            const auto markdown = payload.value("markdown").toString().toUtf8();
            const int actual = (markdown.size() + 3) / 4;
            QCOMPARE(payload.value("estimatedTokens").toInt(), actual);
            QVERIFY(actual <= budget);
            if (!payload.value("anchors").toArray().isEmpty()) { threshold = budget; break; }
            QVERIFY(!markdown.contains("## Pinloom links"));
        }
        QVERIFY(threshold > 128);
        request.maxTokens = threshold - 1;
        data = service.execute(request).envelope.value("data").toObject();
        QVERIFY(data.value("anchors").toArray().isEmpty());
        QVERIFY(!data.value("markdown").toString().contains("## Pinloom links"));
    }

    void changedUsesLiteralGitFileNames()
    {
        QVERIFY2(!QStandardPaths::findExecutable("git").isEmpty(), "Git is required for the changed command regression");
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const auto root = fixture.filePath("repository"); QDir().mkpath(root);
        const auto git = [&root](const QStringList& args) {
            QProcess process; process.setWorkingDirectory(root);
            process.start("git", args);
            return process.waitForFinished(15000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        };
        QVERIFY(git({"init", "-q"}));
        const QStringList paths = {QString::fromUtf8("中文目录/时钟.sv"), "space name.sv", " leading;name.sv", QString::fromUtf8("新建 计数器.sv")};
        for (int i = 0; i < paths.size() - 1; ++i)
            QVERIFY(writeText(QDir(root).filePath(paths[i]), QString("module m%1; logic q; endmodule\n").arg(i)));
        QVERIFY(git({"add", "--", "."}));
        QVERIFY(git({"-c", "user.name=ZeroSlack test", "-c", "user.email=test@example.invalid", "commit", "-qm", "fixture"}));
        PinloomCodeLinkStore store; store.setWorkspaceRoot(root);
        SlangManager slang;
        for (int i = 0; i < paths.size(); ++i) {
            const auto file = QDir(root).filePath(paths[i]);
            const auto text = QString("module m%1; logic q; assign q = 1'b0; endmodule\n").arg(i);
            QVERIFY(writeText(file, text));
            const auto symbol = recordNamed(slang.extractSymbolRecords(file, text), "q");
            const auto source = PinloomSourceSelection::fromSemanticSymbol(root, text, symbol);
            QVERIFY(source.isValid());
            QVERIFY(store.addLink(source, QUrl(QString("pinloom://anchor/q%1").arg(i)), "q", {}));
        }
        ZeroSlackCliService service; ZeroSlackCliRequest request;
        request.command = "changed"; request.workspaceRoot = root; request.baseRef = "HEAD";
        request.cacheDirectory = fixture.filePath("cache");
        const auto result = service.execute(request); QCOMPARE(result.exitCode, 0);
        const auto data = result.envelope.value("data").toObject();
        for (const auto& path : paths) {
            QVERIFY(data.value("files").toArray().contains(path));
            bool symbolFound = false, anchorFound = false;
            for (const auto& value : data.value("symbols").toArray())
                symbolFound |= value.toObject().value("file").toString() == path;
            for (const auto& value : data.value("anchors").toArray())
                anchorFound |= value.toObject().value("file").toString() == path;
            QVERIFY2(symbolFound && anchorFound, qPrintable(path));
        }
        const auto cached = service.execute(request); QCOMPARE(cached.exitCode, 0);
        QCOMPARE(cached.envelope.value("data"), result.envelope.value("data"));
        QVERIFY(!cached.envelope.value("cacheRebuilt").toBool());
    }

    void invalidConfigurationDoesNotReuseGoodCache()
    {
        QTemporaryDir fixture; QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("workspace");
        QVERIFY(writeText(QDir(root).filePath("top.sv"), "module top; endmodule\n"));
        ZeroSlackCliService service;
        ZeroSlackCliRequest request; request.workspaceRoot = root;
        request.cacheDirectory = fixture.filePath("cache"); request.command = "scan";
        QCOMPARE(service.execute(request).exitCode, 0);
        const auto path = WorkspaceConfigurationService::projectFilePath(root);
        for (const auto& bytes : {QString("{broken"), QString(R"({"schema":"ZeroSlack.ProjectConfiguration","version":99})")}) {
            QVERIFY(writeText(path, bytes));
            for (const auto* command : {"scan", "status", "summary"}) {
                request.command = command;
                const auto result = service.execute(request);
                QVERIFY2(result.exitCode != 0, command);
                QVERIFY(!result.envelope.value("ok").toBool());
                QVERIFY(!result.envelope.value("cacheRebuilt").toBool());
            }
            QCOMPARE(fileHash(path), QString::fromLatin1(QCryptographicHash::hash(bytes.toUtf8(), QCryptographicHash::Sha256).toHex()));
        }
    }

    void cacheTracksTopAndConfiguration()
    {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath(QStringLiteral("workspace"));
        QVERIFY(writeText(QDir(root).filePath(QStringLiteral("tops.sv")),
            QStringLiteral("module first; endmodule\nmodule second; endmodule\n")));
        WorkspaceConfigurationService configurationService;
        auto configuration = configurationService.defaultConfiguration(root);
        configuration.topModule = QStringLiteral("first");
        QVERIFY(configurationService.save(configuration));
        ZeroSlackCliService service;
        ZeroSlackCliRequest request;
        request.command = QStringLiteral("scan");
        request.workspaceRoot = root;
        request.cacheDirectory = fixture.filePath(QStringLiteral("cache"));
        QCOMPARE(service.execute(request).exitCode, 0);
        request.command = QStringLiteral("status");
        QVERIFY(service.execute(request).envelope.value(QStringLiteral("data"))
            .toObject().value(QStringLiteral("cacheCurrent")).toBool());
        configuration = configurationService.load(root);
        configuration.topModule = QStringLiteral("second");
        QVERIFY(configurationService.save(configuration));
        QVERIFY(!service.execute(request).envelope.value(QStringLiteral("data"))
            .toObject().value(QStringLiteral("cacheCurrent")).toBool());
        request.command = QStringLiteral("summary");
        QVERIFY(service.execute(request).envelope.value(QStringLiteral("cacheRebuilt")).toBool());
        configuration = configurationService.load(root);
        configuration.defines.insert(QStringLiteral("MODE"), QStringLiteral("2"));
        QVERIFY(configurationService.save(configuration));
        request.command = QStringLiteral("status");
        QVERIFY(!service.execute(request).envelope.value(QStringLiteral("data"))
            .toObject().value(QStringLiteral("cacheCurrent")).toBool());
    }

    void cacheTracksExternalAndMissingIncludes()
    {
        for (bool initiallyMissing : {false, true}) {
            QTemporaryDir fixture;
            QVERIFY(fixture.isValid());
            const QString root = fixture.filePath(QStringLiteral("workspace"));
            const QString header = fixture.filePath(QStringLiteral("external/decls.svh"));
            const QString top = QDir(root).filePath(QStringLiteral("top.sv"));
            QVERIFY(writeText(top, QStringLiteral(
                "module top;\n`include \"../external/decls.svh\"\nendmodule\n")));
            if (!initiallyMissing)
                QVERIFY(writeText(header, QStringLiteral("logic dep_old;\n")));
            const auto oldTime = QFileInfo(header).lastModified();
            const auto topHash = fileHash(top);
            ZeroSlackCliService service;
            ZeroSlackCliRequest request;
            request.command = QStringLiteral("scan");
            request.workspaceRoot = root;
            request.cacheDirectory = fixture.filePath(QStringLiteral("cache"));
            const auto scanned = service.execute(request);
            QCOMPARE(scanned.exitCode, 0);
            request.command = QStringLiteral("status");
            QVERIFY(service.execute(request).envelope.value(QStringLiteral("data"))
                .toObject().value(QStringLiteral("cacheCurrent")).toBool());
            QVERIFY(writeText(header, QStringLiteral("logic dep_new;\n")));
            if (!initiallyMissing) {
                QFile changed(header);
                QVERIFY(changed.open(QIODevice::ReadWrite));
                QVERIFY(changed.setFileTime(oldTime, QFileDevice::FileModificationTime));
            }
            QCOMPARE(fileHash(top), topHash);
            QVERIFY(!service.execute(request).envelope.value(QStringLiteral("data"))
                .toObject().value(QStringLiteral("cacheCurrent")).toBool());
            request.command = QStringLiteral("symbol");
            request.symbol = QStringLiteral("dep_new");
            request.allowRefresh = false;
            QCOMPARE(service.execute(request).exitCode, 3);
            request.allowRefresh = true;
            const auto refreshed = service.execute(request);
            QCOMPARE(refreshed.exitCode, 0);
            QVERIFY(refreshed.envelope.value(QStringLiteral("cacheRebuilt")).toBool());
            QVERIFY(!refreshed.envelope.value(QStringLiteral("data"))
                .toObject().value(QStringLiteral("matches")).toArray().isEmpty());
            const auto warm = service.execute(request);
            QCOMPARE(warm.exitCode, 0);
            QVERIFY(!warm.envelope.value(QStringLiteral("cacheRebuilt")).toBool());
        }
    }

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
