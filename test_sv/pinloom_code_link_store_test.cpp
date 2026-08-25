#include "pinloomcodelinkstore.h"
#include "semanticindex.h"
#include "tsdocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

class PinloomCodeLinkStoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void persistsAndRelocatesWorkspaceRelativeLinks();
    void groupsMultipleTargetsAndSurvivesAssignEdits();
    void survivesAlwaysBodyEdits();
    void migratesV1WithoutDroppingLinks();
    void rejectsArbitraryNewSelections();
    void survivesSymbolMoveAndRename();
    void normalizesAnchorTextWithoutRegex();
};

void PinloomCodeLinkStoreTest::persistsAndRelocatesWorkspaceRelativeLinks()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString filePath =
        QDir(workspace.path()).filePath(QStringLiteral("rtl/top.sv"));
    const QString original = QStringLiteral(
        "module top;\n"
        "  logic reset_value;\n"
        "  assign reset_value = 1'b0;\n"
        "endmodule\n");
    TSDocument syntax;
    syntax.setText(original);
    const TSBindableCodeAnchor assign = syntax.bindableCodeAnchorAt(
        original.indexOf(QStringLiteral("assign")));
    QVERIFY(assign.ok());
    const int start = assign.startChar;
    const PinloomSourceSelection source =
        PinloomSourceSelection::fromSyntaxAnchor(
            workspace.path(), filePath, original, assign);
    QVERIFY(source.isValid());

    PinloomCodeLinkStore store;
    store.setWorkspaceRoot(workspace.path());
    QString failure;
    QVERIFY2(store.addLink(
                 source,
                 QUrl(QStringLiteral(
                     "pinloom://entry/anchor:test?resource=r&anchor=a")),
                 QStringLiteral("Reset assignment"),
                 {{QStringLiteral("resourceId"), QStringLiteral("r")},
                  {QStringLiteral("anchorId"), QStringLiteral("a")}},
                 &failure),
             qPrintable(failure));
    QVERIFY(QFileInfo::exists(store.storagePath()));
    QFile persistedFile(store.storagePath());
    QVERIFY(persistedFile.open(QIODevice::ReadOnly));
    const QJsonObject persisted =
        QJsonDocument::fromJson(persistedFile.readAll()).object();
    QCOMPARE(persisted.value(QStringLiteral("version")).toInt(), 2);
    QCOMPARE(persisted.value(QStringLiteral("anchors"))
                 .toArray().size(), 1);
    QVERIFY(!persisted.contains(QStringLiteral("links")));

    PinloomCodeLinkStore restored;
    restored.setWorkspaceRoot(workspace.path());
    QCOMPARE(restored.records().size(), 1);
    QList<ResolvedPinloomCodeLink> resolved =
        restored.linksForDocument(filePath, original);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.first().resolution,
             PinloomCodeLinkResolution::Exact);
    QCOMPARE(resolved.first().startPosition, start);

    const QString moved = QStringLiteral("// inserted\n") + original;
    resolved = restored.linksForDocument(filePath, moved);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.first().resolution,
             PinloomCodeLinkResolution::Moved);
    QCOMPARE(resolved.first().startPosition,
             start + QStringLiteral("// inserted\n").size());
    QCOMPARE(restored.linksAtPosition(
                 filePath, moved, resolved.first().startPosition + 2).size(),
             1);
}

