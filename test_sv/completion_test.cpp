// Headless completion-logic test. Populates semantic records from Slang, then drives CompletionManager's
// public query methods and asserts the results. No GUI window is shown.
#include "slangmanager.h"
#include "alternatecommandservice.h"
#include "analysisscheduler.h"
#include "completioncontexthelper.h"
#include "completionmanager.h"
#include "completionmodel.h"
#include "completionsemanticquery.h"
#include "completionservice.h"
#include "codetemplateservice.h"
#include "customabbreviationservice.h"
#include "diagnosticsrefreshcontroller.h"
#include "documentmodel.h"
#include "editorsemanticcontextservice.h"
#include "foldblockshelfmodel.h"
#include "foldshelfpersistenceservice.h"
#include "foldshelfrestoreservice.h"
#include "formatterservice.h"
#include "ghostannotationservice.h"
#include "globalcontrolservice.h"
#include "mycodeeditor.h"
#include "myhighlighter.h"
#include "relationshipservice.h"
#include "rtlbatcheditservice.h"
#include "semanticdecorationservice.h"
#include "semantic_fixture_records.h"
#include "semanticindexsnapshot.h"
#include "smartrelationshipbuilder.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "symbolanalyzer.h"
#include "symboltaxonomy.h"
#include "tsdocument.h"
#include "usertemplateservice.h"
#include "wavepreviewservice.h"
#include "workspaceanalysisplanservice.h"
#include "workspaceanalysisrequestqueue.h"
#include "workspaceignoreservice.h"
#include "workspacemanager.h"
#include "workspacesymbolanalysiscontroller.h"
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextStream>
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QKeyEvent>
#include <QSettings>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <QTemporaryDir>
#include <QTextCursor>

static int g_checks = 0;
static int g_fails = 0;

static std::shared_ptr<SemanticIndexSnapshot> sharedSnapshotFromRecords(
    const QList<SemanticSymbolRecord>& records,
    const QList<SemanticRelationship>& relationships = {},
    const QList<SemanticDiagnostic>& diagnostics = {},
    const QHash<QString, QString>& fileContents = {})
{
    return std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records,
            relationships,
            diagnostics,
            fileContents));
}

static QStringList sorted(QStringList l) { l.sort(); return l; }

static void expectEq(const char* what, const QString& got, const QString& want) {
    ++g_checks;
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=\"%s\" want=\"%s\"\n", ok ? "PASS" : "FAIL", what,
           got.toLocal8Bit().constData(), want.toLocal8Bit().constData());
}

static void expectBool(const char* what, bool got, bool want) {
    ++g_checks;
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=%s want=%s\n", ok ? "PASS" : "FAIL", what,
           got ? "true" : "false", want ? "true" : "false");
}

static bool sendEditorKey(MyCodeEditor& editor,
                          int key,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                          const QString& text = QString())
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    QCoreApplication::sendEvent(&editor, &event);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return event.isAccepted();
}

static void insertAtEditorCursor(MyCodeEditor& editor, const QString& text)
{
    QTextCursor cursor = editor.textCursor();
    cursor.insertText(text);
    editor.setTextCursor(cursor);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static void expectList(const char* what, QStringList got, QStringList want) {
    ++g_checks;
    got = sorted(got);
    want = sorted(want);
    bool ok = (got == want);
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", what,
           got.join(",").toLocal8Bit().constData(),
           want.join(",").toLocal8Bit().constData());
}

static void expectExcludes(const char* what, const QStringList& got, const QStringList& mustNot,
                           const QStringList& mustHave) {
    ++g_checks;
    bool ok = true;
    for (const QString& n : mustNot) if (got.contains(n)) ok = false;
    for (const QString& n : mustHave) if (!got.contains(n)) ok = false;
    if (!ok) ++g_fails;
    printf("[%s] %-34s got=[%s]\n", ok ? "PASS" : "FAIL", what,
           got.join(",").toLocal8Bit().constData());
}

template <typename Predicate>
static bool waitForEventPredicate(Predicate predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() <= timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (predicate())
            return true;
    }
    return predicate();
}

static void expectGhostContains(const char* what,
                                const GhostAnnotationReport& report,
                                GhostAnnotationKind kind,
                                int line,
                                const QString& contains)
{
    ++g_checks;
    bool ok = false;
    for (const GhostAnnotation& annotation : report.annotations) {
        if (annotation.kind == kind
            && annotation.line == line
            && annotation.text.contains(contains)) {
            ok = true;
            break;
        }
    }
    if (!ok)
        ++g_fails;
    QStringList seen;
    for (const GhostAnnotation& annotation : report.annotations) {
        if (annotation.line == line)
            seen << annotation.text;
    }
    printf("[%s] %-34s want=\"%s\" line=%d seen=[%s]\n",
           ok ? "PASS" : "FAIL",
           what,
           contains.toLocal8Bit().constData(),
           line,
           seen.join(",").toLocal8Bit().constData());
}

static void expectGhostNotContains(const char* what,
                                   const GhostAnnotationReport& report,
                                   GhostAnnotationKind kind,
                                   int line,
                                   const QString& contains)
{
    ++g_checks;
    bool ok = true;
    QStringList seen;
    for (const GhostAnnotation& annotation : report.annotations) {
        if (annotation.line == line)
            seen << annotation.text;
        if (annotation.kind == kind
            && annotation.line == line
            && annotation.text.contains(contains)) {
            ok = false;
        }
    }
    if (!ok)
        ++g_fails;
    printf("[%s] %-34s reject=\"%s\" line=%d seen=[%s]\n",
           ok ? "PASS" : "FAIL",
           what,
           contains.toLocal8Bit().constData(),
           line,
           seen.join(",").toLocal8Bit().constData());
}

static void expectGhostLineNotContains(const char* what,
                                       const GhostAnnotationReport& report,
                                       int line,
                                       const QString& contains)
{
    ++g_checks;
    bool ok = true;
    QStringList seen;
    for (const GhostAnnotation& annotation : report.annotations) {
        if (annotation.line != line)
            continue;
        seen << annotation.text;
        if (annotation.text.contains(contains))
            ok = false;
    }
    if (!ok)
        ++g_fails;
    printf("[%s] %-34s reject=\"%s\" line=%d seen=[%s]\n",
           ok ? "PASS" : "FAIL",
           what,
           contains.toLocal8Bit().constData(),
           line,
           seen.join(",").toLocal8Bit().constData());
}

static QStringList recordNames(const QList<SemanticSymbolRecord>& records) {
    QStringList names;
    for (const auto& record : records)
        names << record.name;
    return names;
}

static QStringList scoredNames(const QVector<QPair<QString, int>>& scored) {
    QStringList names;
    for (const auto& item : scored)
        names << item.first;
    return names;
}

static const WavePreviewLane* waveLaneNamed(const WavePreviewReport& report,
                                            const QString& name)
{
    for (const WavePreviewLane& lane : report.lanes) {
        if (lane.signalName == name)
            return &lane;
    }
    return nullptr;
}

static const WavePreviewSignalContext* waveContextNamed(
    const WavePreviewReport& report,
    const QString& name)
{
    for (const WavePreviewSignalContext& context : report.signalContexts) {
        if (context.signalName == name)
            return &context;
    }
    return nullptr;
}

static const WavePreviewAssignment* firstWaveAssignment(
    const WavePreviewReport& report,
    const QString& name)
{
    const WavePreviewLane* lane = waveLaneNamed(report, name);
    if (!lane || lane->assignments.isEmpty())
        return nullptr;
    return &lane->assignments.first();
}

static const WavePreviewTraceSignal* waveTraceSignalNamed(
    const WavePreviewReport& report,
    const QString& name)
{
    for (const WavePreviewTraceSignal& signal : report.trace.traceSignals) {
        if (signal.signalName == name)
            return &signal;
    }
    return nullptr;
}

static QStringList waveEdgeLabels(const QList<WavePreviewEdgeSignal>& edges)
{
    QStringList labels;
    for (const WavePreviewEdgeSignal& signal : edges)
        labels.append(signal.label());
    return labels;
}

static SemanticSymbolRecord makeSemanticFixtureRecord(
    const QString& name,
    SymbolTaxonomy::DeclarationKind declarationKind,
    SymbolTaxonomy::CollectorKind collectorKind,
    const QString& ownerName,
    const QString& rawTypeText,
    int localHandle,
    const QString& fileName)
{
    SymbolTaxonomy::SymbolOwnerScope ownerScope =
        semanticFixtureOwnerScopeForDeclaration(declarationKind);
    bool interfaceLikeOwner = false;
    if (!ownerName.isEmpty()) {
        if (declarationKind == SymbolTaxonomy::DeclarationKind::StructMember) {
            ownerScope = SymbolTaxonomy::SymbolOwnerScope::Struct;
        } else if (declarationKind
                   == SymbolTaxonomy::DeclarationKind::Modport) {
            ownerScope = SymbolTaxonomy::SymbolOwnerScope::Interface;
            interfaceLikeOwner = true;
        } else {
            ownerScope = SymbolTaxonomy::SymbolOwnerScope::Module;
        }
    }

    return SemanticFixtureRecordBuilder(name, declarationKind)
        .withFile(fileName)
        .withLocalHandle(localHandle)
        .withLine(localHandle)
        .withCollectorKind(collectorKind)
        .withOwner(ownerScope, ownerName, {}, interfaceLikeOwner)
        .withType(rawTypeText)
        .record();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QString path = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                              : QStringLiteral("test_sv/test_symbols.sv");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QFile::Text)) {
        fprintf(stderr, "cannot open %s\n", path.toLocal8Bit().constData());
        return 2;
    }
    QString content = QTextStream(&f).readAll();
    f.close();

    SlangManager mgr;
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        path,
        mgr.extractSymbolRecords(path, content),
        content);

    CompletionManager* cm = CompletionManager::getInstance();

    expectList("CompletionService keyword names",
               CompletionService::getInstance()->findKeywordCompletions(
                   QStringLiteral("always_f")),
               {"always_ff"});
    expectList("CompletionService keyword scores",
               scoredNames(
                   CompletionService::getInstance()->findScoredKeywordCompletions(
                       QStringLiteral("always_f"))),
               {"always_ff"});
    expectList("CompletionService keyword abbrev",
               CompletionService::getInstance()->findKeywordAbbreviationMatches(
                   {"always_ff", "logic"}, QStringLiteral("af")),
               {"always_ff"});
    ++g_checks;
    const QList<int> keywordPositions =
        CompletionService::getInstance()->findCompletionAbbreviationPositions(
            QStringLiteral("always_ff"), QStringLiteral("af"));
    const bool keywordPositionsOk = keywordPositions == QList<int>({0, 7});
    if (!keywordPositionsOk)
        ++g_fails;
    printf("[%s] %-34s got=[%s]\n",
           keywordPositionsOk ? "PASS" : "FAIL",
           "CompletionService abbrev pos",
           QStringList({
               QString::number(keywordPositions.value(0, -1)),
               QString::number(keywordPositions.value(1, -1))
           }).join(",").toLocal8Bit().constData());
    ++g_checks;
    const bool managerKeywordScoreOk =
        cm->calculateMatchScore(QStringLiteral("always_ff"),
                                QStringLiteral("af"))
        == CompletionService::getInstance()->calculateCompletionMatchScore(
            QStringLiteral("always_ff"), QStringLiteral("af"));
    if (!managerKeywordScoreOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           managerKeywordScoreOk ? "PASS" : "FAIL",
           "CompletionManager score delegation");
    ++g_checks;
    const bool serviceItemScoreOk =
        CompletionService::getInstance()->completionItemScore(QStringLiteral("save"),
                                                              QString())
        == 100;
    if (!serviceItemScoreOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           serviceItemScoreOk ? "PASS" : "FAIL",
           "CompletionService item score");
    expectList("CompletionManager keyword names",
               cm->getKeywordCompletions(QStringLiteral("always_f")),
               {"always_ff"});
    expectList("CompletionManager keyword scores",
               scoredNames(cm->getScoredKeywordMatches(
                   QStringLiteral("always_f"))),
               {"always_ff"});
    expectList("CompletionManager keyword abbrev",
               cm->getAbbreviationMatches({"always_ff", "logic"},
                                          QStringLiteral("af")),
               {"always_ff"});
    GlobalControlService globalControlService;
    const QList<GlobalControlItem> globalRootItems =
        globalControlService.query(QString(),
                                   nullptr,
                                   SemanticIndex::getInstance());
    bool globalRootHasWorkspaceDomain = false;
    bool globalRootHasFoldDomain = false;
    bool globalRootHasCommands = false;
    for (const GlobalControlItem& item : globalRootItems) {
        if (item.kind == GlobalControlItemKind::Domain
            && item.id == QStringLiteral("ow"))
            globalRootHasWorkspaceDomain = true;
        if (item.kind == GlobalControlItemKind::Domain
            && item.id == QStringLiteral("fd"))
            globalRootHasFoldDomain = true;
        if (item.kind == GlobalControlItemKind::Command)
            globalRootHasCommands = true;
    }
    expectBool("GlobalControl root shows only domains",
               globalRootHasWorkspaceDomain
                   && globalRootHasFoldDomain
                   && !globalRootHasCommands,
               true);
    const QList<GlobalControlItem> globalWorkspaceItems =
        globalControlService.query(QStringLiteral("ow"),
                                   nullptr,
                                   SemanticIndex::getInstance());
    bool globalWorkspaceShowsOpenOne = false;
    bool globalWorkspaceShowsOpenTwo = false;
    bool globalWorkspaceShowsRecent = false;
    bool globalWorkspaceShowsDeprecatedOw = false;
    for (const GlobalControlItem& item : globalWorkspaceItems) {
        if (item.kind == GlobalControlItemKind::Command
            && item.id == QStringLiteral("ow 1"))
            globalWorkspaceShowsOpenOne = true;
        if (item.kind == GlobalControlItemKind::Command
            && item.id == QStringLiteral("ow 2"))
            globalWorkspaceShowsOpenTwo = true;
        if (item.kind == GlobalControlItemKind::Command
            && item.id == QStringLiteral("ow r"))
            globalWorkspaceShowsRecent = true;
        if (item.id == QStringLiteral("ow")
            && item.kind == GlobalControlItemKind::Command)
            globalWorkspaceShowsDeprecatedOw = true;
    }
    expectBool("GlobalControl ow domain shows ow r",
               globalWorkspaceShowsOpenOne
                   && globalWorkspaceShowsOpenTwo
                   && globalWorkspaceShowsRecent
                   && !globalWorkspaceShowsDeprecatedOw,
               true);
    const QList<GlobalControlItem> globalRecentItems =
        globalControlService.query(QStringLiteral("ow r"),
                                   nullptr,
                                   SemanticIndex::getInstance());
    bool globalRecentCommandFound = false;
    bool globalRecentCountHintFound = false;
    for (const GlobalControlItem& item : globalRecentItems) {
        if (item.kind == GlobalControlItemKind::Command
            && item.id == QStringLiteral("ow r"))
            globalRecentCommandFound = true;
        if (item.kind == GlobalControlItemKind::Domain
            && item.title == QStringLiteral("ow <num>"))
            globalRecentCountHintFound = true;
    }
    expectBool("GlobalControl ow r query finds recent command",
               globalRecentCommandFound && !globalRecentCountHintFound,
               true);
    const QList<SemanticSymbolRecord> modelScoringRecords{
        SemanticFixtureRecordBuilder(
            QStringLiteral("logic"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(9001)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .record(),
        SemanticFixtureRecordBuilder(
            QStringLiteral("always_ff"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(9002)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .record(),
    };
    CompletionModel modelScoring;
    modelScoring.updateSymbolRecordCompletions(modelScoringRecords,
                                               QStringLiteral("af"),
                                               CompletionCommandKind::Logic);
    ++g_checks;
    const QString firstScoredModelSymbol =
        modelScoring.getItem(modelScoring.index(2, 0)).text;
    const bool modelScoringOk =
        firstScoredModelSymbol == QStringLiteral("always_ff");
    if (!modelScoringOk)
        ++g_fails;
    printf("[%s] %-34s got=\"%s\"\n",
           modelScoringOk ? "PASS" : "FAIL",
           "CompletionModel service scoring",
           firstScoredModelSymbol.toLocal8Bit().constData());
    expectEq("CompletionService symbol desc",
             CompletionService::getInstance()
                 ->commandSymbolPresentation(CompletionCommandKind::Logic)
                 .typeDescription,
             QStringLiteral("logic variables"));
    expectEq("CompletionModel symbol desc",
             modelScoring.getItem(modelScoring.index(2, 0)).description,
             QStringLiteral("logic"));
    expectEq("CompletionModel symbol display",
             modelScoring.getItem(modelScoring.index(2, 0)).displayText,
             QStringLiteral("always_ff (logic)"));
    expectBool("CompletionModel symbol stable key",
               modelScoring.getItem(modelScoring.index(2, 0)).symbolStableKey
                   == modelScoringRecords.at(1).stableKey,
               true);
    expectBool("CompletionModel symbol record",
               modelScoring.getItem(modelScoring.index(2, 0)).symbolRecord.stableKey
                   == modelScoring.getItem(modelScoring.index(2, 0)).symbolStableKey
                   && modelScoring.getItem(modelScoring.index(2, 0))
                          .symbolRecord.localHandle == modelScoringRecords.at(1).localHandle,
               true);
    expectBool("CompletionModel symbol semantic metadata",
               modelScoring.getItem(modelScoring.index(2, 0)).typeDisplayName
                   == QStringLiteral("logic")
                   && modelScoring.getItem(modelScoring.index(2, 0)).ownerScopeName
                       == QStringLiteral("top")
                   && modelScoring.getItem(modelScoring.index(2, 0))
                          .sourceRoleDisplayName == QStringLiteral("design source")
                   && modelScoring.getItem(modelScoring.index(2, 0)).declarationKind
                       == SymbolTaxonomy::DeclarationKind::Signal
                   && modelScoring.getItem(modelScoring.index(2, 0)).ownerScope
                       == SymbolTaxonomy::SymbolOwnerScope::Module
                   && modelScoring.getItem(modelScoring.index(2, 0)).sourceRole
                       == SymbolTaxonomy::SourceRole::DesignSource
                   && modelScoring.getItem(modelScoring.index(2, 0))
                          .symbolRecord.collectorKind
                       == SymbolTaxonomy::CollectorKind::Logic,
               true);
    expectBool("CompletionModel header selectable",
               modelScoring.getItem(modelScoring.index(0, 0)).selectable,
               false);
    expectBool("CompletionModel default metadata",
               modelScoring.getItem(modelScoring.index(1, 0)).emphasized
                   && modelScoring.getItem(modelScoring.index(1, 0))
                          .toolTipText.contains(QStringLiteral("default value")),
               true);
    expectEq("CompletionModel display role",
             modelScoring.data(modelScoring.index(2, 0), Qt::DisplayRole).toString(),
             QStringLiteral("always_ff (logic)"));
    SemanticSymbolRecord metadataModelRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("metadata_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(QStringLiteral("snapshot_only.sv"))
            .withLocalHandle(9005)
            .withSourceRole(SymbolTaxonomy::SourceRole::Unknown)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    metadataModelRecord.analysisBand.label = QStringLiteral("current");
    metadataModelRecord.analysisBand.displayName = QStringLiteral("current");
    metadataModelRecord.analysisBand.priority = true;
    metadataModelRecord.analysisBand.publicationCheckpoint = 1;
    CompletionResult metadataCompletion;
    metadataCompletion.names.append(metadataModelRecord.name);
    CompletionResult::SemanticCompletionItem metadataCompletionItem;
    metadataCompletionItem.label = metadataModelRecord.name;
    metadataCompletionItem.typeDisplayName = QStringLiteral("module");
    metadataCompletionItem.ownerScopeName = QStringLiteral("global");
    metadataCompletionItem.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(metadataModelRecord.sourceRole);
    metadataCompletionItem.analysisBand = metadataModelRecord.analysisBand;
    metadataCompletionItem.analysisBandDisplayName =
        semanticAnalysisBandDisplayName(metadataModelRecord.analysisBand);
    metadataCompletionItem.symbolRecord = metadataModelRecord;
    metadataCompletionItem.symbolStableKey = metadataModelRecord.stableKey;
    metadataCompletionItem.declarationKind = metadataModelRecord.declarationKind;
    metadataCompletionItem.usageRole = metadataModelRecord.usageRole;
    metadataCompletionItem.ownerScope = metadataModelRecord.owner.kind;
    metadataCompletionItem.sourceRole = metadataModelRecord.sourceRole;
    metadataCompletion.items.append(metadataCompletionItem);
    CompletionModel metadataDescriptionModel;
    metadataDescriptionModel.updateCompletions(metadataCompletion,
                                               QStringLiteral("meta"));
    expectEq("CompletionModel metadata desc",
             metadataDescriptionModel.getItem(
                 metadataDescriptionModel.index(0, 0)).description,
             QStringLiteral("module"));
    expectEq("CompletionModel metadata display",
             metadataDescriptionModel.getItem(
                 metadataDescriptionModel.index(0, 0)).displayText,
             QStringLiteral("metadata_top (module)"));
    expectBool("CompletionModel metadata stable key",
               metadataDescriptionModel.getItem(
                   metadataDescriptionModel.index(0, 0)).symbolStableKey
                   == metadataModelRecord.stableKey,
               true);
    expectBool("CompletionModel metadata record",
               metadataDescriptionModel.getItem(
                   metadataDescriptionModel.index(0, 0)).symbolRecord.declarationKind
                   == SymbolTaxonomy::DeclarationKind::Module
                   && metadataDescriptionModel.getItem(
                       metadataDescriptionModel.index(0, 0))
                          .symbolRecord.collectorKind
                       == SymbolTaxonomy::CollectorKind::Module,
               true);
    expectBool("CompletionModel metadata semantic fields",
               metadataDescriptionModel.getItem(
                   metadataDescriptionModel.index(0, 0)).typeDisplayName
                   == QStringLiteral("module")
                   && metadataDescriptionModel.getItem(
                       metadataDescriptionModel.index(0, 0)).ownerScopeName
                       == QStringLiteral("global")
                   && metadataDescriptionModel.getItem(
                       metadataDescriptionModel.index(0, 0)).sourceRoleDisplayName
                       == QStringLiteral("source")
                   && metadataDescriptionModel.getItem(
                       metadataDescriptionModel.index(0, 0)).ownerScope
                       == SymbolTaxonomy::SymbolOwnerScope::Global
                   && metadataDescriptionModel.getItem(
                       metadataDescriptionModel.index(0, 0)).sourceRole
                       == SymbolTaxonomy::SourceRole::Unknown
                   && metadataDescriptionModel.getItem(
                       metadataDescriptionModel.index(0, 0))
                          .analysisBandDisplayName == QStringLiteral("current")
                   && metadataDescriptionModel.getItem(
                       metadataDescriptionModel.index(0, 0))
                          .analysisBand.label == QStringLiteral("current"),
               true);
    expectBool("CompletionModel metadata tooltip band",
               metadataDescriptionModel.data(
                   metadataDescriptionModel.index(0, 0),
                   Qt::ToolTipRole).toString().contains(
                       QStringLiteral("band: current")),
               true);
    SemanticSymbolRecord backgroundMetadataRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("metadata_bg"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(QStringLiteral("background_only.sv"))
            .withLocalHandle(9006)
            .withSourceRole(SymbolTaxonomy::SourceRole::DesignSource)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record();
    backgroundMetadataRecord.analysisBand.label =
        QStringLiteral("background");
    backgroundMetadataRecord.analysisBand.displayName =
        QStringLiteral("background");
    CompletionResult::SemanticCompletionItem backgroundCompletionItem;
    backgroundCompletionItem.label = backgroundMetadataRecord.name;
    backgroundCompletionItem.typeDisplayName = QStringLiteral("module");
    backgroundCompletionItem.ownerScopeName = QStringLiteral("global");
    backgroundCompletionItem.sourceRoleDisplayName =
        SymbolTaxonomy::sourceRoleDisplayName(
            backgroundMetadataRecord.sourceRole);
    backgroundCompletionItem.analysisBand =
        backgroundMetadataRecord.analysisBand;
    backgroundCompletionItem.analysisBandDisplayName =
        semanticAnalysisBandDisplayName(
            backgroundMetadataRecord.analysisBand);
    backgroundCompletionItem.symbolRecord = backgroundMetadataRecord;
    backgroundCompletionItem.symbolStableKey =
        backgroundMetadataRecord.stableKey;
    backgroundCompletionItem.declarationKind =
        backgroundMetadataRecord.declarationKind;
    backgroundCompletionItem.usageRole =
        backgroundMetadataRecord.usageRole;
    backgroundCompletionItem.ownerScope =
        backgroundMetadataRecord.owner.kind;
    backgroundCompletionItem.sourceRole =
        backgroundMetadataRecord.sourceRole;
    CompletionResult bandSummaryCompletion;
    bandSummaryCompletion.items = {
        metadataCompletionItem,
        backgroundCompletionItem,
    };
    expectEq("CompletionResult analysis band summary",
             bandSummaryCompletion.analysisBandSummaryText(),
             QStringLiteral("bands current 1 item, background 1 item"));
    CompletionModel bandSummaryModel;
    bandSummaryModel.updateCompletions(bandSummaryCompletion,
                                       QStringLiteral("metadata"));
    expectBool("CompletionModel renders band summary header",
               bandSummaryModel.rowCount() == 3
                   && !bandSummaryModel.getItem(
                          bandSummaryModel.index(0, 0)).selectable
                   && bandSummaryModel.data(
                          bandSummaryModel.index(0, 0),
                          Qt::DisplayRole).toString()
                          == QStringLiteral(
                              ":: COMPLETION BANDS - bands current 1 item, background 1 item ::")
                   && bandSummaryModel.firstSelectableIndex().row() == 1,
               true);
    CompletionResult hiddenBandSummaryCompletion;
    for (int i = 0; i < 15; ++i) {
        CompletionResult::SemanticCompletionItem currentItem =
            metadataCompletionItem;
        currentItem.label = QStringLiteral("visible_current_%1")
                                .arg(i, 2, 10, QLatin1Char('0'));
        currentItem.insertText = currentItem.label;
        currentItem.symbolRecord = SemanticSymbolRecord();
        currentItem.symbolStableKey = SymbolStableKey();
        hiddenBandSummaryCompletion.items.append(currentItem);
    }
    CompletionResult::SemanticCompletionItem hiddenBackgroundItem =
        backgroundCompletionItem;
    hiddenBackgroundItem.label =
        QStringLiteral("hidden_background_only");
    hiddenBackgroundItem.insertText = hiddenBackgroundItem.label;
    hiddenBackgroundItem.symbolRecord = SemanticSymbolRecord();
    hiddenBackgroundItem.symbolStableKey = SymbolStableKey();
    hiddenBandSummaryCompletion.items.append(hiddenBackgroundItem);
    expectBool("CompletionResult keeps hidden band in full summary",
               hiddenBandSummaryCompletion.analysisBandGroupCount() == 2,
               true);
    CompletionModel visibleBandSummaryModel;
    visibleBandSummaryModel.updateCompletions(hiddenBandSummaryCompletion,
                                              QStringLiteral("visible"));
    bool visibleHeaderPresent = false;
    for (int row = 0; row < visibleBandSummaryModel.rowCount(); ++row) {
        visibleHeaderPresent =
            visibleHeaderPresent
            || visibleBandSummaryModel.data(
                   visibleBandSummaryModel.index(row, 0),
                   Qt::DisplayRole).toString().contains(
                   QStringLiteral("COMPLETION BANDS"));
    }
    expectBool("CompletionModel summarizes only visible completion bands",
               visibleBandSummaryModel.rowCount() == 15
                   && !visibleHeaderPresent
                   && visibleBandSummaryModel.firstSelectableIndex().row() == 0,
               true);
    CompletionModel commandSymbolBandSummaryModel;
    commandSymbolBandSummaryModel.updateSymbolRecordCompletions(
        {metadataModelRecord, backgroundMetadataRecord},
        QString(),
        CompletionCommandKind::Module);
    expectBool("CompletionModel renders command symbol band summary header",
               commandSymbolBandSummaryModel.rowCount() == 5
                   && !commandSymbolBandSummaryModel.getItem(
                          commandSymbolBandSummaryModel.index(2, 0))
                          .selectable
                   && commandSymbolBandSummaryModel.data(
                          commandSymbolBandSummaryModel.index(2, 0),
                          Qt::DisplayRole).toString()
                          == QStringLiteral(
                              ":: COMMAND SYMBOL BANDS - bands current 1 item, background 1 item ::")
                   && commandSymbolBandSummaryModel.firstSelectableIndex()
                          .row() == 1,
               true);
    CompletionModel commandDisplayModel;
    commandDisplayModel.updateCommandCompletions({QStringLiteral("save")},
                                                 QStringLiteral("s"));
    expectEq("CompletionModel command display",
             commandDisplayModel.getItem(commandDisplayModel.index(1, 0)).displayText,
             QStringLiteral("save - Execute save command"));
    CompletionModel noCommandModel;
    noCommandModel.updateCommandCompletions({QStringLiteral("save")},
                                            QStringLiteral("zz"));
    expectBool("CompletionModel no command selectable",
               !noCommandModel.getItem(noCommandModel.index(1, 0)).selectable
                   && noCommandModel.data(noCommandModel.index(1, 0),
                                          Qt::DisplayRole)
                          .toString()
                       == QStringLiteral("No matching commands - No commands match your input"),
               true);
    expectEq("CompletionService interface desc",
             CompletionService::getInstance()
                 ->commandSymbolPresentation(CompletionCommandKind::Interface)
                 .typeDescription,
             QStringLiteral("interfaces"));

    const QString decorationFile = QStringLiteral("decor_fixture.sv");
    const QString decorationText =
        QStringLiteral("module rtl_top(\n"
                       "    input clk_main, // 80m\n"
                       "    lite_if.s S_GENR_LITE_IF // tail\n"
                       ");\n");
    const QList<SemanticSymbolRecord> decorationRecords{
        SemanticFixtureRecordBuilder(
            QStringLiteral("rtl_top"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(decorationFile)
            .withLine(1, 1)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .record(),
        SemanticFixtureRecordBuilder(
            QStringLiteral("clk_main"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(decorationFile)
            .withLine(2, 5)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .record(),
        SemanticFixtureRecordBuilder(
            QStringLiteral("S_GENR_LITE_IF"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(decorationFile)
            .withLine(3, 5)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInterfaceModport)
            .record(),
    };
    SemanticIndex decorationIndex;
    decorationIndex.setSnapshot(sharedSnapshotFromRecords(decorationRecords));
    SemanticDecorationService decorationService(&decorationIndex);
    const SemanticDecorationReport decorationReport =
        decorationService.decorationsForDocument(
            SemanticDecorationQuery{decorationFile, decorationText});
    bool moduleNameDecorated = false;
    bool portNameDecorated = false;
    bool interfacePortNameDecorated = false;
    bool keywordDecorated = false;
    bool commentDecorated = false;
    auto decorationStartsInLineComment =
        [&decorationText](const SemanticDecoration& decoration) {
            const int lineStart =
                decorationText.lastIndexOf(QLatin1Char('\n'),
                                           decoration.startPosition) + 1;
            int lineEnd = decorationText.indexOf(QLatin1Char('\n'),
                                                 lineStart);
            if (lineEnd < 0)
                lineEnd = decorationText.size();
            const int lineComment =
                decorationText.indexOf(QStringLiteral("//"), lineStart);
            return lineComment >= lineStart
                && lineComment < lineEnd
                && decoration.startPosition >= lineComment;
        };
    for (const SemanticDecoration& decoration : decorationReport.decorations) {
        const QString span =
            decorationText.mid(decoration.startPosition, decoration.length);
        moduleNameDecorated |= span == QStringLiteral("rtl_top");
        portNameDecorated |= span == QStringLiteral("clk_main");
        interfacePortNameDecorated |= span == QStringLiteral("S_GENR_LITE_IF");
        keywordDecorated |= span == QStringLiteral("module")
            || span == QStringLiteral("input")
            || span == QStringLiteral("lite_if.s");
        commentDecorated |= decorationStartsInLineComment(decoration);
    }
    expectBool("SemanticDecoration names not keywords",
               moduleNameDecorated
                   && portNameDecorated
                   && interfacePortNameDecorated
                   && !keywordDecorated
                   && !commentDecorated,
               true);

    const QString ghostFile = QStringLiteral("ghost_fixture.sv");
    const QString ghostText =
        QStringLiteral("module child(input logic [7:0] data, output logic ready);\n"
                       "endmodule\n"
                       "module other_child(output logic [3:0] data);\n"
                       "endmodule\n"
                       "module top;\n"
                       "  localparam DEPTH = 8;\n"
                       "  localparam PW = 32;\n"
                       "  logic [PW - 1:0] wide;\n"
                       "  logic [27:18] window;\n"
                       "  logic [7:0] data;\n"
                       "  logic [7:0] mem [16];\n"
                       "  child #(.WIDTH(16)) u_child (\n"
                       "    .data(data),\n"
                       "    .ready()\n"
                       "  );\n"
                       "  enum logic [1:0] {IDLE, RUN = 3, DONE};\n"
                       "  assign slice = data[4 +: 3];\n"
                       "  assign literal = 16'hFF00;\n"
                       "  assign cat = {4{8'hAA}};\n"
                       "  for (genvar i = 0; i < 8; i++) begin : g\n"
                       "  end\n"
                       "  function automatic [31:0] function_add1(input [31:0] x);\n"
                       "    function_add1 = x + 1;\n"
                       "  endfunction\n"
                       "  localparam FROM_PW = PW;\n"
                       "  localparam SUM = 32'ha452_0000 + 32'454654;\n"
                       "  localparam STR = \"test\";\n"
                       "  localparam CLOG = $clog2(PW);\n"
                       "  localparam FADD = function_add1(PW);\n"
                       "endmodule\n");
    const QList<SemanticSymbolRecord> ghostRecords{
        SemanticFixtureRecordBuilder(QStringLiteral("data"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(ghostFile)
            .withLine(1, 30)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortInput)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("child"))
            .withType(QStringLiteral("logic [7:0]"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("data"),
                                     SymbolTaxonomy::DeclarationKind::Port)
            .withFile(ghostFile)
            .withLine(3, 36)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PortOutput)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("other_child"))
            .withType(QStringLiteral("logic [3:0]"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("DEPTH"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(ghostFile)
            .withLine(6, 14)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Localparam)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("PW"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(ghostFile)
            .withLine(7, 14)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Localparam)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("wide"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(ghostFile)
            .withLine(8, 22)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .withType(QStringLiteral("logic [PW - 1:0]"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("window"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(ghostFile)
            .withLine(9, 17)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .withType(QStringLiteral("logic [27:18]"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("data"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(ghostFile)
            .withLine(10, 15)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .withType(QStringLiteral("logic [7:0]"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("mem"),
                                     SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(ghostFile)
            .withLine(11, 15)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .withType(QStringLiteral("logic [7:0]"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("u_child.data"),
                                     SymbolTaxonomy::DeclarationKind::Instance)
            .withFile(ghostFile)
            .withLine(13, 5)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::InstPin)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .withType(QStringLiteral("child"),
                      QStringLiteral("child"),
                      SymbolTaxonomy::DeclarationKind::Module)
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("IDLE"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(ghostFile)
            .withLine(16, 25)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("state_t"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("RUN"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(ghostFile)
            .withLine(16, 31)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("state_t"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("DONE"),
                                     SymbolTaxonomy::DeclarationKind::Enum)
            .withFile(ghostFile)
            .withLine(16, 40)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("state_t"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("FROM_PW"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(ghostFile)
            .withLine(25, 14)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Localparam)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("SUM"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(ghostFile)
            .withLine(26, 14)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Localparam)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("STR"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(ghostFile)
            .withLine(27, 14)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Localparam)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("CLOG"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(ghostFile)
            .withLine(28, 14)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Localparam)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .record(),
        SemanticFixtureRecordBuilder(QStringLiteral("FADD"),
                                     SymbolTaxonomy::DeclarationKind::Localparam)
            .withFile(ghostFile)
            .withLine(29, 14)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Localparam)
            .withOwner(SymbolTaxonomy::SymbolOwnerScope::Module,
                       QStringLiteral("top"))
            .record(),
    };
    SemanticIndex ghostIndex;
    ghostIndex.setSnapshot(
        sharedSnapshotFromRecords(
            ghostRecords,
            {},
            {},
            {{ghostFile, ghostText}}));
    GhostAnnotationService ghostService(&ghostIndex);
    const GhostAnnotationReport ghostReport =
        ghostService.annotationsForDocument(
            GhostAnnotationQuery{ghostFile, ghostText});
    expectGhostContains("Ghost port formal",
                        ghostReport,
                        GhostAnnotationKind::FormalPort,
                        13,
                        QStringLiteral("in logic [7:0]"));
    expectGhostNotContains("Ghost literal parameter hidden",
                           ghostReport,
                           GhostAnnotationKind::ParameterValue,
                           6,
                           QStringLiteral("(D)8"));
    expectGhostContains("Ghost parameter identifier value",
                        ghostReport,
                        GhostAnnotationKind::ParameterValue,
                        25,
                        QStringLiteral("(D)32"));
    expectGhostContains("Ghost parameter expression value",
                        ghostReport,
                        GhostAnnotationKind::ParameterValue,
                        26,
                        QStringLiteral("(H)A458EFFE"));
    expectGhostContains("Ghost parameter ascii value",
                        ghostReport,
                        GhostAnnotationKind::ParameterValue,
                        27,
                        QStringLiteral("(H)74657374"));
    expectGhostContains("Ghost parameter clog2 value",
                        ghostReport,
                        GhostAnnotationKind::ParameterValue,
                        28,
                        QStringLiteral("(D)5"));
    expectGhostContains("Ghost parameter function value",
                        ghostReport,
                        GhostAnnotationKind::ParameterValue,
                        29,
                        QStringLiteral("(D)33"));
    expectGhostContains("Ghost parameter override",
                        ghostReport,
                        GhostAnnotationKind::ParameterOverride,
                        12,
                        QStringLiteral("= 16"));
    expectGhostContains("Ghost signal width",
                        ghostReport,
                        GhostAnnotationKind::SignalWidth,
                        8,
                        QStringLiteral("32 bits"));
    expectGhostContains("Ghost nonzero range width",
                        ghostReport,
                        GhostAnnotationKind::SignalWidth,
                        9,
                        QStringLiteral("[27:18] 10 bits"));
    expectGhostNotContains("Ghost obvious width hidden",
                           ghostReport,
                           GhostAnnotationKind::SignalWidth,
                           10,
                           QStringLiteral("8 bits"));
    expectGhostContains("Ghost array summary",
                        ghostReport,
                        GhostAnnotationKind::ArraySummary,
                        11,
                        QStringLiteral("16 entries"));
    expectGhostContains("Ghost enum idle",
                        ghostReport,
                        GhostAnnotationKind::EnumValue,
                        16,
                        QStringLiteral("= 0"));
    expectGhostContains("Ghost enum run",
                        ghostReport,
                        GhostAnnotationKind::EnumValue,
                        16,
                        QStringLiteral("= 3"));
    expectGhostContains("Ghost enum done",
                        ghostReport,
                        GhostAnnotationKind::EnumValue,
                        16,
                        QStringLiteral("= 4"));
    expectGhostContains("Ghost part select",
                        ghostReport,
                        GhostAnnotationKind::PartSelect,
                        17,
                        QStringLiteral("[6:4] 3 bits"));
    expectGhostLineNotContains("Ghost literal overlay removed",
                               ghostReport,
                               18,
                               QStringLiteral("65280"));
    expectGhostContains("Ghost concat width",
                        ghostReport,
                        GhostAnnotationKind::ConcatenationWidth,
                        19,
                        QStringLiteral("32 bits"));
    expectGhostContains("Ghost generate loop",
                        ghostReport,
                        GhostAnnotationKind::GenerateLoop,
                        20,
                        QStringLiteral("instances=8"));

    const QString numericHoverText =
        QStringLiteral("assign a = 16'hFF00;\n"
                       "assign b = \"123\"; // 456\n"
                       "assign c = 'd4545;\n"
                       "assign d = 'hadda;\n"
                       "`include \"test.sv\"\n");
    const int literalHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("16'hFF00")))
        + 3;
    const int stringHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("123")));
    const int commentHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("456")));
    const int unsizedDecimalHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("'d4545"))) + 2;
    const int unsizedHexHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("'hadda"))) + 2;
    const int includeStringHoverPosition =
        static_cast<int>(numericHoverText.indexOf(QStringLiteral("test.sv")));
    const GhostNumericLiteralReport literalHover =
        ghostService.numericLiteralAt(GhostNumericLiteralQuery{
            numericHoverText,
            literalHoverPosition});
    expectBool("Ghost literal hover available",
               literalHover.available,
               true);
    expectEq("Ghost literal hover text",
             literalHover.displayText,
             QStringLiteral("(B)1111_1111_0000_0000 (D)65280"));
    const GhostNumericLiteralReport stringHover =
        ghostService.numericLiteralAt(GhostNumericLiteralQuery{
            numericHoverText,
            stringHoverPosition});
    expectBool("Ghost literal hover supports ascii string",
               stringHover.available
                   && stringHover.displayText.contains(QStringLiteral("(H)313233")),
               true);
    const GhostNumericLiteralReport includeStringHover =
        ghostService.numericLiteralAt(GhostNumericLiteralQuery{
            numericHoverText,
            includeStringHoverPosition});
    expectBool("Ghost literal hover skips include string",
               includeStringHover.available,
               false);
    const GhostNumericLiteralReport unsizedDecimalHover =
        ghostService.numericLiteralAt(GhostNumericLiteralQuery{
            numericHoverText,
            unsizedDecimalHoverPosition});
    expectBool("Ghost literal hover supports unsized decimal base",
               unsizedDecimalHover.available
                   && unsizedDecimalHover.displayText.contains(QStringLiteral("(H)11C1"))
                   && !unsizedDecimalHover.displayText.contains(QStringLiteral("(D)4545")),
               true);
    const GhostNumericLiteralReport unsizedHexHover =
        ghostService.numericLiteralAt(GhostNumericLiteralQuery{
            numericHoverText,
            unsizedHexHoverPosition});
    expectBool("Ghost literal hover supports unsized hex base",
               unsizedHexHover.available
                   && unsizedHexHover.displayText.contains(QStringLiteral("(D)44506"))
                   && !unsizedHexHover.displayText.contains(QStringLiteral("(H)ADDA")),
               true);
    const GhostNumericLiteralReport legacyStringHover =
        ghostService.numericLiteralAt(GhostNumericLiteralQuery{
            QStringLiteral("`include \"123.sv\"\n"),
            10});
    expectBool("Ghost literal hover skips include legacy string",
               legacyStringHover.available,
               false);
    const GhostNumericLiteralReport commentHover =
        ghostService.numericLiteralAt(GhostNumericLiteralQuery{
            numericHoverText,
            commentHoverPosition});
    expectBool("Ghost literal hover skips comment",
               commentHover.available,
               false);

    const QString commentHighlightText =
        QStringLiteral("assign a = 1; // 80m\n");
    TSDocument commentTsDocument;
    commentTsDocument.setText(commentHighlightText);
    QTextDocument commentDocument(commentHighlightText);
    MyHighlighter commentHighlighter(&commentDocument, &commentTsDocument);
    commentHighlighter.rehighlight();
    const QTextBlock commentBlock = commentDocument.firstBlock();
    const int numberInComment =
        commentBlock.text().indexOf(QStringLiteral("80m"));
    bool commentNumberUsesCommentFormat = false;
    for (const QTextLayout::FormatRange& range :
         commentBlock.layout()->formats()) {
        if (numberInComment >= range.start
            && numberInComment < range.start + range.length) {
            commentNumberUsesCommentFormat =
                range.format.foreground().color() == QColor(QStringLiteral("#7F848E"))
                && range.format.fontItalic();
        }
    }
    expectBool("Highlighter line comment wins",
               commentNumberUsesCommentFormat,
               true);

    const QString portAppendOriginal =
        QStringLiteral("module demo (\n"
                       "    input  logic clk,\n"
                       "    output logic done\n"
                       ");\n"
                       "endmodule\n");
    const QString portAppendExpected =
        QStringLiteral("module demo (\n"
                       "    input  logic clk,\n"
                       "    output logic done,\n"
                       "    \n"
                       ");\n"
                       "endmodule\n");
    MyCodeEditor portAppendEditor;
    portAppendEditor.setPlainText(portAppendOriginal);
    QTextCursor portAppendCursor = portAppendEditor.textCursor();
    portAppendCursor.setPosition(
        portAppendOriginal.indexOf(QStringLiteral("done")));
    portAppendEditor.setTextCursor(portAppendCursor);
    QString portAppendMessage;
    const bool portAppendOk =
        portAppendEditor.executeComPortAppend(&portAppendMessage);
    expectBool("COM gpo editor append succeeds", portAppendOk, true);
    expectEq("COM gpo editor append text",
             portAppendEditor.toPlainText(),
             portAppendExpected);
    expectBool("COM gpo editor caret at indent",
               portAppendEditor.textCursor().position()
                   == portAppendExpected.indexOf(QStringLiteral("    \n);"))
                          + 4,
               true);
    portAppendEditor.undo();
    expectEq("COM gpo editor undo restores original",
             portAppendEditor.toPlainText(),
             portAppendOriginal);

    const QString singleLinePortList =
        QStringLiteral("module demo (input logic clk);\nendmodule\n");
    portAppendEditor.setPlainText(singleLinePortList);
    portAppendCursor = portAppendEditor.textCursor();
    portAppendCursor.setPosition(
        singleLinePortList.indexOf(QStringLiteral("clk")));
    portAppendEditor.setTextCursor(portAppendCursor);
    portAppendMessage.clear();
    expectBool("COM gpo editor rejects unclear list",
               !portAppendEditor.executeComPortAppend(&portAppendMessage)
                   && portAppendMessage
                          == QStringLiteral("No clear port append point")
                   && portAppendEditor.toPlainText() == singleLinePortList,
               true);

    const QString signalInsertOriginal =
        QStringLiteral("module sig_demo;\n"
                       "  logic a;\n"
                       "  wire b;\n"
                       "  assign y = b;\n"
                       "endmodule\n");
    const QString signalInsertExpected =
        QStringLiteral("module sig_demo;\n"
                       "  logic a;\n"
                       "  wire b;\n"
                       "  \n"
                       "  assign y = b;\n"
                       "endmodule\n");
    MyCodeEditor signalInsertEditor;
    signalInsertEditor.setPlainText(signalInsertOriginal);
    QTextCursor signalInsertCursor = signalInsertEditor.textCursor();
    signalInsertCursor.setPosition(
        signalInsertOriginal.indexOf(QStringLiteral("assign")));
    signalInsertEditor.setTextCursor(signalInsertCursor);
    QString signalInsertMessage;
    const bool signalInsertOk =
        signalInsertEditor.executeComSignalInsert(&signalInsertMessage);
    expectBool("COM gsi editor insert succeeds", signalInsertOk, true);
    expectEq("COM gsi editor insert text",
             signalInsertEditor.toPlainText(),
             signalInsertExpected);
    expectBool("COM gsi editor caret at indent",
               signalInsertEditor.textCursor().position()
                   == signalInsertExpected.indexOf(
                          QStringLiteral("  \n  assign")) + 2,
               true);
    signalInsertEditor.undo();
    expectEq("COM gsi editor undo restores original",
             signalInsertEditor.toPlainText(),
             signalInsertOriginal);

    const QString noModuleSignalInsert = QStringLiteral("logic stray;\n");
    signalInsertEditor.setPlainText(noModuleSignalInsert);
    signalInsertCursor = signalInsertEditor.textCursor();
    signalInsertCursor.setPosition(0);
    signalInsertEditor.setTextCursor(signalInsertCursor);
    signalInsertMessage.clear();
    expectBool("COM gsi editor reports no module",
               !signalInsertEditor.executeComSignalInsert(&signalInsertMessage)
                   && signalInsertMessage == QStringLiteral("No current module")
                   && signalInsertEditor.toPlainText() == noModuleSignalInsert,
               true);

    const QString instanceInsertOriginal =
        QStringLiteral("module inst_demo;\n"
                       "  logic a;\n"
                       "  child u_child();\n"
                       "  assign y = a;\n"
                       "endmodule\n");
    const QString instanceInsertExpected =
        QStringLiteral("module inst_demo;\n"
                       "  logic a;\n"
                       "  child u_child();\n"
                       "  \n"
                       "  assign y = a;\n"
                       "endmodule\n");
    MyCodeEditor instanceInsertEditor;
    instanceInsertEditor.setPlainText(instanceInsertOriginal);
    QTextCursor instanceInsertCursor = instanceInsertEditor.textCursor();
    instanceInsertCursor.setPosition(
        instanceInsertOriginal.indexOf(QStringLiteral("assign")));
    instanceInsertEditor.setTextCursor(instanceInsertCursor);
    QString instanceInsertMessage;
    const bool instanceInsertOk =
        instanceInsertEditor.executeComInstanceInsert(&instanceInsertMessage);
    expectBool("COM gii editor insert succeeds", instanceInsertOk, true);
    expectEq("COM gii editor insert text",
             instanceInsertEditor.toPlainText(),
             instanceInsertExpected);
    expectBool("COM gii editor caret at indent",
               instanceInsertEditor.textCursor().position()
                   == instanceInsertExpected.indexOf(
                          QStringLiteral("  \n  assign")) + 2,
               true);
    instanceInsertEditor.undo();
    expectEq("COM gii editor undo restores original",
             instanceInsertEditor.toPlainText(),
             instanceInsertOriginal);

    const QString noModuleInstanceInsert = QStringLiteral("child u0();\n");
    instanceInsertEditor.setPlainText(noModuleInstanceInsert);
    instanceInsertCursor = instanceInsertEditor.textCursor();
    instanceInsertCursor.setPosition(0);
    instanceInsertEditor.setTextCursor(instanceInsertCursor);
    instanceInsertMessage.clear();
    expectBool("COM gii editor reports no module",
               !instanceInsertEditor.executeComInstanceInsert(
                   &instanceInsertMessage)
                   && instanceInsertMessage
                          == QStringLiteral("No current module")
                   && instanceInsertEditor.toPlainText()
                          == noModuleInstanceInsert,
               true);

    const QString assignInsertOriginal =
        QStringLiteral("module assign_demo;\n"
                       "  logic a;\n"
                       "  assign y = a;\n"
                       "  always_comb z = y;\n"
                       "endmodule\n");
    const QString assignInsertExpected =
        QStringLiteral("module assign_demo;\n"
                       "  logic a;\n"
                       "  assign y = a;\n"
                       "  \n"
                       "  always_comb z = y;\n"
                       "endmodule\n");
    MyCodeEditor assignInsertEditor;
    assignInsertEditor.setPlainText(assignInsertOriginal);
    QTextCursor assignInsertCursor = assignInsertEditor.textCursor();
    assignInsertCursor.setPosition(
        assignInsertOriginal.indexOf(QStringLiteral("always")));
    assignInsertEditor.setTextCursor(assignInsertCursor);
    QString assignInsertMessage;
    const bool assignInsertOk =
        assignInsertEditor.executeComAssignInsert(&assignInsertMessage);
    expectBool("COM gac editor insert succeeds", assignInsertOk, true);
    expectEq("COM gac editor insert text",
             assignInsertEditor.toPlainText(),
             assignInsertExpected);
    expectBool("COM gac editor caret at indent",
               assignInsertEditor.textCursor().position()
                   == assignInsertExpected.indexOf(
                          QStringLiteral("  \n  always")) + 2,
               true);
    assignInsertEditor.undo();
    expectEq("COM gac editor undo restores original",
             assignInsertEditor.toPlainText(),
             assignInsertOriginal);

    const QString noModuleAssignInsert = QStringLiteral("assign y = a;\n");
    assignInsertEditor.setPlainText(noModuleAssignInsert);
    assignInsertCursor = assignInsertEditor.textCursor();
    assignInsertCursor.setPosition(0);
    assignInsertEditor.setTextCursor(assignInsertCursor);
    assignInsertMessage.clear();
    expectBool("COM gac editor reports no module",
               !assignInsertEditor.executeComAssignInsert(
                   &assignInsertMessage)
                   && assignInsertMessage == QStringLiteral("No current module")
                   && assignInsertEditor.toPlainText() == noModuleAssignInsert,
               true);

    const QString parameterInsertOriginal =
        QStringLiteral("module parameter_demo #(\n"
                       "  parameter int WIDTH = 8\n"
                       ") (\n"
                       "  input logic clk\n"
                       ");\n"
                       "endmodule\n");
    const QString parameterInsertExpected =
        QStringLiteral("module parameter_demo #(\n"
                       "  parameter int WIDTH = 8,\n"
                       "  \n"
                       ") (\n"
                       "  input logic clk\n"
                       ");\n"
                       "endmodule\n");
    MyCodeEditor parameterInsertEditor;
    parameterInsertEditor.setPlainText(parameterInsertOriginal);
    QTextCursor parameterInsertCursor = parameterInsertEditor.textCursor();
    parameterInsertCursor.setPosition(
        parameterInsertOriginal.indexOf(QStringLiteral("clk")));
    parameterInsertEditor.setTextCursor(parameterInsertCursor);
    QString parameterInsertMessage;
    const bool parameterInsertOk =
        parameterInsertEditor.executeComParameterInsert(
            &parameterInsertMessage);
    expectBool("COM gpi editor insert succeeds", parameterInsertOk, true);
    expectEq("COM gpi editor insert text",
             parameterInsertEditor.toPlainText(),
             parameterInsertExpected);
    expectBool("COM gpi editor caret at indent",
               parameterInsertEditor.textCursor().position()
                   == parameterInsertExpected.indexOf(
                          QStringLiteral("  \n)")) + 2,
               true);
    parameterInsertEditor.undo();
    expectEq("COM gpi editor undo restores original",
             parameterInsertEditor.toPlainText(),
             parameterInsertOriginal);

    const QString noScopeParameterInsert =
        QStringLiteral("parameter int WIDTH = 8;\n");
    parameterInsertEditor.setPlainText(noScopeParameterInsert);
    parameterInsertCursor = parameterInsertEditor.textCursor();
    parameterInsertCursor.setPosition(0);
    parameterInsertEditor.setTextCursor(parameterInsertCursor);
    parameterInsertMessage.clear();
    expectBool("COM gpi editor reports no parameter scope",
               !parameterInsertEditor.executeComParameterInsert(
                   &parameterInsertMessage)
                   && parameterInsertMessage
                          == QStringLiteral("No current parameter scope")
                   && parameterInsertEditor.toPlainText()
                          == noScopeParameterInsert,
               true);

    const QString moduleEndInsertOriginal =
        QStringLiteral("module module_end_demo;\n"
                       "  logic a;\n"
                       "endmodule\n");
    const QString moduleEndInsertExpected =
        QStringLiteral("module module_end_demo;\n"
                       "  logic a;\n"
                       "  \n"
                       "endmodule\n");
    MyCodeEditor moduleEndInsertEditor;
    moduleEndInsertEditor.setPlainText(moduleEndInsertOriginal);
    QTextCursor moduleEndInsertCursor = moduleEndInsertEditor.textCursor();
    moduleEndInsertCursor.setPosition(
        moduleEndInsertOriginal.indexOf(QStringLiteral("logic")));
    moduleEndInsertEditor.setTextCursor(moduleEndInsertCursor);
    QString moduleEndInsertMessage;
    const bool moduleEndInsertOk =
        moduleEndInsertEditor.executeComModuleEndInsert(
            &moduleEndInsertMessage);
    expectBool("COM gef editor insert succeeds", moduleEndInsertOk, true);
    expectEq("COM gef editor insert text",
             moduleEndInsertEditor.toPlainText(),
             moduleEndInsertExpected);
    expectBool("COM gef editor caret at indent",
               moduleEndInsertEditor.textCursor().position()
                   == moduleEndInsertExpected.indexOf(
                          QStringLiteral("  \nendmodule")) + 2,
               true);
    moduleEndInsertEditor.undo();
    expectEq("COM gef editor undo restores original",
             moduleEndInsertEditor.toPlainText(),
             moduleEndInsertOriginal);

    const QString noModuleEndInsert = QStringLiteral("logic a;\n");
    moduleEndInsertEditor.setPlainText(noModuleEndInsert);
    moduleEndInsertCursor = moduleEndInsertEditor.textCursor();
    moduleEndInsertCursor.setPosition(0);
    moduleEndInsertEditor.setTextCursor(moduleEndInsertCursor);
    moduleEndInsertMessage.clear();
    expectBool("COM gef editor reports no module",
               !moduleEndInsertEditor.executeComModuleEndInsert(
                   &moduleEndInsertMessage)
                   && moduleEndInsertMessage
                          == QStringLiteral("No current module")
                   && moduleEndInsertEditor.toPlainText()
                          == noModuleEndInsert,
               true);

    const QString formatterInput =
        QStringLiteral("module top;\n"
                       "logic a;\n"
                       "  // comment-only lines stay where the user put them\n"
                       "always_ff @(posedge clk) begin\n"
                       "if (rst) begin\n"
                       "a <= 1'b0;\n"
                       "end else begin\n"
                       "a <= ~a;\n"
                       "end\n"
                       "end\n"
                       "always_comb begin\n"
                       "case (sel)\n"
                       "2'b00: y = \"begin\";\n"
                       "default: y = a; // endcase\n"
                       "endcase\n"
                       "end\n"
                       "`ifdef KEEP_COLUMN\n"
                       "  assign macro_guarded = a;\n"
                       "`endif\n"
                       "endmodule\n");
    const FormatterReport formatterReport =
        FormatterService::getInstance()->formatDocument(formatterInput);
    expectBool("Formatter report changed",
               formatterReport.changed,
               true);
    expectEq("Formatter conservative indentation",
             formatterReport.formattedText,
             QStringLiteral("module top;\n"
                            "    logic a;\n"
                            "  // comment-only lines stay where the user put them\n"
                            "    always_ff @(posedge clk) begin\n"
                            "        if (rst) begin\n"
                            "            a <= 1'b0;\n"
                            "        end else begin\n"
                            "            a <= ~a;\n"
                            "        end\n"
                            "    end\n"
                            "    always_comb begin\n"
                            "        case (sel)\n"
                            "            2'b00  : y = \"begin\";\n"
                            "            default: y = a;        // endcase\n"
                            "        endcase\n"
                            "    end\n"
                            "`ifdef KEEP_COLUMN\n"
                            "    assign macro_guarded = a;\n"
                            "`endif\n"
                            "endmodule\n"));
    const FormatterReport unchangedFormatterReport =
        FormatterService::getInstance()->formatDocument(
            formatterReport.formattedText);
    expectBool("Formatter idempotent",
               unchangedFormatterReport.changed,
               false);
    const QString formatterSingleStatementInput =
        QStringLiteral("module single_stmt_demo;\n"
                       "always_comb begin\n"
                       "if (en)\n"
                       "y = a;\n"
                       "else if (sel)\n"
                       "y = b;\n"
                       "else\n"
                       "y = c;\n"
                       "for (int i = 0; i < 2; i++)\n"
                       "data[i] = value;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterSingleStatementReport =
        FormatterService::getInstance()->formatDocument(
            formatterSingleStatementInput);
    expectBool("Formatter single statement body report changed",
               formatterSingleStatementReport.changed,
               true);
    expectEq("Formatter indents single statement bodies",
             formatterSingleStatementReport.formattedText,
             QStringLiteral("module single_stmt_demo;\n"
                            "    always_comb begin\n"
                            "        if (en)\n"
                            "            y = a;\n"
                            "        else if (sel)\n"
                            "            y = b;\n"
                            "        else\n"
                            "            y = c;\n"
                            "        for (int i = 0; i < 2; i++)\n"
                            "            data[i] = value;\n"
                            "    end\n"
                            "endmodule\n"));
    const FormatterReport unchangedSingleStatementReport =
        FormatterService::getInstance()->formatDocument(
            formatterSingleStatementReport.formattedText);
    expectBool("Formatter single statement body idempotent",
               unchangedSingleStatementReport.changed,
               false);
    const FormatterReport indentOnlySingleStatementReport =
        FormatterService::getInstance()->formatDocument(
            formatterSingleStatementInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps single statement body indentation",
             indentOnlySingleStatementReport.formattedText,
             formatterSingleStatementReport.formattedText);

    const QString formatterProceduralBodyInput =
        QStringLiteral("module procedural_stmt_demo;\n"
                       "always_ff @(posedge clk)\n"
                       "q <= d;\n"
                       "always_comb\n"
                       "y = a & b;\n"
                       "initial\n"
                       "ready = 1'b0;\n"
                       "final\n"
                       "$display(\"done\");\n"
                       "initial begin\n"
                       "forever\n"
                       "tick = ~tick;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterProceduralBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterProceduralBodyInput);
    expectBool("Formatter procedural body report changed",
               formatterProceduralBodyReport.changed,
               true);
    expectEq("Formatter indents procedural single statement bodies",
             formatterProceduralBodyReport.formattedText,
             QStringLiteral("module procedural_stmt_demo;\n"
                            "    always_ff @(posedge clk)\n"
                            "        q <= d;\n"
                            "    always_comb\n"
                            "        y = a & b;\n"
                            "    initial\n"
                            "        ready = 1'b0;\n"
                            "    final\n"
                            "        $display(\"done\");\n"
                            "    initial begin\n"
                            "        forever\n"
                            "            tick = ~tick;\n"
                            "    end\n"
                            "endmodule\n"));
    const FormatterReport unchangedProceduralBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterProceduralBodyReport.formattedText);
    expectBool("Formatter procedural body idempotent",
               unchangedProceduralBodyReport.changed,
               false);
    const FormatterReport indentOnlyProceduralBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterProceduralBodyInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps procedural body indentation",
             indentOnlyProceduralBodyReport.formattedText,
             formatterProceduralBodyReport.formattedText);

    const QString formatterMultilineHeaderBodyInput =
        QStringLiteral("module multiline_header_demo;\n"
                       "always_ff @(posedge clk or\n"
                       "negedge rst_n)\n"
                       "q <= d;\n"
                       "always @(a or\n"
                       "b)\n"
                       "y = a & b;\n"
                       "always_comb begin\n"
                       "if (sel &&\n"
                       "ready)\n"
                       "z = a;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterMultilineHeaderBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterMultilineHeaderBodyInput);
    expectBool("Formatter multiline header body report changed",
               formatterMultilineHeaderBodyReport.changed,
               true);
    expectEq("Formatter indents multiline header single statement bodies",
             formatterMultilineHeaderBodyReport.formattedText,
             QStringLiteral("module multiline_header_demo;\n"
                            "    always_ff @(posedge clk or\n"
                            "        negedge rst_n)\n"
                            "        q <= d;\n"
                            "    always @(a or\n"
                            "        b)\n"
                            "        y = a & b;\n"
                            "    always_comb begin\n"
                            "        if (sel &&\n"
                            "            ready)\n"
                            "            z = a;\n"
                            "    end\n"
                            "endmodule\n"));
    const FormatterReport unchangedMultilineHeaderBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterMultilineHeaderBodyReport.formattedText);
    expectBool("Formatter multiline header body idempotent",
               unchangedMultilineHeaderBodyReport.changed,
               false);
    const FormatterReport indentOnlyMultilineHeaderBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterMultilineHeaderBodyInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps multiline header body indentation",
             indentOnlyMultilineHeaderBodyReport.formattedText,
             formatterMultilineHeaderBodyReport.formattedText);

    const QString formatterSvBlockInput =
        QStringLiteral("program tb;\n"
                       "default clocking cb @(posedge clk);\n"
                       "input req;\n"
                       "output grant;\n"
                       "endclocking\n"
                       "property req_grant;\n"
                       "req |=> grant;\n"
                       "endproperty\n"
                       "sequence two_req;\n"
                       "req ##1 req;\n"
                       "endsequence\n"
                       "covergroup cg @(posedge clk);\n"
                       "coverpoint req;\n"
                       "endgroup\n"
                       "checker chk;\n"
                       "assert property (req_grant);\n"
                       "endchecker\n"
                       "endprogram\n");
    const FormatterReport formatterSvBlockReport =
        FormatterService::getInstance()->formatDocument(formatterSvBlockInput);
    expectBool("Formatter SystemVerilog block boundary report changed",
               formatterSvBlockReport.changed,
               true);
    expectEq("Formatter indents SystemVerilog block boundaries",
             formatterSvBlockReport.formattedText,
             QStringLiteral("program tb;\n"
                            "    default clocking cb @(posedge clk);\n"
                            "        input  req;\n"
                            "        output grant;\n"
                            "    endclocking\n"
                            "    property req_grant;\n"
                            "        req |=> grant;\n"
                            "    endproperty\n"
                            "    sequence two_req;\n"
                            "        req ##1 req;\n"
                            "    endsequence\n"
                            "    covergroup cg @(posedge clk);\n"
                            "        coverpoint req;\n"
                            "    endgroup\n"
                            "    checker chk;\n"
                            "        assert property (req_grant);\n"
                            "    endchecker\n"
                            "endprogram\n"));
    const FormatterReport unchangedSvBlockReport =
        FormatterService::getInstance()->formatDocument(
            formatterSvBlockReport.formattedText);
    expectBool("Formatter SystemVerilog block boundary idempotent",
               unchangedSvBlockReport.changed,
               false);
    const FormatterReport indentOnlySvBlockReport =
        FormatterService::getInstance()->formatDocument(
            formatterSvBlockInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps SystemVerilog block boundary indentation",
             indentOnlySvBlockReport.formattedText,
             QStringLiteral("program tb;\n"
                            "    default clocking cb @(posedge clk);\n"
                            "        input req;\n"
                            "        output grant;\n"
                            "    endclocking\n"
                            "    property req_grant;\n"
                            "        req |=> grant;\n"
                            "    endproperty\n"
                            "    sequence two_req;\n"
                            "        req ##1 req;\n"
                            "    endsequence\n"
                            "    covergroup cg @(posedge clk);\n"
                            "        coverpoint req;\n"
                            "    endgroup\n"
                            "    checker chk;\n"
                            "        assert property (req_grant);\n"
                            "    endchecker\n"
                            "endprogram\n"));
    const QString formatterForkInput =
        QStringLiteral("module fork_demo;\n"
                       "initial begin\n"
                       "fork\n"
                       "a = 1'b1;\n"
                       "b = 1'b0;\n"
                       "join_any\n"
                       "disable fork;\n"
                       "wait fork;\n"
                       "done = 1'b1;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterForkReport =
        FormatterService::getInstance()->formatDocument(formatterForkInput);
    expectBool("Formatter fork statement report changed",
               formatterForkReport.changed,
               true);
    expectEq("Formatter indents fork statements without disable/wait drift",
             formatterForkReport.formattedText,
             QStringLiteral("module fork_demo;\n"
                            "    initial begin\n"
                            "        fork\n"
                            "            a = 1'b1;\n"
                            "            b = 1'b0;\n"
                            "        join_any\n"
                            "        disable fork;\n"
                            "        wait fork;\n"
                            "        done = 1'b1;\n"
                            "    end\n"
                            "endmodule\n"));
    const FormatterReport unchangedForkReport =
        FormatterService::getInstance()->formatDocument(
            formatterForkReport.formattedText);
    expectBool("Formatter fork statement idempotent",
               unchangedForkReport.changed,
               false);
    const FormatterReport indentOnlyForkReport =
        FormatterService::getInstance()->formatDocument(
            formatterForkInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps fork statement indentation",
             indentOnlyForkReport.formattedText,
             formatterForkReport.formattedText);

    const QString formatterAlignmentInput =
        QStringLiteral("module align_demo;\n"
                       "logic [7:0] data;\n"
                       "logic valid;\n"
                       "parameter int P = 8;\n"
                       "parameter int LONG_NAME = P + 1;\n"
                       "endmodule\n");
    const FormatterReport formatterAlignmentReport =
        FormatterService::getInstance()->formatDocument(formatterAlignmentInput);
    expectBool("Formatter alignment report changed",
               formatterAlignmentReport.changed,
               true);
    expectEq("Formatter aligns declaration blocks",
             formatterAlignmentReport.formattedText,
             QStringLiteral("module align_demo;\n"
                            "    logic [7:0] data;\n"
                            "    logic       valid;\n"
                            "    parameter int P         = 8;\n"
                            "    parameter int LONG_NAME = P + 1;\n"
                            "endmodule\n"));
    const FormatterReport unchangedAlignmentReport =
        FormatterService::getInstance()->formatDocument(
            formatterAlignmentReport.formattedText);
    expectBool("Formatter alignment idempotent",
               unchangedAlignmentReport.changed,
               false);
    const QString formatterArrayDeclInput =
        QStringLiteral("module array_decl_demo;\n"
                       "logic flag [3:0];\n"
                       "logic [7:0] data_bus [DEPTH-1:0];\n"
                       "parameter int LUT [4] = '{0, 1, 2, 3};\n"
                       "parameter int LONG_LUT [DEPTH] = DEFAULT_LUT;\n"
                       "endmodule\n");
    const FormatterReport formatterArrayDeclReport =
        FormatterService::getInstance()->formatDocument(
            formatterArrayDeclInput);
    expectBool("Formatter declaration array dimension report changed",
               formatterArrayDeclReport.changed,
               true);
    expectEq("Formatter aligns declaration array dimensions",
             formatterArrayDeclReport.formattedText,
             QStringLiteral("module array_decl_demo;\n"
                            "    logic       flag     [3:0];\n"
                            "    logic [7:0] data_bus [DEPTH-1:0];\n"
                            "    parameter int LUT      [4]     = '{0, 1, 2, 3};\n"
                            "    parameter int LONG_LUT [DEPTH] = DEFAULT_LUT;\n"
                            "endmodule\n"));
    const FormatterReport unchangedArrayDeclReport =
        FormatterService::getInstance()->formatDocument(
            formatterArrayDeclReport.formattedText);
    expectBool("Formatter declaration array dimension idempotent",
               unchangedArrayDeclReport.changed,
               false);
    const QString formatterParameterPortInput =
        QStringLiteral("module param_port_demo #(\n"
                       "parameter int P = 8,\n"
                       "parameter int LONG_PARAM = P + 1,\n"
                       "localparam logic [7:0] MASK [2] = '{default: 1'b0}\n"
                       ")();\n"
                       "endmodule\n");
    const FormatterReport formatterParameterPortReport =
        FormatterService::getInstance()->formatDocument(
            formatterParameterPortInput);
    expectBool("Formatter parameter port list report changed",
               formatterParameterPortReport.changed,
               true);
    expectEq("Formatter aligns parameter port lists",
             formatterParameterPortReport.formattedText,
             QStringLiteral("module param_port_demo #(\n")
                 + QStringLiteral("    parameter int")
                 + QString(10, QLatin1Char(' '))
                 + QStringLiteral("P")
                 + QString(14, QLatin1Char(' '))
                 + QStringLiteral("= 8,\n")
                 + QStringLiteral("    parameter int")
                 + QString(10, QLatin1Char(' '))
                 + QStringLiteral("LONG_PARAM")
                 + QString(5, QLatin1Char(' '))
                 + QStringLiteral("= P + 1,\n")
                 + QStringLiteral("    localparam logic [7:0] MASK")
                 + QString(7, QLatin1Char(' '))
                 + QStringLiteral("[2] = '{default: 1'b0}\n"
                                  "    )();\n"
                                  "endmodule\n"));
    const FormatterReport unchangedParameterPortReport =
        FormatterService::getInstance()->formatDocument(
            formatterParameterPortReport.formattedText);
    expectBool("Formatter parameter port list idempotent",
               unchangedParameterPortReport.changed,
               false);
    const FormatterOptions indentOnlyOptions =
        FormatterService::optionsForProfile(FormatterProfile::IndentOnly);
    expectBool("Formatter indent-only profile disables alignments",
               !indentOnlyOptions.alignDeclarationBlocks
                   && !indentOnlyOptions.alignPortLists
                   && !indentOnlyOptions.alignInstanceMaps
                   && !indentOnlyOptions.alignCaseItems
                   && !indentOnlyOptions.alignEnumItems
                   && !indentOnlyOptions.alignAssignments
                   && !indentOnlyOptions.alignContinuationOperators
                   && !indentOnlyOptions.alignCallArgumentContinuations,
               true);
    expectEq("Formatter indent-only profile name",
             FormatterService::profileDisplayName(
                 FormatterProfile::IndentOnly),
             QStringLiteral("Indent Only"));
    const FormatterReport indentOnlyReport =
        FormatterService::getInstance()->formatDocument(
            formatterAlignmentInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only profile skips declaration alignment",
             indentOnlyReport.formattedText,
             QStringLiteral("module align_demo;\n"
                            "    logic [7:0] data;\n"
                            "    logic valid;\n"
                            "    parameter int P = 8;\n"
                            "    parameter int LONG_NAME = P + 1;\n"
                            "endmodule\n"));
    const FormatterReport indentOnlyArrayDeclReport =
        FormatterService::getInstance()->formatDocument(
            formatterArrayDeclInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips declaration array dimension alignment",
             indentOnlyArrayDeclReport.formattedText,
             QStringLiteral("module array_decl_demo;\n"
                            "    logic flag [3:0];\n"
                            "    logic [7:0] data_bus [DEPTH-1:0];\n"
                            "    parameter int LUT [4] = '{0, 1, 2, 3};\n"
                            "    parameter int LONG_LUT [DEPTH] = DEFAULT_LUT;\n"
                            "endmodule\n"));
    const FormatterReport indentOnlyParameterPortReport =
        FormatterService::getInstance()->formatDocument(
            formatterParameterPortInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips parameter port list alignment",
             indentOnlyParameterPortReport.formattedText,
             QStringLiteral("module param_port_demo #(\n"
                            "    parameter int P = 8,\n"
                            "    parameter int LONG_PARAM = P + 1,\n"
                            "    localparam logic [7:0] MASK [2] = '{default: 1'b0}\n"
                            "    )();\n"
                            "endmodule\n"));

    const QString formatterPortListInput =
        QStringLiteral("module port_demo(\n"
                       "input logic clk,\n"
                       "input logic [7:0] data,\n"
                       "output logic ready,\n"
                       "inout wire pad\n"
                       ");\n"
                       "endmodule\n");
    const FormatterReport formatterPortListReport =
        FormatterService::getInstance()->formatDocument(formatterPortListInput);
    expectBool("Formatter port list report changed",
               formatterPortListReport.changed,
               true);
    expectEq("Formatter aligns port lists",
             formatterPortListReport.formattedText,
             QStringLiteral("module port_demo(\n"
                            "    input  logic       clk,\n"
                            "    input  logic [7:0] data,\n"
                            "    output logic       ready,\n"
                            "    inout  wire        pad\n"
                            "    );\n"
                            "endmodule\n"));
    const FormatterReport unchangedPortListReport =
        FormatterService::getInstance()->formatDocument(
            formatterPortListReport.formattedText);
    expectBool("Formatter port list idempotent",
               unchangedPortListReport.changed,
               false);

    const QString formatterInstanceMapInput =
        QStringLiteral("module inst_demo;\n"
                       "child #(\n"
                       ".PARAM(8),\n"
                       ".LONG_PARAM(WIDTH)\n"
                       ") u_child (\n"
                       ".clk(clk),\n"
                       ".rst_n(rst_n),\n"
                       ".data_in(data_bus),\n"
                       ".ready(ready)\n"
                       ");\n"
                       "endmodule\n");
    const FormatterReport formatterInstanceMapReport =
        FormatterService::getInstance()->formatDocument(
            formatterInstanceMapInput);
    expectBool("Formatter instance map report changed",
               formatterInstanceMapReport.changed,
               true);
    expectEq("Formatter aligns instance maps",
             formatterInstanceMapReport.formattedText,
             QStringLiteral("module inst_demo;\n"
                            "    child #(\n"
                            "        .PARAM      (8),\n"
                            "        .LONG_PARAM (WIDTH)\n"
                            "    ) u_child (\n"
                            "        .clk     (clk),\n"
                            "        .rst_n   (rst_n),\n"
                            "        .data_in (data_bus),\n"
                            "        .ready   (ready)\n"
                            "    );\n"
                            "endmodule\n"));
    const FormatterReport unchangedInstanceMapReport =
        FormatterService::getInstance()->formatDocument(
            formatterInstanceMapReport.formattedText);
    expectBool("Formatter instance map idempotent",
               unchangedInstanceMapReport.changed,
               false);

    const QString formatterTrailingCommentInput =
        QStringLiteral("module comment_demo(\n"
                       "input logic clk, // clock\n"
                       "output logic ready // done\n"
                       ");\n"
                       "logic a; // flag\n"
                       "logic [7:0] data; // byte\n"
                       "child u_child (\n"
                       ".clk(clk), // clock\n"
                       ".data_in(data) // bus\n"
                       ");\n"
                       "endmodule\n");
    const FormatterReport formatterTrailingCommentReport =
        FormatterService::getInstance()->formatDocument(
            formatterTrailingCommentInput);
    expectBool("Formatter trailing comment report changed",
               formatterTrailingCommentReport.changed,
               true);
    expectEq("Formatter preserves aligned trailing comments",
             formatterTrailingCommentReport.formattedText,
             QStringLiteral("module comment_demo(\n"
                            "    input  logic clk,   // clock\n"
                            "    output logic ready  // done\n"
                            "    );\n"
                            "    logic       a;     // flag\n"
                            "    logic [7:0] data;  // byte\n"
                            "    child u_child (\n"
                            "        .clk     (clk),  // clock\n"
                            "        .data_in (data)  // bus\n"
                            "    );\n"
                            "endmodule\n"));
    const FormatterReport unchangedTrailingCommentReport =
        FormatterService::getInstance()->formatDocument(
            formatterTrailingCommentReport.formattedText);
    expectBool("Formatter trailing comment idempotent",
               unchangedTrailingCommentReport.changed,
               false);

    const QString formatterCaseItemInput =
        QStringLiteral("module case_demo;\n"
                       "always_comb begin\n"
                       "case (sel)\n"
                       "1'b0: y = a; // zero\n"
                       "STATE_LONG: y = b; // long\n"
                       "default: y = c;\n"
                       "endcase\n"
                       "unique casez (mode)\n"
                       "2'b0?: y = a;\n"
                       "default: y = b;\n"
                       "endcase\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterCaseItemReport =
        FormatterService::getInstance()->formatDocument(
            formatterCaseItemInput);
    expectBool("Formatter case item report changed",
               formatterCaseItemReport.changed,
               true);
    expectEq("Formatter aligns case items",
             formatterCaseItemReport.formattedText,
             QStringLiteral("module case_demo;\n"
                            "    always_comb begin\n"
                            "        case (sel)\n"
                            "            1'b0      : y = a;  // zero\n"
                            "            STATE_LONG: y = b;  // long\n"
                            "            default   : y = c;\n"
                            "        endcase\n"
                            "        unique casez (mode)\n"
                            "            2'b0?  : y = a;\n"
                            "            default: y = b;\n"
                            "        endcase\n"
                            "    end\n"
                            "endmodule\n"));
    const FormatterReport unchangedCaseItemReport =
        FormatterService::getInstance()->formatDocument(
            formatterCaseItemReport.formattedText);
    expectBool("Formatter case item idempotent",
               unchangedCaseItemReport.changed,
               false);
    const QString formatterCaseBodyInput =
        QStringLiteral("module case_body_demo;\n"
                       "always_comb begin\n"
                       "case (state)\n"
                       "IDLE:\n"
                       "next = RUN;\n"
                       "LONG_STATE:\n"
                       "next = DONE;\n"
                       "default:\n"
                       "next = IDLE;\n"
                       "endcase\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterCaseBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterCaseBodyInput);
    expectBool("Formatter case item body report changed",
               formatterCaseBodyReport.changed,
               true);
    expectEq("Formatter indents case item bodies",
             formatterCaseBodyReport.formattedText,
             QStringLiteral("module case_body_demo;\n"
                            "    always_comb begin\n"
                            "        case (state)\n"
                            "            IDLE:\n"
                            "                next = RUN;\n"
                            "            LONG_STATE:\n"
                            "                next = DONE;\n"
                            "            default:\n"
                            "                next = IDLE;\n"
                            "        endcase\n"
                            "    end\n"
                            "endmodule\n"));
    const FormatterReport unchangedCaseBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterCaseBodyReport.formattedText);
    expectBool("Formatter case item body idempotent",
               unchangedCaseBodyReport.changed,
               false);
    const FormatterReport indentOnlyCaseBodyReport =
        FormatterService::getInstance()->formatDocument(
            formatterCaseBodyInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps case item body indentation",
             indentOnlyCaseBodyReport.formattedText,
             QStringLiteral("module case_body_demo;\n"
                            "    always_comb begin\n"
                            "        case (state)\n"
                            "            IDLE:\n"
                            "                next = RUN;\n"
                            "            LONG_STATE:\n"
                            "                next = DONE;\n"
                            "            default:\n"
                            "                next = IDLE;\n"
                            "        endcase\n"
                            "    end\n"
                            "endmodule\n"));

    const QString formatterEnumInput =
        QStringLiteral("module enum_demo;\n"
                       "typedef enum logic [1:0] {\n"
                       "IDLE = 2'd0,\n"
                       "LONG_STATE = 2'd1, // active\n"
                       "DONE\n"
                       "} state_e;\n"
                       "endmodule\n");
    const FormatterReport formatterEnumReport =
        FormatterService::getInstance()->formatDocument(
            formatterEnumInput);
    expectBool("Formatter enum item report changed",
               formatterEnumReport.changed,
               true);
    expectEq("Formatter aligns enum items",
             formatterEnumReport.formattedText,
             QStringLiteral("module enum_demo;\n"
                            "    typedef enum logic [1:0] {\n"
                            "        IDLE       = 2'd0,\n"
                            "        LONG_STATE = 2'd1,  // active\n"
                            "        DONE\n"
                            "    } state_e;\n"
                            "endmodule\n"));
    const FormatterReport unchangedEnumReport =
        FormatterService::getInstance()->formatDocument(
            formatterEnumReport.formattedText);
    expectBool("Formatter enum item idempotent",
               unchangedEnumReport.changed,
               false);
    const FormatterReport indentOnlyEnumReport =
        FormatterService::getInstance()->formatDocument(
            formatterEnumInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips enum item alignment",
             indentOnlyEnumReport.formattedText,
             QStringLiteral("module enum_demo;\n"
                            "    typedef enum logic [1:0] {\n"
                            "        IDLE = 2'd0,\n"
                            "        LONG_STATE = 2'd1, // active\n"
                            "        DONE\n"
                            "    } state_e;\n"
                            "endmodule\n"));

    const QString formatterAssignmentInput =
        QStringLiteral("module assign_demo;\n"
                       "assign short = a;\n"
                       "assign very_long_name = b; // output\n"
                       "always_ff @(posedge clk) begin\n"
                       "q <= d;\n"
                       "wide_data[3:0] <= next_data[3:0]; // sample\n"
                       "end\n"
                       "always_comb begin\n"
                       "temp = a == b;\n"
                       "long_temp = temp ? c : d; // combo\n"
                       "if (temp) keep = d;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterAssignmentReport =
        FormatterService::getInstance()->formatDocument(
            formatterAssignmentInput);
    expectBool("Formatter assignment report changed",
               formatterAssignmentReport.changed,
               true);
    expectEq("Formatter aligns assignment blocks",
             formatterAssignmentReport.formattedText,
             QStringLiteral("module assign_demo;\n"
                            "    assign short          = a;\n"
                            "    assign very_long_name = b;  // output\n"
                            "    always_ff @(posedge clk) begin\n"
                            "        q              <= d;\n"
                            "        wide_data[3:0] <= next_data[3:0];  // sample\n"
                            "    end\n"
                            "    always_comb begin\n"
                            "        temp      = a == b;\n"
                            "        long_temp = temp ? c : d;  // combo\n"
                            "        if (temp) keep = d;\n"
                            "    end\n"
                            "endmodule\n"));
    const FormatterReport unchangedAssignmentReport =
        FormatterService::getInstance()->formatDocument(
            formatterAssignmentReport.formattedText);
    expectBool("Formatter assignment idempotent",
               unchangedAssignmentReport.changed,
               false);
    const FormatterReport indentOnlyAssignmentReport =
        FormatterService::getInstance()->formatDocument(
            formatterAssignmentInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only profile skips assignment alignment",
             indentOnlyAssignmentReport.formattedText,
             QStringLiteral("module assign_demo;\n"
                            "    assign short = a;\n"
                            "    assign very_long_name = b; // output\n"
                            "    always_ff @(posedge clk) begin\n"
                            "        q <= d;\n"
                            "        wide_data[3:0] <= next_data[3:0]; // sample\n"
                            "    end\n"
                            "    always_comb begin\n"
                            "        temp = a == b;\n"
                            "        long_temp = temp ? c : d; // combo\n"
                            "        if (temp) keep = d;\n"
                            "    end\n"
                            "endmodule\n"));

    const QString formatterContinuationInput =
        QStringLiteral("module continuation_demo;\n"
                       "assign out = {\n"
                       "a,\n"
                       "b\n"
                       "};\n"
                       "always_comb begin\n"
                       "result = func(\n"
                       "a,\n"
                       "b\n"
                       ");\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterContinuationReport =
        FormatterService::getInstance()->formatDocument(
            formatterContinuationInput,
            FormatterProfile::IndentOnly);
    expectBool("Formatter continuation indent report changed",
               formatterContinuationReport.changed,
               true);
    expectEq("Formatter indents continuation lines",
             formatterContinuationReport.formattedText,
             QStringLiteral("module continuation_demo;\n"
                            "    assign out = {\n"
                            "        a,\n"
                            "        b\n"
                            "    };\n"
                            "    always_comb begin\n"
                            "        result = func(\n"
                            "            a,\n"
                            "            b\n"
                            "        );\n"
                            "    end\n"
                            "endmodule\n"));
    const FormatterReport unchangedContinuationReport =
        FormatterService::getInstance()->formatDocument(
            formatterContinuationReport.formattedText,
            FormatterProfile::IndentOnly);
    expectBool("Formatter continuation indent idempotent",
               unchangedContinuationReport.changed,
               false);

    const QString formatterRhsContinuationInput =
        QStringLiteral("module rhs_demo;\n"
                       "always_comb begin\n"
                       "result =\n"
                       "lhs\n"
                       "+ rhs;\n"
                       "call_result =\n"
                       "func(\n"
                       "a,\n"
                       "b\n"
                       ");\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterRhsContinuationReport =
        FormatterService::getInstance()->formatDocument(
            formatterRhsContinuationInput);
    expectBool("Formatter RHS continuation report changed",
               formatterRhsContinuationReport.changed,
               true);
    expectEq("Formatter indents assignment RHS continuations",
             formatterRhsContinuationReport.formattedText,
             QStringLiteral("module rhs_demo;\n"
                            "    always_comb begin\n"
                            "        result =\n"
                            "            lhs\n"
                            "            + rhs;\n"
                            "        call_result =\n"
                            "            func(\n"
                            "                 a,\n"
                            "                 b\n"
                            "            );\n"
                            "    end\n"
                            "endmodule\n"));
    const FormatterReport unchangedRhsContinuationReport =
        FormatterService::getInstance()->formatDocument(
            formatterRhsContinuationReport.formattedText);
    expectBool("Formatter RHS continuation idempotent",
               unchangedRhsContinuationReport.changed,
               false);
    const FormatterReport indentOnlyRhsContinuationReport =
        FormatterService::getInstance()->formatDocument(
            formatterRhsContinuationInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only keeps RHS continuation indentation",
             indentOnlyRhsContinuationReport.formattedText,
             QStringLiteral("module rhs_demo;\n"
                            "    always_comb begin\n"
                            "        result =\n"
                            "            lhs\n"
                            "            + rhs;\n"
                            "        call_result =\n"
                            "            func(\n"
                            "                a,\n"
                            "                b\n"
                            "            );\n"
                            "    end\n"
                            "endmodule\n"));

    const QString formatterCallArgumentInput =
        QStringLiteral("module call_arg_demo;\n"
                       "always_comb begin\n"
                       "result = func(\n"
                       "a,\n"
                       "long_arg\n"
                       ");\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterCallArgumentReport =
        FormatterService::getInstance()->formatDocument(
            formatterCallArgumentInput);
    expectBool("Formatter call argument continuation report changed",
               formatterCallArgumentReport.changed,
               true);
    expectEq("Formatter aligns call argument continuations",
             formatterCallArgumentReport.formattedText,
             QStringLiteral("module call_arg_demo;\n"
                            "    always_comb begin\n"
                            "        result = func(\n")
                 + QString(22, QLatin1Char(' '))
                 + QStringLiteral("a,\n")
                 + QString(22, QLatin1Char(' '))
                 + QStringLiteral("long_arg\n"
                                  "        );\n"
                                  "    end\n"
                                  "endmodule\n"));
    const FormatterReport unchangedCallArgumentReport =
        FormatterService::getInstance()->formatDocument(
            formatterCallArgumentReport.formattedText);
    expectBool("Formatter call argument continuation idempotent",
               unchangedCallArgumentReport.changed,
               false);
    const FormatterReport indentOnlyCallArgumentReport =
        FormatterService::getInstance()->formatDocument(
            formatterCallArgumentInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips call argument continuation alignment",
             indentOnlyCallArgumentReport.formattedText,
             QStringLiteral("module call_arg_demo;\n"
                            "    always_comb begin\n"
                            "        result = func(\n"
                            "            a,\n"
                            "            long_arg\n"
                            "        );\n"
                            "    end\n"
                            "endmodule\n"));

    const QString formatterOperatorInput =
        QStringLiteral("module op_demo;\n"
                       "always_comb begin\n"
                       "result = lhs\n"
                       "+ short\n"
                       "- very_long_term\n"
                       "| mask;\n"
                       "assign out = lhs\n"
                       "^ rhs;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterOperatorReport =
        FormatterService::getInstance()->formatDocument(
            formatterOperatorInput);
    expectBool("Formatter continuation operator report changed",
               formatterOperatorReport.changed,
               true);
    expectEq("Formatter aligns continuation operators",
             formatterOperatorReport.formattedText,
             QStringLiteral("module op_demo;\n"
                            "    always_comb begin\n"
                            "        result = lhs\n")
                 + QString(17, QLatin1Char(' '))
                 + QStringLiteral("+ short\n")
                 + QString(17, QLatin1Char(' '))
                 + QStringLiteral("- very_long_term\n")
                 + QString(17, QLatin1Char(' '))
                 + QStringLiteral("| mask;\n"
                                  "        assign out = lhs\n")
                 + QString(21, QLatin1Char(' '))
                 + QStringLiteral("^ rhs;\n"
                                  "    end\n"
                                  "endmodule\n"));
    const FormatterReport unchangedOperatorReport =
        FormatterService::getInstance()->formatDocument(
            formatterOperatorReport.formattedText);
    expectBool("Formatter continuation operator idempotent",
               unchangedOperatorReport.changed,
               false);
    const FormatterReport indentOnlyOperatorReport =
        FormatterService::getInstance()->formatDocument(
            formatterOperatorInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips continuation operator alignment",
             indentOnlyOperatorReport.formattedText,
             QStringLiteral("module op_demo;\n"
                            "    always_comb begin\n"
                            "        result = lhs\n"
                            "        + short\n"
                            "        - very_long_term\n"
                            "        | mask;\n"
                            "        assign out = lhs\n"
                            "        ^ rhs;\n"
                            "    end\n"
                            "endmodule\n"));

    const QString formatterTernaryInput =
        QStringLiteral("module ternary_demo;\n"
                       "assign mux = sel\n"
                       "? data_a\n"
                       ": data_b;\n"
                       "always_comb begin\n"
                       "next = enable\n"
                       "? value_a\n"
                       ": value_b;\n"
                       "end\n"
                       "endmodule\n");
    const FormatterReport formatterTernaryReport =
        FormatterService::getInstance()->formatDocument(
            formatterTernaryInput);
    expectBool("Formatter ternary continuation report changed",
               formatterTernaryReport.changed,
               true);
    expectEq("Formatter aligns ternary continuations",
             formatterTernaryReport.formattedText,
             QStringLiteral("module ternary_demo;\n"
                            "    assign mux = sel\n")
                 + QString(17, QLatin1Char(' '))
                 + QStringLiteral("? data_a\n")
                 + QString(17, QLatin1Char(' '))
                 + QStringLiteral(": data_b;\n"
                                  "    always_comb begin\n"
                                  "        next = enable\n")
                 + QString(15, QLatin1Char(' '))
                 + QStringLiteral("? value_a\n")
                 + QString(15, QLatin1Char(' '))
                 + QStringLiteral(": value_b;\n"
                                  "    end\n"
                                  "endmodule\n"));
    const FormatterReport unchangedTernaryReport =
        FormatterService::getInstance()->formatDocument(
            formatterTernaryReport.formattedText);
    expectBool("Formatter ternary continuation idempotent",
               unchangedTernaryReport.changed,
               false);
    const FormatterReport indentOnlyTernaryReport =
        FormatterService::getInstance()->formatDocument(
            formatterTernaryInput,
            FormatterProfile::IndentOnly);
    expectEq("Formatter indent-only skips ternary continuation alignment",
             indentOnlyTernaryReport.formattedText,
             QStringLiteral("module ternary_demo;\n"
                            "    assign mux = sel\n"
                            "    ? data_a\n"
                            "    : data_b;\n"
                            "    always_comb begin\n"
                            "        next = enable\n"
                            "        ? value_a\n"
                            "        : value_b;\n"
                            "    end\n"
                            "endmodule\n"));

    const QString formatterSelectionInput =
        QStringLiteral("    logic a; // flag\n"
                       "    logic [7:0] data; // byte\n");
    const FormatterReport formatterSelectionReport =
        FormatterService::getInstance()->formatSelection(
            formatterSelectionInput);
    expectBool("Formatter selection report changed",
               formatterSelectionReport.changed,
               true);
    expectEq("Formatter selection preserves base indentation",
             formatterSelectionReport.formattedText,
             QStringLiteral("    logic       a;     // flag\n"
                            "    logic [7:0] data;  // byte\n"));
    const FormatterReport unchangedSelectionReport =
        FormatterService::getInstance()->formatSelection(
            formatterSelectionReport.formattedText);
    expectBool("Formatter selection idempotent",
               unchangedSelectionReport.changed,
               false);

    const QString wavePreviewInput =
        QStringLiteral("module wave_probe(\n"
                       "    input logic clk,\n"
                       "    input logic rst_n,\n"
                       "    input logic en,\n"
                       "    input logic [7:0] data,\n"
                       "    output logic [7:0] out\n"
                       ");\n"
                       "logic [7:0] q;\n"
                       "logic [7:0] next;\n"
                       "assign out = q + data;\n"
                       "// assign ghost = data;\n"
                       "initial $display(\"ghost <= value\");\n"
                       "always_ff @(posedge clk or negedge rst_n) begin\n"
                       "    if (!rst_n) q <= '0;\n"
                       "    else if (en) q <= data;\n"
                       "end\n"
                       "always_comb begin\n"
                       "    next = q + data;\n"
                       "end\n"
                       "always @(posedge clk) pulse <= en;\n"
                       "endmodule\n");
    const WavePreviewReport waveReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("wave_probe.sv"), wavePreviewInput});
    expectBool("WavePreview report available",
               waveReport.available,
               true);
    expectBool("WavePreview block count",
               waveReport.blocks.size() == 3,
               true);
    expectBool("WavePreview assignment count",
               waveReport.assignmentCount == 5,
               true);
    expectBool("WavePreview no comment/string ghost lane",
               waveLaneNamed(waveReport, QStringLiteral("ghost")) == nullptr,
               true);
    TSDocument waveScopeDocument;
    waveScopeDocument.setText(wavePreviewInput);
    const int firstAlwaysPosition =
        wavePreviewInput.indexOf(QStringLiteral("always_ff"));
    const int combAlwaysPosition =
        wavePreviewInput.indexOf(QStringLiteral("always_comb"));
    const int pulseAlwaysPosition =
        wavePreviewInput.indexOf(QStringLiteral("always @(posedge"));
    const TSAlwaysScopeTarget firstAlwaysScope =
        waveScopeDocument.alwaysScopeTarget(
            wavePreviewInput.indexOf(QStringLiteral("q <= '0")));
    const TSAlwaysScopeTarget combSelectedScope =
        waveScopeDocument.alwaysScopeTarget(combAlwaysPosition,
                                            combAlwaysPosition,
                                            pulseAlwaysPosition);
    const TSAlwaysScopeTarget ambiguousAlwaysScope =
        waveScopeDocument.alwaysScopeTarget(combAlwaysPosition,
                                            combAlwaysPosition,
                                            pulseAlwaysPosition + 12);
    expectBool("TSDocument current always scope",
               firstAlwaysScope.ok()
                   && firstAlwaysScope.startChar == firstAlwaysPosition
                   && firstAlwaysScope.kindText == QStringLiteral("always_ff")
                   && firstAlwaysScope.label.contains(QStringLiteral("always_ff")),
               true);
    expectBool("TSDocument selected always scope trims whitespace",
               combSelectedScope.ok()
                   && combSelectedScope.startChar == combAlwaysPosition
                   && combSelectedScope.kindText == QStringLiteral("always_comb"),
               true);
    expectBool("TSDocument selected always rejects multiple blocks",
               !ambiguousAlwaysScope.ok()
                   && ambiguousAlwaysScope.status
                          == TSAlwaysScopeStatus::AmbiguousSelection,
               true);

    MyCodeEditor waveScopeEditor;
    waveScopeEditor.setPlainText(wavePreviewInput);
    QTextCursor waveScopeCursor(waveScopeEditor.document());
    waveScopeCursor.setPosition(
        wavePreviewInput.indexOf(QStringLiteral("next = q")));
    waveScopeEditor.setTextCursor(waveScopeCursor);
    const EditorAlwaysScopeTarget editorAlwaysScope =
        waveScopeEditor.currentAlwaysScopeTarget();
    WavePreviewQuery editorAlwaysQuery;
    editorAlwaysQuery.fileName = QStringLiteral("wave_probe.sv");
    editorAlwaysQuery.documentText = wavePreviewInput;
    editorAlwaysQuery.scopeStartPosition = editorAlwaysScope.startPosition;
    editorAlwaysQuery.scopeEndPosition = editorAlwaysScope.endPosition;
    editorAlwaysQuery.scopeLabel = editorAlwaysScope.label;
    const WavePreviewReport editorAlwaysReport =
        WavePreviewService::getInstance()->previewForDocument(
            editorAlwaysQuery);
    expectBool("Editor WavePreview always scope report",
               editorAlwaysScope.ok()
                   && editorAlwaysScope.label.contains(
                       QStringLiteral("always_comb"))
                   && editorAlwaysReport.scoped
                   && editorAlwaysReport.scopeLabel == editorAlwaysScope.label
                   && editorAlwaysReport.available
                   && editorAlwaysReport.blocks.size() == 1
                   && editorAlwaysReport.assignmentCount == 1
                   && waveLaneNamed(editorAlwaysReport,
                                    QStringLiteral("next"))
                   && !waveLaneNamed(editorAlwaysReport,
                                     QStringLiteral("q"))
                   && !waveLaneNamed(editorAlwaysReport,
                                     QStringLiteral("out"))
                   && !waveLaneNamed(editorAlwaysReport,
                                     QStringLiteral("pulse")),
               true);

    const QString moduleScopedWaveInput =
        QStringLiteral("module wave_a(\n"
                       "    input logic a,\n"
                       "    output logic y\n"
                       ");\n"
                       "always_comb begin\n"
                       "    y = a;\n"
                       "end\n"
                       "endmodule\n"
                       "\n"
                       "module wave_b(\n"
                       "    input logic b,\n"
                       "    output logic z\n"
                       ");\n"
                       "assign z = b;\n"
                       "endmodule\n");
    TSDocument moduleScopeDocument;
    moduleScopeDocument.setText(moduleScopedWaveInput);
    const int waveAModulePosition =
        moduleScopedWaveInput.indexOf(QStringLiteral("module wave_a"));
    const int waveBModulePosition =
        moduleScopedWaveInput.indexOf(QStringLiteral("module wave_b"));
    const int waveBAssignPosition =
        moduleScopedWaveInput.indexOf(QStringLiteral("assign z"));
    const TSModuleScopeTarget waveBModuleScope =
        moduleScopeDocument.moduleScopeTarget(waveBAssignPosition);
    const TSModuleScopeTarget selectedWaveAModuleScope =
        moduleScopeDocument.moduleScopeTarget(waveAModulePosition,
                                              waveAModulePosition,
                                              waveBModulePosition);
    const TSModuleScopeTarget ambiguousModuleScope =
        moduleScopeDocument.moduleScopeTarget(waveAModulePosition,
                                              waveAModulePosition,
                                              moduleScopedWaveInput.size());
    expectBool("TSDocument current module scope",
               waveBModuleScope.ok()
                   && waveBModuleScope.moduleName == QStringLiteral("wave_b")
                   && waveBModuleScope.label.contains(QStringLiteral("wave_b")),
               true);
    expectBool("TSDocument selected module scope trims whitespace",
               selectedWaveAModuleScope.ok()
                   && selectedWaveAModuleScope.moduleName
                          == QStringLiteral("wave_a")
                   && selectedWaveAModuleScope.startChar
                          == waveAModulePosition,
               true);
    expectBool("TSDocument selected module rejects multiple modules",
               !ambiguousModuleScope.ok()
                   && ambiguousModuleScope.status
                          == TSModuleScopeStatus::AmbiguousSelection,
               true);

    MyCodeEditor moduleScopeEditor;
    moduleScopeEditor.setPlainText(moduleScopedWaveInput);
    QTextCursor moduleScopeCursor(moduleScopeEditor.document());
    moduleScopeCursor.setPosition(waveBAssignPosition);
    moduleScopeEditor.setTextCursor(moduleScopeCursor);
    const EditorModuleScopeTarget editorModuleScope =
        moduleScopeEditor.currentModuleScopeTarget();
    WavePreviewQuery editorModuleQuery;
    editorModuleQuery.fileName = QStringLiteral("module_wave_probe.sv");
    editorModuleQuery.documentText = moduleScopedWaveInput;
    editorModuleQuery.scopeStartPosition = editorModuleScope.startPosition;
    editorModuleQuery.scopeEndPosition = editorModuleScope.endPosition;
    editorModuleQuery.scopeLabel = editorModuleScope.label;
    const WavePreviewReport editorModuleReport =
        WavePreviewService::getInstance()->previewForDocument(
            editorModuleQuery);
    expectBool("Editor WavePreview module scope report",
               editorModuleScope.ok()
                   && editorModuleScope.moduleName == QStringLiteral("wave_b")
                   && editorModuleReport.scoped
                   && editorModuleReport.scopeLabel == editorModuleScope.label
                   && editorModuleReport.available
                   && editorModuleReport.assignmentCount == 1
                   && waveLaneNamed(editorModuleReport,
                                    QStringLiteral("z"))
                   && !waveLaneNamed(editorModuleReport,
                                     QStringLiteral("y")),
               true);
    expectBool("WavePreview q lane has two events",
               waveLaneNamed(waveReport, QStringLiteral("q"))
                   && waveLaneNamed(waveReport, QStringLiteral("q"))->assignments.size() == 2,
               true);
    const WavePreviewLane* qSummaryLane =
        waveLaneNamed(waveReport, QStringLiteral("q"));
    expectBool("WavePreview q lane summary",
               qSummaryLane
                   && qSummaryLane->summary.eventCount == 2
                   && qSummaryLane->summary.sourceSignalCount == 1
                   && qSummaryLane->summary.blockCount == 1
                   && qSummaryLane->summary.maxCycleOffset == 1
                   && qSummaryLane->summary.continuousEventCount == 0
                   && qSummaryLane->summary.combinationalEventCount == 0
                   && qSummaryLane->summary.sequentialEventCount == 2
                   && qSummaryLane->summary.guardTexts
                       == QStringList{QStringLiteral("if !rst_n"),
                                      QStringLiteral("if en")}
                   && qSummaryLane->summary.hasSequentialEvent
                   && !qSummaryLane->summary.hasContinuousEvent,
               true);
    const WavePreviewLane* outSummaryLane =
        waveLaneNamed(waveReport, QStringLiteral("out"));
    expectBool("WavePreview continuous lane summary",
               outSummaryLane
                   && outSummaryLane->summary.eventCount == 1
                   && outSummaryLane->summary.sourceSignalCount == 2
                   && outSummaryLane->summary.blockCount == 0
                   && outSummaryLane->summary.maxCycleOffset == 0
                   && outSummaryLane->summary.continuousEventCount == 1
                   && outSummaryLane->summary.combinationalEventCount == 0
                   && outSummaryLane->summary.sequentialEventCount == 0
                   && outSummaryLane->summary.hasContinuousEvent,
               true);
    const WavePreviewSignalContext* dataContext =
        waveContextNamed(waveReport, QStringLiteral("data"));
    expectBool("WavePreview input declaration context",
               dataContext
                   && dataContext->direction == QStringLiteral("input")
                   && dataContext->typeText == QStringLiteral("logic [7:0]"),
               true);
    const WavePreviewSignalContext* outContext =
        waveContextNamed(waveReport, QStringLiteral("out"));
    expectBool("WavePreview output declaration context",
               outContext
                   && outContext->direction == QStringLiteral("output")
                   && outContext->typeText == QStringLiteral("logic [7:0]"),
               true);
    const WavePreviewLane* qLane = waveLaneNamed(waveReport, QStringLiteral("q"));
    expectBool("WavePreview lane carries internal context",
               qLane
                   && qLane->context.direction == QStringLiteral("internal")
                   && qLane->context.typeText == QStringLiteral("logic [7:0]"),
               true);
    const QString waveWarningInput =
        QStringLiteral("module wave_warning;\n"
                       "logic clk;\n"
                       "logic a;\n"
                       "logic q;\n"
                       "assign q = a;\n"
                       "always_ff @(posedge clk) q <= a;\n"
                       "always_comb q = a;\n"
                       "endmodule\n");
    const WavePreviewReport waveWarningReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("wave_warning.sv"), waveWarningInput});
    expectBool("WavePreview lane warning count",
               waveWarningReport.warnings.size() == 2,
               true);
    expectBool("WavePreview warns mixed lane activity",
               waveWarningReport.warnings.contains(
                   QStringLiteral(
                       "signal q mixes assign/comb/seq activity; Wave Preview does not resolve writer priority")),
               true);
    expectBool("WavePreview warns multi-block lane activity",
               waveWarningReport.warnings.contains(
                   QStringLiteral(
                       "signal q is assigned from 2 procedural blocks; inspect block ownership before trusting lane timing")),
               true);
    const WavePreviewLane* warningQLane =
        waveLaneNamed(waveWarningReport, QStringLiteral("q"));
    expectBool("WavePreview lane summary carries warnings",
               warningQLane
                   && warningQLane->summary.warningTexts.size() == 2
                   && warningQLane->summary.warningTexts.contains(
                       QStringLiteral(
                           "signal q mixes assign/comb/seq activity; Wave Preview does not resolve writer priority"))
                   && warningQLane->summary.warningTexts.contains(
                       QStringLiteral(
                           "signal q is assigned from 2 procedural blocks; inspect block ownership before trusting lane timing")),
               true);
    QList<SemanticSymbolRecord> waveSemanticRecords;
    waveSemanticRecords.append(
        makeSemanticFixtureRecord(QStringLiteral("remote_cfg"),
                                  SymbolTaxonomy::DeclarationKind::Port,
                                  SymbolTaxonomy::CollectorKind::PortInput,
                                  QStringLiteral("remote_top"),
                                  QStringLiteral("logic [3:0]"),
                                  7001,
                                  QStringLiteral("remote_wave.sv")));
    waveSemanticRecords.append(
        makeSemanticFixtureRecord(QStringLiteral("remote_state"),
                                  SymbolTaxonomy::DeclarationKind::Signal,
                                  SymbolTaxonomy::CollectorKind::Logic,
                                  QStringLiteral("remote_top"),
                                  QStringLiteral("logic [5:0]"),
                                  7003,
                                  QStringLiteral("remote_wave.sv")));
    waveSemanticRecords.append(
        makeSemanticFixtureRecord(QStringLiteral("local_cfg"),
                                  SymbolTaxonomy::DeclarationKind::Signal,
                                  SymbolTaxonomy::CollectorKind::Logic,
                                  QStringLiteral("semantic_wave"),
                                  QStringLiteral("logic [9:0]"),
                                  7002,
                                  QStringLiteral("semantic_wave.sv")));
    const auto waveSemanticSnapshot =
        sharedSnapshotFromRecords(waveSemanticRecords);
    const QString waveSemanticInput =
        QStringLiteral("module semantic_wave;\n"
                       "logic [1:0] local_cfg;\n"
                       "logic [3:0] out;\n"
                       "assign out = remote_cfg + remote_state + local_cfg;\n"
                       "endmodule\n");
    const WavePreviewReport waveSemanticReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("semantic_wave.sv"),
             waveSemanticInput,
             waveSemanticSnapshot});
    const WavePreviewSignalContext* remoteCfgContext =
        waveContextNamed(waveSemanticReport, QStringLiteral("remote_cfg"));
    expectBool("WavePreview semantic source context",
               remoteCfgContext
                   && remoteCfgContext->direction == QStringLiteral("input")
                   && remoteCfgContext->typeText == QStringLiteral("logic [3:0]")
                   && remoteCfgContext->line == 7001,
               true);
    const WavePreviewSignalContext* remoteStateContext =
        waveContextNamed(waveSemanticReport, QStringLiteral("remote_state"));
    expectBool("WavePreview semantic internal source context",
               remoteStateContext
                   && remoteStateContext->direction == QStringLiteral("internal")
                   && remoteStateContext->typeText == QStringLiteral("logic [5:0]")
                   && remoteStateContext->line == 7003,
               true);
    const WavePreviewSignalContext* localCfgContext =
        waveContextNamed(waveSemanticReport, QStringLiteral("local_cfg"));
    expectBool("WavePreview local context beats semantic snapshot",
               localCfgContext
                   && localCfgContext->direction == QStringLiteral("internal")
                   && localCfgContext->typeText == QStringLiteral("logic [1:0]"),
               true);
    expectBool("WavePreview reset guard label",
               qLane
                   && qLane->assignments.size() > 0
                   && qLane->assignments.first().guardText == QStringLiteral("if !rst_n"),
               true);
    expectBool("WavePreview enable guard label",
               qLane
                   && qLane->assignments.size() > 1
                   && qLane->assignments.at(1).guardText == QStringLiteral("if en"),
               true);
    expectBool("WavePreview always_ff block kind",
               !waveReport.blocks.isEmpty()
                   && waveReport.blocks.first().kind == WavePreviewBlockKind::AlwaysFf
                   && waveReport.blocks.first().assignmentCount == 2,
               true);
    expectList("WavePreview always_ff clock signals",
               !waveReport.blocks.isEmpty()
                   ? waveReport.blocks.first().clockSignals
                   : QStringList(),
               {QStringLiteral("clk")});
    expectList("WavePreview always_ff reset signals",
               !waveReport.blocks.isEmpty()
                   ? waveReport.blocks.first().resetSignals
                   : QStringList(),
               {QStringLiteral("rst_n")});
    expectList("WavePreview always_ff clock edges",
               !waveReport.blocks.isEmpty()
                   ? waveEdgeLabels(waveReport.blocks.first().clockEdgeSignals)
                   : QStringList(),
               {QStringLiteral("posedge clk")});
    expectList("WavePreview always_ff reset edges",
               !waveReport.blocks.isEmpty()
                   ? waveEdgeLabels(waveReport.blocks.first().resetEdgeSignals)
                   : QStringList(),
               {QStringLiteral("negedge rst_n")});
    expectBool("WavePreview always_comb block kind",
               waveReport.blocks.size() > 1
                   && waveReport.blocks.at(1).kind == WavePreviewBlockKind::AlwaysComb
                   && waveReport.blocks.at(1).assignmentCount == 1,
               true);
    expectBool("WavePreview clocked always block kind",
               waveReport.blocks.size() > 2
                   && waveReport.blocks.at(2).kind == WavePreviewBlockKind::AlwaysClocked
                   && waveReport.blocks.at(2).assignmentCount == 1,
               true);
    const WavePreviewAssignment* outAssign =
        firstWaveAssignment(waveReport, QStringLiteral("out"));
    expectBool("WavePreview continuous assignment kind",
               outAssign
                   && outAssign->kind == WavePreviewAssignmentKind::Continuous
                   && outAssign->cycleOffset == 0,
               true);
    expectList("WavePreview continuous sources",
               outAssign ? outAssign->sourceSignals : QStringList(),
               {QStringLiteral("q"), QStringLiteral("data")});
    const WavePreviewAssignment* qAssign =
        firstWaveAssignment(waveReport, QStringLiteral("q"));
    expectBool("WavePreview sequential cycle offset",
               qAssign
                   && qAssign->kind == WavePreviewAssignmentKind::NonBlocking
                   && qAssign->cycleOffset == 1
                   && qAssign->trigger.contains(QStringLiteral("posedge clk")),
               true);
    const WavePreviewAssignment* nextAssign =
        firstWaveAssignment(waveReport, QStringLiteral("next"));
    expectBool("WavePreview combinational cycle offset",
               nextAssign
                   && nextAssign->kind == WavePreviewAssignmentKind::Blocking
                   && nextAssign->cycleOffset == 0,
               true);
    expectList("WavePreview combinational sources",
               nextAssign ? nextAssign->sourceSignals : QStringList(),
               {QStringLiteral("q"), QStringLiteral("data")});
    const WavePreviewLane* nextSummaryLane =
        waveLaneNamed(waveReport, QStringLiteral("next"));
    expectBool("WavePreview blocking lane activity count",
               nextSummaryLane
                   && nextSummaryLane->summary.combinationalEventCount == 1
                   && nextSummaryLane->summary.sequentialEventCount == 0
                   && nextSummaryLane->summary.continuousEventCount == 0,
               true);
    const WavePreviewAssignment* pulseAssign =
        firstWaveAssignment(waveReport, QStringLiteral("pulse"));
    expectBool("WavePreview plain clocked always",
               pulseAssign
                   && pulseAssign->cycleOffset == 1
                   && pulseAssign->sourceSignals.contains(QStringLiteral("en")),
               true);
    expectBool("WavePreview clock reset groups",
               waveReport.clockResetGroups.size() == 2
                   && waveReport.clockResetGroups.first().clockSignals
                       == QStringList{QStringLiteral("clk")}
                   && waveReport.clockResetGroups.first().resetSignals
                       == QStringList{QStringLiteral("rst_n")}
                   && waveEdgeLabels(
                          waveReport.clockResetGroups.first().clockEdgeSignals)
                       == QStringList{QStringLiteral("posedge clk")}
                   && waveEdgeLabels(
                          waveReport.clockResetGroups.first().resetEdgeSignals)
                       == QStringList{QStringLiteral("negedge rst_n")}
                   && waveReport.clockResetGroups.first().assignmentCount == 2,
               true);

    const QString waveCasePreviewInput =
        QStringLiteral("module wave_case;\n"
                       "logic [1:0] sel;\n"
                       "logic a;\n"
                       "logic b;\n"
                       "logic y;\n"
                       "always_comb begin\n"
                       "    case (sel)\n"
                       "    2'b00: y = a;\n"
                       "    default: y = b;\n"
                       "    endcase\n"
                       "end\n"
                       "endmodule\n");
    const WavePreviewReport waveCaseReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("wave_case.sv"), waveCasePreviewInput});
    const WavePreviewLane* yLane =
        waveLaneNamed(waveCaseReport, QStringLiteral("y"));
    expectBool("WavePreview case item guard label",
               yLane
                   && yLane->assignments.size() > 0
                   && yLane->assignments.first().guardText
                       == QStringLiteral("case sel: 2'b00"),
               true);
    expectBool("WavePreview default case guard label",
               yLane
                   && yLane->assignments.size() > 1
                   && yLane->assignments.at(1).guardText
                       == QStringLiteral("case sel: default"),
               true);
    expectBool("WavePreview case lane guard summary",
               yLane
                   && yLane->summary.guardTexts
                       == QStringList{QStringLiteral("case sel: 2'b00"),
                                      QStringLiteral("case sel: default")},
               true);

    const QString waveLoopPreviewInput =
        QStringLiteral("module wave_loop;\n"
                       "logic en;\n"
                       "logic [1:0] i;\n"
                       "logic [7:0] data [4];\n"
                       "logic [7:0] next;\n"
                       "logic [7:0] hold;\n"
                       "logic [7:0] q;\n"
                       "always_comb begin\n"
                       "    for (int j = 0; j < 4; j++) begin\n"
                       "        q = data[j];\n"
                       "    end\n"
                       "    foreach (data[i]) q = data[i];\n"
                       "    while (en) q = next;\n"
                       "    repeat (3) q = hold;\n"
                       "end\n"
                       "endmodule\n");
    const WavePreviewReport waveLoopReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("wave_loop.sv"), waveLoopPreviewInput});
    const WavePreviewLane* loopLane =
        waveLaneNamed(waveLoopReport, QStringLiteral("q"));
    expectBool("WavePreview loop guard labels",
               loopLane
                   && loopLane->assignments.size() == 4
                   && loopLane->assignments.at(0).guardText
                       == QStringLiteral("for int j = 0; j < 4; j++")
                   && loopLane->assignments.at(1).guardText
                       == QStringLiteral("foreach data[i]")
                   && loopLane->assignments.at(2).guardText
                       == QStringLiteral("while en")
                   && loopLane->assignments.at(3).guardText
                       == QStringLiteral("repeat 3"),
               true);
    expectBool("WavePreview loop lane guard summary",
               loopLane
                   && loopLane->summary.guardTexts.size() == 4
                   && loopLane->summary.guardTexts.first()
                       == QStringLiteral("for int j = 0; j < 4; j++")
                   && loopLane->summary.guardTexts.last()
                       == QStringLiteral("repeat 3"),
               true);

    const QString waveTernaryPreviewInput =
        QStringLiteral("module wave_ternary;\n"
                       "logic en;\n"
                       "logic sel;\n"
                       "logic a;\n"
                       "logic b;\n"
                       "logic out;\n"
                       "logic next;\n"
                       "assign out = sel ? a : b;\n"
                       "always_comb begin\n"
                       "    if (en) next = sel ? a : b;\n"
                       "end\n"
                       "endmodule\n");
    const WavePreviewReport waveTernaryReport =
        WavePreviewService::getInstance()->previewForDocument(
            {QStringLiteral("wave_ternary.sv"), waveTernaryPreviewInput});
    const WavePreviewAssignment* ternaryOutAssign =
        firstWaveAssignment(waveTernaryReport, QStringLiteral("out"));
    expectBool("WavePreview continuous ternary guard label",
               ternaryOutAssign
                   && ternaryOutAssign->guardText == QStringLiteral("?: sel"),
               true);
    const WavePreviewAssignment* ternaryNextAssign =
        firstWaveAssignment(waveTernaryReport, QStringLiteral("next"));
    expectBool("WavePreview combines if and ternary guards",
               ternaryNextAssign
                   && ternaryNextAssign->guardText
                       == QStringLiteral("if en && ?: sel"),
               true);
    const WavePreviewLane* ternaryNextLane =
        waveLaneNamed(waveTernaryReport, QStringLiteral("next"));
    expectBool("WavePreview ternary lane guard summary",
               ternaryNextLane
                   && ternaryNextLane->summary.guardTexts
                       == QStringList{QStringLiteral("if en && ?: sel")},
               true);

    const QString rstGenPath =
        QFileInfo(path).dir().absoluteFilePath(
            QStringLiteral("new/elec_phy_import/top/rst_gen.v"));
    QFile rstGenFile(rstGenPath);
    const bool rstGenOpened =
        rstGenFile.open(QIODevice::ReadOnly | QFile::Text);
    expectBool("WavePreview rst_gen fixture opens",
               rstGenOpened,
               true);
    QString rstGenInput;
    if (rstGenOpened) {
        rstGenInput = QTextStream(&rstGenFile).readAll();
        rstGenFile.close();
    }
    const WavePreviewReport rstGenReport =
        WavePreviewService::getInstance()->previewForDocument(
            {rstGenPath, rstGenInput});
    expectBool("WavePreview rst_gen full file shape",
               rstGenReport.available
                   && rstGenReport.blocks.size() == 2
                   && rstGenReport.assignmentCount == 6
                   && waveLaneNamed(rstGenReport, QStringLiteral("srst_o"))
                   && waveLaneNamed(rstGenReport, QStringLiteral("srst_n_o"))
                   && waveLaneNamed(rstGenReport, QStringLiteral("cnt"))
                   && waveLaneNamed(rstGenReport, QStringLiteral("srst")),
               true);
    const WavePreviewTraceSignal* rstCntTrace =
        waveTraceSignalNamed(rstGenReport, QStringLiteral("cnt"));
    const WavePreviewTraceSignal* rstSrstTrace =
        waveTraceSignalNamed(rstGenReport, QStringLiteral("srst"));
    const WavePreviewTraceSignal* rstSrstOTrace =
        waveTraceSignalNamed(rstGenReport, QStringLiteral("srst_o"));
    const WavePreviewTraceSignal* rstSrstNOTrace =
        waveTraceSignalNamed(rstGenReport, QStringLiteral("srst_n_o"));
    expectBool("WavePreview rst_gen local waveform trace",
               rstGenReport.trace.isValid()
                   && rstCntTrace
                   && rstCntTrace->values
                       == QStringList{QStringLiteral("1"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0")}
                   && rstSrstTrace
                   && rstSrstTrace->values
                       == QStringList{QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("0")}
                   && rstSrstOTrace
                   && rstSrstOTrace->values == rstSrstTrace->values
                   && rstSrstNOTrace
                   && rstSrstNOTrace->values
                       == QStringList{QStringLiteral("0"),
                                      QStringLiteral("0"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1")},
               true);

    const int firstRstAlways =
        rstGenInput.indexOf(QStringLiteral("always@"));
    const int secondRstAlways =
        firstRstAlways >= 0
            ? rstGenInput.indexOf(QStringLiteral("always@"), firstRstAlways + 1)
            : -1;
    const int rstEndmodule =
        secondRstAlways >= 0
            ? rstGenInput.indexOf(QStringLiteral("endmodule"), secondRstAlways)
            : -1;
    WavePreviewQuery rstSecondAlwaysQuery;
    rstSecondAlwaysQuery.fileName = rstGenPath;
    rstSecondAlwaysQuery.documentText = rstGenInput;
    rstSecondAlwaysQuery.scopeStartPosition = secondRstAlways;
    rstSecondAlwaysQuery.scopeEndPosition = rstEndmodule;
    rstSecondAlwaysQuery.scopeLabel = QStringLiteral("selected rst always");
    const WavePreviewReport rstSecondAlwaysReport =
        WavePreviewService::getInstance()->previewForDocument(
            rstSecondAlwaysQuery);
    const WavePreviewLane* rstSelectedSrstLane =
        waveLaneNamed(rstSecondAlwaysReport, QStringLiteral("srst"));
    expectBool("WavePreview rst_gen selected always scope",
               rstSecondAlwaysReport.scoped
                   && rstSecondAlwaysReport.scopeLabel
                       == QStringLiteral("selected rst always")
                   && rstSecondAlwaysReport.available
                   && rstSecondAlwaysReport.blocks.size() == 1
                   && rstSecondAlwaysReport.assignmentCount == 2
                   && rstSelectedSrstLane
                   && rstSelectedSrstLane->assignments.size() == 2
                   && rstSelectedSrstLane->summary.sequentialEventCount == 2
                   && !waveLaneNamed(rstSecondAlwaysReport,
                                     QStringLiteral("cnt"))
                   && !waveLaneNamed(rstSecondAlwaysReport,
                                     QStringLiteral("srst_o"))
                   && !waveLaneNamed(rstSecondAlwaysReport,
                                     QStringLiteral("srst_n_o")),
               true);
    const WavePreviewTraceSignal* rstSelectedTrace =
        waveTraceSignalNamed(rstSecondAlwaysReport, QStringLiteral("srst"));
    expectBool("WavePreview rst_gen selected always waveform",
               rstSecondAlwaysReport.trace.isValid()
                   && rstSelectedTrace
                   && rstSelectedTrace->values
                       == QStringList{QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1"),
                                      QStringLiteral("1")},
               true);

    const QString newProjectRoot =
        QFileInfo(path).dir().absoluteFilePath(QStringLiteral("new"));
    QDirIterator waveProjectFiles(
        newProjectRoot,
        QStringList{QStringLiteral("*.v"),
                    QStringLiteral("*.sv"),
                    QStringLiteral("*.svh")},
        QDir::Files,
        QDirIterator::Subdirectories);
    int waveProjectFileCount = 0;
    int waveProjectAvailableReports = 0;
    int waveProjectAssignments = 0;
    int waveProjectBlocks = 0;
    int waveProjectLanes = 0;
    QStringList waveProjectOpenFailures;
    while (waveProjectFiles.hasNext()) {
        const QString projectFilePath = waveProjectFiles.next();
        QFile projectFile(projectFilePath);
        if (!projectFile.open(QIODevice::ReadOnly | QFile::Text)) {
            waveProjectOpenFailures.append(projectFilePath);
            continue;
        }
        const QString projectText = QTextStream(&projectFile).readAll();
        projectFile.close();
        ++waveProjectFileCount;

        const WavePreviewReport projectReport =
            WavePreviewService::getInstance()->previewForDocument(
                {projectFilePath, projectText});
        if (projectReport.available)
            ++waveProjectAvailableReports;
        waveProjectAssignments += projectReport.assignmentCount;
        waveProjectBlocks += projectReport.blocks.size();
        waveProjectLanes += projectReport.lanes.size();
    }
    expectBool("WavePreview scans full test_sv/new project",
               waveProjectOpenFailures.isEmpty()
                   && waveProjectFileCount >= 25
                   && waveProjectAvailableReports >= 10
                   && waveProjectAssignments >= 50
                   && waveProjectBlocks >= 20
                   && waveProjectLanes >= 20,
               true);

    const QString planRoot =
        QDir::current().absoluteFilePath(QStringLiteral("test_sv/huge_plan"));
    const QString planA = QDir(planRoot).absoluteFilePath(QStringLiteral("a.sv"));
    const QString planB = QDir(planRoot).absoluteFilePath(QStringLiteral("b.sv"));
    const QString planC = QDir(planRoot).absoluteFilePath(QStringLiteral("c.sv"));
    const QString planD = QDir(planRoot).absoluteFilePath(QStringLiteral("d.sv"));
    ProjectSnapshot planProject;
    planProject.workspaceRoot = planRoot;
    planProject.systemVerilogFiles = {planA, planB, planC, planD};

    DocumentSnapshot dirtyOpen;
    dirtyOpen.fileName = planB;
    dirtyOpen.dirty = true;
    DocumentSnapshot cleanOpen;
    cleanOpen.fileName = planD;
    cleanOpen.dirty = false;

    WorkspaceAnalysisPlan priorityPlan =
        WorkspaceAnalysisPlanService::getInstance()->planForWorkspace(
            {planProject, QDir::toNativeSeparators(planC), {dirtyOpen, cleanOpen}});
    expectEq("Workspace plan prioritizes active/open files",
             priorityPlan.project.systemVerilogFiles.join(QStringLiteral("|")),
             QStringList{planC, planB, planD, planA}.join(QStringLiteral("|")));
    expectEq("Workspace plan protects dirty files",
             priorityPlan.protectedFiles.join(QStringLiteral("|")),
             QStringList{planB}.join(QStringLiteral("|")));
    expectBool("Workspace plan counts priority files",
               priorityPlan.priorityFileCount == 3
                   && priorityPlan.backgroundFileCount == 1
                   && priorityPlan.openFiles.size() == 2
                   && priorityPlan.protectedFiles.size() == 1
                   && priorityPlan.currentFileInWorkspace,
               true);
    expectEq("Workspace plan records priority bands",
             QStringList{
                 priorityPlan.currentFilePriorityFiles.join(QStringLiteral(",")),
                 priorityPlan.dirtyOpenPriorityFiles.join(QStringLiteral(",")),
                 priorityPlan.cleanOpenPriorityFiles.join(QStringLiteral(",")),
                 priorityPlan.backgroundFiles.join(QStringLiteral(","))
             }.join(QStringLiteral("|")),
             QStringList{planC, planB, planD, planA}.join(QStringLiteral("|")));
    expectEq("Workspace plan indexes file bands",
             QStringList{
                 priorityPlan.bandForFile(QDir::toNativeSeparators(planC)),
                 priorityPlan.bandForFile(planB),
                 priorityPlan.bandForFile(planD),
                 priorityPlan.bandForFile(planA)
             }.join(QStringLiteral("|")),
             QStringLiteral("current|dirty-open|open|background"));
    expectBool("Workspace plan band index covers workspace files",
               priorityPlan.fileBandsByNormalizedPath.size() == 4,
               true);
    QStringList priorityCheckpointText;
    for (int checkpoint : priorityPlan.priorityPublicationCheckpoints)
        priorityCheckpointText.append(QString::number(checkpoint));
    expectEq("Workspace plan records publication checkpoints",
             priorityCheckpointText.join(QStringLiteral(",")),
             QStringLiteral("1,2,3"));
    QStringList bandSummaryText;
    for (const WorkspaceAnalysisBandSummary& summary :
         priorityPlan.bandSummaries) {
        bandSummaryText.append(
            QStringLiteral("%1:%2:%3:%4:%5")
                .arg(summary.label,
                     summary.displayName,
                     QString::number(summary.fileCount),
                     summary.priority ? QStringLiteral("priority")
                                      : QStringLiteral("background"),
                     QString::number(summary.publicationCheckpoint)));
    }
    expectEq("Workspace plan records band summaries",
             bandSummaryText.join(QStringLiteral("|")),
             QStringLiteral("current:current:1:priority:1|"
                            "dirty-open:dirty:1:priority:2|"
                            "open:open:1:priority:3|"
                            "background:background:1:background:0"));
    expectEq("Workspace plan formats band summary text",
             priorityPlan.bandSummaryText(),
             QStringLiteral("bands current 1, dirty 1, open 1, background 1"));
    QStringList bandMetadataText;
    for (const QString& fileName : {planC, planB, planD, planA}) {
        const WorkspaceAnalysisFileBandMetadata metadata =
            priorityPlan.bandMetadataForFile(fileName);
        bandMetadataText.append(
            QStringLiteral("%1:%2:%3:%4")
                .arg(metadata.label,
                     metadata.displayName,
                     metadata.priority ? QStringLiteral("priority")
                                       : QStringLiteral("background"),
                     QString::number(metadata.publicationCheckpoint)));
    }
    expectEq("Workspace plan exposes file band metadata",
             bandMetadataText.join(QStringLiteral("|")),
             QStringLiteral("current:current:priority:1|"
                            "dirty-open:dirty:priority:2|"
                            "open:open:priority:3|"
                            "background:background:background:0"));
    QHash<QString, SemanticAnalysisBandMetadata> semanticBandMetadata;
    for (auto it = priorityPlan.fileBandMetadataByNormalizedPath.constBegin();
         it != priorityPlan.fileBandMetadataByNormalizedPath.constEnd();
         ++it) {
        const WorkspaceAnalysisFileBandMetadata planMetadata = it.value();
        SemanticAnalysisBandMetadata metadata;
        metadata.label = planMetadata.label;
        metadata.displayName = planMetadata.displayName;
        metadata.priority = planMetadata.priority;
        metadata.publicationCheckpoint = planMetadata.publicationCheckpoint;
        semanticBandMetadata.insert(it.key(), metadata);
    }
    SemanticIndex tierIndex;
    tierIndex.setWorkspaceFileAnalysisBands(semanticBandMetadata);
    SemanticSymbolRecord tierRecord;
    tierRecord.name = QStringLiteral("tier_sig");
    tierRecord.location.fileName = planC;
    tierRecord.location.startLine = 1;
    tierRecord.declarationKind = SymbolTaxonomy::DeclarationKind::Signal;
    SemanticSymbolRecord currentTierQueryRecord;
    currentTierQueryRecord.name = QStringLiteral("zz_tier_match");
    currentTierQueryRecord.location.fileName = planC;
    currentTierQueryRecord.location.startLine = 2;
    currentTierQueryRecord.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    SemanticSymbolRecord currentTierDuplicateRecord;
    currentTierDuplicateRecord.name = QStringLiteral("shared_tier_symbol");
    currentTierDuplicateRecord.location.fileName = planC;
    currentTierDuplicateRecord.location.startLine = 3;
    currentTierDuplicateRecord.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    const SemanticSymbolRecord currentTierModuleRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("zz_tier_module"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(planC)
            .withLine(4)
            .record();
    tierIndex.updateSymbolRecordsForFile(
        planC,
        {tierRecord,
         currentTierQueryRecord,
         currentTierDuplicateRecord,
         currentTierModuleRecord},
        QStringLiteral("module tier_top; logic tier_sig; logic zz_tier_match; logic shared_tier_symbol; endmodule\nmodule zz_tier_module; endmodule\n"));
    SemanticSymbolRecord backgroundTierQueryRecord;
    backgroundTierQueryRecord.name = QStringLiteral("aa_tier_match");
    backgroundTierQueryRecord.location.fileName = planA;
    backgroundTierQueryRecord.location.startLine = 1;
    backgroundTierQueryRecord.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    SemanticSymbolRecord backgroundTierDuplicateRecord;
    backgroundTierDuplicateRecord.name = QStringLiteral("shared_tier_symbol");
    backgroundTierDuplicateRecord.location.fileName = planA;
    backgroundTierDuplicateRecord.location.startLine = 2;
    backgroundTierDuplicateRecord.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    const SemanticSymbolRecord backgroundTierModuleRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("aa_tier_module"),
            SymbolTaxonomy::DeclarationKind::Module)
            .withFile(planA)
            .withLine(3)
            .record();
    tierIndex.updateSymbolRecordsForFile(
        planA,
        {backgroundTierQueryRecord,
         backgroundTierDuplicateRecord,
         backgroundTierModuleRecord},
        QStringLiteral("module tier_bg; logic aa_tier_match; logic shared_tier_symbol; endmodule\nmodule aa_tier_module; endmodule\n"));
    const QList<SemanticSymbolRecord> tierRecords =
        tierIndex.getSymbolRecords(QDir::toNativeSeparators(planC));
    expectBool("SemanticIndex records carry analysis band metadata",
               !tierRecords.isEmpty()
                   && tierRecords.first().analysisBand.label
                       == QStringLiteral("current")
                   && tierRecords.first().analysisBand.priority
                   && tierRecords.first().analysisBand.publicationCheckpoint == 1,
               true);
    const std::shared_ptr<const SemanticIndexSnapshot> tierSnapshot =
        tierIndex.captureSnapshotPreservingDiagnostics();
    const QList<SemanticSymbolRecord> tierSnapshotRecords =
        tierSnapshot->getSymbolRecords(planC);
    expectBool("Semantic snapshot preserves analysis band metadata",
               !tierSnapshotRecords.isEmpty()
                   && tierSnapshotRecords.first().analysisBand.label
                       == QStringLiteral("current")
                   && tierSnapshotRecords.first().analysisBand.displayName
                       == QStringLiteral("current"),
               true);
    const SemanticAnalysisBandReport tierIndexBandReport =
        tierIndex.analysisBandReport();
    expectEq("SemanticIndex reports analysis band summary",
             tierIndexBandReport.summaryText(),
             QStringLiteral("bands current 4 symbols/1 file, background 3 symbols/1 file"));
    expectBool("SemanticIndex report exposes band provenance",
               tierIndexBandReport.totalSymbolCount == 7
                   && tierIndexBandReport.totalFileCount == 2
                   && tierIndexBandReport.bands.size() == 2
                   && tierIndexBandReport.bands.first().label
                       == QStringLiteral("current")
                   && tierIndexBandReport.bands.first().priority
                   && tierIndexBandReport.bands.first().publicationCheckpoint == 1
                   && tierIndexBandReport.bands.first().files.contains(planC),
               true);
    const SemanticAnalysisBandReport tierSnapshotBandReport =
        tierSnapshot->analysisBandReport();
    expectBool("Semantic snapshot reports analysis bands",
               tierSnapshotBandReport.summaryText()
                       == tierIndexBandReport.summaryText()
                   && tierSnapshotBandReport.bands.size()
                       == tierIndexBandReport.bands.size(),
               true);
    CompletionService tierCompletionService(&tierIndex);
    CompletionQuery tierCompletionQuery;
    tierCompletionQuery.prefix = QStringLiteral("zz_tier");
    const CompletionResult tierCompletionResult =
        tierCompletionService.findCompletionResult(tierCompletionQuery);
    expectBool("Completion result exposes analysis band provenance",
               tierCompletionResult.items.size() == 1
                   && tierCompletionResult.items.first().label
                       == QStringLiteral("zz_tier_module")
                   && tierCompletionResult.items.first()
                          .analysisBandDisplayName == QStringLiteral("current")
                   && tierCompletionResult.items.first().analysisBand.label
                       == QStringLiteral("current"),
               true);
    CompletionModel tierCompletionModel;
    tierCompletionModel.updateCompletions(tierCompletionResult,
                                          QStringLiteral("zz_tier"));
    expectBool("CompletionModel exposes analysis band provenance",
               tierCompletionModel.rowCount() == 1
                   && tierCompletionModel.getItem(
                          tierCompletionModel.index(0, 0))
                          .analysisBandDisplayName == QStringLiteral("current")
                   && tierCompletionModel.data(
                          tierCompletionModel.index(0, 0),
                          Qt::ToolTipRole).toString().contains(
                              QStringLiteral("band: current")),
               true);
    const CommandSymbolCompletionItem tierCommandItem =
        tierCompletionService.commandSymbolCompletionItem(
            tierCompletionResult.items.isEmpty()
                ? SemanticSymbolRecord()
                : tierCompletionResult.items.first().symbolRecord,
            CompletionCommandKind::Module,
            QStringLiteral("zz"));
    expectBool("Command symbol item exposes analysis band provenance",
               tierCommandItem.analysisBandDisplayName
                       == QStringLiteral("current")
                   && tierCommandItem.analysisBand.label
                       == QStringLiteral("current"),
               true);
    const QList<SemanticSymbolSearchResult> tierSearchResults =
        tierIndex.searchSymbols({QStringLiteral("tier_match"),
                                 QString(),
                                 {SymbolTaxonomy::DeclarationKind::Signal},
                                 SymbolTaxonomy::SymbolSearchIntent::Any,
                                 false,
                                 false,
                                 -1});
    expectBool("Semantic search prefers current analysis band",
               tierSearchResults.size() >= 2
                   && tierSearchResults.first().symbolRecord.name
                       == QStringLiteral("zz_tier_match"),
               true);
    const QStringList tierCompletionNames =
        tierIndex.getCompletionSymbolNames();
    expectBool("Completion names prefer priority analysis bands",
               tierCompletionNames.indexOf(QStringLiteral("zz_tier_match"))
                   >= 0
                   && tierCompletionNames.indexOf(QStringLiteral("aa_tier_match"))
                   >= 0
                   && tierCompletionNames.indexOf(
                          QStringLiteral("zz_tier_match"))
                       < tierCompletionNames.indexOf(
                           QStringLiteral("aa_tier_match")),
               true);
    const QList<SemanticSymbolRecord> tierDefinitionRecords =
        tierIndex.findDefinitionRecords(QStringLiteral("shared_tier_symbol"));
    expectBool("Definition records prefer priority analysis bands",
               tierDefinitionRecords.size() >= 2
                   && tierDefinitionRecords.first().location.fileName == planC,
               true);

    WorkspaceAnalysisPlan externalCurrentPlan =
        WorkspaceAnalysisPlanService::getInstance()->planForWorkspace(
            {planProject,
             QDir(planRoot).absoluteFilePath(QStringLiteral("external.sv")),
             {dirtyOpen}});
    expectEq("Workspace plan ignores external current file",
             externalCurrentPlan.project.systemVerilogFiles.join(QStringLiteral("|")),
             QStringList{planB, planA, planC, planD}.join(QStringLiteral("|")));
    expectBool("Workspace plan reports external current file",
               !externalCurrentPlan.currentFileInWorkspace
                   && externalCurrentPlan.priorityFileCount == 1
                   && externalCurrentPlan.backgroundFileCount == 3,
               true);
    expectBool("Workspace plan bands omit external current file",
               externalCurrentPlan.currentFilePriorityFiles.isEmpty()
                   && externalCurrentPlan.dirtyOpenPriorityFiles
                       == QStringList{planB}
                   && externalCurrentPlan.cleanOpenPriorityFiles.isEmpty()
                   && externalCurrentPlan.backgroundFiles
                       == QStringList{planA, planC, planD},
               true);
    expectBool("Workspace plan band index omits external current file",
               externalCurrentPlan.bandForFile(
                   QDir(planRoot).absoluteFilePath(
                       QStringLiteral("external.sv"))).isEmpty()
                   && externalCurrentPlan.bandForFile(planB)
                       == QStringLiteral("dirty-open")
                   && externalCurrentPlan.bandForFile(planA)
                       == QStringLiteral("background"),
               true);
    expectEq("Workspace plan summarizes external current bands",
             externalCurrentPlan.bandSummaryText(),
             QStringLiteral("bands current 0, dirty 1, open 0, background 3"));

    QTemporaryDir foregroundWorkspace;
    expectBool("Workspace foreground temp dir valid",
               foregroundWorkspace.isValid(),
               true);
    const QString foregroundFile =
        QDir(foregroundWorkspace.path()).absoluteFilePath(
            QStringLiteral("foreground.sv"));
    QFile foregroundDisk(foregroundFile);
    expectBool("Workspace foreground disk file created",
               foregroundDisk.open(QIODevice::WriteOnly | QIODevice::Text),
               true);
    if (foregroundDisk.isOpen()) {
        QTextStream out(&foregroundDisk);
        out << "module disk_version; logic disk_sig; endmodule\n";
        foregroundDisk.close();
    }

    MyCodeEditor foregroundEditor;
    foregroundEditor.setPlainText(
        QStringLiteral("module open_tabs_first; logic live_sig; endmodule\n"));
    DocumentModel foregroundDocuments;
    foregroundDocuments.registerEditor(&foregroundEditor, foregroundFile);

    AnalysisScheduler foregroundScheduler;
    SymbolAnalyzer foregroundAnalyzer;
    foregroundScheduler.setDocumentModel(&foregroundDocuments);
    foregroundScheduler.setSymbolAnalyzer(&foregroundAnalyzer);
    foregroundScheduler.setWorkspaceOpenProvider([]() { return true; });
    foregroundScheduler.setCurrentFileProvider(
        [foregroundFile]() { return foregroundFile; });

    QStringList foregroundStartedOrder;
    QObject::connect(&foregroundAnalyzer,
                     &SymbolAnalyzer::analysisStarted,
                     &foregroundAnalyzer,
                     [&foregroundStartedOrder](const QString& fileName) {
                         foregroundStartedOrder.append(fileName);
                     });
    bool sawForegroundPlan = false;
    WorkspaceAnalysisPlan foregroundPlan;
    QObject::connect(&foregroundScheduler,
                     &AnalysisScheduler::workspaceAnalysisPlanPrepared,
                     &foregroundScheduler,
                     [&sawForegroundPlan, &foregroundPlan](
                         const WorkspaceAnalysisPlan& plan) {
                         sawForegroundPlan = true;
                         foregroundPlan = plan;
                     });

    ProjectSnapshot foregroundProject;
    foregroundProject.workspaceRoot = foregroundWorkspace.path();
    foregroundProject.systemVerilogFiles = {foregroundFile};
    foregroundProject.includeDirs = {foregroundWorkspace.path()};
    foregroundScheduler.requestWorkspaceAnalysis(foregroundProject);
    expectBool("Workspace foreground analysis avoids duplicate open-doc refresh",
               foregroundStartedOrder.size() == 1
                   && foregroundStartedOrder.first()
                       == foregroundProject.workspaceRoot,
               true);
    expectBool("Workspace plan signal reports foreground priority",
               sawForegroundPlan
                   && foregroundPlan.priorityFileCount == 1
                   && foregroundPlan.backgroundFileCount == 0
                   && foregroundPlan.currentFilePriorityFiles
                       == QStringList{foregroundFile}
                   && foregroundPlan.backgroundFiles.isEmpty()
                   && foregroundPlan.currentFileInWorkspace,
               true);
    foregroundAnalyzer.cancelWorkspaceAnalysisAndInvalidate();

    WorkspaceSymbolAnalysisController cachedWorkspaceController;
    SymbolAnalyzer cachedWorkspaceAnalyzer;
    cachedWorkspaceController.setSymbolAnalyzer(&cachedWorkspaceAnalyzer);
    int cachedWorkspaceStarts = 0;
    int cachedRelationshipCancels = 0;
    QObject::connect(&cachedWorkspaceController,
                     &WorkspaceSymbolAnalysisController::workspaceSymbolAnalysisStarted,
                     &cachedWorkspaceController,
                     [&cachedWorkspaceStarts](const ProjectSnapshot&, int) {
                         ++cachedWorkspaceStarts;
                     });
    QObject::connect(&cachedWorkspaceController,
                     &WorkspaceSymbolAnalysisController::workspaceRelationshipAnalysisCancelRequested,
                     &cachedWorkspaceController,
                     [&cachedRelationshipCancels]() {
                         ++cachedRelationshipCancels;
                     });
    cachedWorkspaceController.requestWorkspaceAnalysis(foregroundProject);
    cachedWorkspaceAnalyzer.batchAnalysisCompleted(1, 1);
    cachedWorkspaceController.requestWorkspaceAnalysis(foregroundProject);
    expectBool("Workspace cached activation skips completed symbol analysis",
               cachedWorkspaceStarts == 1,
               true);
    expectBool("Workspace cached activation cancels stale relationship analysis",
               cachedRelationshipCancels == 1,
               true);
    cachedWorkspaceController.cancelWorkspaceAnalysis();
    cachedWorkspaceAnalyzer.cancelWorkspaceAnalysisAndInvalidate();

    SmartRelationshipBuilder foregroundRelationshipBuilder(nullptr, nullptr);
    foregroundScheduler.setRelationshipBuilder(&foregroundRelationshipBuilder);
    QStringList foregroundRelationshipOrder;
    QObject::connect(&foregroundAnalyzer,
                     &SymbolAnalyzer::analysisStarted,
                     &foregroundAnalyzer,
                     [&foregroundRelationshipOrder](const QString& fileName) {
                         if (fileName == QStringLiteral("open_tabs"))
                             foregroundRelationshipOrder.append(fileName);
                     });
    QObject::connect(&foregroundScheduler,
                     &AnalysisScheduler::workspaceRelationshipAnalysisStarted,
                     &foregroundScheduler,
                     [&foregroundRelationshipOrder](const ProjectSnapshot&, int) {
                         foregroundRelationshipOrder.append(
                             QStringLiteral("workspace_relationship"));
                     });
    foregroundScheduler.requestWorkspaceRelationshipAnalysis(foregroundProject);
    expectBool("Workspace relationship foreground refresh starts open docs first",
               foregroundRelationshipOrder.size() >= 2
                   && foregroundRelationshipOrder.first()
                       == QStringLiteral("open_tabs")
                   && foregroundRelationshipOrder.at(1)
                       == QStringLiteral("workspace_relationship"),
               true);
    foregroundScheduler.cancelWorkspaceRelationshipAnalysis();

    WorkspaceAnalysisRequestQueue requestQueue;
    ProjectSnapshot queueFirst = planProject;
    queueFirst.workspaceRoot = QDir(planRoot).absoluteFilePath(
        QStringLiteral("first"));
    ProjectSnapshot queueSecond = planProject;
    queueSecond.workspaceRoot = QDir(planRoot).absoluteFilePath(
        QStringLiteral("second"));
    ProjectSnapshot queueThird = planProject;
    queueThird.workspaceRoot = QDir(planRoot).absoluteFilePath(
        QStringLiteral("third"));
    requestQueue.start(queueFirst);
    expectBool("Workspace request queue starts active",
               requestQueue.active() && !requestQueue.hasPending(),
               true);
    const WorkspaceAnalysisRequestTelemetry activeTelemetry =
        requestQueue.telemetry();
    expectBool("Workspace request telemetry tracks active age",
               activeTelemetry.active
                   && !activeTelemetry.pending
                   && activeTelemetry.activeAgeMs >= 0
                   && activeTelemetry.pendingAgeMs < 0,
               true);
    expectBool("Workspace request queue accepts pending",
               requestQueue.queueLatest(queueSecond)
                   && requestQueue.queueLatest(queueThird)
                   && requestQueue.hasPending(),
               true);
    const WorkspaceAnalysisRequestTelemetry pendingTelemetry =
        requestQueue.telemetry();
    expectBool("Workspace request telemetry tracks pending pressure",
               pendingTelemetry.active
                   && pendingTelemetry.pending
                   && pendingTelemetry.activeAgeMs >= 0
                   && pendingTelemetry.pendingAgeMs >= 0
                   && pendingTelemetry.pendingUpdateCount == 2,
               true);
    ProjectSnapshot queuedNext;
    expectBool("Workspace request queue returns latest pending",
               requestQueue.finishAndTakePending(&queuedNext)
                   && queuedNext.workspaceRoot == queueThird.workspaceRoot
                   && !requestQueue.active()
                   && !requestQueue.hasPending(),
               true);
    const WorkspaceAnalysisRequestTelemetry finishedTelemetry =
        requestQueue.telemetry();
    expectBool("Workspace request telemetry records completed pressure",
               !finishedTelemetry.active
                   && !finishedTelemetry.pending
                   && finishedTelemetry.lastFinishedActiveAgeMs >= 0
                   && finishedTelemetry.lastTakenPendingAgeMs >= 0
                   && finishedTelemetry.lastTakenPendingUpdateCount == 2,
               true);
    requestQueue.start(queuedNext);
    requestQueue.clear();
    expectBool("Workspace request queue clears state",
               !requestQueue.active() && !requestQueue.hasPending(),
               true);
    const WorkspaceAnalysisRequestTelemetry clearedTelemetry =
        requestQueue.telemetry();
    expectBool("Workspace request telemetry clears state",
               !clearedTelemetry.active
                   && !clearedTelemetry.pending
                   && clearedTelemetry.activeAgeMs < 0
                   && clearedTelemetry.pendingAgeMs < 0
                   && clearedTelemetry.lastFinishedActiveAgeMs < 0
                   && clearedTelemetry.lastTakenPendingAgeMs < 0
                   && clearedTelemetry.lastTakenPendingUpdateCount == 0,
               true);
    requestQueue.start(queueFirst);
    requestQueue.queueLatest(queueSecond);
    requestQueue.queueLatest(queueThird);
    const WorkspaceAnalysisRequestTelemetry cancelTelemetry =
        requestQueue.cancel();
    expectBool("Workspace request queue cancels active and pending",
               !requestQueue.active()
                   && !requestQueue.hasPending()
                   && !cancelTelemetry.active
                   && !cancelTelemetry.pending,
               true);
    expectBool("Workspace request cancel keeps discarded telemetry",
               cancelTelemetry.lastFinishedActiveAgeMs >= 0
                   && cancelTelemetry.lastTakenPendingAgeMs >= 0
                   && cancelTelemetry.lastTakenPendingUpdateCount == 2,
               true);

    DiagnosticsRefreshController diagnosticsRefresh;
    QStringList emittedDiagnosticsRefreshes;
    QObject::connect(&diagnosticsRefresh,
                     &DiagnosticsRefreshController::diagnosticsRefreshRequested,
                     &diagnosticsRefresh,
                     [&emittedDiagnosticsRefreshes](const QString& fileName) {
                         emittedDiagnosticsRefreshes.append(
                             fileName.isEmpty()
                                 ? QStringLiteral("<all>")
                                 : fileName);
                     });
    diagnosticsRefresh.requestRefresh(QStringLiteral("first.sv"));
    diagnosticsRefresh.requestRefresh(QString());
    diagnosticsRefresh.requestRefresh(QStringLiteral("second.sv"));
    expectBool("Diagnostics refresh debounce emits coalesced request",
               waitForEventPredicate(
                   [&emittedDiagnosticsRefreshes]() {
                       return !emittedDiagnosticsRefreshes.isEmpty();
                   },
                   1000),
               true);
    expectEq("Diagnostics refresh preserves full scope",
             emittedDiagnosticsRefreshes.join(QStringLiteral("|")),
             QStringLiteral("<all>"));
    emittedDiagnosticsRefreshes.clear();
    diagnosticsRefresh.requestRefresh(QStringLiteral("second.sv"));
    expectBool("Diagnostics refresh emits file request",
               waitForEventPredicate(
                   [&emittedDiagnosticsRefreshes]() {
                       return !emittedDiagnosticsRefreshes.isEmpty();
                   },
                   1000),
               true);
    expectEq("Diagnostics refresh keeps file scope",
             emittedDiagnosticsRefreshes.join(QStringLiteral("|")),
             QStringLiteral("second.sv"));

    expectEq("SymbolTaxonomy modport label",
             SymbolTaxonomy::symbolTypeLabel(
                 semanticFixtureMetadata(
                     SymbolTaxonomy::DeclarationKind::Modport,
                     SymbolTaxonomy::SymbolOwnerScope::Interface,
                     SymbolTaxonomy::CollectorKind::InterfaceModport)),
             QStringLiteral("modport"));
    expectBool("SymbolTaxonomy struct variable metadata",
               semanticFixtureMetadata(
                   SymbolTaxonomy::DeclarationKind::StructVariable)
                   .declarationKind
                   == SymbolTaxonomy::DeclarationKind::StructVariable,
               true);
    expectBool("SymbolTaxonomy direct context metadata",
               semanticFixtureMetadata(
                   SymbolTaxonomy::DeclarationKind::StructVariable)
                   .declarationKind
                   == SymbolTaxonomy::DeclarationKind::StructVariable,
               true);
    SymbolTaxonomy::SemanticMetadata enumTypedefMetadata;
    enumTypedefMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Typedef;
    expectBool("SymbolTaxonomy enum typedef semantic completion",
               SymbolTaxonomy::semanticCompletionKindMatches(
                   enumTypedefMetadata,
                   SymbolTaxonomy::SemanticCompletionKind::EnumType,
                   QStringLiteral("enum")),
               true);
    const SymbolTaxonomy::SemanticMetadata metadataSignal =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Signal,
                                SymbolTaxonomy::SymbolOwnerScope::Module);
    expectBool("SymbolTaxonomy metadata command type",
               SymbolTaxonomy::semanticCompletionKindMatches(
                   metadataSignal,
                   SymbolTaxonomy::SemanticCompletionKind::Logic),
               true);
    expectBool("SymbolTaxonomy metadata typed completion",
               SymbolTaxonomy::semanticCompletionKindMatches(
                   metadataSignal,
                   SymbolTaxonomy::SemanticCompletionKind::Logic),
               true);
    expectBool("SymbolTaxonomy metadata signal definition candidate",
               SymbolTaxonomy::isDefinitionCandidate(metadataSignal),
               true);
    expectBool("SymbolTaxonomy metadata signal definition priority",
               SymbolTaxonomy::definitionPriority(metadataSignal) == 5,
               true);
    const SymbolTaxonomy::SemanticMetadata moduleMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Module,
                                SymbolTaxonomy::SymbolOwnerScope::Global,
                                SymbolTaxonomy::CollectorKind::Module);
    expectBool("SymbolTaxonomy metadata global completion",
               SymbolTaxonomy::isGlobalCompletionCandidate(moduleMetadata),
               true);
    const SymbolTaxonomy::SemanticMetadata logicMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Signal,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::Logic);
    expectBool("SymbolTaxonomy metadata internal completion",
               SymbolTaxonomy::isInternalCompletionCandidate(logicMetadata),
               true);
    expectBool("SymbolTaxonomy metadata raw compatibility",
               logicMetadata.collectorKind
                   == SymbolTaxonomy::CollectorKind::Logic,
               true);
    expectBool("SymbolTaxonomy metadata global completion type",
               SymbolTaxonomy::isGlobalCompletionCandidate(metadataSignal),
               false);
    const SymbolTaxonomy::SemanticMetadata metadataGlobal =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Module,
                                SymbolTaxonomy::SymbolOwnerScope::Global);
    expectBool("SymbolTaxonomy metadata global completion candidate",
               SymbolTaxonomy::isGlobalCompletionCandidate(metadataGlobal),
               true);
    expectBool("SymbolTaxonomy metadata command global type",
               SymbolTaxonomy::isCommandGlobalCompletionType(metadataGlobal),
               true);
    expectBool("SymbolTaxonomy metadata global info type",
               SymbolTaxonomy::isGlobalSemanticSymbolType(metadataGlobal),
               true);
    SymbolTaxonomy::SemanticMetadata syntheticModuleMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Module,
                                SymbolTaxonomy::SymbolOwnerScope::Global);
    expectBool("SymbolTaxonomy search intent uses semantic metadata",
               SymbolTaxonomy::matchesSearchIntent(
                   syntheticModuleMetadata,
                   SymbolTaxonomy::SymbolSearchIntent::ModuleDeclarations),
               true);
    expectBool("SymbolTaxonomy global definition uses semantic metadata",
               SymbolTaxonomy::isGlobalDefinition(syntheticModuleMetadata),
               true);
    expectBool("SymbolTaxonomy definition candidate uses metadata",
               SymbolTaxonomy::isDefinitionCandidate(syntheticModuleMetadata),
               true);
    expectBool("SymbolTaxonomy definition priority uses metadata",
               SymbolTaxonomy::definitionPriority(syntheticModuleMetadata) == 0,
               true);
    expectBool("SymbolTaxonomy outline uses semantic metadata",
               SymbolTaxonomy::isOutlineSymbol(syntheticModuleMetadata),
               true);
    expectBool("SymbolTaxonomy requested type uses metadata",
               SymbolTaxonomy::isModuleDeclaration(syntheticModuleMetadata),
               true);
    expectEq("SymbolTaxonomy metadata label",
             SymbolTaxonomy::symbolTypeLabel(syntheticModuleMetadata),
             QStringLiteral("module"));
    SymbolTaxonomy::SemanticMetadata syntheticInstanceMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Instance);
    expectBool("SymbolTaxonomy metadata instance declaration",
               SymbolTaxonomy::isInstanceDeclaration(syntheticInstanceMetadata),
               true);
    syntheticInstanceMetadata.usageRole =
        SymbolTaxonomy::SymbolUsageRole::Reference;
    expectBool("SymbolTaxonomy metadata instance pin not declaration",
               SymbolTaxonomy::isInstanceDeclaration(syntheticInstanceMetadata),
               false);
    const SymbolTaxonomy::SemanticMetadata metadataInstanceSymbolMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Instance);
    expectBool("SymbolTaxonomy metadata instance declaration",
               SymbolTaxonomy::isInstanceDeclaration(
                   metadataInstanceSymbolMetadata),
               true);
    const SymbolStableKey metadataKey =
        semanticFixtureStableKey(QString(),
                                 QStringLiteral("metadata_top"),
                                 SymbolTaxonomy::DeclarationKind::Module);
    expectBool("stable key uses semantic declaration kind",
               metadataKey.declarationKind
                   == SymbolTaxonomy::DeclarationKind::Module,
               true);
    SymbolTaxonomy::SemanticMetadata syntheticPortMetadata;
    syntheticPortMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Port;
    syntheticPortMetadata.usageRole =
        SymbolTaxonomy::SymbolUsageRole::Declaration;
    expectBool("SymbolTaxonomy metadata declaration group",
               SymbolTaxonomy::declarationGroup(syntheticPortMetadata)
                   == SymbolTaxonomy::DeclarationGroup::Port,
               true);
    expectBool("SymbolTaxonomy metadata port declaration",
               SymbolTaxonomy::isPortDeclaration(syntheticPortMetadata),
               true);
    expectBool("SymbolTaxonomy metadata port connection peer",
               SymbolTaxonomy::isPortConnectionPeer(syntheticPortMetadata),
               true);
    SymbolTaxonomy::SemanticMetadata syntheticSignalMetadata;
    syntheticSignalMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Signal;
    expectBool("SymbolTaxonomy metadata signal declaration",
               SymbolTaxonomy::isSignalDeclaration(syntheticSignalMetadata),
               true);
    expectBool("SymbolTaxonomy metadata logic declaration",
               SymbolTaxonomy::isLogicDeclaration(logicMetadata),
               true);
    const SymbolTaxonomy::SemanticMetadata stateRegisterMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Signal,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::Logic);
    expectBool("SymbolTaxonomy metadata fsm state register",
               SymbolTaxonomy::isFsmStateRegisterDeclaration(
                   stateRegisterMetadata),
               true);
    const SymbolTaxonomy::SemanticMetadata stateValueMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Enum,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::EnumValue);
    expectBool("SymbolTaxonomy metadata fsm state value",
               SymbolTaxonomy::isFsmStateValueDeclaration(stateValueMetadata),
               true);
    SymbolTaxonomy::SemanticMetadata syntheticTaskMetadata;
    syntheticTaskMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Task;
    expectBool("SymbolTaxonomy metadata subroutine declaration",
               SymbolTaxonomy::isSubroutineDeclaration(syntheticTaskMetadata),
               true);
    SymbolTaxonomy::SemanticMetadata syntheticPackageParameterMetadata;
    syntheticPackageParameterMetadata.declarationKind =
        SymbolTaxonomy::DeclarationKind::Parameter;
    expectBool("SymbolTaxonomy metadata package visibility",
               SymbolTaxonomy::isPackageVisibleDefinition(
                   syntheticPackageParameterMetadata),
               true);
    QSet<QString> packageScopes;
    packageScopes.insert(QStringLiteral("pkg_scope"));
    const SymbolTaxonomy::SemanticMetadata packageMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Package,
                                SymbolTaxonomy::SymbolOwnerScope::Global,
                                SymbolTaxonomy::CollectorKind::Package);
    expectBool("SymbolTaxonomy package metadata declaration",
               SymbolTaxonomy::isPackageDeclaration(packageMetadata),
               true);
    expectBool("SymbolTaxonomy package scope names",
               packageScopes.contains(QStringLiteral("pkg_scope")),
               true);
    const SymbolTaxonomy::SemanticMetadata packageParameterMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Parameter,
                                SymbolTaxonomy::SymbolOwnerScope::Package);
    const QString packageParameterOwner = QStringLiteral("pkg_scope");
    expectBool("SymbolTaxonomy package definition visible",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   packageParameterMetadata,
                   packageParameterOwner,
                   QStringLiteral("top")),
               true);
    const SymbolTaxonomy::SemanticMetadata metadataPackageParameterMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Parameter,
                                SymbolTaxonomy::SymbolOwnerScope::Package);
    const QString metadataPackageParameterOwner = QStringLiteral("pkg_scope");
    expectBool("SymbolTaxonomy metadata package definition visible",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   metadataPackageParameterMetadata,
                   metadataPackageParameterOwner,
                   QStringLiteral("top")),
               true);
    const SymbolTaxonomy::SemanticMetadata metadataStructMemberMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::StructMember,
                                SymbolTaxonomy::SymbolOwnerScope::Struct);
    const QString metadataStructMemberOwner = QStringLiteral("pixel_t");
    expectBool("SymbolTaxonomy metadata member definition visible",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   metadataStructMemberMetadata,
                   metadataStructMemberOwner,
                   QStringLiteral("top")),
               true);
    const SymbolTaxonomy::SemanticMetadata metadataEnumValueMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Enum,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::EnumValue);
    const QString metadataEnumValueOwner = QStringLiteral("state_t");
    expectBool("SymbolTaxonomy metadata enum value definition visible",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   metadataEnumValueMetadata,
                   metadataEnumValueOwner,
                   QStringLiteral("top")),
               true);
    const SymbolTaxonomy::SemanticMetadata scopedLogicMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Signal,
                                SymbolTaxonomy::SymbolOwnerScope::Module,
                                SymbolTaxonomy::CollectorKind::Logic);
    const QString scopedLogicOwner = QStringLiteral("top");
    expectBool("SymbolTaxonomy scoped logic definition hidden",
               SymbolTaxonomy::isDefinitionVisibleInContext(
                   scopedLogicMetadata,
                   scopedLogicOwner,
                   QStringLiteral("other_top")),
               false);
    const SymbolTaxonomy::SemanticMetadata finalModuleMetadata =
        semanticFixtureMetadata(SymbolTaxonomy::DeclarationKind::Module,
                                SymbolTaxonomy::SymbolOwnerScope::Global,
                                SymbolTaxonomy::CollectorKind::Module);
    expectBool("SymbolTaxonomy module metadata declaration",
               SymbolTaxonomy::isModuleDeclaration(finalModuleMetadata),
               true);

    AlternateCommandService* alternateCommandService =
        AlternateCommandService::getInstance();
    expectEq("AlternateCommand normalize",
             alternateCommandService->normalizeCommandInput(QStringLiteral(" SAVE_AS ")),
             QStringLiteral("save_as"));
    expectList("AlternateCommand catalog",
               alternateCommandService->commands(),
               {"save", "save_as", "open", "new", "close",
                "copy", "paste", "cut", "undo", "redo",
                "find", "replace", "goto_line", "select_all",
                "comment", "uncomment", "indent", "unindent",
                "clear_rhs"});
    expectList("AlternateCommand filter",
               alternateCommandService->matchingCommands(QStringLiteral("s")),
               {"save", "save_as", "select_all"});
    const AlternateCommandCompletionState alternateCompletionState =
        alternateCommandService->completionState(QStringLiteral(" S "));
    expectEq("AlternateCommand state input",
             alternateCompletionState.normalizedInput,
             QStringLiteral("s"));
    expectList("AlternateCommand state matches",
               alternateCompletionState.matches,
               {"save", "save_as", "select_all"});
    expectBool("AlternateCommand state visible",
               alternateCompletionState.showCompletions,
               true);
    const AlternateCommandCompletionState contextAlternateState =
        EditorSemanticContextService::getInstance()
            ->alternateCommandCompletionState(QStringLiteral(" S "));
    expectEq("EditorContext alternate input",
             contextAlternateState.normalizedInput,
             QStringLiteral("s"));
    expectList("EditorContext alternate matches",
               contextAlternateState.matches,
               {"save", "save_as", "select_all"});
    const EditorAlternateModeCompletionDisplayState alternateDisplayState =
        EditorSemanticContextService::getInstance()
            ->alternateModeCompletionDisplayState(QStringLiteral(" S "));
    expectBool("EditorContext alternate display",
               alternateDisplayState.updateCompletions
                   && alternateDisplayState.showPopup
                   && alternateDisplayState.normalizedInput == QStringLiteral("s")
                   && alternateDisplayState.matches
                       == QStringList{QStringLiteral("save"),
                                      QStringLiteral("save_as"),
                                      QStringLiteral("select_all")},
               true);
    const EditorAlternateModeCompletionDisplayState alternateNoMatchDisplayState =
        EditorSemanticContextService::getInstance()
            ->alternateModeCompletionDisplayState(QStringLiteral("zz"));
    expectBool("EditorContext alternate display no match",
               alternateNoMatchDisplayState.updateCompletions
                   && !alternateNoMatchDisplayState.showPopup
                   && alternateNoMatchDisplayState.normalizedInput
                       == QStringLiteral("zz")
                   && alternateNoMatchDisplayState.matches.isEmpty(),
               true);
    EditorAlternateModeKeyContext alternateKeyContext;
    alternateKeyContext.key = Qt::Key_Backspace;
    alternateKeyContext.buffer = QStringLiteral("sav");
    const EditorAlternateModeKeyState alternateBackspaceState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key backspace",
               alternateBackspaceState.action
                       == EditorAlternateModeKeyAction::UpdateInput
                   && alternateBackspaceState.nextInput == QStringLiteral("sa")
                   && alternateBackspaceState.completion.updateCompletions
                   && alternateBackspaceState.completion.showPopup
                   && alternateBackspaceState.completion.normalizedInput
                       == QStringLiteral("sa")
                   && alternateBackspaceState.completion.matches
                       == QStringList{QStringLiteral("save"),
                                      QStringLiteral("save_as")},
               true);
    alternateKeyContext.buffer.clear();
    const EditorAlternateModeKeyState alternateEmptyBackspaceState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key empty backspace",
               alternateEmptyBackspaceState.action
                       == EditorAlternateModeKeyAction::RefreshCompletions
                   && alternateEmptyBackspaceState.nextInput.isEmpty()
                   && alternateEmptyBackspaceState.completion.updateCompletions
                   && alternateEmptyBackspaceState.completion.showPopup
                   && alternateEmptyBackspaceState.completion.matches.size()
                       == alternateCommandService->commands().size(),
               true);
    alternateKeyContext.key = Qt::Key_Escape;
    alternateKeyContext.buffer = QStringLiteral("save");
    const EditorAlternateModeKeyState alternateEscapeState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key escape",
               alternateEscapeState.action
                       == EditorAlternateModeKeyAction::ClearAndHide
                   && alternateEscapeState.hidePopup
                   && alternateEscapeState.clearBuffer,
               true);
    alternateKeyContext.key = Qt::Key_Return;
    const EditorAlternateModeKeyState alternateReturnState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key return",
               alternateReturnState.action
                       == EditorAlternateModeKeyAction::ExecuteCommand
                   && alternateReturnState.command == QStringLiteral("save"),
               true);
    alternateKeyContext.key = Qt::Key_A;
    alternateKeyContext.text = QStringLiteral("a");
    alternateKeyContext.buffer = QStringLiteral("s");
    const EditorAlternateModeKeyState alternatePrintableState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key printable",
               alternatePrintableState.action
                       == EditorAlternateModeKeyAction::UpdateInput
                   && alternatePrintableState.nextInput == QStringLiteral("sa")
                   && alternatePrintableState.completion.updateCompletions
                   && alternatePrintableState.completion.normalizedInput
                       == QStringLiteral("sa")
                   && alternatePrintableState.completion.matches
                       == QStringList{QStringLiteral("save"),
                                      QStringLiteral("save_as")},
               true);
    alternateKeyContext.key = Qt::Key_F1;
    alternateKeyContext.text.clear();
    const EditorAlternateModeKeyState alternateConsumeState =
        EditorSemanticContextService::getInstance()
            ->alternateModeKeyState(alternateKeyContext);
    expectBool("EditorContext alternate key consume",
               alternateConsumeState.action
                   == EditorAlternateModeKeyAction::Consume,
               true);
    const AlternateCommandCompletionState alternateEmptyState =
        alternateCommandService->completionState(QString());
    expectBool("AlternateCommand empty visible",
               alternateEmptyState.showCompletions,
               true);
    const AlternateCommandCompletionState alternateNoMatchState =
        alternateCommandService->completionState(QStringLiteral("zz"));
    expectBool("AlternateCommand no-match hidden",
               alternateNoMatchState.showCompletions,
               false);
    ++g_checks;
    const bool alternateKnownOk =
        alternateCommandService->isKnownCommand(QStringLiteral(" SELECT_ALL "));
    if (!alternateKnownOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           alternateKnownOk ? "PASS" : "FAIL",
           "AlternateCommand known command");
    ++g_checks;
    const bool alternateActionOk =
        alternateCommandService->commandAction(QStringLiteral(" SAVE_AS "))
            == AlternateCommandAction::SaveAs
        && alternateCommandService->commandAction(QStringLiteral("new"))
            == AlternateCommandAction::NewFile
        && alternateCommandService->commandAction(QStringLiteral("select_all"))
            == AlternateCommandAction::SelectAll
        && alternateCommandService->commandAction(QStringLiteral("clear_rhs"))
            == AlternateCommandAction::ClearRhs
        && alternateCommandService->commandAction(QStringLiteral("unknown"))
            == AlternateCommandAction::None;
    if (!alternateActionOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           alternateActionOk ? "PASS" : "FAIL",
           "AlternateCommand action mapping");

    CompletionModel commandSelectionModel;
    commandSelectionModel.updateCommandCompletions(
        QStringList{QStringLiteral("save"), QStringLiteral("open")},
        QStringLiteral("s"));
    ++g_checks;
    const QModelIndex commandSelectableIndex =
        commandSelectionModel.firstSelectableIndex();
    const bool commandSelectableOk = commandSelectableIndex.isValid()
        && commandSelectionModel.getItem(commandSelectableIndex).text == QStringLiteral("save")
        && commandSelectionModel.isSelectableIndex(commandSelectableIndex)
        && commandSelectionModel.getItem(commandSelectableIndex).score
            == CompletionService::getInstance()->completionItemScore(QStringLiteral("save"),
                                                                     QStringLiteral("s"));
    if (!commandSelectableOk)
        ++g_fails;
    printf("[%s] %-34s row=%d\n",
           commandSelectableOk ? "PASS" : "FAIL",
           "CompletionModel selectable command",
           commandSelectableIndex.row());

    CompletionModel noMatchSelectionModel;
    noMatchSelectionModel.updateCommandCompletions(
        QStringList{QStringLiteral("save"), QStringLiteral("open")},
        QStringLiteral("zz"));
    ++g_checks;
    const bool noMatchSelectableOk =
        !noMatchSelectionModel.firstSelectableIndex().isValid();
    if (!noMatchSelectableOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           noMatchSelectableOk ? "PASS" : "FAIL",
           "CompletionModel no-match hidden");

    CompletionModel defaultSelectionModel;
    defaultSelectionModel.updateSymbolRecordCompletions({},
                                                        QStringLiteral("missing"),
                                                        CompletionCommandKind::Logic);
    ++g_checks;
    const QModelIndex defaultSelectableIndex =
        defaultSelectionModel.firstSelectableIndex();
    const bool defaultSelectableOk = defaultSelectableIndex.isValid()
        && defaultSelectionModel.getItem(defaultSelectableIndex)
               .text
               .startsWith(QStringLiteral("[DEFAULT]"))
        && defaultSelectionModel.isSelectableIndex(defaultSelectableIndex);
    if (!defaultSelectableOk)
        ++g_fails;
    printf("[%s] %-34s row=%d\n",
           defaultSelectableOk ? "PASS" : "FAIL",
           "CompletionModel selectable default",
           defaultSelectableIndex.row());

    const CommandSymbolPresentation logicPresentation =
        CompletionService::getInstance()->commandSymbolPresentation(
            CompletionCommandKind::Logic);
    expectEq("CompletionService command default",
             logicPresentation.defaultValue,
             QStringLiteral("logic"));
    expectEq("CompletionService command desc",
             logicPresentation.typeDescription,
             QStringLiteral("logic variables"));
    const CommandSymbolPresentation modulePresentation =
        CompletionService::getInstance()->commandSymbolPresentation(
            CompletionCommandKind::Module);
    expectEq("CompletionService module command default",
             modulePresentation.defaultValue,
             QStringLiteral("module_name u_module_name (\n);"));
    expectEq("CompletionService module command desc",
             modulePresentation.typeDescription,
             QStringLiteral("module instantiations"));

    const SemanticSymbolRecord modulePresentationRecord =
        SemanticFixtureRecordBuilder(QStringLiteral("uart_core"),
                                     SymbolTaxonomy::DeclarationKind::Module)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Module)
            .withLocalHandle(9002)
            .record();
    const CommandSymbolCompletionItem modulePresentationItem =
        CompletionService::getInstance()->commandSymbolCompletionItem(
            modulePresentationRecord,
            CompletionCommandKind::Module);
    expectEq("CompletionService module instantiates",
             modulePresentationItem.defaultValue,
             QStringLiteral("uart_core u_uart_core (\n);"));

    const SemanticSymbolRecord structPresentationRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("pixel"),
            SymbolTaxonomy::DeclarationKind::StructVariable)
            .inModule(QStringLiteral("pixel_t"))
            .withLocalHandle(9003)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::PackedStructVariable)
            .record();
    const CommandSymbolCompletionItem structPresentationItem =
        CompletionService::getInstance()->commandSymbolCompletionItem(
            structPresentationRecord,
            CompletionCommandKind::PackedStructVariable);
    expectEq("CompletionService struct text",
             structPresentationItem.text,
             QStringLiteral("pixel(%1)")
                 .arg(structPresentationItem.symbolRecord.owner.name));
    expectEq("CompletionService struct key",
             structPresentationItem.uniqueKey,
             QStringLiteral("pixel:%1")
                 .arg(structPresentationItem.symbolRecord.owner.name));
    expectBool("CompletionService struct item record",
               structPresentationItem.symbolRecord.isValid()
                   && structPresentationItem.symbolRecord.localHandle == 9003
                   && structPresentationItem.symbolRecord.stableKey
                       == structPresentationItem.symbolStableKey
                   && structPresentationItem.symbolStableKey
                       == structPresentationRecord.stableKey
                   && structPresentationItem.symbolRecord.owner.name
                       == QStringLiteral("pixel_t")
                   && structPresentationItem.declarationKind
                       == SymbolTaxonomy::DeclarationKind::StructVariable
                   && structPresentationItem.ownerScope
                       == SymbolTaxonomy::SymbolOwnerScope::Module,
               true);

    const SemanticSymbolRecord enumPresentationRecord =
        SemanticFixtureRecordBuilder(
            QStringLiteral("IDLE"),
            SymbolTaxonomy::DeclarationKind::Enum)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(9004)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::EnumValue)
            .withType(QStringLiteral("state_t"))
            .record();
    const CommandSymbolCompletionItem enumPresentationItem =
        CompletionService::getInstance()->commandSymbolCompletionItem(
            enumPresentationRecord,
            CompletionCommandKind::EnumValue);
    expectEq("CompletionService enum desc",
             enumPresentationItem.description,
             enumPresentationItem.symbolRecord.type.rawTypeText);
    expectBool("CompletionService enum item record",
               enumPresentationItem.symbolRecord.isValid()
                   && enumPresentationItem.symbolRecord.localHandle == 9004
                   && enumPresentationItem.symbolRecord.stableKey
                       == enumPresentationItem.symbolStableKey
                   && enumPresentationItem.symbolRecord.name
                       == QStringLiteral("IDLE")
                   && enumPresentationItem.symbolRecord.type.rawTypeText
                       == QStringLiteral("state_t")
                   && enumPresentationItem.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Enum
                   && enumPresentationItem.ownerScope
                       == SymbolTaxonomy::SymbolOwnerScope::Module,
               true);

    CompletionActivationQuery editorActivationQuery;
    editorActivationQuery.selectable = true;
    editorActivationQuery.mode = CompletionActivationMode::EditorWord;
    editorActivationQuery.itemText = QStringLiteral("enable");
    const CompletionActivationState editorActivationState =
        CompletionService::getInstance()->completionActivationState(
            editorActivationQuery);
    ++g_checks;
    const bool editorActivationOk =
        editorActivationState.action == CompletionActivationAction::ReplaceWord
        && editorActivationState.text == QStringLiteral("enable")
        && !editorActivationState.clearCommandMode
        && editorActivationState.hidePopup;
    if (!editorActivationOk)
        ++g_fails;
    printf("[%s] %-34s text=\"%s\"\n",
           editorActivationOk ? "PASS" : "FAIL",
           "CompletionService activate word",
           editorActivationState.text.toLocal8Bit().constData());

    CompletionActivationQuery commandActivationQuery;
    commandActivationQuery.selectable = true;
    commandActivationQuery.mode = CompletionActivationMode::CommandMode;
    commandActivationQuery.itemText = QStringLiteral("[DEFAULT] logic");
    commandActivationQuery.defaultValue = QStringLiteral("logic");
    const CompletionActivationState commandActivationState =
        CompletionService::getInstance()->completionActivationState(
            commandActivationQuery);
    ++g_checks;
    const bool commandActivationOk =
        commandActivationState.action
            == CompletionActivationAction::ReplaceCommandInput
        && commandActivationState.text == QStringLiteral("logic")
        && commandActivationState.clearCommandMode
        && commandActivationState.hidePopup;
    if (!commandActivationOk)
        ++g_fails;
    printf("[%s] %-34s text=\"%s\"\n",
           commandActivationOk ? "PASS" : "FAIL",
           "CompletionService activate command",
           commandActivationState.text.toLocal8Bit().constData());

    CompletionActivationQuery commandFallbackQuery;
    commandFallbackQuery.selectable = true;
    commandFallbackQuery.mode = CompletionActivationMode::CommandMode;
    commandFallbackQuery.itemText = QStringLiteral("enable");
    const CompletionActivationState commandFallbackState =
        CompletionService::getInstance()->completionActivationState(
            commandFallbackQuery);
    ++g_checks;
    const bool commandFallbackOk =
        commandFallbackState.action
            == CompletionActivationAction::ReplaceCommandInput
        && commandFallbackState.text == QStringLiteral("enable")
        && commandFallbackState.clearCommandMode
        && commandFallbackState.hidePopup;
    if (!commandFallbackOk)
        ++g_fails;
    printf("[%s] %-34s text=\"%s\"\n",
           commandFallbackOk ? "PASS" : "FAIL",
           "CompletionService activate fallback",
           commandFallbackState.text.toLocal8Bit().constData());

    CompletionActivationQuery alternateActivationQuery;
    alternateActivationQuery.selectable = true;
    alternateActivationQuery.mode = CompletionActivationMode::AlternateMode;
    alternateActivationQuery.itemText = QStringLiteral("save");
    const CompletionActivationState alternateActivationState =
        CompletionService::getInstance()->completionActivationState(
            alternateActivationQuery);
    ++g_checks;
    const bool alternateActivationOk =
        alternateActivationState.action
            == CompletionActivationAction::ExecuteAlternateCommand
        && alternateActivationState.text == QStringLiteral("save")
        && !alternateActivationState.clearCommandMode
        && !alternateActivationState.hidePopup;
    if (!alternateActivationOk)
        ++g_fails;
    printf("[%s] %-34s text=\"%s\"\n",
           alternateActivationOk ? "PASS" : "FAIL",
           "CompletionService activate alt",
           alternateActivationState.text.toLocal8Bit().constData());
    EditorCompletionActivationContext contextCommandActivation;
    contextCommandActivation.selectable = true;
    contextCommandActivation.commandModeActive = true;
    contextCommandActivation.itemText = QStringLiteral("clk");
    contextCommandActivation.defaultValue = QStringLiteral("logic clk");
    const CompletionActivationState contextCommandActivationState =
        EditorSemanticContextService::getInstance()
            ->completionActivationState(contextCommandActivation);
    expectBool("EditorContext command activation",
               contextCommandActivationState.action
                       == CompletionActivationAction::ReplaceCommandInput
                   && contextCommandActivationState.text
                       == QStringLiteral("logic clk")
                   && contextCommandActivationState.clearCommandMode
                   && contextCommandActivationState.hidePopup,
               true);
    EditorCompletionActivationContext contextAlternateActivation;
    contextAlternateActivation.selectable = true;
    contextAlternateActivation.alternateModeActive = true;
    contextAlternateActivation.commandModeActive = true;
    contextAlternateActivation.itemText = QStringLiteral("save");
    const CompletionActivationState contextAlternateActivationState =
        EditorSemanticContextService::getInstance()
            ->completionActivationState(contextAlternateActivation);
    expectBool("EditorContext alternate activation",
               contextAlternateActivationState.action
                       == CompletionActivationAction::ExecuteAlternateCommand
                   && contextAlternateActivationState.text
                       == QStringLiteral("save")
                   && !contextAlternateActivationState.clearCommandMode,
               true);

    CompletionActivationQuery inactiveActivationQuery;
    inactiveActivationQuery.selectable = false;
    inactiveActivationQuery.mode = CompletionActivationMode::EditorWord;
    inactiveActivationQuery.itemText = QStringLiteral("enable");
    const CompletionActivationState inactiveActivationState =
        CompletionService::getInstance()->completionActivationState(
            inactiveActivationQuery);
    ++g_checks;
    const bool inactiveActivationOk =
        inactiveActivationState.action == CompletionActivationAction::None
        && inactiveActivationState.text.isEmpty()
        && !inactiveActivationState.clearCommandMode
        && !inactiveActivationState.hidePopup;
    if (!inactiveActivationOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           inactiveActivationOk ? "PASS" : "FAIL",
           "CompletionService activate inactive");

    CompletionPopupKeyQuery popupQuery;
    popupQuery.mode = CompletionActivationMode::EditorWord;
    popupQuery.key = Qt::Key_Down;
    expectBool("CompletionService popup forwards arrows",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action == CompletionPopupKeyAction::ForwardToPopup,
               true);
    popupQuery.key = Qt::Key_Return;
    popupQuery.hasRows = true;
    popupQuery.currentIndexValid = false;
    expectBool("CompletionService popup activates selectable",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable,
               true);
    popupQuery.key = Qt::Key_Escape;
    expectBool("CompletionService popup hides",
               CompletionService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action == CompletionPopupKeyAction::HidePopup,
               true);
    popupQuery.key = Qt::Key_Return;
    popupQuery.hasRows = true;
    popupQuery.currentIndexValid = false;
    expectBool("EditorContext popup activates selectable",
               EditorSemanticContextService::getInstance()
                       ->completionPopupKeyState(popupQuery)
                       .action
                   == CompletionPopupKeyAction::ActivateCurrentOrFirstSelectable,
               true);
    EditorCompletionPopupKeyContext contextPopupQuery;
    contextPopupQuery.key = Qt::Key_Escape;
    contextPopupQuery.alternateModeActive = true;
    contextPopupQuery.commandModeActive = true;
    expectBool("EditorContext alternate popup clears",
               EditorSemanticContextService::getInstance()
                       ->completionPopupKeyState(contextPopupQuery)
                       .action
                   == CompletionPopupKeyAction::HidePopupAndClearAlternate,
               true);
    EditorCompletionPopupKeyContext contextCommandPopupQuery;
    contextCommandPopupQuery.key = Qt::Key_Escape;
    contextCommandPopupQuery.commandModeActive = true;
    expectBool("EditorContext command popup clears",
               EditorSemanticContextService::getInstance()
                       ->completionPopupKeyState(contextCommandPopupQuery)
                       .action
                   == CompletionPopupKeyAction::HidePopupAndClearCommand,
               true);

    CompletionPopupKeyQuery alternatePopupQuery;
    alternatePopupQuery.mode = CompletionActivationMode::AlternateMode;
    alternatePopupQuery.key = Qt::Key_Backspace;
    alternatePopupQuery.alternateBufferEmpty = false;
    expectBool("CompletionService alternate popup edits buffer",
               CompletionService::getInstance()
                       ->completionPopupKeyState(alternatePopupQuery)
                       .action == CompletionPopupKeyAction::BackspaceAlternateInput,
               true);
    alternatePopupQuery.key = Qt::Key_Escape;
    expectBool("CompletionService alternate popup clears",
               CompletionService::getInstance()
                       ->completionPopupKeyState(alternatePopupQuery)
                       .action
                   == CompletionPopupKeyAction::HidePopupAndClearAlternate,
               true);
    alternatePopupQuery.key = Qt::Key_Return;
    alternatePopupQuery.currentIndexValid = false;
    expectBool("CompletionService alternate popup consumes empty return",
               CompletionService::getInstance()
                       ->completionPopupKeyState(alternatePopupQuery)
                       .action == CompletionPopupKeyAction::Consume,
               true);

    const CommandModeMatch oldCommandModeMatch =
        CompletionService::getInstance()->matchCommandMode(QStringLiteral("l ena"));
    expectBool("CompletionService old command reject",
               !oldCommandModeMatch.matched,
               true);
    expectBool("CompletionService old module command reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("m top"))
                    .matched,
               true);
    expectBool("CompletionService old reg command reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("r reset"))
                    .matched,
               true);

    const CommandModeMatch commandModeMatch =
        CompletionService::getInstance()->matchCommandMode(QStringLiteral(";l ena"));
    ++g_checks;
    const bool commandModeMatchOk = commandModeMatch.matched
        && commandModeMatch.prefixPosition == 0
        && commandModeMatch.input == QStringLiteral("ena")
        && commandModeMatch.command.kind == CompletionCommandKind::Logic;
    if (!commandModeMatchOk)
        ++g_fails;
    printf("[%s] %-34s input=\"%s\"\n",
           commandModeMatchOk ? "PASS" : "FAIL",
           "CompletionService command match",
           commandModeMatch.input.toLocal8Bit().constData());
    expectBool("CompletionService module command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";m top"))
                       .command.kind == CompletionCommandKind::Module,
               true);
    expectBool("CompletionService reg command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";r reset"))
                       .command.kind == CompletionCommandKind::Reg,
               true);
    expectBool("CompletionService wire command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";w net"))
                       .command.kind == CompletionCommandKind::Wire,
               true);
    expectBool("CompletionService parameter command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";p WIDTH"))
                       .command.kind == CompletionCommandKind::Parameter,
               true);
    expectBool("CompletionService localparam command match",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";lp LOCAL"))
                       .command.kind == CompletionCommandKind::Localparam,
               true);

    const CommandModeInputState commandInputState =
        CompletionService::getInstance()->commandModeInputState(QStringLiteral(";l ena"));
    ++g_checks;
    const bool commandInputStateOk = commandInputState.matched
        && !commandInputState.exitRequested
        && commandInputState.prefixPosition == 0
        && commandInputState.input == QStringLiteral("ena")
        && commandInputState.command.kind == CompletionCommandKind::Logic;
    if (!commandInputStateOk)
        ++g_fails;
    printf("[%s] %-34s input=\"%s\"\n",
           commandInputStateOk ? "PASS" : "FAIL",
           "CompletionService command input",
           commandInputState.input.toLocal8Bit().constData());

    const CommandModeInputState commandExitState =
        CompletionService::getInstance()->commandModeInputState(QStringLiteral(";"));
    ++g_checks;
    const bool commandExitStateOk = !commandExitState.matched;
    if (!commandExitStateOk)
        ++g_fails;
    printf("[%s] %-34s input=\"%s\"\n",
           commandExitStateOk ? "PASS" : "FAIL",
           "CompletionService command exit",
           commandExitState.input.toLocal8Bit().constData());

    ++g_checks;
    const bool commandModeRejectOk =
        !CompletionService::getInstance()
             ->matchCommandMode(QStringLiteral("assign l ena"))
             .matched;
    if (!commandModeRejectOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandModeRejectOk ? "PASS" : "FAIL",
           "CompletionService command reject");

    ++g_checks;
    const bool commandInputRejectOk =
        !CompletionService::getInstance()
             ->commandModeInputState(QStringLiteral("assign l ena"))
             .matched;
    if (!commandInputRejectOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandInputRejectOk ? "PASS" : "FAIL",
           "CompletionService command input reject");

    CommandModeCompletionQuery commandCompletionQuery;
    commandCompletionQuery.lineUpToCursor = QStringLiteral(";l en");
    commandCompletionQuery.fileName = path;
    commandCompletionQuery.moduleName = QStringLiteral("top");
    commandCompletionQuery.documentText = content;
    const CommandModeCompletionState commandCompletionState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionQuery);
    expectList("CompletionService command state",
               recordNames(commandCompletionState.symbolRecords),
               {"enable"});
    ++g_checks;
    const bool commandCompletionStateOk = commandCompletionState.matched
        && !commandCompletionState.exitRequested
        && !commandCompletionState.hidePopup
        && commandCompletionState.showCompletions
        && commandCompletionState.completionPrefix == QStringLiteral("en")
        && commandCompletionState.command.kind == CompletionCommandKind::Logic
        && commandCompletionState.commandKind == CompletionCommandKind::Logic;
    if (!commandCompletionStateOk)
        ++g_fails;
    printf("[%s] %-34s prefix=\"%s\"\n",
           commandCompletionStateOk ? "PASS" : "FAIL",
           "CompletionService command state flags",
           commandCompletionState.completionPrefix.toLocal8Bit().constData());
    expectBool("CompletionService command state records",
               commandCompletionState.symbolRecords.size() == 1
                   && commandCompletionState.symbolStableKeys.size() == 1
                   && commandCompletionState.symbolRecords.first().isValid()
                   && commandCompletionState.symbolRecords.first().localHandle
                       >= 0
                   && commandCompletionState.symbolRecords.first().stableKey
                       == commandCompletionState.symbolStableKeys.first()
                   && commandCompletionState.symbolRecords.first().name
                       == QStringLiteral("enable")
                   && commandCompletionState.symbolRecords.first().owner.name
                       == QStringLiteral("top")
                   && commandCompletionState.symbolStableKeys.first()
                       == commandCompletionState.symbolRecords.first().stableKey,
               true);

    CommandModeCompletionQuery commandCompletionExitQuery;
    commandCompletionExitQuery.lineUpToCursor = QStringLiteral(";");
    const CommandModeCompletionState commandCompletionExitState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionExitQuery);
    ++g_checks;
    const bool commandCompletionExitOk = !commandCompletionExitState.matched
        && !commandCompletionExitState.exitRequested
        && !commandCompletionExitState.showCompletions
        && commandCompletionExitState.symbolRecords.isEmpty()
        && commandCompletionExitState.symbolStableKeys.isEmpty();
    if (!commandCompletionExitOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandCompletionExitOk ? "PASS" : "FAIL",
           "CompletionService command state exit");

    CommandModeCompletionQuery commandCompletionHideQuery;
    commandCompletionHideQuery.lineUpToCursor = QStringLiteral(";sp pix");
    commandCompletionHideQuery.fileName = path;
    commandCompletionHideQuery.documentText = content;
    const CommandModeCompletionState commandCompletionHideState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionHideQuery);
    ++g_checks;
    const bool commandCompletionHideOk = commandCompletionHideState.matched
        && commandCompletionHideState.hidePopup
        && !commandCompletionHideState.showCompletions
        && commandCompletionHideState.command.kind
            == CompletionCommandKind::PackedStructVariable;
    if (!commandCompletionHideOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           commandCompletionHideOk ? "PASS" : "FAIL",
           "CompletionService command state hide");

    CompletionTriggerQuery commandTriggerQuery;
    commandTriggerQuery.lineUpToCursor = QStringLiteral(";l ena ");
    commandTriggerQuery.commandModeActive = true;
    expectBool("CompletionService trigger command",
               CompletionService::getInstance()->shouldContinueCompletion(
                   commandTriggerQuery),
               true);
    const CompletionTriggerState commandTriggerState =
        CompletionService::getInstance()->completionTriggerState(commandTriggerQuery);
    expectBool("CompletionService trigger command state",
               commandTriggerState.continueCompletion,
               true);
    expectBool("CompletionService trigger command hide",
               commandTriggerState.hidePopup,
               false);

    CompletionTriggerQuery wordTriggerQuery;
    wordTriggerQuery.lineUpToCursor = QStringLiteral("assign en");
    expectBool("CompletionService trigger word",
               CompletionService::getInstance()->shouldContinueCompletion(
                   wordTriggerQuery),
               true);
    const CompletionTriggerState wordTriggerState =
        CompletionService::getInstance()->completionTriggerState(wordTriggerQuery);
    expectBool("CompletionService trigger word state",
               wordTriggerState.continueCompletion,
               true);
    expectBool("CompletionService trigger word hide",
               wordTriggerState.hidePopup,
               false);
    CompletionTriggerQuery shortWordTriggerQuery;
    shortWordTriggerQuery.lineUpToCursor = QStringLiteral("assign e");
    const CompletionTriggerState shortWordTriggerState =
        CompletionService::getInstance()->completionTriggerState(
            shortWordTriggerQuery);
    expectBool("CompletionService trigger short word",
               shortWordTriggerState.continueCompletion,
               false);
    expectBool("CompletionService trigger short word hide",
               shortWordTriggerState.hidePopup,
               true);
    CommandModeCompletionQuery commandCompletionFilterQuery;
    commandCompletionFilterQuery.lineUpToCursor = QStringLiteral(";l clk");
    commandCompletionFilterQuery.fileName = path;
    commandCompletionFilterQuery.moduleName = QStringLiteral("top");
    commandCompletionFilterQuery.documentText = content;
    const CommandModeCompletionState commandCompletionFilterState =
        CompletionService::getInstance()->commandModeCompletionState(
            commandCompletionFilterQuery);
    expectBool("CompletionService command filter prefix",
               commandCompletionFilterState.matched
                   && commandCompletionFilterState.completionPrefix
                       == QStringLiteral("clk")
                   && commandCompletionFilterState.command.kind
                       == CompletionCommandKind::Logic,
               true);

    CompletionTriggerQuery dotTriggerQuery;
    dotTriggerQuery.lineUpToCursor = QStringLiteral("pixel.");
    expectBool("CompletionService trigger dot",
               CompletionService::getInstance()->shouldContinueCompletion(
                   dotTriggerQuery),
               true);
    CompletionTriggerQuery macroTriggerQuery;
    macroTriggerQuery.lineUpToCursor = QStringLiteral("`");
    expectBool("CompletionService trigger macro",
               CompletionService::getInstance()->shouldContinueCompletion(
                   macroTriggerQuery),
               true);
    CompletionTriggerQuery systemTaskTriggerQuery;
    systemTaskTriggerQuery.lineUpToCursor = QStringLiteral("$");
    expectBool("CompletionService trigger system task",
               CompletionService::getInstance()->shouldContinueCompletion(
                   systemTaskTriggerQuery),
               true);
    CompletionTriggerQuery packageTriggerQuery;
    packageTriggerQuery.lineUpToCursor = QStringLiteral("pkg::");
    expectBool("CompletionService trigger package scope",
               CompletionService::getInstance()->shouldContinueCompletion(
                   packageTriggerQuery),
               true);

    CompletionTriggerQuery structSpaceTriggerQuery;
    structSpaceTriggerQuery.lineUpToCursor = QStringLiteral("pixel. ");
    structSpaceTriggerQuery.moduleName = QStringLiteral("top");
    expectBool("CompletionService trigger struct space",
               CompletionService::getInstance()->shouldContinueCompletion(
                   structSpaceTriggerQuery),
               false);
    const CompletionTriggerState structSpaceTriggerState =
        CompletionService::getInstance()->completionTriggerState(
            structSpaceTriggerQuery);
    expectBool("CompletionService trigger struct state",
               structSpaceTriggerState.continueCompletion,
               false);
    expectBool("CompletionService trigger struct hide",
               structSpaceTriggerState.hidePopup,
               true);

    CompletionTriggerQuery plainSpaceTriggerQuery;
    plainSpaceTriggerQuery.lineUpToCursor = QStringLiteral("assign value ");
    plainSpaceTriggerQuery.moduleName = QStringLiteral("top");
    expectBool("CompletionService trigger plain space",
               CompletionService::getInstance()->shouldContinueCompletion(
                   plainSpaceTriggerQuery),
               false);
    const CompletionTriggerState plainSpaceTriggerState =
        CompletionService::getInstance()->completionTriggerState(
            plainSpaceTriggerQuery);
    expectBool("CompletionService trigger plain state",
               plainSpaceTriggerState.continueCompletion,
               false);
    expectBool("CompletionService trigger plain hide",
               plainSpaceTriggerState.hidePopup,
               true);

    CompletionTriggerQuery commandStopTriggerQuery;
    commandStopTriggerQuery.lineUpToCursor = QStringLiteral(";l ena;");
    commandStopTriggerQuery.commandModeActive = true;
    const CompletionTriggerState commandStopTriggerState =
        CompletionService::getInstance()->completionTriggerState(
            commandStopTriggerQuery);
    expectBool("CompletionService trigger command stop",
               commandStopTriggerState.continueCompletion,
               false);
    expectBool("CompletionService trigger command stop hide",
               commandStopTriggerState.hidePopup,
               false);

    const CommandModeCompletionState helpCompletionState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";?")});
    expectBool("CompletionService command help",
               helpCompletionState.matched
                   && helpCompletionState.helpRequested
                   && helpCompletionState.showCompletions
                   && helpCompletionState.helpCommands.size() >= 3,
               true);

    const CommandModeCompletionState templateCompletionState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;l clk")});
    expectBool("CompletionService template mode",
               templateCompletionState.matched
                   && templateCompletionState.intent
                       == InlineCommandIntent::CodeTemplate
                   && templateCompletionState.showCompletions
                   && !templateCompletionState.templateItems.isEmpty()
                   && templateCompletionState.templateItems.first().insertText
                       == QStringLiteral("logic clk;"),
               true);
    expectBool("CompletionService template command",
               CompletionService::getInstance()
                       ->matchCommandMode(QStringLiteral(";;m uart"))
                       .intent == InlineCommandIntent::CodeTemplate,
               true);
    const CommandModeCompletionState moduleTemplateState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;m uart")});
    expectBool("CompletionService module template mode",
               moduleTemplateState.matched
                   && moduleTemplateState.intent
                       == InlineCommandIntent::CodeTemplate
                   && !moduleTemplateState.templateItems.isEmpty()
                   && moduleTemplateState.templateItems.first().insertText
                       == QStringLiteral("`timescale 1ns / 1ps\n"
                                         "module uart(\n"
                                         ");\n"
                                         "endmodule"),
               true);
    const CommandModeCompletionState actionHelpState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";:?")});
    expectBool("CompletionService reserved action help inactive",
               !actionHelpState.matched
                   && !actionHelpState.showCompletions,
               true);
    const CommandModeMatch foldActionMatch =
        CompletionService::getInstance()->matchCommandMode(QStringLiteral(";:fd"));
    expectBool("CompletionService reserved fold action unmatched",
               !foldActionMatch.matched,
               true);
    const CommandModeCompletionState foldActionState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";:fd")});
    expectBool("CompletionService reserved fold action has no completion",
               !foldActionState.matched
                   && !foldActionState.showCompletions
                   && foldActionState.templateItems.isEmpty(),
               true);
    CompletionActivationQuery foldActionActivation;
    foldActionActivation.selectable = true;
    foldActionActivation.mode = CompletionActivationMode::CommandMode;
    foldActionActivation.itemText = QStringLiteral(";:fd");
    foldActionActivation.defaultValue = QStringLiteral(";:fd");
    const CompletionActivationState foldActionActivationState =
        CompletionService::getInstance()->completionActivationState(
            foldActionActivation);
    expectBool("CompletionService reserved fold action does not execute",
               foldActionActivationState.action
                       != CompletionActivationAction::ExecuteEditorAction
                   && foldActionActivationState.text == QStringLiteral(";:fd"),
               true);
    const CommandModeMatch foldShelfActionMatch =
        CompletionService::getInstance()->matchCommandMode(QStringLiteral(";:fds"));
    expectBool("CompletionService reserved fold shelf action unmatched",
               !foldShelfActionMatch.matched,
               true);
    const CommandModeCompletionState templateHelpState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;?")});
    expectBool("CompletionService template help",
               templateHelpState.matched
                   && templateHelpState.intent == InlineCommandIntent::CodeTemplate
                   && templateHelpState.helpRequested
                   && templateHelpState.showCompletions,
               true);
    expectBool("CompletionService bare double semicolon reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral(";;"))
                    .matched,
               true);
    const CommandModeCompletionState actionPrefixState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";:")});
    expectBool("CompletionService reserved action prefix inactive",
               !actionPrefixState.matched
                   && !actionPrefixState.showCompletions
                   && actionPrefixState.templateItems.isEmpty(),
               true);
    expectBool("CompletionService template statement reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("assign a = b;;l "))
                    .matched,
               true);
    expectBool("CompletionService template comment reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("// ;;l "))
                    .matched,
               true);
    expectBool("CompletionService template string reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("string s = \";;l "))
                    .matched,
               true);
    expectEq("CodeTemplateService seeds logic",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"), QStringLiteral("clk"))
                 .insertText,
             QStringLiteral("logic clk;"));
    expectEq("CodeTemplateService module template",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;m"),
                                      QStringLiteral("uart"))
                 .insertText,
             QStringLiteral("`timescale 1ns / 1ps\n"
                            "module uart(\n"
                            ");\n"
                            "endmodule"));

    QTemporaryDir userTemplateSettingsDir;
    expectBool("UserTemplateService temp dir valid",
               userTemplateSettingsDir.isValid(),
               true);
    const QString userTemplateSettingsFile =
        QDir(userTemplateSettingsDir.path()).absoluteFilePath(
            QStringLiteral("user_templates.ini"));
    UserTemplateService userTemplateService(userTemplateSettingsFile);
    UserTemplateRecord pipeTemplate;
    pipeTemplate.id = QStringLiteral("pipe_stage");
    pipeTemplate.commandToken = QStringLiteral(";;pipe");
    pipeTemplate.label = QStringLiteral("pipeline stage");
    pipeTemplate.description = QStringLiteral("user pipeline register");
    pipeTemplate.insertText =
        QStringLiteral("logic [WIDTH-1:0] data_q;\n"
                       "always_ff @(posedge clk) data_q <= data_d;");
    pipeTemplate.selectionStart =
        pipeTemplate.insertText.indexOf(QStringLiteral("data_q"));
    pipeTemplate.selectionLength = QStringLiteral("data_q").size();
    CodeTemplateSlot pipeSlot;
    pipeSlot.name = QStringLiteral("signal");
    pipeSlot.start = pipeTemplate.selectionStart;
    pipeSlot.length = pipeTemplate.selectionLength;
    pipeTemplate.templateSlots.append(pipeSlot);

    const UserTemplateSaveReport userTemplateSaveReport =
        userTemplateService.setRecords({pipeTemplate});
    expectBool("UserTemplateService saves valid template",
               userTemplateSaveReport.valid
                   && userTemplateSaveReport.records.size() == 1
                   && userTemplateSaveReport.records.first().id
                       == QStringLiteral("pipe_stage"),
               true);
    UserTemplateService reloadedUserTemplates(userTemplateSettingsFile);
    const QList<CodeTemplateItem> userTemplateCatalog =
        reloadedUserTemplates.catalog();
    expectBool("UserTemplateService reloads catalog item",
               userTemplateCatalog.size() == 1
                   && userTemplateCatalog.first().commandToken
                       == QStringLiteral(";;pipe")
                   && userTemplateCatalog.first().label
                       == QStringLiteral("pipeline stage"),
               true);
    const QList<CodeTemplateItem> userTemplateMatches =
        reloadedUserTemplates.matchingTemplates(QStringLiteral(";;pipe"));
    expectBool("UserTemplateService query preserves slots",
               userTemplateMatches.size() == 1
                   && userTemplateMatches.first().insertText
                       == pipeTemplate.insertText
                   && userTemplateMatches.first().selectionStart
                       == pipeTemplate.selectionStart
                   && userTemplateMatches.first().selectionLength
                       == pipeTemplate.selectionLength
                   && userTemplateMatches.first().templateSlots.size() == 1
                   && userTemplateMatches.first().templateSlots.first().name
                       == QStringLiteral("signal"),
               true);
    expectBool("UserTemplateService unknown command empty",
               reloadedUserTemplates
                   .matchingTemplates(QStringLiteral(";;missing"))
                   .isEmpty(),
               true);

    CompletionService userTemplateCompletionService;
    userTemplateCompletionService.setUserTemplateService(
        &reloadedUserTemplates);
    const CommandModeInputState userTemplateInputState =
        userTemplateCompletionService.commandModeInputState(
            QStringLiteral(";;pipe stage"));
    expectBool("CompletionService recognizes user template command",
               userTemplateInputState.matched
                   && userTemplateInputState.intent
                       == InlineCommandIntent::CodeTemplate
                   && userTemplateInputState.descriptor.label
                       == QStringLiteral(";;pipe")
                   && userTemplateInputState.input == QStringLiteral("stage"),
               true);
    const CommandModeCompletionState userTemplateCompletionState =
        userTemplateCompletionService.commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;pipe ")});
    expectBool("CompletionService returns user template item",
               userTemplateCompletionState.matched
                   && userTemplateCompletionState.intent
                       == InlineCommandIntent::CodeTemplate
                   && userTemplateCompletionState.showCompletions
                   && userTemplateCompletionState.templateItems.size() == 1
                   && userTemplateCompletionState.templateItems.first()
                          .commandToken == QStringLiteral(";;pipe")
                   && userTemplateCompletionState.templateItems.first()
                          .insertText == pipeTemplate.insertText
                   && userTemplateCompletionState.templateItems.first()
                          .templateSlots.size() == 1,
               true);
    const CodeTemplateItem userTemplateCompletionItem =
        userTemplateCompletionState.templateItems.isEmpty()
            ? CodeTemplateItem()
            : userTemplateCompletionState.templateItems.first();
    CompletionActivationQuery userTemplateActivation;
    userTemplateActivation.selectable = true;
    userTemplateActivation.mode = CompletionActivationMode::CommandMode;
    userTemplateActivation.defaultValue =
        userTemplateCompletionItem.insertText;
    userTemplateActivation.selectionStart =
        userTemplateCompletionItem.selectionStart;
    userTemplateActivation.selectionLength =
        userTemplateCompletionItem.selectionLength;
    userTemplateActivation.templateSlots =
        userTemplateCompletionItem.templateSlots;
    const CompletionActivationState userTemplateActivationState =
        userTemplateCompletionService.completionActivationState(
            userTemplateActivation);
    expectBool("CompletionService user template activation carries slot",
               userTemplateActivationState.action
                       == CompletionActivationAction::ReplaceCommandInput
                   && userTemplateActivationState.clearCommandMode
                   && userTemplateActivationState.hidePopup
                   && userTemplateActivationState.text == pipeTemplate.insertText
                   && userTemplateActivationState.templateSlots.size() == 1,
               true);
    MyCodeEditor userTemplateSlotEditor;
    userTemplateSlotEditor.setPlainText(pipeTemplate.insertText);
    userTemplateSlotEditor.startTemplateSlotMode(
        0,
        pipeTemplate.insertText.size(),
        userTemplateCompletionItem.templateSlots);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("User template Slot Mode starts on slot",
               userTemplateSlotEditor.templateSlotModeActive()
                   && userTemplateSlotEditor.templateSlotModeActiveIndex() == 0
                   && userTemplateSlotEditor.textCursor().selectedText()
                       == QStringLiteral("data_q"),
               true);
    insertAtEditorCursor(userTemplateSlotEditor,
                         QStringLiteral("stage_data"));
    expectBool("User template Slot Mode edit keeps document",
               userTemplateSlotEditor.toPlainText().startsWith(
                   QStringLiteral("logic [WIDTH-1:0] stage_data;"))
                   && userTemplateSlotEditor.templateSlotModeActive(),
               true);
    expectBool("User template Slot Mode final Tab completes",
               sendEditorKey(userTemplateSlotEditor, Qt::Key_Tab)
                   && !userTemplateSlotEditor.templateSlotModeActive(),
               true);

    UserTemplateRecord invalidTemplate = pipeTemplate;
    invalidTemplate.id = QStringLiteral("bad_token");
    invalidTemplate.commandToken = QStringLiteral(";pipe");
    const UserTemplateSaveReport invalidTokenReport =
        userTemplateService.setRecords({invalidTemplate});
    expectBool("UserTemplateService rejects non-template token",
               !invalidTokenReport.valid
                   && !invalidTokenReport.failureReason.isEmpty()
                   && reloadedUserTemplates.records().size() == 1,
               true);

    UserTemplateRecord duplicateTemplate = pipeTemplate;
    duplicateTemplate.commandToken = QStringLiteral(";;other_pipe");
    const UserTemplateSaveReport duplicateReport =
        userTemplateService.validateRecords(
            {pipeTemplate, duplicateTemplate});
    expectBool("UserTemplateService rejects duplicate ids",
               !duplicateReport.valid
                   && duplicateReport.failureReason.contains(
                       QStringLiteral("unique"),
                       Qt::CaseInsensitive),
               true);
    expectBool("UserTemplateService leaves built-in templates separate",
               CodeTemplateService::getInstance()
                       ->templateForCommand(QStringLiteral(";;pipe"))
                       .insertText.isEmpty()
                   && CodeTemplateService::getInstance()
                          ->templateForCommand(QStringLiteral(";;l"),
                                               QStringLiteral("clk"))
                          .insertText
                       == QStringLiteral("logic clk;"),
               true);

    QTemporaryDir customAbbreviationSettingsDir;
    expectBool("CustomAbbreviationService temp dir valid",
               customAbbreviationSettingsDir.isValid(),
               true);
    const QString customAbbreviationSettingsFile =
        QDir(customAbbreviationSettingsDir.path()).absoluteFilePath(
            QStringLiteral("custom_abbreviations.ini"));
    CustomAbbreviationService customAbbreviationService(
        customAbbreviationSettingsFile);
    CustomAbbreviationRecord logicAlias;
    logicAlias.id = QStringLiteral("logic_alias");
    logicAlias.abbreviation = QStringLiteral("lg");
    logicAlias.commandToken = QStringLiteral(";l");
    logicAlias.label = QStringLiteral("logic command");
    logicAlias.description = QStringLiteral("semantic logic shortcut");
    CustomAbbreviationRecord pipeAlias;
    pipeAlias.id = QStringLiteral("pipe_template_alias");
    pipeAlias.abbreviation = QStringLiteral("pt");
    pipeAlias.commandToken = QStringLiteral(";;pipe");
    pipeAlias.label = QStringLiteral("pipe template");
    pipeAlias.description = QStringLiteral("template shortcut");

    const CustomAbbreviationSaveReport abbreviationSaveReport =
        customAbbreviationService.setRecords({logicAlias, pipeAlias});
    expectBool("CustomAbbreviationService saves valid aliases",
               abbreviationSaveReport.valid
                   && abbreviationSaveReport.records.size() == 2,
               true);
    CustomAbbreviationService reloadedAbbreviations(
        customAbbreviationSettingsFile);
    const CustomAbbreviationResolution logicResolution =
        reloadedAbbreviations.resolveForIntent(
            QStringLiteral("LG"),
            InlineCommandIntent::SemanticCompletion);
    expectBool("CustomAbbreviationService resolves ;cmd alias",
               logicResolution.matched
                   && logicResolution.record.commandToken == QStringLiteral(";l")
                   && logicResolution.intent
                       == InlineCommandIntent::SemanticCompletion,
               true);
    const CustomAbbreviationResolution pipeResolution =
        reloadedAbbreviations.resolveForIntent(
            QStringLiteral("pt"),
            InlineCommandIntent::CodeTemplate);
    expectBool("CustomAbbreviationService resolves ;;cmd alias",
               pipeResolution.matched
                   && pipeResolution.record.commandToken
                       == QStringLiteral(";;pipe")
                   && pipeResolution.intent == InlineCommandIntent::CodeTemplate,
               true);
    expectBool("CustomAbbreviationService intent filter separates aliases",
               !reloadedAbbreviations
                    .resolveForIntent(QStringLiteral("pt"),
                                      InlineCommandIntent::SemanticCompletion)
                    .matched,
               true);
    const QList<CustomAbbreviationRecord> prefixMatches =
        reloadedAbbreviations.matchingRecords(QStringLiteral("p"));
    expectBool("CustomAbbreviationService prefix query",
               prefixMatches.size() == 1
                   && prefixMatches.first().commandToken
                       == QStringLiteral(";;pipe"),
               true);

    CustomAbbreviationRecord invalidActionAlias = logicAlias;
    invalidActionAlias.id = QStringLiteral("fold_action_alias");
    invalidActionAlias.abbreviation = QStringLiteral("fd");
    invalidActionAlias.commandToken = QStringLiteral(";:fd");
    const CustomAbbreviationSaveReport invalidActionReport =
        customAbbreviationService.setRecords({invalidActionAlias});
    expectBool("CustomAbbreviationService rejects ;: namespace",
               !invalidActionReport.valid
                   && !invalidActionReport.failureReason.isEmpty()
                   && reloadedAbbreviations.records().size() == 2,
               true);
    CustomAbbreviationRecord duplicateAlias = pipeAlias;
    duplicateAlias.id = QStringLiteral("other_pipe_template_alias");
    duplicateAlias.abbreviation = QStringLiteral("LG");
    duplicateAlias.commandToken = QStringLiteral(";;m");
    const CustomAbbreviationSaveReport duplicateAliasReport =
        customAbbreviationService.validateRecords({logicAlias, duplicateAlias});
    expectBool("CustomAbbreviationService rejects duplicate aliases",
               !duplicateAliasReport.valid
                   && duplicateAliasReport.failureReason.contains(
                       QStringLiteral("unique"),
                       Qt::CaseInsensitive),
               true);
    expectBool("CustomAbbreviationService leaves built-in ;cmd unchanged",
               CompletionService::getInstance()
                   ->matchCommandMode(QStringLiteral(";l "))
                   .matched,
               true);
    expectEq("CustomAbbreviationService leaves built-in ;;cmd unchanged",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral("clk"))
                 .insertText,
             QStringLiteral("logic clk;"));

    QTemporaryDir foldShelfSettingsDir;
    expectBool("FoldShelfPersistence temp dir valid",
               foldShelfSettingsDir.isValid(),
               true);
    const QString foldShelfSettingsFile =
        QDir(foldShelfSettingsDir.path()).absoluteFilePath(
            QStringLiteral("fold_shelf.ini"));
    const QString foldShelfWorkspaceA =
        QDir(foldShelfSettingsDir.path()).absoluteFilePath(
            QStringLiteral("workspace_a"));
    const QString foldShelfWorkspaceB =
        QDir(foldShelfSettingsDir.path()).absoluteFilePath(
            QStringLiteral("workspace_b"));
    const QString foldShelfWorkspaceC =
        QDir(foldShelfSettingsDir.path()).absoluteFilePath(
            QStringLiteral("workspace_c"));
    FoldShelfPersistenceService foldShelfPersistence(
        foldShelfSettingsFile);
    FoldBlockShelfModel persistentFoldShelf;
    persistentFoldShelf.setPersistenceService(&foldShelfPersistence);
    persistentFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);

    FoldShelfItem persistentFoldItem;
    persistentFoldItem.alias = QStringLiteral("pipe block");
    persistentFoldItem.text =
        QStringLiteral("// fold pipe block\nlogic valid;\n// endfold\n");
    persistentFoldItem.sourceFile =
        QDir(foldShelfWorkspaceA).absoluteFilePath(
            QStringLiteral("rtl/top.sv"));
    persistentFoldItem.sourceModule = QStringLiteral("top");
    persistentFoldItem.sourceStartLine = 10;
    persistentFoldItem.sourceEndLine = 12;
    persistentFoldItem.originKind = FoldShelfOriginKind::Moved;
    const QString persistentFoldId =
        persistentFoldShelf.addItem(persistentFoldItem);
    expectBool("FoldShelfPersistence saves add",
               !persistentFoldId.isEmpty()
                   && persistentFoldShelf.items().size() == 1,
               true);

    FoldBlockShelfModel reloadedFoldShelf;
    reloadedFoldShelf.setPersistenceService(&foldShelfPersistence);
    reloadedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    const FoldShelfItem reloadedFoldItem =
        reloadedFoldShelf.item(persistentFoldId);
    expectBool("FoldShelfPersistence reloads workspace item",
               reloadedFoldShelf.items().size() == 1
                   && reloadedFoldItem.alias == QStringLiteral("pipe block")
                   && reloadedFoldItem.text == persistentFoldItem.text
                   && reloadedFoldItem.sourceModule == QStringLiteral("top")
                   && reloadedFoldItem.sourceFile.endsWith(
                       QStringLiteral("workspace_a/rtl/top.sv"))
                   && reloadedFoldItem.lineCount == 3,
               true);

    FoldBlockShelfModel isolatedFoldShelf;
    isolatedFoldShelf.setPersistenceService(&foldShelfPersistence);
    isolatedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceB);
    expectBool("FoldShelfPersistence scopes by workspace",
               isolatedFoldShelf.items().isEmpty(),
               true);

    expectBool("FoldShelfPersistence saves consume",
               reloadedFoldShelf.consumeItem(persistentFoldId),
               true);
    FoldBlockShelfModel consumedFoldShelf;
    consumedFoldShelf.setPersistenceService(&foldShelfPersistence);
    consumedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    expectBool("FoldShelfPersistence reloads consumed item",
               consumedFoldShelf.item(persistentFoldId).consumed,
               true);

    expectBool("FoldShelfPersistence saves stale mark",
               consumedFoldShelf.markItemStale(persistentFoldId),
               true);
    FoldBlockShelfModel staleFoldShelf;
    staleFoldShelf.setPersistenceService(&foldShelfPersistence);
    staleFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    expectBool("FoldShelfPersistence reloads stale item",
               staleFoldShelf.item(persistentFoldId).stale,
               true);

    expectBool("FoldShelfPersistence saves remove",
               staleFoldShelf.removeItem(persistentFoldId),
               true);
    FoldBlockShelfModel removedFoldShelf;
    removedFoldShelf.setPersistenceService(&foldShelfPersistence);
    removedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    expectBool("FoldShelfPersistence reloads removed empty",
               removedFoldShelf.items().isEmpty(),
               true);

    const QString clearFoldId =
        removedFoldShelf.addItem(persistentFoldItem);
    expectBool("FoldShelfPersistence re-add before clear",
               !clearFoldId.isEmpty()
                   && !removedFoldShelf.items().isEmpty(),
               true);
    removedFoldShelf.clear();
    FoldBlockShelfModel clearedFoldShelf;
    clearedFoldShelf.setPersistenceService(&foldShelfPersistence);
    clearedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    expectBool("FoldShelfPersistence saves clear",
               clearedFoldShelf.items().isEmpty(),
               true);

    FoldBlockShelfModel crossFileFoldShelf;
    crossFileFoldShelf.setPersistenceService(&foldShelfPersistence);
    crossFileFoldShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    const QString crossFileFoldId =
        crossFileFoldShelf.addItem(persistentFoldItem);
    MyCodeEditor crossFileTargetEditor;
    crossFileTargetEditor.setDocumentFileName(
        QDir(foldShelfWorkspaceB).absoluteFilePath(
            QStringLiteral("rtl/target.sv")));
    crossFileTargetEditor.setPlainText(
        QStringLiteral("module target;\nendmodule\n"));
    const FoldShelfRestoreReport crossFileRestoreReport =
        FoldShelfRestoreService::restoreIntoEditor(
            &crossFileFoldShelf,
            &crossFileTargetEditor,
            crossFileFoldId,
            1,
            FoldShelfRestoreCompletion::ConsumeItem);
    expectBool("FoldShelfRestore cross-file consumes after insert",
               crossFileRestoreReport.success
                   && crossFileRestoreReport.inserted
                   && crossFileRestoreReport.itemConsumed
                   && crossFileFoldShelf.item(crossFileFoldId).consumed
                   && crossFileTargetEditor.toPlainText().contains(
                       QStringLiteral("// fold pipe block"))
                   && QDir::fromNativeSeparators(
                          crossFileTargetEditor.documentFileName())
                          .endsWith(QStringLiteral(
                              "workspace_b/rtl/target.sv")),
               true);
    FoldBlockShelfModel crossFileReloadedShelf;
    crossFileReloadedShelf.setPersistenceService(&foldShelfPersistence);
    crossFileReloadedShelf.setWorkspaceRoot(foldShelfWorkspaceA);
    expectBool("FoldShelfRestore persists cross-file consume",
               crossFileReloadedShelf.item(crossFileFoldId).consumed,
               true);

    FoldShelfItem staleCandidate = persistentFoldItem;
    staleCandidate.alias = QStringLiteral("stale candidate");
    FoldBlockShelfModel failedCrossFileShelf;
    failedCrossFileShelf.setPersistenceService(&foldShelfPersistence);
    failedCrossFileShelf.setWorkspaceRoot(foldShelfWorkspaceB);
    const QString staleFoldId = failedCrossFileShelf.addItem(staleCandidate);
    const FoldShelfRestoreReport failedCrossFileReport =
        FoldShelfRestoreService::restoreIntoEditor(
            &failedCrossFileShelf,
            nullptr,
            staleFoldId,
            -1,
            FoldShelfRestoreCompletion::ConsumeItem);
    expectBool("FoldShelfRestore marks failed target stale",
               !failedCrossFileReport.success
                   && failedCrossFileReport.staleMarked
                   && failedCrossFileReport.failureReason.contains(
                       QStringLiteral("active editor"),
                       Qt::CaseInsensitive),
               true);
    FoldBlockShelfModel staleReloadedShelf;
    staleReloadedShelf.setPersistenceService(&foldShelfPersistence);
    staleReloadedShelf.setWorkspaceRoot(foldShelfWorkspaceB);
    expectBool("FoldShelfRestore persists failed target stale",
               staleReloadedShelf.item(staleFoldId).stale,
               true);

    FoldBlockShelfModel removeRestoreShelf;
    removeRestoreShelf.setPersistenceService(&foldShelfPersistence);
    removeRestoreShelf.setWorkspaceRoot(foldShelfWorkspaceB);
    const QString removeRestoreId =
        removeRestoreShelf.addItem(persistentFoldItem);
    MyCodeEditor removeRestoreEditor;
    removeRestoreEditor.setPlainText(
        QStringLiteral("module remove_target;\nendmodule\n"));
    const FoldShelfRestoreReport removeRestoreReport =
        FoldShelfRestoreService::restoreIntoEditor(
            &removeRestoreShelf,
            &removeRestoreEditor,
            removeRestoreId,
            1,
            FoldShelfRestoreCompletion::RemoveItem);
    expectBool("FoldShelfRestore removes after successful insert",
               removeRestoreReport.success
                   && removeRestoreReport.itemRemoved
                   && removeRestoreShelf.item(removeRestoreId).id.isEmpty()
                   && removeRestoreEditor.toPlainText().contains(
                       QStringLiteral("logic valid")),
               true);

    FoldBlockShelfModel managedFoldShelf;
    managedFoldShelf.setPersistenceService(&foldShelfPersistence);
    managedFoldShelf.setWorkspaceRoot(foldShelfWorkspaceC);
    FoldShelfItem activeManagedItem = persistentFoldItem;
    activeManagedItem.alias = QStringLiteral("active pipe");
    activeManagedItem.sourceModule = QStringLiteral("alpha_top");
    activeManagedItem.consumed = false;
    activeManagedItem.stale = false;
    const QString activeManagedId =
        managedFoldShelf.addItem(activeManagedItem);
    FoldShelfItem consumedManagedItem = persistentFoldItem;
    consumedManagedItem.alias = QStringLiteral("done response");
    consumedManagedItem.sourceModule = QStringLiteral("beta_mod");
    const QString consumedManagedId =
        managedFoldShelf.addItem(consumedManagedItem);
    FoldShelfItem staleManagedItem = persistentFoldItem;
    staleManagedItem.alias = QStringLiteral("stale cache");
    staleManagedItem.sourceModule = QStringLiteral("gamma_mod");
    const QString staleManagedId =
        managedFoldShelf.addItem(staleManagedItem);
    expectBool("FoldShelfManagement marks setup states",
               managedFoldShelf.consumeItem(consumedManagedId)
                   && managedFoldShelf.markItemStale(staleManagedId),
               true);
    expectBool("FoldShelfManagement renames item",
               managedFoldShelf.renameItem(
                   activeManagedId,
                   QStringLiteral("  renamed pipe  "))
                   && managedFoldShelf.item(activeManagedId).alias
                          == QStringLiteral("renamed pipe"),
               true);
    expectBool("FoldShelfManagement rejects blank rename",
               !managedFoldShelf.renameItem(activeManagedId,
                                            QStringLiteral("   "))
                   && managedFoldShelf.item(activeManagedId).alias
                          == QStringLiteral("renamed pipe"),
               true);
    FoldBlockShelfModel renamedManagedReload;
    renamedManagedReload.setPersistenceService(&foldShelfPersistence);
    renamedManagedReload.setWorkspaceRoot(foldShelfWorkspaceC);
    expectBool("FoldShelfManagement persists rename",
               renamedManagedReload.item(activeManagedId).alias
                   == QStringLiteral("renamed pipe"),
               true);
    const QList<FoldShelfItem> alphaMatches =
        managedFoldShelf.itemsMatching(QStringLiteral("renamed alpha"));
    expectBool("FoldShelfManagement filters by terms",
               alphaMatches.size() == 1
                   && alphaMatches.first().id == activeManagedId,
               true);
    const QList<FoldShelfItem> staleMatches =
        managedFoldShelf.itemsMatching(QStringLiteral("stale gamma"));
    expectBool("FoldShelfManagement filters stale item",
               staleMatches.size() == 1
                   && staleMatches.first().id == staleManagedId,
               true);
    const int cleanedManagedCount =
        managedFoldShelf.removeConsumedOrStaleItems();
    expectBool("FoldShelfManagement cleans consumed and stale",
               cleanedManagedCount == 2
                   && managedFoldShelf.items().size() == 1
                   && managedFoldShelf.item(activeManagedId).id
                          == activeManagedId
                   && managedFoldShelf.item(consumedManagedId).id.isEmpty()
                   && managedFoldShelf.item(staleManagedId).id.isEmpty(),
               true);
    FoldBlockShelfModel cleanedManagedReload;
    cleanedManagedReload.setPersistenceService(&foldShelfPersistence);
    cleanedManagedReload.setWorkspaceRoot(foldShelfWorkspaceC);
    expectBool("FoldShelfManagement persists clean",
               cleanedManagedReload.items().size() == 1
                   && cleanedManagedReload.item(activeManagedId).id
                          == activeManagedId,
               true);
    expectBool("FoldShelfManagement clean no-op",
               managedFoldShelf.removeConsumedOrStaleItems() == 0,
               true);

    const CodeTemplateItem widthLogic =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;l"), QStringLiteral("8 sig"));
    expectEq("CodeTemplateService logic width before name",
             widthLogic.insertText,
             QStringLiteral("logic [7:0] sig;"));
    expectBool("CodeTemplateService logic selects name",
               widthLogic.selectionStart == widthLogic.insertText.indexOf(QStringLiteral("sig"))
                   && widthLogic.selectionLength == QStringLiteral("sig").size()
                   && widthLogic.templateSlots.size() == 1
                   && widthLogic.templateSlots.first().name == QStringLiteral("name")
                   && widthLogic.templateSlots.first().start == widthLogic.selectionStart
                   && widthLogic.templateSlots.first().length == widthLogic.selectionLength,
               true);
    expectEq("CodeTemplateService logic unpacked dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral("8 sig 8"))
                 .insertText,
             QStringLiteral("logic [7:0] sig [7:0];"));
    expectEq("CodeTemplateService logic multi packed dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral("8 8 sig"))
                 .insertText,
             QStringLiteral("logic [7:0][7:0] sig;"));
    expectEq("CodeTemplateService logic parameter dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral(":P_W sig 9"))
                 .insertText,
             QStringLiteral("logic [P_W - 1:0] sig [8:0];"));
    expectEq("CodeTemplateService logic exact range dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral(":PW+DW:0 sig"))
                 .insertText,
             QStringLiteral("logic [PW+DW:0] sig;"));
    expectEq("CodeTemplateService logic signed dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;l"),
                                      QStringLiteral("-s :P_W sig"))
                 .insertText,
             QStringLiteral("logic signed [P_W - 1:0] sig;"));
    const CodeTemplateItem widthWire =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;w"), QStringLiteral("8 net_sig 4"));
    expectEq("CodeTemplateService wire dimensions",
             widthWire.insertText,
             QStringLiteral("wire [7:0] net_sig [3:0];"));
    expectBool("CodeTemplateService wire selects name",
               widthWire.selectionStart
                       == widthWire.insertText.indexOf(QStringLiteral("net_sig"))
                   && widthWire.selectionLength
                       == QStringLiteral("net_sig").size()
                   && widthWire.templateSlots.size() == 1
                   && widthWire.templateSlots.first().start
                       == widthWire.selectionStart,
               true);
    expectEq("CodeTemplateService reg signed dimensions",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;r"),
                                      QStringLiteral("-s :P_W state 8"))
                 .insertText,
             QStringLiteral("reg signed [P_W - 1:0] state [7:0];"));
    expectEq("CodeTemplateService reg exact range dimension",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;r"),
                                      QStringLiteral(":PW+DW:0 state"))
                 .insertText,
             QStringLiteral("reg [PW+DW:0] state;"));
    const CodeTemplateItem regTemplate =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;r"), QStringLiteral(":PW+DW:0 state"));
    expectBool("CodeTemplateService reg carries name slot",
               regTemplate.templateSlots.size() == 1
                   && regTemplate.templateSlots.first().name
                       == QStringLiteral("name")
                   && regTemplate.templateSlots.first().start
                       == regTemplate.selectionStart
                   && regTemplate.templateSlots.first().length
                       == regTemplate.selectionLength,
               true);
    const CommandModeCompletionState widthTemplateState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;l 8 data")});
    expectBool("CompletionService template carries selection",
               widthTemplateState.matched
                   && !widthTemplateState.templateItems.isEmpty()
                   && widthTemplateState.templateItems.first().insertText
                       == QStringLiteral("logic [7:0] data;")
                   && widthTemplateState.templateItems.first().selectionStart
                       == QStringLiteral("logic [7:0] ").size()
                   && widthTemplateState.templateItems.first().selectionLength
                       == QStringLiteral("data").size()
                   && widthTemplateState.templateItems.first().templateSlots.size()
                       == 1,
               true);
    const CommandModeCompletionState wireTemplateState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;w 8 net_sig")});
    expectBool("CompletionService wire template carries selection",
               wireTemplateState.matched
                   && !wireTemplateState.templateItems.isEmpty()
                   && wireTemplateState.templateItems.first().insertText
                       == QStringLiteral("wire [7:0] net_sig;")
                   && wireTemplateState.templateItems.first().selectionStart
                       == QStringLiteral("wire [7:0] ").size()
                   && wireTemplateState.templateItems.first().selectionLength
                       == QStringLiteral("net_sig").size()
                   && wireTemplateState.templateItems.first().templateSlots.size()
                       == 1,
               true);
    CompletionActivationQuery signalTemplateActivation;
    signalTemplateActivation.selectable = true;
    signalTemplateActivation.mode = CompletionActivationMode::CommandMode;
    signalTemplateActivation.defaultValue = widthLogic.insertText;
    signalTemplateActivation.selectionStart = widthLogic.selectionStart;
    signalTemplateActivation.selectionLength = widthLogic.selectionLength;
    signalTemplateActivation.templateSlots = widthLogic.templateSlots;
    const CompletionActivationState signalTemplateActivationState =
        CompletionService::getInstance()->completionActivationState(
            signalTemplateActivation);
    expectBool("CompletionService signal activation carries slot",
               signalTemplateActivationState.action
                       == CompletionActivationAction::ReplaceCommandInput
                   && signalTemplateActivationState.clearCommandMode
                   && signalTemplateActivationState.hidePopup
                   && signalTemplateActivationState.templateSlots.size() == 1,
               true);
    MyCodeEditor signalSlotEditor;
    signalSlotEditor.setPlainText(widthLogic.insertText);
    signalSlotEditor.startTemplateSlotMode(0,
                                           widthLogic.insertText.size(),
                                           widthLogic.templateSlots);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("Signal Slot Mode starts on name",
               signalSlotEditor.templateSlotModeActive()
                   && signalSlotEditor.templateSlotModeActiveIndex() == 0
                   && signalSlotEditor.textCursor().selectedText()
                       == QStringLiteral("sig"),
               true);
    insertAtEditorCursor(signalSlotEditor, QStringLiteral("data_bus"));
    expectBool("Signal Slot Mode edit updates name",
               signalSlotEditor.toPlainText()
                       == QStringLiteral("logic [7:0] data_bus;")
                   && signalSlotEditor.templateSlotModeActive(),
               true);
    expectBool("Signal Slot Mode final Tab completes",
               sendEditorKey(signalSlotEditor, Qt::Key_Tab)
                   && !signalSlotEditor.templateSlotModeActive()
                   && signalSlotEditor.textCursor().position()
                       == signalSlotEditor.toPlainText()
                              .indexOf(QStringLiteral(";")),
               true);
    const CodeTemplateItem parameterScalar =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;p"), QStringLiteral("WIDTH"));
    expectEq("CodeTemplateService parameter scalar",
             parameterScalar.insertText,
             QStringLiteral("parameter WIDTH = ;"));
    expectBool("CodeTemplateService parameter slots",
               parameterScalar.selectionStart
                       == parameterScalar.insertText.indexOf(QStringLiteral("WIDTH"))
                   && parameterScalar.selectionLength == QStringLiteral("WIDTH").size()
                   && parameterScalar.templateSlots.size() == 2
                   && parameterScalar.templateSlots.at(0).name == QStringLiteral("name")
                   && parameterScalar.templateSlots.at(0).start
                       == parameterScalar.insertText.indexOf(QStringLiteral("WIDTH"))
                   && parameterScalar.templateSlots.at(0).length
                       == QStringLiteral("WIDTH").size()
                   && parameterScalar.templateSlots.at(1).name == QStringLiteral("value")
                   && parameterScalar.templateSlots.at(1).start
                       == QStringLiteral("parameter WIDTH = ").size()
                   && parameterScalar.templateSlots.at(1).length == 0,
               true);
    expectBool("CodeTemplateService parameter primary slot selects name",
               parameterScalar.selectionStart
                       == parameterScalar.templateSlots.at(0).start
                   && parameterScalar.selectionLength
                       == parameterScalar.templateSlots.at(0).length,
               true);
    const CodeTemplateItem parameterArray =
        CodeTemplateService::getInstance()->templateForCommand(
            QStringLiteral(";;p"), QStringLiteral("8 test 8"));
    expectEq("CodeTemplateService parameter array",
             parameterArray.insertText,
             QStringLiteral("parameter [7:0] test [7:0] = '{};"));
    expectBool("CodeTemplateService parameter array slots",
               parameterArray.templateSlots.size() == 2
                   && parameterArray.templateSlots.at(0).start
                       == parameterArray.insertText.indexOf(QStringLiteral("test"))
                   && parameterArray.templateSlots.at(0).length
                       == QStringLiteral("test").size()
                   && parameterArray.templateSlots.at(1).start
                       == parameterArray.insertText.indexOf(QStringLiteral("{}")) + 1
                   && parameterArray.templateSlots.at(1).length == 0,
               true);
    expectEq("CodeTemplateService typed parameter array",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;p"),
                                      QStringLiteral("-logic 8 test 8"))
                 .insertText,
             QStringLiteral("parameter logic [7:0] test [7:0] = '{};"));
    expectEq("CodeTemplateService localparam typed scalar",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;lp"),
                                      QStringLiteral("-integer DEPTH"))
                 .insertText,
             QStringLiteral("localparam integer DEPTH = ;"));
    expectEq("CodeTemplateService localparam typed array",
             CodeTemplateService::getInstance()
                 ->templateForCommand(QStringLiteral(";;lp"),
                                      QStringLiteral("-bit :P_W LUT 16"))
                 .insertText,
             QStringLiteral("localparam bit [P_W - 1:0] LUT [15:0] = '{};"));
    const CommandModeCompletionState parameterTypeState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;p -")});
    expectBool("CompletionService parameter type suggestions",
               parameterTypeState.matched
                   && parameterTypeState.templateItems.size() >= 4
                   && parameterTypeState.templateItems.first().label
                       == QStringLiteral("int")
                   && parameterTypeState.templateItems.first().insertText
                       == QStringLiteral(";;p -int "),
               true);
    const CommandModeCompletionState fuzzyParameterTypeState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;p -lc")});
    expectBool("CompletionService parameter type fuzzy suggestions",
               fuzzyParameterTypeState.matched
                   && fuzzyParameterTypeState.templateItems.size() == 1
                   && fuzzyParameterTypeState.templateItems.first().label
                       == QStringLiteral("logic")
                   && fuzzyParameterTypeState.templateItems.first().insertText
                       == QStringLiteral(";;p -logic "),
               true);
    CompletionActivationQuery parameterTypeActivation;
    parameterTypeActivation.selectable = true;
    parameterTypeActivation.mode = CompletionActivationMode::CommandMode;
    parameterTypeActivation.itemText = QStringLiteral("logic");
    parameterTypeActivation.defaultValue = QStringLiteral(";;p -logic ");
    const CompletionActivationState parameterTypeActivationState =
        CompletionService::getInstance()->completionActivationState(
            parameterTypeActivation);
    expectBool("CompletionService parameter type keeps command mode",
               parameterTypeActivationState.action
                       == CompletionActivationAction::ReplaceCommandInput
                   && parameterTypeActivationState.text
                       == QStringLiteral(";;p -logic ")
                   && !parameterTypeActivationState.clearCommandMode
                   && !parameterTypeActivationState.hidePopup,
               true);

    const CommandModeCompletionState parameterTemplateState =
        CompletionService::getInstance()->commandModeCompletionState(
            CommandModeCompletionQuery{QStringLiteral(";;p WIDTH")});
    expectBool("CompletionService parameter template carries slots",
               parameterTemplateState.matched
                   && !parameterTemplateState.templateItems.isEmpty()
                   && parameterTemplateState.templateItems.first().templateSlots.size() == 2
                   && parameterTemplateState.templateItems.first().templateSlots.at(0).name
                       == QStringLiteral("name")
                   && parameterTemplateState.templateItems.first().templateSlots.at(1).name
                       == QStringLiteral("value"),
               true);
    CompletionActivationQuery parameterTemplateActivation;
    parameterTemplateActivation.selectable = true;
    parameterTemplateActivation.mode = CompletionActivationMode::CommandMode;
    parameterTemplateActivation.defaultValue = parameterScalar.insertText;
    parameterTemplateActivation.selectionStart = parameterScalar.selectionStart;
    parameterTemplateActivation.selectionLength = parameterScalar.selectionLength;
    parameterTemplateActivation.templateSlots = parameterScalar.templateSlots;
    const CompletionActivationState parameterTemplateActivationState =
        CompletionService::getInstance()->completionActivationState(
            parameterTemplateActivation);
    expectBool("CompletionService parameter activation carries slots",
               parameterTemplateActivationState.action
                       == CompletionActivationAction::ReplaceCommandInput
                   && parameterTemplateActivationState.clearCommandMode
                   && parameterTemplateActivationState.hidePopup
                   && parameterTemplateActivationState.templateSlots.size() == 2,
               true);

    MyCodeEditor slotEditor;
    slotEditor.setPlainText(parameterScalar.insertText);
    slotEditor.startTemplateSlotMode(0,
                                     parameterScalar.insertText.size(),
                                     parameterScalar.templateSlots);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("Slot Mode starts on parameter name",
               slotEditor.templateSlotModeActive()
                   && slotEditor.templateSlotModeActiveIndex() == 0
                   && slotEditor.textCursor().selectedText()
                       == QStringLiteral("WIDTH"),
               true);
    insertAtEditorCursor(slotEditor, QStringLiteral("DEPTH"));
    expectBool("Slot Mode edit updates active slot",
               slotEditor.toPlainText()
                       == QStringLiteral("parameter DEPTH = ;")
                   && slotEditor.templateSlotModeActive(),
               true);
    expectBool("Slot Mode Tab advances to value",
               sendEditorKey(slotEditor, Qt::Key_Tab)
                   && slotEditor.templateSlotModeActiveIndex() == 1
                   && !slotEditor.textCursor().hasSelection()
                   && slotEditor.textCursor().position()
                       == slotEditor.toPlainText().indexOf(QStringLiteral(";")),
               true);
    insertAtEditorCursor(slotEditor, QStringLiteral("8"));
    expectBool("Slot Mode Shift+Tab returns to name",
               sendEditorKey(slotEditor, Qt::Key_Tab, Qt::ShiftModifier)
                   && slotEditor.templateSlotModeActiveIndex() == 0
                   && slotEditor.textCursor().selectedText()
                       == QStringLiteral("DEPTH"),
               true);
    expectBool("Slot Mode Tab returns to value",
               sendEditorKey(slotEditor, Qt::Key_Tab)
                   && slotEditor.templateSlotModeActiveIndex() == 1
                   && slotEditor.textCursor().selectedText()
                       == QStringLiteral("8"),
               true);
    expectBool("Slot Mode final Tab completes",
               sendEditorKey(slotEditor, Qt::Key_Tab)
                   && !slotEditor.templateSlotModeActive()
                   && slotEditor.textCursor().position()
                       == slotEditor.toPlainText().indexOf(QStringLiteral(";")),
               true);

    MyCodeEditor slotCancelEditor;
    slotCancelEditor.setPlainText(parameterScalar.insertText);
    slotCancelEditor.startTemplateSlotMode(0,
                                           parameterScalar.insertText.size(),
                                           parameterScalar.templateSlots);
    expectBool("Slot Mode Esc cancels without rollback",
               sendEditorKey(slotCancelEditor, Qt::Key_Escape)
                   && !slotCancelEditor.templateSlotModeActive()
                   && slotCancelEditor.toPlainText() == parameterScalar.insertText,
               true);

    MyCodeEditor slotStaleEditor;
    slotStaleEditor.setPlainText(parameterScalar.insertText);
    slotStaleEditor.startTemplateSlotMode(0,
                                          parameterScalar.insertText.size(),
                                          parameterScalar.templateSlots);
    QTextCursor staleCursor(slotStaleEditor.document());
    staleCursor.setPosition(slotStaleEditor.document()->characterCount() - 1);
    slotStaleEditor.setTextCursor(staleCursor);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("Slot Mode cursor outside exits",
               !slotStaleEditor.templateSlotModeActive(),
               true);

    const RtlClearAssignmentRhsReport clearRhsReport =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{
                QStringLiteral("a <= xxx;\n"
                               "b = foo(bar);\n"
                               "assign c = d ? e : f;\n"),
                100});
    expectBool("RtlBatch clear RHS report ready",
               clearRhsReport.canApply()
                   && clearRhsReport.edits.size() == 3
                   && clearRhsReport.templateSlots.size() == 3,
               true);
    expectEq("RtlBatch clear RHS replacement",
             clearRhsReport.replacementText,
             QStringLiteral("a <= ;\n"
                            "b = ;\n"
                            "assign c = ;\n"));
    expectBool("RtlBatch clear RHS slots",
               clearRhsReport.templateSlots.at(0).name == QStringLiteral("rhs1")
                   && clearRhsReport.templateSlots.at(0).start
                       == clearRhsReport.replacementText
                              .indexOf(QStringLiteral(";"))
                   && clearRhsReport.edits.at(0).documentRhsStart
                       == clearRhsReport.edits.at(0).selectionRhsStart + 100,
               true);
    const RtlClearAssignmentRhsReport clearRhsDeclaration =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{QStringLiteral("logic a = b;"), 0});
    expectBool("RtlBatch clear RHS rejects declaration",
               clearRhsDeclaration.status
                   == RtlClearAssignmentRhsStatus::UnsupportedSelection,
               true);
    const RtlClearAssignmentRhsReport clearRhsIncomplete =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{QStringLiteral("a <= b"), 0});
    expectBool("RtlBatch clear RHS rejects incomplete",
               clearRhsIncomplete.status
                   == RtlClearAssignmentRhsStatus::UnsupportedSelection,
               true);
    const RtlClearAssignmentRhsReport clearRhsEmpty =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{QStringLiteral("  // comment\n"), 0});
    expectBool("RtlBatch clear RHS empty selection",
               clearRhsEmpty.status
                   == RtlClearAssignmentRhsStatus::EmptySelection,
               true);
    MyCodeEditor clearRhsEditor;
    const QString clearRhsOriginal =
        QStringLiteral("module batch_demo;\n"
                       "  a <= foo;\n"
                       "  b = bar;\n"
                       "endmodule\n");
    clearRhsEditor.setPlainText(clearRhsOriginal);
    const int clearRhsStart = clearRhsOriginal.indexOf(QStringLiteral("  a"));
    const int clearRhsEnd =
        clearRhsOriginal.indexOf(QStringLiteral("endmodule"));
    QTextCursor clearRhsCursor(clearRhsEditor.document());
    clearRhsCursor.setPosition(clearRhsStart);
    clearRhsCursor.setPosition(clearRhsEnd, QTextCursor::KeepAnchor);
    clearRhsEditor.setTextCursor(clearRhsCursor);
    expectBool("Editor clear RHS applies selection",
               clearRhsEditor.clearSelectedAssignmentRhs(),
               true);
    expectEq("Editor clear RHS text",
             clearRhsEditor.toPlainText(),
             QStringLiteral("module batch_demo;\n"
                            "  a <= ;\n"
                            "  b = ;\n"
                            "endmodule\n"));
    expectBool("Editor clear RHS starts slot mode",
               clearRhsEditor.templateSlotModeActive()
                   && clearRhsEditor.templateSlotModeActiveIndex() == 0
                   && clearRhsEditor.textCursor().position()
                       == clearRhsEditor.toPlainText().indexOf(
                              QStringLiteral("a <= ;"))
                              + QStringLiteral("a <= ").size(),
               true);
    clearRhsEditor.undo();
    expectEq("Editor clear RHS undo restores original",
             clearRhsEditor.toPlainText(),
             clearRhsOriginal);

    MyCodeEditor clearRhsSlotEditor;
    clearRhsSlotEditor.setPlainText(clearRhsOriginal);
    QTextCursor clearRhsSlotCursor(clearRhsSlotEditor.document());
    clearRhsSlotCursor.setPosition(clearRhsStart);
    clearRhsSlotCursor.setPosition(clearRhsEnd, QTextCursor::KeepAnchor);
    clearRhsSlotEditor.setTextCursor(clearRhsSlotCursor);
    clearRhsSlotEditor.clearSelectedAssignmentRhs();
    insertAtEditorCursor(clearRhsSlotEditor, QStringLiteral("foo_next"));
    expectBool("Editor clear RHS slot edit shifts next",
               clearRhsSlotEditor.templateSlotModeActive()
                   && clearRhsSlotEditor.toPlainText().contains(
                       QStringLiteral("a <= foo_next;")),
               true);
    expectBool("Editor clear RHS Tab advances",
               sendEditorKey(clearRhsSlotEditor, Qt::Key_Tab)
                   && clearRhsSlotEditor.templateSlotModeActiveIndex() == 1
                   && clearRhsSlotEditor.textCursor().position()
                       == clearRhsSlotEditor.toPlainText()
                              .lastIndexOf(QStringLiteral(";")),
               true);
    insertAtEditorCursor(clearRhsSlotEditor, QStringLiteral("bar_next"));
    expectBool("Editor clear RHS final Tab exits slot mode",
               sendEditorKey(clearRhsSlotEditor, Qt::Key_Tab)
                   && !clearRhsSlotEditor.templateSlotModeActive()
                   && clearRhsSlotEditor.toPlainText().contains(
                       QStringLiteral("b = bar_next;")),
               true);

    MyCodeEditor clearRhsRejectEditor;
    clearRhsRejectEditor.setPlainText(QStringLiteral("logic a = b;\n"));
    QTextCursor clearRhsRejectCursor(clearRhsRejectEditor.document());
    clearRhsRejectCursor.select(QTextCursor::Document);
    clearRhsRejectEditor.setTextCursor(clearRhsRejectCursor);
    expectBool("Editor clear RHS rejects declaration",
               !clearRhsRejectEditor.clearSelectedAssignmentRhs()
                   && clearRhsRejectEditor.toPlainText()
                       == QStringLiteral("logic a = b;\n"),
               true);

    expectBool("CompletionService command statement reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("assign a = b;l "))
                    .matched,
               true);
    expectBool("CompletionService command comment reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("// ;l "))
                    .matched,
               true);
    expectBool("CompletionService command string reject",
               !CompletionService::getInstance()
                    ->matchCommandMode(QStringLiteral("string s = \";l "))
                    .matched,
               true);

    CompletionTriggerQuery emptyTriggerQuery;
    const CompletionTriggerState emptyTriggerState =
        CompletionService::getInstance()->completionTriggerState(emptyTriggerQuery);
    expectBool("CompletionService trigger empty state",
               emptyTriggerState.continueCompletion,
               false);
    expectBool("CompletionService trigger empty hide",
               emptyTriggerState.hidePopup,
               true);

    const CommandSymbolPresentation interfacePresentation =
        CompletionService::getInstance()->commandSymbolPresentation(
            CompletionCommandKind::Interface);
    expectEq("CompletionService interface default",
             interfacePresentation.defaultValue,
             QStringLiteral("interface"));

    // --- struct member completion (typedef'd) ---
    expectEq("getStructTypeForVariable(pixel)", cm->getStructTypeForVariable("pixel", "top"), "pixel_t");
    expectList("members of pixel_t", cm->getStructMemberCompletions("", "pixel_t"),
               {"red", "green", "blue"});

    // --- struct member completion (inline anonymous) ---
    expectEq("getStructTypeForVariable(byte_split)", cm->getStructTypeForVariable("byte_split", "top"), "byte_split");
    expectList("members of byte_split", cm->getStructMemberCompletions("", "byte_split"),
               {"hi", "lo"});

    // --- prefix filtering on members (note: matching is fuzzy/abbreviation, not strict prefix) ---
    // 'bl' is a subsequence only of "blue"; "green"/"red" don't contain b..l in order.
    expectList("members of pixel_t prefix 'bl'", cm->getStructMemberCompletions("bl", "pixel_t"),
               {"blue"});

    // --- module-internal logic must NOT leak function locals (x, add_one return var) ---
    const QStringList logicNames =
        CompletionService::getInstance()->findModuleSymbolsByKind(
            QStringLiteral("top"),
            CompletionCommandKind::Logic);
    expectExcludes("top logic excludes fn-locals", logicNames,
                   /*mustNot*/ {"x", "add_one"}, /*mustHave*/ {"enable", "result"});

    CompletionQuery query;
    query.prefix = "en";
    query.fileName = path;
    query.moduleName = "top";
    expectList("CompletionService module prefix", CompletionService::getInstance()->findCompletions(query),
               {"enable"});
    const CompletionResult moduleCompletion =
        CompletionService::getInstance()->findCompletionResult(query);
    expectList("CompletionService module result",
               moduleCompletion.names,
               {"enable"});
    ++g_checks;
    const bool moduleResultItemsAvailableOk = moduleCompletion.items.size() == 1
        && moduleCompletion.items.first().label == QStringLiteral("enable");
    if (!moduleResultItemsAvailableOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           moduleResultItemsAvailableOk ? "PASS" : "FAIL",
           "CompletionService result items available",
           moduleCompletion.items.size());
    ++g_checks;
    const bool moduleResultItemsOk = moduleCompletion.items.size() == 1
        && moduleCompletion.items.first().label == QStringLiteral("enable")
        && moduleCompletion.items.first().insertText == QStringLiteral("enable")
        && moduleCompletion.items.first().symbolRecord.isValid()
        && moduleCompletion.items.first().symbolRecord.stableKey
            == moduleCompletion.items.first().symbolStableKey
        && moduleCompletion.items.first().symbolRecord.owner.name
            == QStringLiteral("top")
        && moduleCompletion.items.first().symbolStableKey
            == moduleCompletion.items.first().symbolRecord.stableKey
        && moduleCompletion.items.first().declarationKind
            == SymbolTaxonomy::DeclarationKind::Signal
        && moduleCompletion.items.first().ownerScope
            == SymbolTaxonomy::SymbolOwnerScope::Module
        && moduleCompletion.items.first().sourceRole
            == SymbolTaxonomy::SourceRole::DesignSource
        && moduleCompletion.items.first().typeDisplayName == QStringLiteral("logic")
        && moduleCompletion.items.first().ownerScopeName == QStringLiteral("top")
        && moduleCompletion.items.first().ownerScopeName
            == moduleCompletion.items.first().symbolRecord.owner.name;
    if (!moduleResultItemsOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           moduleResultItemsOk ? "PASS" : "FAIL",
           "CompletionService result items",
           moduleCompletion.items.size());
    CompletionModel semanticResultModel;
    semanticResultModel.updateCompletions(moduleCompletion, QStringLiteral("en"));
    ++g_checks;
    const bool semanticResultModelOk = !moduleCompletion.items.isEmpty()
        && semanticResultModel.rowCount() == 1
        && semanticResultModel.getItem(semanticResultModel.index(0, 0)).text
            == QStringLiteral("enable")
        && semanticResultModel.getItem(semanticResultModel.index(0, 0)).description
            == QStringLiteral("logic")
        && semanticResultModel.getItem(semanticResultModel.index(0, 0)).typeDisplayName
            == QStringLiteral("logic")
        && semanticResultModel.getItem(semanticResultModel.index(0, 0)).ownerScopeName
            == QStringLiteral("top")
        && semanticResultModel.getItem(semanticResultModel.index(0, 0)).ownerScopeName
            == semanticResultModel.getItem(semanticResultModel.index(0, 0))
                   .symbolRecord.owner.name
        && semanticResultModel.getItem(semanticResultModel.index(0, 0)).sourceRoleDisplayName
            == QStringLiteral("design source")
        && semanticResultModel.getItem(semanticResultModel.index(0, 0)).ownerScope
            == SymbolTaxonomy::SymbolOwnerScope::Module
        && semanticResultModel.getItem(semanticResultModel.index(0, 0)).symbolRecord.stableKey
            == moduleCompletion.items.first().symbolRecord.stableKey
        && semanticResultModel.getItem(semanticResultModel.index(0, 0)).symbolStableKey
            == moduleCompletion.items.first().symbolStableKey
        && semanticResultModel.data(
               semanticResultModel.index(0, 0),
               Qt::ToolTipRole).toString().contains(QStringLiteral("owner: top"));
    if (!semanticResultModelOk)
        ++g_fails;
    printf("[%s] %-34s rows=%d\n",
           semanticResultModelOk ? "PASS" : "FAIL",
           "CompletionModel semantic result",
           semanticResultModel.rowCount());

    CompletionQuery memberQuery;
    memberQuery.structTypeNameForMember = "pixel_t";
    expectList("CompletionService struct members",
               CompletionService::getInstance()->findCompletions(memberQuery),
               {"red", "green", "blue"});

    memberQuery.prefix = "bl";
    const CompletionResult memberCompletion =
        CompletionService::getInstance()->findCompletionResult(memberQuery);
    expectList("CompletionService struct prefix",
               memberCompletion.names,
               {"blue"});
    ++g_checks;
    const bool serviceSymbolOk = memberCompletion.items.size() == 1
        && memberCompletion.items.first().label == QStringLiteral("blue")
        && memberCompletion.items.first().declarationKind
            == SymbolTaxonomy::DeclarationKind::StructMember
        && memberCompletion.items.first().ownerScopeName == QStringLiteral("pixel_t");
    if (!serviceSymbolOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           serviceSymbolOk ? "PASS" : "FAIL",
           "CompletionService struct symbols",
           memberCompletion.items.size());
    ++g_checks;
    const bool memberItemsOk = memberCompletion.items.size() == 1
        && memberCompletion.items.first().label == QStringLiteral("blue")
        && memberCompletion.items.first().declarationKind
            == SymbolTaxonomy::DeclarationKind::StructMember
        && memberCompletion.items.first().ownerScope
            == SymbolTaxonomy::SymbolOwnerScope::Struct
        && memberCompletion.items.first().typeDisplayName == QStringLiteral("member")
        && memberCompletion.items.first().ownerScopeName == QStringLiteral("pixel_t")
        && memberCompletion.items.first().symbolRecord.owner.name
            == QStringLiteral("pixel_t")
        && memberCompletion.items.first().ownerScopeName
            == memberCompletion.items.first().symbolRecord.owner.name
        && memberCompletion.items.first().symbolRecord.declarationKind
            == SymbolTaxonomy::DeclarationKind::StructMember
        && memberCompletion.items.first().symbolStableKey
            == memberCompletion.items.first().symbolRecord.stableKey;
    if (!memberItemsOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           memberItemsOk ? "PASS" : "FAIL",
           "CompletionService member items",
           memberCompletion.items.size());
    QString parsedVariableName;
    QString parsedMemberPrefix;
    ++g_checks;
    const bool parsedMemberContext =
        CompletionService::getInstance()->tryParseStructMemberContext(
            QStringLiteral("assign result = pixel.bl"),
            parsedVariableName,
            parsedMemberPrefix);
    const bool parseOk = parsedMemberContext
        && parsedVariableName == QStringLiteral("pixel")
        && parsedMemberPrefix == QStringLiteral("bl");
    if (!parseOk)
        ++g_fails;
    printf("[%s] %-34s var=\"%s\" prefix=\"%s\"\n",
           parseOk ? "PASS" : "FAIL",
           "CompletionService member parse",
           parsedVariableName.toLocal8Bit().constData(),
           parsedMemberPrefix.toLocal8Bit().constData());
    ++g_checks;
    const bool rejectsNonMemberContext =
        !CompletionService::getInstance()->tryParseStructMemberContext(
            QStringLiteral("assign result = pixel"),
            parsedVariableName,
            parsedMemberPrefix);
    if (!rejectsNonMemberContext)
        ++g_fails;
    printf("[%s] %-34s\n",
           rejectsNonMemberContext ? "PASS" : "FAIL",
           "CompletionService member parse reject");
    expectBool("CompletionContext member empty prefix",
               CompletionService::getInstance()->tryParseStructMemberContext(
                   QStringLiteral("pixel."),
                   parsedVariableName,
                   parsedMemberPrefix)
                   && parsedVariableName == QStringLiteral("pixel")
                   && parsedMemberPrefix.isEmpty(),
               true);
    expectEq("CompletionContext struct array variable",
             CompletionContextHelper::extractStructVariable(
                 QStringLiteral("pixel_array[idx].")),
             QStringLiteral("pixel_array"));
    expectEq("CompletionContext enum assignment",
             CompletionContextHelper::extractEnumVariable(
                 QStringLiteral("next_state <= SNAP_IDLE")),
             QStringLiteral("next_state"));
    expectEq("CompletionContext enum case",
             CompletionContextHelper::extractEnumVariable(
                 QStringLiteral("case (snap_state)")),
             QStringLiteral("snap_state"));
    expectEq("CompletionContext enum if equality",
             CompletionContextHelper::extractEnumVariable(
                 QStringLiteral("if (snap_state == SNAP_IDLE")),
             QStringLiteral("snap_state"));
    expectEq("CompletionContext module type",
             CompletionContextHelper::extractModuleType(
                 QStringLiteral("snap_child u_child (")),
             QStringLiteral("snap_child"));

    EditorCompletionQuery editorMemberQuery;
    editorMemberQuery.lineUpToCursor = QStringLiteral("assign result = pixel.bl");
    editorMemberQuery.wordPrefix = QStringLiteral("bl");
    editorMemberQuery.fileName = path;
    editorMemberQuery.moduleName = QStringLiteral("top");
    editorMemberQuery.cursorLine = 1;
    editorMemberQuery.cursorPosition = editorMemberQuery.lineUpToCursor.size();
    const EditorCompletionState editorMemberState =
        CompletionService::getInstance()->editorCompletionState(editorMemberQuery);
    expectList("CompletionService editor member",
               editorMemberState.completion.names,
               {"blue"});
    ++g_checks;
    const bool editorMemberStateOk = editorMemberState.available
        && editorMemberState.prefix == QStringLiteral("bl")
        && editorMemberState.replacementStartColumn
            == editorMemberQuery.lineUpToCursor.lastIndexOf(QLatin1Char('.')) + 1
        && editorMemberState.completion.items.size() == 1
        && editorMemberState.completion.items.first().declarationKind
            == SymbolTaxonomy::DeclarationKind::StructMember;
    if (!editorMemberStateOk)
        ++g_fails;
    printf("[%s] %-34s start=%d\n",
           editorMemberStateOk ? "PASS" : "FAIL",
           "CompletionService editor member state",
           editorMemberState.replacementStartColumn);

    EditorCompletionQuery editorWordQuery;
    editorWordQuery.lineUpToCursor = QStringLiteral("assign en");
    editorWordQuery.wordPrefix = QStringLiteral("en");
    editorWordQuery.fileName = path;
    editorWordQuery.moduleName = QStringLiteral("top");
    editorWordQuery.cursorLine = 1;
    editorWordQuery.cursorPosition = editorWordQuery.lineUpToCursor.size();
    const EditorCompletionState editorWordState =
        CompletionService::getInstance()->editorCompletionState(editorWordQuery);
    expectList("CompletionService editor word",
               editorWordState.completion.names,
               {"enable"});
    ++g_checks;
    const bool editorWordStateOk = editorWordState.available
        && editorWordState.prefix == QStringLiteral("en")
        && editorWordState.replacementStartColumn
            == editorWordQuery.lineUpToCursor.size() - editorWordQuery.wordPrefix.size()
        && editorWordState.completion.items.size() == 1
        && editorWordState.completion.items.first().label
            == QStringLiteral("enable");
    if (!editorWordStateOk)
        ++g_fails;
    printf("[%s] %-34s start=%d\n",
           editorWordStateOk ? "PASS" : "FAIL",
           "CompletionService editor word state",
           editorWordState.replacementStartColumn);

    EditorCompletionQuery editorEmptyQuery;
    editorEmptyQuery.lineUpToCursor = QStringLiteral("assign ");
    editorEmptyQuery.fileName = path;
    editorEmptyQuery.moduleName = QStringLiteral("top");
    ++g_checks;
    const bool editorEmptyStateOk =
        !CompletionService::getInstance()
             ->editorCompletionState(editorEmptyQuery)
             .available;
    if (!editorEmptyStateOk)
        ++g_fails;
    printf("[%s] %-34s\n",
           editorEmptyStateOk ? "PASS" : "FAIL",
           "CompletionService editor empty");

    EditorSemanticContext editorCompletionContext;
    editorCompletionContext.lineUpToCursor = editorWordQuery.lineUpToCursor;
    editorCompletionContext.wordPrefix = editorWordQuery.wordPrefix;
    editorCompletionContext.fileName = path;
    editorCompletionContext.moduleName = QStringLiteral("top");
    editorCompletionContext.cursorLine = 1;
    editorCompletionContext.cursorPosition =
        editorCompletionContext.lineUpToCursor.size();
    const EditorCompletionState contextEditorState =
        EditorSemanticContextService::getInstance()
            ->editorCompletionState(editorCompletionContext);
    expectList("EditorSemanticContext editor word",
               contextEditorState.completion.names,
               {"enable"});
    expectList("EditorSemanticContext names",
               EditorSemanticContextService::getInstance()
                   ->completionNames(QStringLiteral("en"), editorCompletionContext),
               {"enable"});

    EditorSemanticContext triggerContext;
    triggerContext.lineUpToCursor = QStringLiteral("assign en");
    const CompletionTriggerState contextTriggerState =
        EditorSemanticContextService::getInstance()
            ->completionTriggerState(triggerContext);
    expectBool("EditorSemanticContext trigger state",
               contextTriggerState.continueCompletion,
               true);

    EditorSemanticContext commandContext;
    commandContext.lineUpToCursor = QStringLiteral(";l ena ");
    commandContext.fileName = path;
    commandContext.moduleName = QStringLiteral("top");
    commandContext.documentText = content;
    const CommandModeCompletionState contextCommandState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionState(commandContext);
    expectList("EditorSemanticContext command state",
               recordNames(contextCommandState.symbolRecords),
               {"enable"});
    expectBool("EditorSemanticContext command state range",
               contextCommandState.matched
                   && contextCommandState.prefixPosition == 0,
               true);
    expectBool("EditorSemanticContext command state records",
               contextCommandState.symbolRecords.size() == 1
                   && contextCommandState.symbolStableKeys.size() == 1
                   && contextCommandState.symbolRecords.first().stableKey
                       == contextCommandState.symbolStableKeys.first()
                   && contextCommandState.symbolRecords.first().name
                       == QStringLiteral("enable")
                   && contextCommandState.symbolStableKeys.first()
                       == contextCommandState.symbolRecords.first().stableKey,
               true);
    const EditorCommandModeCompletionRefreshState contextRefreshState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionRefreshState(commandContext, false);
    expectBool("EditorSemanticContext command refresh show",
               contextRefreshState.matched
                   && contextRefreshState.commandModeActive
                   && contextRefreshState.highlightCommand
                   && contextRefreshState.showCompletions
                   && !contextRefreshState.hidePopup
                   && contextRefreshState.completion.completionPrefix
                       == QStringLiteral("ena"),
               true);
    const EditorCommandModeCompletionRefreshState suppressedRefreshState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionRefreshState(commandContext, true);
    expectBool("EditorSemanticContext command refresh no suppress",
               suppressedRefreshState.matched
                   && suppressedRefreshState.commandModeActive
                   && !suppressedRefreshState.suppressAfterExit
                   && suppressedRefreshState.showCompletions,
               true);
    EditorSemanticContext commandExitContext;
    commandExitContext.lineUpToCursor = QStringLiteral(";");
    const EditorCommandModeCompletionRefreshState contextExitRefreshState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionRefreshState(commandExitContext, false);
    expectBool("EditorSemanticContext command refresh exit",
               !contextExitRefreshState.matched
                   && contextExitRefreshState.resetExitedByDoubleSpace,
               true);
    EditorSemanticContext noCommandRefreshContext;
    noCommandRefreshContext.lineUpToCursor = QStringLiteral("assign value");
    const EditorCommandModeCompletionRefreshState noCommandRefreshState =
        EditorSemanticContextService::getInstance()
            ->commandModeCompletionRefreshState(noCommandRefreshContext, true);
    expectBool("EditorSemanticContext command refresh reset",
               !noCommandRefreshState.matched
                   && noCommandRefreshState.resetExitedByDoubleSpace,
               true);
    const CommandModeInputState contextInputState =
        EditorSemanticContextService::getInstance()
            ->commandModeInputState(commandContext);
    expectBool("EditorSemanticContext command input",
               contextInputState.matched,
               true);
    const EditorCompletionTextChangeState contextTextChangeState =
        EditorSemanticContextService::getInstance()
            ->completionTextChangeState(commandContext);
    expectBool("EditorSemanticContext text-change command",
               contextTextChangeState.commandModeActive
                   && contextTextChangeState.commandInput.matched
                   && contextTextChangeState.startCompletionTimer
                   && !contextTextChangeState.hidePopup,
               true);
    EditorSemanticContext plainTextChangeContext;
    plainTextChangeContext.lineUpToCursor = QStringLiteral("assign value ");
    plainTextChangeContext.moduleName = QStringLiteral("top");
    const EditorCompletionTextChangeState plainTextChangeState =
        EditorSemanticContextService::getInstance()
            ->completionTextChangeState(plainTextChangeContext);
    expectBool("EditorSemanticContext text-change hide",
               !plainTextChangeState.commandModeActive
                   && !plainTextChangeState.startCompletionTimer
                   && plainTextChangeState.hidePopup,
               true);
    const CommandModeMatch contextCommandMatch =
        EditorSemanticContextService::getInstance()
            ->commandModeMatch(commandContext);
    expectBool("EditorSemanticContext command match",
               contextCommandMatch.matched,
               true);

    CompletionActivationQuery contextActivationQuery;
    contextActivationQuery.selectable = true;
    contextActivationQuery.mode = CompletionActivationMode::EditorWord;
    contextActivationQuery.itemText = QStringLiteral("enable");
    const CompletionActivationState contextActivationState =
        EditorSemanticContextService::getInstance()
            ->completionActivationState(contextActivationQuery);
    expectBool("EditorSemanticContext activation",
               contextActivationState.action == CompletionActivationAction::ReplaceWord,
               true);

    EditorSemanticContext includeNavigationContext;
    includeNavigationContext.lineText = QStringLiteral("`include \"defs.svh\"");
    includeNavigationContext.column =
        includeNavigationContext.lineText.indexOf(QStringLiteral("defs"));
    const EditorSourceNavigationTarget includeNavigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(includeNavigationContext, 100);
    expectBool("EditorSemanticContext nav include target",
               includeNavigationTarget.matched
                   && includeNavigationTarget.includeTarget
                   && includeNavigationTarget.jumpable
                   && includeNavigationTarget.text == QStringLiteral("defs.svh")
                   && includeNavigationTarget.startPos > 100,
               true);
    const EditorSourceNavigationClickState includeClickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(includeNavigationTarget);
    expectBool("EditorSemanticContext nav include click",
               includeClickState.action
                       == EditorSourceNavigationClickAction::OpenInclude
                   && includeClickState.text == QStringLiteral("defs.svh")
                   && includeClickState.acceptEvent,
               true);

    const QString navPath = QStringLiteral("navigation_target.sv");
    const SemanticSymbolRecord navSymbol =
        SemanticFixtureRecordBuilder(
            QStringLiteral("jump_sig"),
            SymbolTaxonomy::DeclarationKind::Signal)
            .withFile(navPath)
            .inModule(QStringLiteral("top"))
            .withLocalHandle(2100)
            .withCollectorKind(SymbolTaxonomy::CollectorKind::Logic)
            .withType(QStringLiteral("logic"))
            .record();
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        navPath,
        {navSymbol},
        QStringLiteral("module top; logic jump_sig; endmodule\n"));
    EditorSemanticContext identifierNavigationContext;
    identifierNavigationContext.fileName = navPath;
    identifierNavigationContext.moduleName = QStringLiteral("top");
    identifierNavigationContext.lineText = QStringLiteral("assign jump_sig = 1'b1;");
    identifierNavigationContext.column =
        identifierNavigationContext.lineText.indexOf(QStringLiteral("jump_sig"));
    const EditorSourceNavigationTarget identifierNavigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(identifierNavigationContext, 200);
    expectBool("EditorSemanticContext nav identifier target",
               identifierNavigationTarget.matched
                   && identifierNavigationTarget.identifierTarget
                   && identifierNavigationTarget.jumpable
                   && identifierNavigationTarget.text == QStringLiteral("jump_sig")
                   && identifierNavigationTarget.cursorPosition
                       == 200 + identifierNavigationContext.column,
               true);
    const EditorSourceNavigationClickState identifierClickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(identifierNavigationTarget);
    expectBool("EditorSemanticContext nav identifier click",
               identifierClickState.action
                       == EditorSourceNavigationClickAction::NavigateToDefinition
                   && identifierClickState.contextCursorPosition
                       == identifierNavigationTarget.cursorPosition
                   && identifierClickState.acceptEvent,
               true);

    identifierNavigationContext.lineText =
        QStringLiteral("assign missing_sig = 1'b1;");
    identifierNavigationContext.column =
        identifierNavigationContext.lineText.indexOf(QStringLiteral("missing_sig"));
    const EditorSourceNavigationTarget unresolvedNavigationTarget =
        EditorSemanticContextService::getInstance()
            ->editorSourceNavigationTarget(identifierNavigationContext, 300);
    expectBool("EditorSemanticContext nav unresolved target",
               unresolvedNavigationTarget.matched
                   && unresolvedNavigationTarget.identifierTarget
                   && !unresolvedNavigationTarget.jumpable
                   && unresolvedNavigationTarget.text
                       == QStringLiteral("missing_sig"),
               true);
    const EditorSourceNavigationClickState emptyClickState =
        EditorSemanticContextService::getInstance()
            ->sourceNavigationClickState(EditorSourceNavigationTarget{});
    expectBool("EditorSemanticContext nav empty click",
               emptyClickState.action == EditorSourceNavigationClickAction::None
                   && !emptyClickState.acceptEvent,
               true);
    SemanticIndex::getInstance()->updateSymbolRecordsForFile(
        navPath,
        {},
        QString());
    SemanticIndex::getInstance()->clearSnapshot();

    EditorSourceSymbolShortcutContext sourceShortcutContext;
    sourceShortcutContext.key = Qt::Key_F12;
    sourceShortcutContext.modifiers = int(Qt::ShiftModifier);
    sourceShortcutContext.semanticContext = identifierNavigationContext;
    const EditorSourceSymbolShortcutState findReferencesShortcutState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext source shortcut refs",
               findReferencesShortcutState.matched
                   && findReferencesShortcutState.acceptEvent
                   && findReferencesShortcutState.action
                       == SourceSymbolAction::FindReferences
                   && findReferencesShortcutState.semanticContext.lineText
                       == identifierNavigationContext.lineText,
               true);
    sourceShortcutContext.key = Qt::Key_R;
    sourceShortcutContext.modifiers =
        int(Qt::ControlModifier | Qt::ShiftModifier);
    const EditorSourceSymbolShortcutState relationshipsShortcutState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext source shortcut rels",
               relationshipsShortcutState.matched
                   && relationshipsShortcutState.acceptEvent
                   && relationshipsShortcutState.action
                       == SourceSymbolAction::ShowRelationships,
               true);
    sourceShortcutContext.key = Qt::Key_F12;
    sourceShortcutContext.modifiers = 0;
    const EditorSourceSymbolShortcutState plainF12State =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext source shortcut plain f12",
               !plainF12State.matched && !plainF12State.acceptEvent,
               true);
    sourceShortcutContext.key = Qt::Key_R;
    sourceShortcutContext.modifiers = int(Qt::ControlModifier);
    const EditorSourceSymbolShortcutState plainControlRState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolShortcutState(sourceShortcutContext);
    expectBool("EditorSemanticContext source shortcut plain ctrl-r",
               !plainControlRState.matched && !plainControlRState.acceptEvent,
               true);

    EditorSemanticContext sourceSymbolContext;
    sourceSymbolContext.fileName = path;
    sourceSymbolContext.moduleName = QStringLiteral("top");
    sourceSymbolContext.lineText = QStringLiteral("assign menu_sig = 1'b1;");
    sourceSymbolContext.column =
        sourceSymbolContext.lineText.indexOf(QStringLiteral("menu_sig"));
    const EditorSourceSymbolContextMenuState sourceMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(sourceSymbolContext);
    expectBool("EditorSemanticContext source menu enabled",
               sourceMenuState.items.size() == 3
                   && sourceMenuState.items.at(0).enabled
                   && sourceMenuState.items.at(0).action
                       == SourceSymbolAction::FindReferences
                   && sourceMenuState.items.at(1).enabled
                   && sourceMenuState.items.at(1).action
                       == SourceSymbolAction::ShowRelationships
                   && sourceMenuState.items.at(2).enabled
                   && sourceMenuState.items.at(2).action
                       == SourceSymbolAction::ShowSignalKernelGraph,
               true);
    const EditorSourceSymbolActionRequestState sourceRefsRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::FindReferences,
                sourceSymbolContext);
    expectBool("EditorSemanticContext source refs request",
               sourceRefsRequest.available
                   && sourceRefsRequest.action == SourceSymbolAction::FindReferences
                   && sourceRefsRequest.symbolName == QStringLiteral("menu_sig")
                   && sourceRefsRequest.fileName == path
                   && sourceRefsRequest.moduleName == QStringLiteral("top"),
               true);
    const EditorSourceSymbolActionRequestState sourceRelsRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowRelationships,
                sourceSymbolContext);
    expectBool("EditorSemanticContext source rels request",
               sourceRelsRequest.available
                   && sourceRelsRequest.action
                       == SourceSymbolAction::ShowRelationships
                   && sourceRelsRequest.symbolName == QStringLiteral("menu_sig"),
               true);
    const EditorSourceSymbolActionRequestState sourceKernelGraphRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::ShowSignalKernelGraph,
                sourceSymbolContext);
    expectBool("EditorSemanticContext source kernel graph request",
               sourceKernelGraphRequest.available
                   && sourceKernelGraphRequest.action
                       == SourceSymbolAction::ShowSignalKernelGraph
                   && sourceKernelGraphRequest.symbolName
                       == QStringLiteral("menu_sig")
                   && sourceKernelGraphRequest.fileName == path
                   && sourceKernelGraphRequest.moduleName
                       == QStringLiteral("top"),
               true);
    EditorSemanticContext unavailableSourceSymbolContext;
    unavailableSourceSymbolContext.lineText = sourceSymbolContext.lineText;
    unavailableSourceSymbolContext.column = sourceSymbolContext.column;
    const EditorSourceSymbolContextMenuState disabledSourceMenuState =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolContextMenuState(unavailableSourceSymbolContext);
    expectBool("EditorSemanticContext source menu disabled",
               disabledSourceMenuState.items.size() == 3
                   && !disabledSourceMenuState.items.at(0).enabled
                   && !disabledSourceMenuState.items.at(1).enabled
                   && !disabledSourceMenuState.items.at(2).enabled,
               true);
    const EditorSourceSymbolActionRequestState unavailableSourceRequest =
        EditorSemanticContextService::getInstance()
            ->sourceSymbolActionRequestState(
                SourceSymbolAction::FindReferences,
                unavailableSourceSymbolContext);
    expectBool("EditorSemanticContext source request unavailable",
               !unavailableSourceRequest.available,
               true);

    QWidget signalKernelGraphHost;
    SignalKernelGraphPanelCoordinator signalKernelGraphPanel(
        &signalKernelGraphHost);
    SignalKernelGraphReport groupedSignalGraph;
    groupedSignalGraph.found = true;
    groupedSignalGraph.kernelModuleName = QStringLiteral("fanout_top");
    groupedSignalGraph.fanoutGroupingThreshold = 5;
    groupedSignalGraph.kernel.id = 1;
    groupedSignalGraph.kernel.role = SignalKernelGraphNodeRole::Kernel;
    groupedSignalGraph.kernel.displayName = QStringLiteral("fanout_sig");
    groupedSignalGraph.kernel.moduleDisplayName =
        QStringLiteral("fanout_top");
    groupedSignalGraph.kernel.typeDisplayName = QStringLiteral("logic");
    groupedSignalGraph.kernel.navigateCodeLink =
        RtlInsightLink::fromFileLine(QStringLiteral("fanout_top.sv"), 10, 1);
    SignalKernelGraphFanoutGroup outputGroup;
    outputGroup.id = 1;
    outputGroup.role = SignalKernelGraphNodeRole::Output;
    outputGroup.groupKey = QStringLiteral("output:fanout_top");
    outputGroup.displayName = QStringLiteral("Outputs in fanout_top");
    outputGroup.moduleName = QStringLiteral("fanout_top");
    outputGroup.totalRoleNodeCount = 6;
    outputGroup.highFanout = true;
    for (int i = 0; i < 6; ++i) {
        SignalKernelGraphNode outputNode;
        outputNode.id = 10 + i;
        outputNode.role = SignalKernelGraphNodeRole::Output;
        outputNode.displayName = QStringLiteral("reader_%1").arg(i + 1);
        outputNode.moduleDisplayName = QStringLiteral("fanout_top");
        outputNode.typeDisplayName = QStringLiteral("logic");
        outputNode.navigateCodeLink =
            RtlInsightLink::fromFileLine(QStringLiteral("fanout_top.sv"),
                                         20 + i,
                                         1);
        groupedSignalGraph.outputs.append(outputNode);
        groupedSignalGraph.edges.append(
            {groupedSignalGraph.kernel.id, outputNode.id, QString()});
        outputGroup.nodeIds.append(outputNode.id);
    }
    outputGroup.nodeCount = outputGroup.nodeIds.size();
    groupedSignalGraph.outputFanoutGroups.append(outputGroup);
    signalKernelGraphPanel.renderReportForTest(groupedSignalGraph);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("SignalKernelGraphPanel defaults fanout collapsed",
               signalKernelGraphPanel.collapsedFanoutGroupCountForTest() == 1
                   && signalKernelGraphPanel.visibleGraphNodeCountForTest() == 1
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 1,
               true);
    expectBool("SignalKernelGraphPanel expands fanout group",
               signalKernelGraphPanel.toggleFanoutGroupForTest(
                   outputGroup.groupKey)
                   && signalKernelGraphPanel
                          .collapsedFanoutGroupCountForTest() == 0
                   && signalKernelGraphPanel.visibleGraphNodeCountForTest()
                          == 7
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 1,
               true);
    expectBool("SignalKernelGraphPanel collapses fanout group again",
               signalKernelGraphPanel.toggleFanoutGroupForTest(
                   outputGroup.groupKey)
                   && signalKernelGraphPanel
                          .collapsedFanoutGroupCountForTest() == 1
                   && signalKernelGraphPanel.visibleGraphNodeCountForTest()
                          == 1,
               true);

    SignalKernelGraphReport filteredSignalGraph;
    filteredSignalGraph.found = true;
    filteredSignalGraph.kernelModuleName = QStringLiteral("graph_top");
    filteredSignalGraph.kernel.id = 100;
    filteredSignalGraph.kernel.role = SignalKernelGraphNodeRole::Kernel;
    filteredSignalGraph.kernel.displayName = QStringLiteral("core_signal");
    filteredSignalGraph.kernel.moduleDisplayName =
        QStringLiteral("graph_top");
    filteredSignalGraph.kernel.typeDisplayName = QStringLiteral("logic");
    filteredSignalGraph.kernel.navigateCodeLink =
        RtlInsightLink::fromFileLine(QStringLiteral("graph_top.sv"), 12, 1);

    SignalKernelGraphNode localInputNode;
    localInputNode.id = 101;
    localInputNode.role = SignalKernelGraphNodeRole::Input;
    localInputNode.inputLane = SignalKernelGraphInputLane::Control;
    localInputNode.displayName = QStringLiteral("start_pulse");
    localInputNode.moduleDisplayName = QStringLiteral("graph_top");
    localInputNode.typeDisplayName = QStringLiteral("logic");
    localInputNode.navigateCodeLink =
        RtlInsightLink::fromFileLine(QStringLiteral("graph_top.sv"), 18, 1);
    filteredSignalGraph.inputs.append(localInputNode);
    filteredSignalGraph.edges.append(
        {localInputNode.id, filteredSignalGraph.kernel.id, QString()});

    SignalKernelGraphNode localOutputNode;
    localOutputNode.id = 102;
    localOutputNode.role = SignalKernelGraphNodeRole::Output;
    localOutputNode.displayName = QStringLiteral("done_local");
    localOutputNode.moduleDisplayName = QStringLiteral("graph_top");
    localOutputNode.typeDisplayName = QStringLiteral("logic");
    localOutputNode.navigateCodeLink =
        RtlInsightLink::fromFileLine(QStringLiteral("graph_top.sv"), 22, 1);
    filteredSignalGraph.outputs.append(localOutputNode);
    filteredSignalGraph.edges.append(
        {filteredSignalGraph.kernel.id, localOutputNode.id, QString()});

    SignalKernelGraphFanoutGroup crossOutputGroup;
    crossOutputGroup.id = 2;
    crossOutputGroup.role = SignalKernelGraphNodeRole::Output;
    crossOutputGroup.groupKey = QStringLiteral("output:consumer_mod");
    crossOutputGroup.displayName =
        QStringLiteral("Outputs in consumer_mod");
    crossOutputGroup.moduleName = QStringLiteral("consumer_mod");
    crossOutputGroup.totalRoleNodeCount = 3;
    crossOutputGroup.highFanout = true;
    for (int i = 0; i < 2; ++i) {
        SignalKernelGraphNode remoteOutputNode;
        remoteOutputNode.id = 103 + i;
        remoteOutputNode.role = SignalKernelGraphNodeRole::Output;
        remoteOutputNode.displayName =
            i == 0 ? QStringLiteral("result_remote")
                   : QStringLiteral("status_remote");
        remoteOutputNode.moduleDisplayName = QStringLiteral("consumer_mod");
        remoteOutputNode.typeDisplayName = QStringLiteral("logic");
        remoteOutputNode.crossModule = true;
        remoteOutputNode.navigateCodeLink =
            RtlInsightLink::fromFileLine(
                QStringLiteral("consumer_mod.sv"),
                30 + i,
                1);
        filteredSignalGraph.outputs.append(remoteOutputNode);
        filteredSignalGraph.edges.append(
            {filteredSignalGraph.kernel.id, remoteOutputNode.id, QString()});
        crossOutputGroup.nodeIds.append(remoteOutputNode.id);
    }
    crossOutputGroup.nodeCount = crossOutputGroup.nodeIds.size();
    filteredSignalGraph.outputFanoutGroups.append(crossOutputGroup);

    signalKernelGraphPanel.renderReportForTest(filteredSignalGraph);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("SignalKernelGraphPanel filters inputs",
               signalKernelGraphPanel.visibleGraphNodeCountForTest() == 3
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 1,
               true);
    signalKernelGraphPanel.setGraphFilterForTest(true, false, false);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("SignalKernelGraphPanel hides outputs",
               signalKernelGraphPanel.visibleGraphNodeCountForTest() == 2
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 0,
               true);
    signalKernelGraphPanel.setGraphFilterForTest(false, true, false);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("SignalKernelGraphPanel hides inputs",
               signalKernelGraphPanel.visibleGraphNodeCountForTest() == 2
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 1,
               true);
    signalKernelGraphPanel.setGraphFilterForTest(true, true, true);
    signalKernelGraphPanel.setGraphSearchTextForTest(
        QStringLiteral("status_remote"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expectBool("SignalKernelGraphPanel searches collapsed cross-module group",
               signalKernelGraphPanel.visibleGraphNodeCountForTest() == 1
                   && signalKernelGraphPanel
                          .renderedFanoutGroupItemCountForTest() == 1
                   && signalKernelGraphPanel.searchMatchCountForTest() == 1
                   && signalKernelGraphPanel.focusedSearchNodeIdForTest()
                          == 104,
               true);
    expectBool("SignalKernelGraphPanel expands filtered search group",
               signalKernelGraphPanel.toggleFanoutGroupForTest(
                   crossOutputGroup.groupKey)
                   && signalKernelGraphPanel.visibleGraphNodeCountForTest()
                          == 3
                   && signalKernelGraphPanel.searchMatchCountForTest() == 1
                   && signalKernelGraphPanel.focusedSearchNodeIdForTest()
                          == 104,
               true);
    signalKernelGraphPanel.setGraphFilterForTest(true, true, false);
    signalKernelGraphPanel.setGraphSearchTextForTest(QString());

    CommandCompletionQuery commandQuery;
    commandQuery.fileName = path;
    commandQuery.moduleName = "top";
    commandQuery.commandKind = CompletionCommandKind::Logic;
    commandQuery.prefix = "en";
    expectList("CompletionService command logic",
               CompletionService::getInstance()->findCommandCompletions(commandQuery),
               {"enable"});

    const QList<SemanticSymbolRecord> commandLogicSymbols =
        CompletionService::getInstance()->findCommandCompletionSymbolRecords(commandQuery);
    ++g_checks;
    const bool commandLogicOk = commandLogicSymbols.size() == 1
        && commandLogicSymbols.first().name == QStringLiteral("enable")
        && commandLogicSymbols.first().collectorKind
            == SymbolTaxonomy::CollectorKind::Logic
        && commandLogicSymbols.first().owner.name == QStringLiteral("top");
    if (!commandLogicOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           commandLogicOk ? "PASS" : "FAIL",
           "CompletionService command symbols",
           commandLogicSymbols.size());

    commandQuery.commandKind = CompletionCommandKind::Wire;
    commandQuery.prefix = "net";
    expectList("CompletionService command wire",
               CompletionService::getInstance()->findCommandCompletions(commandQuery),
               {"net_sig"});

    commandQuery.commandKind = CompletionCommandKind::Reg;
    commandQuery.prefix = "cou";
    expectList("CompletionService command reg",
               CompletionService::getInstance()->findCommandCompletions(commandQuery),
               {"counter"});

    commandQuery.commandKind = CompletionCommandKind::Parameter;
    commandQuery.prefix = "DAT";
    expectList("CompletionService command parameter",
               CompletionService::getInstance()->findCommandCompletions(commandQuery),
               {"DATA_WIDTH"});

    commandQuery.commandKind = CompletionCommandKind::Localparam;
    commandQuery.prefix = "LOC";
    expectList("CompletionService command localparam",
               CompletionService::getInstance()->findCommandCompletions(commandQuery),
               {"LOCAL_MAX"});

    commandQuery.commandKind = CompletionCommandKind::PackedStructVariable;
    commandQuery.prefix = "pix";
    commandQuery.documentText = content;
    const QList<SemanticSymbolRecord> packedStructVars =
        CompletionService::getInstance()->findCommandCompletionSymbolRecords(commandQuery);
    ++g_checks;
    const bool packedStructOk = packedStructVars.size() == 1
        && packedStructVars.first().name == QStringLiteral("pixel")
        && packedStructVars.first().collectorKind
            == SymbolTaxonomy::CollectorKind::PackedStructVariable
        && packedStructVars.first().owner.name == QStringLiteral("top");
    if (!packedStructOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           packedStructOk ? "PASS" : "FAIL",
           "CompletionService command struct",
           packedStructVars.size());

    commandQuery.moduleName.clear();
    ++g_checks;
    const bool structGlobalHidden =
        CompletionService::getInstance()
            ->findCommandCompletionSymbolRecords(commandQuery)
            .isEmpty();
    if (!structGlobalHidden)
        ++g_fails;
    printf("[%s] %-34s\n",
           structGlobalHidden ? "PASS" : "FAIL",
           "CompletionService command struct hidden");

    using CollectorKind = SymbolTaxonomy::CollectorKind;
    using DeclarationKind = SymbolTaxonomy::DeclarationKind;
    using SourceRole = SymbolTaxonomy::SourceRole;
    using SymbolOwnerScope = SymbolTaxonomy::SymbolOwnerScope;

    const QString snapshotOnlyFile = QStringLiteral("snapshot_only.sv");
    QList<SemanticSymbolRecord> snapshotRecords;
    const SemanticSymbolRecord snapshotTop = makeSemanticFixtureRecord(
        QStringLiteral("snap_top"),
        DeclarationKind::Module,
        CollectorKind::Module,
        QString(),
        QString(),
        4000,
        snapshotOnlyFile);
    snapshotRecords.append(snapshotTop);
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_child"),
        DeclarationKind::Module,
        CollectorKind::Module,
        QString(),
        QString(),
        7000,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_if"),
        DeclarationKind::Interface,
        CollectorKind::Interface,
        QString(),
        QString(),
        4002,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_pkg"),
        DeclarationKind::Package,
        CollectorKind::Package,
        QString(),
        QString(),
        4004,
        snapshotOnlyFile));
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("semantic_pkg_param"))
            .withFile(snapshotOnlyFile)
            .withLocalHandle(4005)
            .withLine(4005)
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Parameter,
                                                  SymbolOwnerScope::Package,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Package, QStringLiteral("snap_pkg"))
            .record());
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("SNAP_FEATURE"),
        DeclarationKind::Macro,
        CollectorKind::DefDefine,
        QString(),
        QString(),
        4003,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_pixel"),
        DeclarationKind::StructVariable,
        CollectorKind::PackedStructVariable,
        QString(),
        QStringLiteral("global_pixel_t"),
        5001,
        QStringLiteral("other_snapshot.sv")));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_pixel"),
        DeclarationKind::StructVariable,
        CollectorKind::PackedStructVariable,
        QStringLiteral("snap_top"),
        QStringLiteral("snap_pixel_t"),
        5002,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_pair"),
        DeclarationKind::StructVariable,
        CollectorKind::UnpackedStructVariable,
        QStringLiteral("snap_top"),
        QStringLiteral("snap_pair_t"),
        5003,
        snapshotOnlyFile));
    const SemanticSymbolRecord snapshotEnable = makeSemanticFixtureRecord(
        QStringLiteral("snap_enable"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        QStringLiteral("snap_top"),
        QString(),
        5008,
        snapshotOnlyFile);
    snapshotRecords.append(snapshotEnable);
    const SemanticSymbolRecord duplicateSnapshotEnable =
        makeSemanticFixtureRecord(QStringLiteral("snap_enable"),
                                  DeclarationKind::Signal,
                                  CollectorKind::Logic,
                                  QStringLiteral("snap_top"),
                                  QString(),
                                  5017,
                                  snapshotOnlyFile);
    snapshotRecords.append(duplicateSnapshotEnable);
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("semantic_top_signal"))
            .withFile(snapshotOnlyFile)
            .withLocalHandle(5016)
            .withLine(5016)
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Signal,
                                                  SymbolOwnerScope::Module,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Module, QStringLiteral("snap_top"))
            .record());
    const SemanticSymbolRecord snapshotOtherEnable = makeSemanticFixtureRecord(
        QStringLiteral("snap_other_enable"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        QStringLiteral("other_top"),
        QString(),
        5009,
        snapshotOnlyFile);
    snapshotRecords.append(snapshotOtherEnable);
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_task"),
        DeclarationKind::Task,
        CollectorKind::Task,
        QString(),
        QString(),
        5010,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_state_t"),
        DeclarationKind::Typedef,
        CollectorKind::Typedef,
        QString(),
        QStringLiteral("enum"),
        5011,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_local_state_t"),
        DeclarationKind::Typedef,
        CollectorKind::Typedef,
        QStringLiteral("snap_top"),
        QStringLiteral("enum"),
        5012,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("snap_state"),
        DeclarationKind::Enum,
        CollectorKind::EnumVariable,
        QStringLiteral("snap_top"),
        QString(),
        5013,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("SNAP_IDLE"),
        DeclarationKind::Enum,
        CollectorKind::EnumValue,
        QStringLiteral("snap_top"),
        QString(),
        5014,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("SNAP_RUN"),
        DeclarationKind::Enum,
        CollectorKind::EnumValue,
        QStringLiteral("snap_top"),
        QString(),
        5015,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("red"),
        DeclarationKind::StructMember,
        CollectorKind::StructMember,
        QStringLiteral("other_t"),
        QString(),
        5004,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("red"),
        DeclarationKind::StructMember,
        CollectorKind::StructMember,
        QStringLiteral("snap_pixel_t"),
        QString(),
        5005,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("green"),
        DeclarationKind::StructMember,
        CollectorKind::StructMember,
        QStringLiteral("snap_pixel_t"),
        QString(),
        5006,
        snapshotOnlyFile));
    snapshotRecords.append(makeSemanticFixtureRecord(
        QStringLiteral("blue"),
        DeclarationKind::StructMember,
        CollectorKind::StructMember,
        QStringLiteral("snap_pixel_t"),
        QString(),
        5007,
        snapshotOnlyFile));
    const QString snapshotScopeFile = QStringLiteral("snapshot_scope.sv");
    const QString snapshotScopeContent =
        QStringLiteral("module snap_scope;\n"
                       "  logic snap_signal;\n"
                       "endmodule\n"
                       "module other_scope;\n"
                       "endmodule\n");
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("snap_scope"),
                                     DeclarationKind::Module)
            .withFile(snapshotScopeFile)
            .withLocalHandle(6000)
            .withRange(1, 1, 3, 1)
            .withTextSpan(snapshotScopeContent.indexOf(
                              QStringLiteral("module snap_scope")),
                          QStringLiteral("snap_scope").size())
            .withCollectorKind(CollectorKind::Module)
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("snap_signal"),
                                     DeclarationKind::Signal)
            .withFile(snapshotScopeFile)
            .withLocalHandle(6001)
            .withRange(2, 1, 2, 1)
            .withTextSpan(snapshotScopeContent.indexOf(
                              QStringLiteral("snap_signal")),
                          QStringLiteral("snap_signal").size())
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("snap_scope"))
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("meta_signal"))
            .withFile(snapshotScopeFile)
            .withLocalHandle(6003)
            .withRange(2, 1, 2, 1)
            .withTextSpan(snapshotScopeContent.indexOf(
                              QStringLiteral("snap_signal")),
                          QStringLiteral("meta_signal").size())
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Signal,
                                                  SymbolOwnerScope::Module,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Module, QStringLiteral("snap_scope"))
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("other_scope"),
                                     DeclarationKind::Module)
            .withFile(snapshotScopeFile)
            .withLocalHandle(6002)
            .withRange(4, 1, 5, 1)
            .withTextSpan(snapshotScopeContent.indexOf(
                              QStringLiteral("module other_scope")),
                          QStringLiteral("other_scope").size())
            .withCollectorKind(CollectorKind::Module)
            .record());
    const SemanticSymbolRecord snapshotClock = makeSemanticFixtureRecord(
        QStringLiteral("snap_clk"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        QStringLiteral("snap_top"),
        QString(),
        6003,
        snapshotScopeFile);
    snapshotRecords.append(snapshotClock);
    const SemanticSymbolRecord snapshotReset = makeSemanticFixtureRecord(
        QStringLiteral("snap_rst_n"),
        DeclarationKind::Signal,
        CollectorKind::Logic,
        QStringLiteral("snap_top"),
        QString(),
        6004,
        snapshotScopeFile);
    snapshotRecords.append(snapshotReset);
    const QString semanticModuleScopeFile =
        QStringLiteral("semantic_module_scope.sv");
    const QString semanticModuleScopeContent =
        QStringLiteral("module semantic_scope;\n"
                       "  logic semantic_signal;\n"
                       "endmodule\n");
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("semantic_scope"))
            .withFile(semanticModuleScopeFile)
            .withLocalHandle(6010)
            .withRange(1, 1, 3, 1)
            .withTextSpan(semanticModuleScopeContent.indexOf(
                              QStringLiteral("module semantic_scope")),
                          QStringLiteral("semantic_scope").size())
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Module,
                                                  SymbolOwnerScope::Global,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Global)
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(QStringLiteral("semantic_signal"),
                                     DeclarationKind::Signal)
            .withFile(semanticModuleScopeFile)
            .withLocalHandle(6011)
            .withRange(2, 1, 2, 1)
            .withTextSpan(semanticModuleScopeContent.indexOf(
                              QStringLiteral("semantic_signal")),
                          QStringLiteral("semantic_signal").size())
            .withCollectorKind(CollectorKind::Logic)
            .inModule(QStringLiteral("semantic_scope"))
            .record());
    snapshotRecords.append(
        SemanticFixtureRecordBuilder(
            QStringLiteral("semantic_metadata_signal"))
            .withFile(semanticModuleScopeFile)
            .withLocalHandle(6012)
            .withRange(2, 1, 2, 1)
            .withTextSpan(semanticModuleScopeContent.indexOf(
                              QStringLiteral("semantic_signal")),
                          QStringLiteral("semantic_metadata_signal").size())
            .withMetadata(semanticFixtureMetadata(DeclarationKind::Signal,
                                                  SymbolOwnerScope::Module,
                                                  CollectorKind::User,
                                                  SourceRole::DesignSource))
            .withOwner(SymbolOwnerScope::Module,
                       QStringLiteral("semantic_scope"))
            .record());
    QList<SemanticRelationship> snapshotRelationships;
    snapshotRelationships.append(
        semanticFixtureRelationship(snapshotTop,
                                    snapshotEnable,
                                    SymbolRelationshipEngine::CONTAINS));
    snapshotRelationships.append(semanticFixtureRelationship(
        snapshotTop,
        duplicateSnapshotEnable,
        SymbolRelationshipEngine::CONTAINS));
    snapshotRelationships.append(semanticFixtureRelationship(
        snapshotOtherEnable,
        snapshotEnable,
        SymbolRelationshipEngine::REFERENCES));
    snapshotRelationships.append(
        semanticFixtureRelationship(snapshotClock,
                                    snapshotTop,
                                    SymbolRelationshipEngine::CLOCKS));
    snapshotRelationships.append(
        semanticFixtureRelationship(snapshotReset,
                                    snapshotTop,
                                    SymbolRelationshipEngine::RESETS));
    QHash<QString, QString> snapshotFileContents;
    snapshotFileContents.insert(snapshotScopeFile, snapshotScopeContent);
    snapshotFileContents.insert(semanticModuleScopeFile, semanticModuleScopeContent);
    SemanticIndex snapshotIndex;
    snapshotIndex.setSnapshot(
        sharedSnapshotFromRecords(
            snapshotRecords,
            snapshotRelationships,
            QList<SemanticDiagnostic>{},
            snapshotFileContents));
    CompletionService snapshotCompletionService(&snapshotIndex);
    RelationshipService snapshotRelationshipService(&snapshotIndex);
    expectBool("RelationshipService named contains",
               snapshotRelationshipService.hasNamedRelationship(
                   QStringLiteral("snap_top"),
                   QStringLiteral("snap_enable"),
                   SymbolRelationshipEngine::CONTAINS),
               true);
    expectBool("RelationshipService named rejects missing",
               snapshotRelationshipService.hasNamedRelationship(
                   QStringLiteral("snap_enable"),
                   QStringLiteral("snap_top"),
                   SymbolRelationshipEngine::CONTAINS),
               false);
    expectEq("snapshot struct prefers module",
             snapshotCompletionService.getStructTypeForVariable("snap_pixel", "snap_top"),
             "snap_pixel_t");
    expectEq("snapshot struct fallback",
             snapshotCompletionService.getStructTypeForVariable("snap_pixel", "other_top"),
             "global_pixel_t");
    expectEq("snapshot unpacked struct var",
             snapshotCompletionService.getStructTypeForVariable("snap_pair", "snap_top"),
             "snap_pair_t");
    const QList<SemanticSymbolRecord> snapshotStructRecords =
        snapshotIndex.getSymbolRecords();
    bool snapshotStructRecordOk = false;
    for (const SemanticSymbolRecord& record : snapshotStructRecords) {
        if (record.collectorKind
            != SymbolTaxonomy::CollectorKind::PackedStructVariable)
            continue;
        if (record.name == QStringLiteral("snap_pixel")
            && record.owner.name == QStringLiteral("snap_top")
            && record.type.rawTypeText == QStringLiteral("snap_pixel_t")) {
            snapshotStructRecordOk = true;
            break;
        }
    }
    expectBool("snapshot struct variable semantic record",
               snapshotStructRecordOk,
               true);
    CompletionQuery snapshotModuleQuery;
    snapshotModuleQuery.prefix = QStringLiteral("snap_e");
    snapshotModuleQuery.moduleName = QStringLiteral("snap_top");
    expectList("snapshot module completions",
               snapshotCompletionService.findCompletions(snapshotModuleQuery),
               {"snap_enable"});
    CompletionQuery snapshotSemanticModuleQuery;
    snapshotSemanticModuleQuery.prefix = QStringLiteral("semantic");
    snapshotSemanticModuleQuery.moduleName = QStringLiteral("snap_top");
    const CompletionResult snapshotSemanticModuleResult =
        snapshotCompletionService.findCompletionResult(snapshotSemanticModuleQuery);
    expectList("snapshot semantic module completions",
               snapshotSemanticModuleResult.names,
               {"semantic_top_signal"});
    ++g_checks;
    const bool snapshotSemanticModuleItemOk =
        snapshotSemanticModuleResult.items.size() == 1
        && snapshotSemanticModuleResult.items.first().label
            == QStringLiteral("semantic_top_signal")
        && snapshotSemanticModuleResult.items.first().symbolRecord.owner.name
            == QStringLiteral("snap_top")
        && snapshotSemanticModuleResult.items.first().ownerScopeName
            == snapshotSemanticModuleResult.items.first().symbolRecord.owner.name;
    if (!snapshotSemanticModuleItemOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotSemanticModuleItemOk ? "PASS" : "FAIL",
           "snapshot semantic module items",
           snapshotSemanticModuleResult.items.size());
    const CompletionResult snapshotModuleCompletion =
        snapshotCompletionService.findCompletionResult(snapshotModuleQuery);
    ++g_checks;
    const bool snapshotModuleSymbolOk = snapshotModuleCompletion.items.size() == 1
        && snapshotModuleCompletion.items.first().label
            == QStringLiteral("snap_enable")
        && snapshotModuleCompletion.items.first().symbolRecord.collectorKind
            == SymbolTaxonomy::CollectorKind::Logic
        && snapshotModuleCompletion.items.first().symbolRecord.owner.name
            == QStringLiteral("snap_top");
    if (!snapshotModuleSymbolOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotModuleSymbolOk ? "PASS" : "FAIL",
           "snapshot module symbols",
           snapshotModuleCompletion.items.size());
    CommandCompletionQuery snapshotLogicCommandQuery;
    snapshotLogicCommandQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotLogicCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotLogicCommandQuery.commandKind = CompletionCommandKind::Logic;
    snapshotLogicCommandQuery.prefix = QStringLiteral("snap_e");
    expectList("snapshot command logic names",
               snapshotCompletionService.findCommandCompletions(snapshotLogicCommandQuery),
               {"snap_enable"});
    const QList<SemanticSymbolRecord> snapshotLogicCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotLogicCommandQuery);
    ++g_checks;
    const bool snapshotLogicCommandOk = snapshotLogicCommandSymbols.size() == 1
        && snapshotLogicCommandSymbols.first().name == QStringLiteral("snap_enable")
        && snapshotLogicCommandSymbols.first().collectorKind
            == SymbolTaxonomy::CollectorKind::Logic
        && snapshotLogicCommandSymbols.first().owner.name == QStringLiteral("snap_top");
    if (!snapshotLogicCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotLogicCommandOk ? "PASS" : "FAIL",
           "snapshot command logic symbols",
           snapshotLogicCommandSymbols.size());
    CompletionQuery snapshotGlobalQuery;
    snapshotGlobalQuery.prefix = QStringLiteral("snap");
    expectList("snapshot global completions",
               snapshotCompletionService.findCompletions(snapshotGlobalQuery),
               {"snap_child", "snap_if", "snap_pkg", "snap_scope", "snap_task", "snap_top"});
    snapshotGlobalQuery.prefix = QStringLiteral("semantic");
    expectList("snapshot metadata global completions",
               snapshotCompletionService.findCompletions(snapshotGlobalQuery),
               {"semantic_scope"});
    CommandCompletionQuery snapshotMetadataModuleCommandQuery;
    snapshotMetadataModuleCommandQuery.commandKind =
        CompletionCommandKind::Module;
    snapshotMetadataModuleCommandQuery.prefix = QStringLiteral("semantic");
    expectList("snapshot metadata command module",
               snapshotCompletionService.findCommandCompletions(
                   snapshotMetadataModuleCommandQuery),
               {"semantic_scope"});
    CommandCompletionQuery snapshotTaskCommandQuery;
    snapshotTaskCommandQuery.commandKind = CompletionCommandKind::Task;
    snapshotTaskCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command task names",
               snapshotCompletionService.findCommandCompletions(snapshotTaskCommandQuery),
               {"snap_task"});
    const QList<SemanticSymbolRecord> snapshotTaskCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotTaskCommandQuery);
    ++g_checks;
    const bool snapshotTaskCommandOk = snapshotTaskCommandSymbols.size() == 1
        && snapshotTaskCommandSymbols.first().name == QStringLiteral("snap_task")
        && snapshotTaskCommandSymbols.first().collectorKind
            == SymbolTaxonomy::CollectorKind::Task
        && snapshotTaskCommandSymbols.first().owner.name.isEmpty();
    if (!snapshotTaskCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotTaskCommandOk ? "PASS" : "FAIL",
           "snapshot command task symbols",
           snapshotTaskCommandSymbols.size());
    CommandCompletionQuery snapshotModuleCommandQuery;
    snapshotModuleCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotModuleCommandQuery.commandKind = CompletionCommandKind::Module;
    snapshotModuleCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command module in scope",
               snapshotCompletionService.findCommandCompletions(snapshotModuleCommandQuery),
               {"snap_child", "snap_scope", "snap_top"});
    const QList<SemanticSymbolRecord> snapshotModuleCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotModuleCommandQuery);
    ++g_checks;
    const bool snapshotModuleCommandOk = snapshotModuleCommandSymbols.size() == 3
        && snapshotModuleCommandSymbols.first().localHandle == 7000
        && snapshotModuleCommandSymbols.at(1).localHandle == 6000
        && snapshotModuleCommandSymbols.last().localHandle == 4000;
    if (!snapshotModuleCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotModuleCommandOk ? "PASS" : "FAIL",
           "snapshot command module symbols",
           snapshotModuleCommandSymbols.size());
    CommandCompletionQuery snapshotInterfaceCommandQuery;
    snapshotInterfaceCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotInterfaceCommandQuery.commandKind =
        CompletionCommandKind::Interface;
    snapshotInterfaceCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command interface in scope",
               snapshotCompletionService.findCommandCompletions(snapshotInterfaceCommandQuery),
               {"snap_if"});
    const QList<SemanticSymbolRecord> snapshotInterfaceCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotInterfaceCommandQuery);
    ++g_checks;
    const bool snapshotInterfaceCommandOk = snapshotInterfaceCommandSymbols.size() == 1
        && snapshotInterfaceCommandSymbols.first().localHandle == 4002;
    if (!snapshotInterfaceCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotInterfaceCommandOk ? "PASS" : "FAIL",
           "snapshot command interface symbols",
           snapshotInterfaceCommandSymbols.size());
    CommandCompletionQuery snapshotPackageCommandQuery;
    snapshotPackageCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotPackageCommandQuery.commandKind = CompletionCommandKind::Package;
    snapshotPackageCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command package in scope",
               snapshotCompletionService.findCommandCompletions(snapshotPackageCommandQuery),
               {"snap_pkg"});
    const QList<SemanticSymbolRecord> snapshotPackageCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotPackageCommandQuery);
    ++g_checks;
    const bool snapshotPackageCommandOk = snapshotPackageCommandSymbols.size() == 1
        && snapshotPackageCommandSymbols.first().localHandle == 4004;
    if (!snapshotPackageCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotPackageCommandOk ? "PASS" : "FAIL",
           "snapshot command package symbols",
           snapshotPackageCommandSymbols.size());
    CommandCompletionQuery snapshotSemanticPackageParamQuery;
    snapshotSemanticPackageParamQuery.moduleName = QStringLiteral("snap_top");
    snapshotSemanticPackageParamQuery.commandKind =
        CompletionCommandKind::Parameter;
    snapshotSemanticPackageParamQuery.prefix = QStringLiteral("semantic");
    expectList("snapshot semantic package parameter command",
               snapshotCompletionService.findCommandCompletions(
                   snapshotSemanticPackageParamQuery),
               {"semantic_pkg_param"});
    CommandCompletionQuery snapshotDefineCommandQuery;
    snapshotDefineCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotDefineCommandQuery.commandKind = CompletionCommandKind::Macro;
    snapshotDefineCommandQuery.prefix = QStringLiteral("SNAP");
    expectList("snapshot command define in scope",
               snapshotCompletionService.findCommandCompletions(snapshotDefineCommandQuery),
               {"SNAP_FEATURE"});
    const QList<SemanticSymbolRecord> snapshotDefineCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(
            snapshotDefineCommandQuery);
    ++g_checks;
    const bool snapshotDefineCommandOk = snapshotDefineCommandSymbols.size() == 1
        && snapshotDefineCommandSymbols.first().localHandle == 4003;
    if (!snapshotDefineCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotDefineCommandOk ? "PASS" : "FAIL",
           "snapshot command define symbols",
           snapshotDefineCommandSymbols.size());
    CommandCompletionQuery snapshotEnumCommandQuery;
    snapshotEnumCommandQuery.commandKind = CompletionCommandKind::EnumType;
    snapshotEnumCommandQuery.prefix = QStringLiteral("snap");
    expectList("snapshot command enum typedef",
               snapshotCompletionService.findCommandCompletions(snapshotEnumCommandQuery),
               {"snap_state_t"});
    snapshotEnumCommandQuery.moduleName = QStringLiteral("snap_top");
    expectList("snapshot command local enum typedef",
               snapshotCompletionService.findCommandCompletions(snapshotEnumCommandQuery),
               {"snap_local_state_t"});
    CompletionQuery snapshotMemberQuery;
    snapshotMemberQuery.structTypeNameForMember = QStringLiteral("snap_pixel_t");
    expectList("snapshot struct members",
               snapshotCompletionService.findCompletions(snapshotMemberQuery),
               {"red", "green", "blue"});
    snapshotMemberQuery.prefix = QStringLiteral("bl");
    const CompletionResult snapshotMemberCompletion =
        snapshotCompletionService.findCompletionResult(snapshotMemberQuery);
    expectList("snapshot struct member prefix",
               snapshotCompletionService.findCompletions(snapshotMemberQuery),
               {"blue"});
    ++g_checks;
    const bool snapshotMemberSymbolOk = snapshotMemberCompletion.items.size() == 1
        && snapshotMemberCompletion.items.first().label == QStringLiteral("blue")
        && snapshotMemberCompletion.items.first().symbolRecord.collectorKind
            == SymbolTaxonomy::CollectorKind::StructMember
        && snapshotMemberCompletion.items.first().symbolRecord.owner.name
            == QStringLiteral("snap_pixel_t");
    if (!snapshotMemberSymbolOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotMemberSymbolOk ? "PASS" : "FAIL",
           "snapshot struct member symbols",
           snapshotMemberCompletion.items.size());
    const int snapshotScopeCursor =
        snapshotScopeContent.indexOf(QStringLiteral("snap_signal")) + 2;
    expectEq("snapshot current module",
             snapshotCompletionService.currentModuleAt(snapshotScopeFile, snapshotScopeCursor),
             "snap_scope");
    const int semanticModuleScopeCursor =
        semanticModuleScopeContent.indexOf(QStringLiteral("semantic_signal")) + 2;
    expectEq("snapshot semantic metadata current module",
             snapshotCompletionService.currentModuleAt(semanticModuleScopeFile,
                                                       semanticModuleScopeCursor),
             "semantic_scope");
    expectList("snapshot semantic metadata scope names",
               snapshotIndex.getScopeSymbolNames(semanticModuleScopeFile, 2),
               {"semantic_scope", "semantic_signal", "semantic_metadata_signal"});
    CompletionQuery snapshotScopeQuery;
    snapshotScopeQuery.prefix = QStringLiteral("snap");
    snapshotScopeQuery.fileName = snapshotScopeFile;
    snapshotScopeQuery.cursorLine = 2;
    expectList("snapshot scope completions",
               snapshotCompletionService.findScopeCompletions(snapshotScopeQuery),
               {"snap_scope", "snap_signal"});
    SemanticQueryContext snapshotFacadeQuery;
    snapshotFacadeQuery.prefix = QStringLiteral("snap");
    snapshotFacadeQuery.fileName = snapshotScopeFile;
    snapshotFacadeQuery.cursorLine = 2;
    expectList("SemanticIndex snapshot completions",
               snapshotIndex.findCompletions(snapshotFacadeQuery),
               {"snap_scope", "snap_signal"});
    expectList("snapshot child completions",
               snapshotCompletionService.findModuleChildCompletions(
                   QStringLiteral("snap_top"),
                   QStringLiteral("snap")),
               {"snap_enable"});
    expectList("snapshot related completions",
               snapshotCompletionService.findRelatedSymbolCompletions(
                   QStringLiteral("snap_enable"),
                   QStringLiteral("snap_other")),
               {"snap_other_enable"});
    expectList("snapshot reference completions",
               snapshotCompletionService.findSymbolReferenceCompletions(
                   QStringLiteral("snap_enable"),
                   QStringLiteral("snap_other")),
               {"snap_other_enable"});
    ++g_checks;
    const bool snapshotRelationshipsAvailable =
        snapshotCompletionService.relationshipCompletionsAvailable();
    if (!snapshotRelationshipsAvailable)
        ++g_fails;
    printf("[%s] %-34s\n",
           snapshotRelationshipsAvailable ? "PASS" : "FAIL",
           "snapshot relationship availability");
    expectList("snapshot clock completions",
               snapshotCompletionService.findClockDomainCompletions(QStringLiteral("snap_c")),
               {"snap_clk"});
    expectList("snapshot reset completions",
               snapshotCompletionService.findResetSignalCompletions(QStringLiteral("snap_r")),
               {"snap_rst_n"});
    expectList("snapshot module internal variables",
               snapshotCompletionService.findModuleInternalVariableCompletions(
                   QStringLiteral("snap_top"),
                   QStringLiteral("snap_")),
               {"snap_clk", "snap_enable", "snap_rst_n"});
    expectList("snapshot module symbols by type",
               snapshotCompletionService.findModuleSymbolsByKind(
                   QStringLiteral("snap_top"),
                   CompletionCommandKind::Logic,
                   QStringLiteral("snap_e")),
               {"snap_enable"});
    expectList("snapshot metadata module symbols by type",
               snapshotCompletionService.findModuleSymbolsByKind(
                   QStringLiteral("snap_top"),
                   CompletionCommandKind::Logic,
                   QStringLiteral("semantic")),
               {"semantic_top_signal"});
    expectList("snapshot global symbol names",
               snapshotCompletionService.findGlobalSymbolCompletions(QStringLiteral("snap")),
               {"snap_child", "snap_if", "snap_pkg", "snap_scope", "snap_task", "snap_top"});
    expectList("snapshot global symbols by type",
               snapshotCompletionService.findGlobalSymbolsByKind(
                   CompletionCommandKind::Task,
                   QStringLiteral("snap")),
               {"snap_task"});
    expectList("snapshot metadata global symbols by type",
               snapshotCompletionService.findGlobalSymbolsByKind(
                   CompletionCommandKind::Module,
                   QStringLiteral("semantic")),
               {"semantic_scope"});
    expectList("snapshot global struct variables are not type completions",
               snapshotCompletionService.findGlobalSymbolsByKind(
                   CompletionCommandKind::PackedStructVariable,
                   QStringLiteral("snap")),
               {});
    expectList("snapshot scoped variables by type",
               snapshotCompletionService.findVariableCompletionsInScope(
                   QStringLiteral("snap_top"),
                   CompletionCommandKind::Logic,
                   QStringLiteral("snap_r")),
               {"snap_rst_n"});
    expectList("snapshot task/function completions",
               snapshotCompletionService.findTaskFunctionCompletions(QStringLiteral("snap")),
               {"snap_task"});
    expectList("snapshot instantiable modules",
               snapshotCompletionService.findInstantiableModuleCompletions(QStringLiteral("snap")),
               {"snap_child", "snap_scope", "snap_top"});
    expectList("snapshot struct member service",
               snapshotCompletionService.findStructMemberCompletions(
                   QStringLiteral("bl"),
                   QStringLiteral("snap_pixel_t")),
               {"blue"});
    expectList("snapshot enum value service",
               snapshotCompletionService.findEnumValueCompletions(
                   QStringLiteral("SNAP_"),
                   QStringLiteral("snap_top")),
               {"SNAP_IDLE", "SNAP_RUN"});
    const QList<SemanticSymbolRecord> snapshotEnumRecords =
        snapshotIndex.getSymbolRecords();
    bool snapshotEnumRecordOk = false;
    for (const SemanticSymbolRecord& record : snapshotEnumRecords) {
        if (record.collectorKind != SymbolTaxonomy::CollectorKind::EnumValue)
            continue;
        if (record.name == QStringLiteral("SNAP_IDLE")
            && record.owner.name == QStringLiteral("snap_top")) {
            snapshotEnumRecordOk = true;
            break;
        }
    }
    expectBool("snapshot enum value semantic record",
               snapshotEnumRecordOk,
               true);
    expectEq("snapshot enum variable type",
             snapshotCompletionService.findEnumTypeForVariable(
                 QStringLiteral("snap_state"),
                 QStringLiteral("snap_top")),
             "snap_top");
    ContextCompletionQuery snapshotStructContextQuery;
    snapshotStructContextQuery.prefix = QStringLiteral("bl");
    snapshotStructContextQuery.currentModule = QStringLiteral("snap_top");
    snapshotStructContextQuery.context = QStringLiteral("snap_pixel.");
    expectList("snapshot context struct members",
               snapshotCompletionService.findContextAwareCompletions(snapshotStructContextQuery),
               {"blue"});
    ContextCompletionQuery snapshotEnumContextQuery;
    snapshotEnumContextQuery.prefix = QStringLiteral("SNAP_");
    snapshotEnumContextQuery.currentModule = QStringLiteral("snap_top");
    snapshotEnumContextQuery.context = QStringLiteral("case(snap_state)");
    expectList("snapshot context enum values",
               snapshotCompletionService.findContextAwareCompletions(snapshotEnumContextQuery),
               {"SNAP_IDLE", "SNAP_RUN", "snap_enable"});
    ContextCompletionQuery snapshotGeneralContextQuery;
    snapshotGeneralContextQuery.prefix = QStringLiteral("snap_e");
    snapshotGeneralContextQuery.currentModule = QStringLiteral("snap_top");
    snapshotGeneralContextQuery.context = QStringLiteral("general");
    expectList("snapshot context general",
               snapshotCompletionService.findContextAwareCompletions(snapshotGeneralContextQuery),
               {"snap_enable", "snap_scope", "snap_state_t"});
    expectList("snapshot all-symbol completions",
               snapshotCompletionService.findAllSymbolCompletions(QStringLiteral("snap_clk")),
               {"snap_clk"});
    expectList("snapshot scored all-symbol completions",
               scoredNames(snapshotCompletionService.findScoredAllSymbolCompletions(
                   QStringLiteral("snap_clk"))),
               {"snap_clk"});
    expectList("snapshot typed symbol completions",
               snapshotCompletionService.findSymbolCompletionsByKind(
                   CompletionCommandKind::Logic,
                   QStringLiteral("snap_e")),
               {"snap_enable", "snap_other_enable"});
    const QList<SemanticSymbolRecord> snapshotTypedSymbols =
        CompletionSemanticQuery::typedSymbolRecords(
            &snapshotIndex,
            CompletionCommandKind::Logic,
            QStringLiteral("snap_e"));
    int snapshotEnableTypedCount = 0;
    bool snapshotTypedStableKeyOk = false;
    for (const SemanticSymbolRecord& record : snapshotTypedSymbols) {
        if (record.name == QStringLiteral("snap_enable")) {
            ++snapshotEnableTypedCount;
            snapshotTypedStableKeyOk = record.stableKey.isValid()
                && record.stableKey.symbolName == QStringLiteral("snap_enable");
        }
    }
    expectBool("snapshot typed stable dedupe",
               snapshotEnableTypedCount == 1 && snapshotTypedStableKeyOk,
               true);
    expectList("snapshot smart completions no relationships",
               scoredNames(snapshotCompletionService.findSmartCompletions(
                   QStringLiteral("snap_clk"),
                   QString(),
                   -1,
                   false)),
               {"snap_clk"});
    expectList("snapshot smart completions in scope",
               scoredNames(snapshotCompletionService.findSmartCompletions(
                   QStringLiteral("snap_sig"),
                   snapshotScopeFile,
                   snapshotScopeCursor,
                   true)),
               {"snap_signal"});
    SemanticIndex::getInstance()->setSnapshot(
        sharedSnapshotFromRecords(
            snapshotRecords,
            snapshotRelationships,
            QList<SemanticDiagnostic>{},
            snapshotFileContents));
    expectList("CompletionManager child delegation",
               cm->getModuleChildrenCompletions(QStringLiteral("snap_top"),
                                                QStringLiteral("snap")),
               {"snap_enable"});
    expectList("CompletionManager related delegation",
               cm->getRelatedSymbolCompletions(QStringLiteral("snap_enable"),
                                               QStringLiteral("snap_other")),
               {"snap_other_enable"});
    expectList("CompletionManager reference delegation",
               cm->getSymbolReferencesCompletions(QStringLiteral("snap_enable"),
                                                  QStringLiteral("snap_other")),
               {"snap_other_enable"});
    expectList("CompletionManager clock delegation",
               cm->getClockDomainCompletions(QStringLiteral("snap_c")),
               {"snap_clk"});
    expectList("CompletionManager reset delegation",
               cm->getResetSignalCompletions(QStringLiteral("snap_r")),
               {"snap_rst_n"});
    expectList("CompletionManager module variable delegation",
               cm->getModuleInternalVariables(QStringLiteral("snap_top"),
                                              QStringLiteral("snap_e")),
               {"snap_enable"});
    expectList("CompletionManager global delegation",
               cm->getGlobalSymbolCompletions(QStringLiteral("snap")),
               {"snap_child", "snap_if", "snap_pkg", "snap_scope", "snap_task", "snap_top"});
    expectList("CompletionManager metadata global delegation",
               cm->getGlobalSymbolCompletions(QStringLiteral("semantic")),
               {"semantic_scope"});
    expectList("CompletionManager type delegation",
               cm->getGlobalSymbolsByKind(
                   CompletionCommandKind::Task,
                   QStringLiteral("snap")),
               {"snap_task"});
    expectList("CompletionManager task/function delegation",
               cm->getTaskFunctionCompletions(QStringLiteral("snap")),
               {"snap_task"});
    expectList("CompletionManager module delegation",
               cm->getInstantiableModules(QStringLiteral("snap")),
               {"snap_child", "snap_scope", "snap_top"});
    expectList("CompletionManager context struct delegation",
               cm->getContextAwareCompletions(QStringLiteral("bl"),
                                              QStringLiteral("snap_top"),
                                              QStringLiteral("snap_pixel.")),
               {"blue"});
    expectList("CompletionManager context enum delegation",
               cm->getContextAwareCompletions(QStringLiteral("SNAP_"),
                                              QStringLiteral("snap_top"),
                                              QStringLiteral("case(snap_state)")),
               {"SNAP_IDLE", "SNAP_RUN", "snap_enable"});
    expectList("CompletionManager struct member delegation",
               cm->getStructMemberCompletions(QStringLiteral("bl"),
                                              QStringLiteral("snap_pixel_t")),
               {"blue"});
    expectList("CompletionManager all-symbol delegation",
               cm->getAllSymbolCompletions(QStringLiteral("snap_clk")),
               {"snap_clk"});
    expectList("CompletionManager scored all-symbol delegation",
               scoredNames(cm->getScoredAllSymbolMatches(QStringLiteral("snap_clk"))),
               {"snap_clk"});
    expectList("CompletionManager typed symbol delegation",
               cm->getSymbolCompletions(
                   CompletionCommandKind::Logic,
                   QStringLiteral("snap_e")),
               {"snap_enable", "snap_other_enable"});
    expectList("CompletionManager smart delegation",
               scoredNames(cm->getSmartCompletions(QStringLiteral("snap_sig"),
                                                   snapshotScopeFile,
                                                   snapshotScopeCursor)),
               {"snap_signal"});
    SemanticIndex::getInstance()->clearSnapshot();

    CommandCompletionQuery snapshotCommandQuery;
    snapshotCommandQuery.fileName = QStringLiteral("snapshot_only.sv");
    snapshotCommandQuery.moduleName = QStringLiteral("snap_top");
    snapshotCommandQuery.documentText = QStringLiteral("module snap_top;\nendmodule\n");
    snapshotCommandQuery.commandKind =
        CompletionCommandKind::PackedStructVariable;
    snapshotCommandQuery.prefix = QStringLiteral("snap");
    const QList<SemanticSymbolRecord> snapshotCommandSymbols =
        snapshotCompletionService.findCommandCompletionSymbolRecords(snapshotCommandQuery);
    ++g_checks;
    const bool snapshotCommandOk = snapshotCommandSymbols.size() == 1
        && snapshotCommandSymbols.first().name == QStringLiteral("snap_pixel")
        && snapshotCommandSymbols.first().collectorKind
            == SymbolTaxonomy::CollectorKind::PackedStructVariable
        && snapshotCommandSymbols.first().owner.name == QStringLiteral("snap_top")
        && snapshotCommandSymbols.first().type.rawTypeText
            == QStringLiteral("snap_pixel_t");
    if (!snapshotCommandOk)
        ++g_fails;
    printf("[%s] %-34s got_count=%d\n",
           snapshotCommandOk ? "PASS" : "FAIL",
           "snapshot command struct symbols",
           snapshotCommandSymbols.size());

    QTemporaryDir stagedPublicationWorkspace;
    expectBool("Workspace staged publication temp dir valid",
               stagedPublicationWorkspace.isValid(),
               true);
    const QString stagedCurrentFile =
        QDir(stagedPublicationWorkspace.path()).absoluteFilePath(
            QStringLiteral("staged_current.sv"));
    const QString stagedOpenFile =
        QDir(stagedPublicationWorkspace.path()).absoluteFilePath(
            QStringLiteral("staged_open.sv"));
    const QString stagedBackgroundFile =
        QDir(stagedPublicationWorkspace.path()).absoluteFilePath(
            QStringLiteral("staged_background.sv"));
    const QString stagedCurrentModule =
        QStringLiteral("staged_current_pub_module");
    const QString stagedOpenModule =
        QStringLiteral("staged_open_pub_module");
    const QString stagedBackgroundModule =
        QStringLiteral("staged_background_pub_module");

    auto writeTextFile = [](const QString& fileName, const QString& text) {
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        QTextStream out(&file);
        out << text;
        return true;
    };

    {
        QTemporaryDir ignoreSettingsDir;
        expectBool("Workspace ignore settings temp dir valid",
                   ignoreSettingsDir.isValid(),
                   true);
        const QSettings::Format previousSettingsFormat =
            QSettings::defaultFormat();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,
                           QSettings::UserScope,
                           ignoreSettingsDir.path());

        QTemporaryDir ignoreWorkspaceA;
        QTemporaryDir ignoreWorkspaceB;
        expectBool("Workspace ignore temp dir A valid",
                   ignoreWorkspaceA.isValid(),
                   true);
        expectBool("Workspace ignore temp dir B valid",
                   ignoreWorkspaceB.isValid(),
                   true);

        const QString rtlDir =
            QDir(ignoreWorkspaceA.path()).absoluteFilePath(
                QStringLiteral("rtl"));
        const QString generatedDir =
            QDir(ignoreWorkspaceA.path()).absoluteFilePath(
                QStringLiteral("generated"));
        expectBool("Workspace ignore child dirs created",
                   QDir().mkpath(rtlDir) && QDir().mkpath(generatedDir),
                   true);

        const QString keepFile =
            QDir(rtlDir).absoluteFilePath(QStringLiteral("keep_top.sv"));
        const QString ignoredFile =
            QDir(generatedDir).absoluteFilePath(
                QStringLiteral("ignored_top.sv"));
        const QString workspaceBFile =
            QDir(ignoreWorkspaceB.path()).absoluteFilePath(
                QStringLiteral("b_top.sv"));
        expectBool("Workspace ignore keep file created",
                   writeTextFile(
                       keepFile,
                       QStringLiteral("module keep_top; endmodule\n")),
                   true);
        expectBool("Workspace ignore generated file created",
                   writeTextFile(
                       ignoredFile,
                       QStringLiteral("module ignored_top; endmodule\n")),
                   true);
        expectBool("Workspace ignore B file created",
                   writeTextFile(
                       workspaceBFile,
                       QStringLiteral("module b_top; endmodule\n")),
                   true);

        auto normalizeTestPath = [](const QString& path) {
            return QDir::cleanPath(
                QDir::fromNativeSeparators(
                    QFileInfo(path).absoluteFilePath()));
        };
        const QString normalizedGeneratedDir =
            normalizeTestPath(generatedDir);
        const QString normalizedKeepFile = normalizeTestPath(keepFile);
        const QString normalizedIgnoredFile = normalizeTestPath(ignoredFile);
        const QString normalizedWorkspaceBFile =
            normalizeTestPath(workspaceBFile);

        const WorkspaceIgnoreReport relativeIgnoreReport =
            WorkspaceIgnoreService::getInstance()
                ->normalizeIgnoredDirectories(
                    WorkspaceIgnoreQuery{
                        ignoreWorkspaceA.path(),
                        {QStringLiteral("generated"), generatedDir}});
        expectBool("Workspace ignore service normalizes and dedupes",
                   relativeIgnoreReport.valid
                       && relativeIgnoreReport.ignoredDirectories
                              == QStringList{normalizedGeneratedDir},
                   true);

        const WorkspaceIgnoreReport outsideIgnoreReport =
            WorkspaceIgnoreService::getInstance()
                ->normalizeIgnoredDirectories(
                    WorkspaceIgnoreQuery{
                        ignoreWorkspaceA.path(),
                        {QDir::tempPath()}});
        expectBool("Workspace ignore service rejects outside path",
                   !outsideIgnoreReport.valid
                       && !outsideIgnoreReport.failureReason.isEmpty(),
                   true);

        const WorkspaceIgnoreReport rootIgnoreReport =
            WorkspaceIgnoreService::getInstance()
                ->normalizeIgnoredDirectories(
                    WorkspaceIgnoreQuery{
                        ignoreWorkspaceA.path(),
                        {ignoreWorkspaceA.path()}});
        expectBool("Workspace ignore service rejects workspace root",
                   !rootIgnoreReport.valid
                       && !rootIgnoreReport.failureReason.isEmpty(),
                   true);

        WorkspaceManager ignoreWorkspaceManager;
        expectBool("Workspace ignore open workspace A",
                   ignoreWorkspaceManager.openWorkspace(
                       ignoreWorkspaceA.path()),
                   true);
        expectBool("Workspace ignore scan workspace A",
                   waitForEventPredicate(
                       [&]() {
                           const QStringList svFiles =
                               ignoreWorkspaceManager.getSystemVerilogFiles();
                           return ignoreWorkspaceManager.workspaceEntries().size()
                                      == 1
                               && ignoreWorkspaceManager.workspaceEntries()
                                      .first()
                                      .scanComplete
                               && svFiles.contains(normalizedKeepFile)
                               && svFiles.contains(normalizedIgnoredFile);
                       },
                       3000),
                   true);

        QString ignoreError;
        expectBool("Workspace ignore manager applies generated dir",
                   ignoreWorkspaceManager.setIgnoredDirectories(
                       {generatedDir},
                       &ignoreError),
                   true);
        expectList("Workspace ignore manager stores normalized dir",
                   ignoreWorkspaceManager.ignoredDirectories(),
                   {normalizedGeneratedDir});
        expectBool("Workspace ignore manager hides generated file",
                   ignoreWorkspaceManager.getSystemVerilogFiles().contains(
                       normalizedKeepFile)
                       && !ignoreWorkspaceManager.getSystemVerilogFiles()
                               .contains(normalizedIgnoredFile),
                   true);

        const QStringList rejectedBaseline =
            ignoreWorkspaceManager.ignoredDirectories();
        expectBool("Workspace ignore manager rejects outside dir",
                   !ignoreWorkspaceManager.setIgnoredDirectories(
                       {QDir::tempPath()},
                       &ignoreError)
                       && !ignoreError.isEmpty(),
                   true);
        expectList("Workspace ignore reject preserves dirs",
                   ignoreWorkspaceManager.ignoredDirectories(),
                   rejectedBaseline);

        expectBool("Workspace ignore open workspace B",
                   ignoreWorkspaceManager.openWorkspace(
                       ignoreWorkspaceB.path()),
                   true);
        expectBool("Workspace ignore scan workspace B",
                   waitForEventPredicate(
                       [&]() {
                           return ignoreWorkspaceManager.activeWorkspaceIndex()
                                      == 1
                               && ignoreWorkspaceManager.workspaceEntries().size()
                                      == 2
                               && ignoreWorkspaceManager.workspaceEntries()
                                      .at(1)
                                      .scanComplete
                               && ignoreWorkspaceManager.getSystemVerilogFiles()
                                      .contains(normalizedWorkspaceBFile);
                       },
                       3000),
                   true);
        expectBool("Workspace ignore switch back to A",
                   ignoreWorkspaceManager.switchWorkspace(0),
                   true);
        expectList("Workspace ignore cached switch restores dirs",
                   ignoreWorkspaceManager.ignoredDirectories(),
                   {normalizedGeneratedDir});
        expectBool("Workspace ignore cached switch filters files",
                   ignoreWorkspaceManager.getSystemVerilogFiles().contains(
                       normalizedKeepFile)
                       && !ignoreWorkspaceManager.getSystemVerilogFiles()
                               .contains(normalizedIgnoredFile),
                   true);

        expectBool("Workspace ignore clear succeeds",
                   ignoreWorkspaceManager.setIgnoredDirectories(
                       QStringList(),
                       &ignoreError),
                   true);
        expectList("Workspace ignore clear resets dirs",
                   ignoreWorkspaceManager.ignoredDirectories(),
                   QStringList());
        expectBool("Workspace ignore clear restores hidden file",
                   ignoreWorkspaceManager.getSystemVerilogFiles().contains(
                       normalizedKeepFile)
                       && ignoreWorkspaceManager.getSystemVerilogFiles()
                              .contains(normalizedIgnoredFile),
                   true);

        QSettings::setDefaultFormat(previousSettingsFormat);
    }

    expectBool("Workspace staged current file created",
               writeTextFile(
                   stagedCurrentFile,
                   QStringLiteral("module staged_current_pub_module; logic a; endmodule\n")),
               true);
    expectBool("Workspace staged open file created",
               writeTextFile(
                   stagedOpenFile,
                   QStringLiteral("module staged_open_pub_module; logic o; endmodule\n")),
               true);
    expectBool("Workspace staged background file created",
               writeTextFile(
                   stagedBackgroundFile,
                   QStringLiteral("module staged_background_pub_module; logic b; endmodule\n")),
               true);

    auto snapshotContainsModule =
        [](std::shared_ptr<const SemanticIndexSnapshot> snapshot,
           const QString& moduleName) {
            if (!snapshot)
                return false;
            for (const SemanticSymbolRecord& record
                 : snapshot->getSymbolRecords()) {
                if (record.name == moduleName
                    && record.declarationKind
                        == SymbolTaxonomy::DeclarationKind::Module) {
                    return true;
                }
            }
            return false;
        };

    ProjectSnapshot stagedProject;
    stagedProject.workspaceRoot = stagedPublicationWorkspace.path();
    stagedProject.systemVerilogFiles = {
        stagedCurrentFile,
        stagedOpenFile,
        stagedBackgroundFile
    };
    stagedProject.includeDirs = {stagedPublicationWorkspace.path()};
    SymbolAnalyzer stagedAnalyzer;
    stagedAnalyzer.setWorkspacePriorityPublicationCheckpoints({1, 2});
    bool sawCurrentStageSnapshot = false;
    bool currentStageHasCurrent = false;
    bool currentStageOmitsOpen = false;
    bool currentStageOmitsBackground = false;
    bool sawOpenStageSnapshot = false;
    bool openStageHasCurrent = false;
    bool openStageHasOpen = false;
    bool openStageOmitsBackground = false;
    QObject::connect(&stagedAnalyzer,
                     &SymbolAnalyzer::batchProgress,
                     &stagedAnalyzer,
                     [&](int filesDone,
                         int totalFiles,
                         const QString& currentFileName) {
                         if (filesDone != 1
                             && filesDone != 2) {
                             return;
                         }
                         if (totalFiles != 3)
                             return;
                         const auto snapshot =
                             SemanticIndex::getInstance()->snapshot();
                         if (filesDone == 1
                             && currentFileName == stagedCurrentFile) {
                             sawCurrentStageSnapshot = true;
                             currentStageHasCurrent =
                                 snapshotContainsModule(snapshot,
                                                        stagedCurrentModule);
                             currentStageOmitsOpen =
                                 !snapshotContainsModule(snapshot,
                                                         stagedOpenModule);
                             currentStageOmitsBackground =
                                 !snapshotContainsModule(snapshot,
                                                         stagedBackgroundModule);
                         }
                         if (filesDone == 2
                             && currentFileName == stagedOpenFile) {
                             sawOpenStageSnapshot = true;
                             openStageHasCurrent =
                                 snapshotContainsModule(snapshot,
                                                        stagedCurrentModule);
                             openStageHasOpen =
                                 snapshotContainsModule(snapshot,
                                                        stagedOpenModule);
                             openStageOmitsBackground =
                                 !snapshotContainsModule(snapshot,
                                                         stagedBackgroundModule);
                         }
                     });
    stagedAnalyzer.analyzeProject(stagedProject);
    expectBool("Workspace current band publishes symbols early",
               sawCurrentStageSnapshot
                   && currentStageHasCurrent
                   && currentStageOmitsOpen
                   && currentStageOmitsBackground,
               true);
    expectBool("Workspace open band publishes symbols next",
               sawOpenStageSnapshot
                   && openStageHasCurrent
                   && openStageHasOpen
                   && openStageOmitsBackground,
               true);
    const auto finalStagedSnapshot = SemanticIndex::getInstance()->snapshot();
    expectBool("Workspace final publication includes background symbols",
               snapshotContainsModule(finalStagedSnapshot,
                                      stagedCurrentModule)
                   && snapshotContainsModule(finalStagedSnapshot,
                                             stagedOpenModule)
                   && snapshotContainsModule(finalStagedSnapshot,
                                             stagedBackgroundModule),
               true);

    SemanticIndex::getInstance()->clearSnapshot();
    QTemporaryDir cancelWorkspace;
    expectBool("Workspace cancel temp dir valid",
               cancelWorkspace.isValid(),
               true);
    const QString cancelFirstFile =
        QDir(cancelWorkspace.path()).absoluteFilePath(
            QStringLiteral("cancel_first.sv"));
    const QString cancelSecondFile =
        QDir(cancelWorkspace.path()).absoluteFilePath(
            QStringLiteral("cancel_second.sv"));
    expectBool("Workspace cancel first file created",
               writeTextFile(
                   cancelFirstFile,
                   QStringLiteral("module cancel_first_pub_module; logic a; endmodule\n")),
               true);
    expectBool("Workspace cancel second file created",
               writeTextFile(
                   cancelSecondFile,
                   QStringLiteral("module cancel_second_pub_module; logic b; endmodule\n")),
               true);

    ProjectSnapshot cancelProject;
    cancelProject.workspaceRoot = cancelWorkspace.path();
    cancelProject.systemVerilogFiles = {
        cancelFirstFile,
        cancelSecondFile
    };
    cancelProject.includeDirs = {cancelWorkspace.path()};
    SymbolAnalyzer cancelAnalyzer;
    bool cancelExpired = false;
    int cancelCompletionSymbols = -1;
    QObject::connect(&cancelAnalyzer,
                     &SymbolAnalyzer::workspaceAnalysisExpired,
                     &cancelAnalyzer,
                     [&cancelExpired]() {
                         cancelExpired = true;
                     });
    QObject::connect(&cancelAnalyzer,
                     &SymbolAnalyzer::analysisCompleted,
                     &cancelAnalyzer,
                     [&cancelCompletionSymbols](
                         const QString&,
                         int symbolsFound) {
                         cancelCompletionSymbols = symbolsFound;
                     });
    int cancelChecks = 0;
    cancelAnalyzer.analyzeProject(cancelProject, [&cancelChecks]() {
        ++cancelChecks;
        return cancelChecks >= 3;
    });
    expectBool("Workspace cancellation expires before publication",
               cancelExpired
                   && cancelCompletionSymbols == 0
                   && !SemanticIndex::getInstance()->snapshot(),
               true);
    expectBool("Workspace cancellation provider reached Slang boundary",
               cancelChecks >= 3,
               true);
    int slangSymbolCancelChecks = 0;
    SlangManager cancelSlangSymbols;
    const QList<SemanticSymbolRecord> cancelledWorkspaceRecords =
        cancelSlangSymbols.extractWorkspaceSymbolRecords(
            cancelProject.systemVerilogFiles,
            cancelProject.includeDirs,
            QHash<QString, QString>{},
            [&slangSymbolCancelChecks]() {
                ++slangSymbolCancelChecks;
                return slangSymbolCancelChecks >= 8;
            });
    expectBool("Workspace Slang symbols honor cancel boundary",
               cancelledWorkspaceRecords.isEmpty()
                   && slangSymbolCancelChecks >= 8,
               true);

    const QString cancelDiagnosticFile =
        QDir(cancelWorkspace.path()).absoluteFilePath(
            QStringLiteral("cancel_diagnostic.sv"));
    expectBool("Workspace cancel diagnostic file created",
               writeTextFile(
                   cancelDiagnosticFile,
                   QStringLiteral("module cancel_bad(input logic a\nendmodule\n")),
               true);
    SlangManager diagnosticsProbe;
    const QList<SemanticDiagnostic> uncancelledDiagnostics =
        diagnosticsProbe.extractWorkspaceDiagnostics(
            QStringList{cancelDiagnosticFile},
            cancelProject.includeDirs);
    expectBool("Workspace diagnostic fixture produces diagnostics",
               !uncancelledDiagnostics.isEmpty(),
               true);
    int slangDiagnosticCancelChecks = 0;
    SlangManager cancelSlangDiagnostics;
    const QList<SemanticDiagnostic> cancelledWorkspaceDiagnostics =
        cancelSlangDiagnostics.extractWorkspaceDiagnostics(
            QStringList{cancelDiagnosticFile},
            cancelProject.includeDirs,
            QHash<QString, QString>{},
            [&slangDiagnosticCancelChecks]() {
                ++slangDiagnosticCancelChecks;
                return slangDiagnosticCancelChecks >= 5;
            });
    expectBool("Workspace Slang diagnostics honor cancel boundary",
               cancelledWorkspaceDiagnostics.isEmpty()
                   && slangDiagnosticCancelChecks >= 5,
               true);

    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