void PinloomCodeLinkStoreTest::groupsMultipleTargetsAndSurvivesAssignEdits()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString filePath =
        QDir(workspace.path()).filePath(QStringLiteral("rtl/top.sv"));
    const QString original = QStringLiteral(
        "module top;\n"
        "  logic reset_value;\n"
        "  assign reset_value = 1'b0;\n"
        "endmodule\n");
    TSDocument syntax;
    syntax.setText(original);
    const TSBindableCodeAnchor assign = syntax.bindableCodeAnchorAt(
        original.indexOf(QStringLiteral("reset_value =")));
    QVERIFY(assign.ok());
    const PinloomSourceSelection source =
        PinloomSourceSelection::fromSyntaxAnchor(
            workspace.path(), filePath, original, assign);
    QCOMPARE(source.anchorKind,
             PinloomCodeAnchorKind::ContinuousAssign);

    PinloomCodeLinkStore store;
    store.setWorkspaceRoot(workspace.path());
    QString failure;
    QVERIFY2(store.addLink(
                 source,
                 QUrl(QStringLiteral(
                     "pinloom://entry/anchor:first?resource=r&anchor=a")),
                 QStringLiteral("First"),
                 {{QStringLiteral("resourceId"), QStringLiteral("r")},
                  {QStringLiteral("anchorId"), QStringLiteral("a")}},
                 &failure),
             qPrintable(failure));
    QVERIFY2(store.addLink(
                 source,
                 QUrl(QStringLiteral(
                     "pinloom://entry/anchor:second?resource=r&anchor=b")),
                 QStringLiteral("Second"),
                 {{QStringLiteral("resourceId"), QStringLiteral("r")},
                  {QStringLiteral("anchorId"), QStringLiteral("b")}},
                 &failure),
             qPrintable(failure));
    QCOMPARE(store.anchors().size(), 1);
    QCOMPARE(store.anchors().constFirst().links.size(), 2);
    QCOMPARE(store.records().size(), 2);

    const QString edited = QStringLiteral("// moved\n")
        + QString(original).replace(
            QStringLiteral("1'b0"), QStringLiteral("enable_i"));
    TSDocument editedSyntax;
    editedSyntax.setText(edited);
    const QList<ResolvedPinloomCodeLink> resolved =
        store.linksForDocument(filePath, edited, &editedSyntax);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.constFirst().resolution,
             PinloomCodeLinkResolution::Moved);
    QCOMPARE(resolved.constFirst().linkCount(), 2);
    QCOMPARE(edited.mid(resolved.constFirst().startPosition,
                        resolved.constFirst().endPosition
                            - resolved.constFirst().startPosition),
             QStringLiteral("assign reset_value = enable_i;"));
}

void PinloomCodeLinkStoreTest::survivesAlwaysBodyEdits()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString filePath =
        QDir(workspace.path()).filePath(QStringLiteral("rtl/top.sv"));
    const QString original = QStringLiteral(
        "module top(input logic clk, d, x, output logic q, other);\n"
        "  always_ff @(posedge clk) begin\n"
        "    q <= d;\n"
        "  end\n"
        "  always_ff @(posedge clk) begin\n"
        "    other <= x;\n"
        "  end\n"
        "endmodule\n");
    TSDocument syntax;
    syntax.setText(original);
    const TSBindableCodeAnchor always = syntax.bindableCodeAnchorAt(
        original.indexOf(QStringLiteral("q <=")));
    QVERIFY(always.ok());
    const PinloomSourceSelection source =
        PinloomSourceSelection::fromSyntaxAnchor(
            workspace.path(), filePath, original, always);
    QCOMPARE(source.anchorKind, PinloomCodeAnchorKind::AlwaysBlock);

    PinloomCodeLinkStore store;
    store.setWorkspaceRoot(workspace.path());
    QString failure;
    QVERIFY2(store.addLink(
                 source,
                 QUrl(QStringLiteral(
                     "pinloom://entry/anchor:always?resource=r&anchor=p")),
                 QStringLiteral("Sequential process"),
                 {{QStringLiteral("resourceId"), QStringLiteral("r")},
                  {QStringLiteral("anchorId"), QStringLiteral("p")}},
                 &failure),
             qPrintable(failure));

    const QString edited = QString(original).replace(
        QStringLiteral("q <= d;"),
        QStringLiteral("q <= d & enable_i;"));
    TSDocument editedSyntax;
    editedSyntax.setText(edited);
    const QList<ResolvedPinloomCodeLink> resolved =
        store.linksForDocument(filePath, edited, &editedSyntax);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.constFirst().resolution,
             PinloomCodeLinkResolution::Moved);
    QVERIFY(resolved.constFirst().available());
    const QString resolvedText = edited.mid(
        resolved.constFirst().startPosition,
        resolved.constFirst().endPosition
            - resolved.constFirst().startPosition);
    QVERIFY(resolvedText.contains(QStringLiteral("q <=")));
    QVERIFY(!resolvedText.contains(QStringLiteral("other <=")));
}

void PinloomCodeLinkStoreTest::migratesV1WithoutDroppingLinks()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString filePath =
        QDir(workspace.path()).filePath(QStringLiteral("rtl/top.sv"));
    const QString text = QStringLiteral(
        "module top;\n  assign a = b;\nendmodule\n");
    const int start = text.indexOf(QStringLiteral("assign"));
    const int end = text.indexOf(QLatin1Char(';'), start) + 1;
    const PinloomSourceSelection legacy =
        PinloomSourceSelection::fromDocumentSelection(
            workspace.path(), filePath, QStringLiteral("top"),
            text, start, end);
    const QString metadataDirectory =
        QDir(workspace.path()).filePath(QStringLiteral(".zeroslack"));
    QVERIFY(QDir().mkpath(metadataDirectory));
    QFile file(QDir(metadataDirectory).filePath(
        QStringLiteral("pinloom-links.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QJsonObject legacyRecord{
        {QStringLiteral("id"), QStringLiteral("legacy")},
        {QStringLiteral("title"), QStringLiteral("Legacy")},
        {QStringLiteral("uri"), QStringLiteral(
            "pinloom://entry/anchor:legacy?resource=r&anchor=a")},
        {QStringLiteral("identity"), QJsonObject{
            {QStringLiteral("resourceId"), QStringLiteral("r")},
            {QStringLiteral("anchorId"), QStringLiteral("a")}}},
        {QStringLiteral("source"),
         QJsonObject::fromVariantMap(legacy.toVariantMap())},
        {QStringLiteral("createdAtUtc"),
         QStringLiteral("2026-01-01T00:00:00.000Z")}
    };
    const QJsonObject v1{
        {QStringLiteral("schema"),
         QStringLiteral("ZeroSlack.PinloomCodeLinks")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("links"), QJsonArray{legacyRecord}}
    };
    QCOMPARE(file.write(QJsonDocument(v1).toJson()),
             QJsonDocument(v1).toJson().size());
    file.close();

    PinloomCodeLinkStore store;
    store.setWorkspaceRoot(workspace.path());
    QCOMPARE(store.anchors().size(), 1);
    QCOMPARE(store.records().size(), 1);
    QCOMPARE(store.anchors().constFirst().source.anchorKind,
             PinloomCodeAnchorKind::LegacySelection);
    const QList<ResolvedPinloomCodeLink> resolved =
        store.linksForDocument(filePath, text);
    QCOMPARE(resolved.size(), 1);
    QVERIFY(resolved.constFirst().available());
}

void PinloomCodeLinkStoreTest::rejectsArbitraryNewSelections()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString filePath =
        QDir(workspace.path()).filePath(QStringLiteral("rtl/top.sv"));
    const QString text = QStringLiteral(
        "module top;\n  logic a;\nendmodule\n");
    const PinloomSourceSelection arbitrary =
        PinloomSourceSelection::fromDocumentSelection(
            workspace.path(), filePath, QStringLiteral("top"),
            text, text.indexOf(QStringLiteral("logic")),
            text.indexOf(QLatin1Char(';'),
                         text.indexOf(QStringLiteral("logic"))) + 1);
    QVERIFY(arbitrary.isValid());
    PinloomCodeLinkStore store;
    store.setWorkspaceRoot(workspace.path());
    QString failure;
    QVERIFY(!store.addLink(
        arbitrary,
        QUrl(QStringLiteral(
            "pinloom://entry/anchor:invalid?resource=r&anchor=a")),
        QStringLiteral("Invalid"), {}, &failure));
    QVERIFY(!failure.isEmpty());
    QVERIFY(store.records().isEmpty());
}

void PinloomCodeLinkStoreTest::survivesSymbolMoveAndRename()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString filePath =
        QDir(workspace.path()).filePath(QStringLiteral("rtl/top.sv"));
    const QString original = QStringLiteral(
        "module top;\n"
        "  logic [7:0] count;\n"
        "  logic [7:0] shadow_count;\n"
        "endmodule\n");
    const int originalPosition =
        original.indexOf(QStringLiteral("count"));
    SemanticSymbolRecord symbol;
    symbol.name = QStringLiteral("count");
    symbol.stableKey.fileName = filePath;
    symbol.stableKey.symbolName = symbol.name;
    symbol.stableKey.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    symbol.stableKey.ownerScope = QStringLiteral("top");
    symbol.stableKey.sourcePosition = originalPosition;
    symbol.stableKey.sourceLength = symbol.name.size();
    symbol.location.fileName = filePath;
    symbol.location.startLine = 2;
    symbol.location.startColumn = 15;
    symbol.location.position = originalPosition;
    symbol.location.length = symbol.name.size();
    symbol.declarationKind = SymbolTaxonomy::DeclarationKind::Signal;
    symbol.collectorKind = SymbolTaxonomy::CollectorKind::Logic;
    symbol.visibility = SymbolTaxonomy::SymbolVisibility::ScopeLocal;
    symbol.owner.kind = SymbolTaxonomy::SymbolOwnerScope::Module;
    symbol.owner.name = QStringLiteral("top");
    symbol.type.rawTypeText = QStringLiteral("logic [7:0]");
    symbol.presentation.declarationText =
        QStringLiteral("logic [7:0] count;");

    SemanticSymbolRecord sibling = symbol;
    sibling.name = QStringLiteral("shadow_count");
    sibling.location.position =
        original.indexOf(QStringLiteral("shadow_count"));
    sibling.location.length = sibling.name.size();
    sibling.location.startLine = 3;
    sibling.stableKey.symbolName = sibling.name;
    sibling.stableKey.sourcePosition = sibling.location.position;
    sibling.stableKey.sourceLength = sibling.name.size();
    sibling.presentation.declarationText =
        QStringLiteral("logic [7:0] shadow_count;");

    SemanticIndex* index = SemanticIndex::getInstance();
    index->clearSemanticState();
    index->updateSymbolRecordsForFile(filePath, {symbol, sibling}, original);
    const PinloomSourceSelection source =
        PinloomSourceSelection::fromSemanticSymbol(
            workspace.path(), original, symbol);
    QVERIFY(source.isValid());
    QCOMPARE(source.anchorKind, PinloomCodeAnchorKind::Symbol);

    PinloomCodeLinkStore store;
    store.setWorkspaceRoot(workspace.path());
    QString failure;
    QVERIFY2(store.addLink(
                 source,
                 QUrl(QStringLiteral(
                     "pinloom://entry/anchor:symbol?resource=r&anchor=s")),
                 QStringLiteral("Counter"),
                 {{QStringLiteral("resourceId"), QStringLiteral("r")},
                  {QStringLiteral("anchorId"), QStringLiteral("s")}},
                 &failure),
             qPrintable(failure));

    const QString edited = QStringLiteral("// moved\n")
        + QString(original).replace(
            QStringLiteral("logic [7:0] count;"),
            QStringLiteral("logic [7:0] sample_count;"));
    SemanticSymbolRecord renamed = symbol;
    renamed.name = QStringLiteral("sample_count");
    renamed.location.position =
        edited.indexOf(QStringLiteral("sample_count"));
    renamed.location.length = renamed.name.size();
    renamed.location.startLine = 3;
    renamed.location.startColumn = 15;
    renamed.stableKey.symbolName = renamed.name;
    renamed.stableKey.sourcePosition = renamed.location.position;
    renamed.stableKey.sourceLength = renamed.name.size();
    renamed.presentation.declarationText =
        QStringLiteral("logic [7:0] sample_count;");
    SemanticSymbolRecord movedSibling = sibling;
    movedSibling.location.position =
        edited.indexOf(QStringLiteral("shadow_count"));
    movedSibling.location.startLine = 4;
    movedSibling.stableKey.sourcePosition =
        movedSibling.location.position;
    index->updateSymbolRecordsForFile(
        filePath, {renamed, movedSibling}, edited);
    const QList<ResolvedPinloomCodeLink> resolved =
        store.linksForDocument(filePath, edited);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.constFirst().resolution,
             PinloomCodeLinkResolution::Moved);
    QCOMPARE(edited.mid(resolved.constFirst().startPosition,
                        resolved.constFirst().endPosition
                            - resolved.constFirst().startPosition),
             QStringLiteral("sample_count"));
    index->clearSemanticState();
}

void PinloomCodeLinkStoreTest::normalizesAnchorTextWithoutRegex()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString filePath =
        QDir(workspace.path()).filePath(QStringLiteral("rtl/top.sv"));
    const QString commented = QStringLiteral(
        "module top;\n"
        "  always_comb begin\n"
        "    /* block\n       comment */ foo_bar$1 = data_2; // tail\n"
        "  end\n"
        "endmodule\n");
    const QString compact = QStringLiteral(
        "module top;\n"
        "always_comb begin foo_bar$1=data_2; end\n"
        "endmodule\n");

    TSDocument commentedSyntax;
    commentedSyntax.setText(commented);
    const TSBindableCodeAnchor commentedAnchor =
        commentedSyntax.bindableCodeAnchorAt(
            commented.indexOf(QStringLiteral("foo_bar$1")));
    QVERIFY(commentedAnchor.ok());
    TSDocument compactSyntax;
    compactSyntax.setText(compact);
    const TSBindableCodeAnchor compactAnchor =
        compactSyntax.bindableCodeAnchorAt(
            compact.indexOf(QStringLiteral("foo_bar$1")));
    QVERIFY(compactAnchor.ok());

    const PinloomSourceSelection commentedSource =
        PinloomSourceSelection::fromSyntaxAnchor(
            workspace.path(), filePath, commented, commentedAnchor);
    const PinloomSourceSelection compactSource =
        PinloomSourceSelection::fromSyntaxAnchor(
            workspace.path(), filePath, compact, compactAnchor);
    QCOMPARE(commentedSource.structuralFingerprint,
             compactSource.structuralFingerprint);
    QVERIFY(commentedSource.semanticTokens.contains(
        QStringLiteral("foo_bar$1")));
    QVERIFY(commentedSource.semanticTokens.contains(
        QStringLiteral("data_2")));
}

QTEST_MAIN(PinloomCodeLinkStoreTest)
#include "pinloom_code_link_store_test.moc"
