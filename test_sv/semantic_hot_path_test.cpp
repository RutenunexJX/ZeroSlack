#include <QtWidgets>
#include <QtTest>
#include <algorithm>
#include <cstdio>
#include <memory>

#define private public
#include "semanticindexsnapshot.h"
#undef private
#include "completionsemanticquery.h"
#include "completionservice.h"
#include "editorcoordinator.h"
#include "mycodeeditor.h"
#include "pinloomcodelinkstore.h"
#include "slangmanager.h"
#include "tabmanager.h"
#include "tsdocument.h"

namespace {
int checks = 0, failures = 0;
void expect(const char* label, bool ok)
{
    ++checks;
    failures += !ok;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
}
QStringList names(const QList<SemanticSymbolRecord>& records)
{
    QStringList result;
    for (const auto& record : records)
        result.append(record.name);
    return result;
}
QStringList recordKeys(const QList<SemanticSymbolRecord>& records)
{
    QStringList result;
    for (const auto& record : records)
        result.append(QStringLiteral("%1|%2|%3|%4|%5|%6")
            .arg(symbolStableKeyText(record.stableKey)).arg(record.localHandle)
            .arg(record.name, record.owner.name, record.type.rawTypeText,
                 record.type.resolvedTypeName));
    return result;
}
QString sourceText()
{
    return QStringLiteral(
        "package types_a;\n"
        "typedef struct packed { logic pkg_field; } packet_t;\n"
        "typedef enum logic { A_IDLE, A_RUN } state_t;\nendpackage\n"
        "module first;\nimport types_a::*;\n"
        "typedef struct packed { logic local_field; } local_t;\n"
        "typedef struct { int unpacked_field; } unpacked_t;\n"
        "typedef enum logic { LOCAL_IDLE, LOCAL_RUN } local_state_t;\n"
        "packet_t packet;\nstate_t state;\nlocal_t local_packet;\n"
        "unpacked_t unpacked_packet;\nlocal_state_t local_state;\nendmodule\n"
        "module second;\n"
        "typedef struct packed { logic second_field; } second_t;\n"
        "typedef enum logic { SECOND_IDLE, SECOND_RUN } second_state_t;\n"
        "second_t second_packet;\nsecond_state_t second_state;\nendmodule\n");
}
void install(const QString& file, const QString& source, bool native = false)
{
    SlangManager slang;
    auto records = slang.extractSymbolRecords(file, source);
    for (qsizetype i = 0; i < records.size(); ++i)
        records[i].localHandle = int(i) + 100;
    auto* index = SemanticIndex::getInstance();
    if (native) {
        index->updateSymbolRecordsForFile(file, records, source);
    } else {
        index->clearSemanticState();
        index->setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
            SemanticIndexSnapshot::fromSymbolRecords(records, {}, {}, {{file, source}})));
    }
}
QList<EditorAnnotation> markers(MyCodeEditor* editor)
{
    QList<EditorAnnotation> result;
    for (const auto& row : editor->annotationLayerReportForTest().annotations)
        if (row.annotation.kind == EditorAnnotationKind::PinloomLink)
            result.append(row.annotation);
    return result;
}
void drain(EditorCoordinator& coordinator)
{
    QCoreApplication::sendPostedEvents(&coordinator, QEvent::MetaCall);
}
void edit(MyCodeEditor* editor, const QString& text, bool atStart = false)
{
    QTextCursor cursor(editor->document());
    cursor.movePosition(atStart ? QTextCursor::Start : QTextCursor::End);
    cursor.insertText(text);
}
void testMarkers()
{
    QTemporaryDir workspace, otherWorkspace;
    PinloomCodeLinkStore store;
    store.setWorkspaceRoot(workspace.path());
    QWidget host;
    auto* layout = new QVBoxLayout(&host);
    auto* tabs = new QTabWidget(&host);
    layout->addWidget(tabs);
    TabManager manager(tabs);
    manager.enableSplitLayout(&host);
    EditorCoordinator coordinator(&manager);
    coordinator.connectSignals();
    coordinator.setPinloomCodeLinkStore(&store);
    manager.createNewTab();
    auto* editor = manager.getCurrentEditor();
    const QString file = workspace.filePath("top.sv");
    const QString source = "module top;\nlogic a;\nassign a = 1'b0;\nendmodule\n";
    editor->setDocumentFileName(file);
    editor->setPlainText(source);
    drain(coordinator);
    editor->resetHotPathMetricsForTest();
    edit(editor, " ");
    expect("document edit schedules actual marker callback",
           editor->property("pinloomCodeLinkRefreshPending").toBool());
    const auto direct = editor->hotPathMetricsForTest().fullTextMaterializations;
    drain(coordinator);
    expect("empty store refresh does not materialize text",
           editor->hotPathMetricsForTest().fullTextMaterializations == direct);
    expect("empty refresh clears pending flag",
           !editor->property("pinloomCodeLinkRefreshPending").toBool());
    TSDocument syntax;
    syntax.setText(source);
    const auto selection = PinloomSourceSelection::fromSyntaxAnchor(
        workspace.path(), file, source, syntax.bindableCodeAnchorAt(source.indexOf("assign")));
    expect("syntax anchor can be linked", selection.isValid());
    QString failure;
    expect("adding first link updates attached editor", store.addLink(
        selection, QUrl("pinloom://entry/anchor:test?resource=r&anchor=a"),
        "Assignment", {{"resourceId", "r"}, {"anchorId", "a"}}, &failure)
        && markers(editor).size() == 1);
    const int originalStart = markers(editor).value(0).range.startPosition;
    editor->resetHotPathMetricsForTest();
    edit(editor, "// moved\n", true);
    edit(editor, " ");
    drain(coordinator);
    expect("linked edits coalesce into one text materialization",
           editor->hotPathMetricsForTest().fullTextMaterializations == 1);
    expect("existing anchor relocates after editing", markers(editor).size() == 1
           && markers(editor).first().range.startPosition == originalStart + 9);
    coordinator.togglePinloomCodeLinkMarkers();
    expect("marker toggle hides annotations", markers(editor).isEmpty());
    editor->resetHotPathMetricsForTest();
    edit(editor, " ");
    drain(coordinator);
    expect("disabled markers do not read text",
           editor->hotPathMetricsForTest().fullTextMaterializations == 0);
    coordinator.togglePinloomCodeLinkMarkers();
    expect("marker toggle restores existing link", markers(editor).size() == 1);
    editor->setDocumentFileName(workspace.filePath("unrelated.sv"));
    expect("rename away clears old marker", markers(editor).isEmpty());
    editor->resetHotPathMetricsForTest();
    edit(editor, " ");
    drain(coordinator);
    expect("unrelated link refresh does not read text",
           editor->hotPathMetricsForTest().fullTextMaterializations == 0);
    editor->setDocumentFileName(file);
    expect("rename back restores marker", markers(editor).size() == 1);
    expect("predicate preserves workspace-relative file identity",
           store.hasLinksForDocument(workspace.filePath("./top.sv"))
               && !store.hasLinksForDocument(workspace.filePath("different.sv")));
    store.setWorkspaceRoot(otherWorkspace.path());
    expect("workspace change clears old annotations", markers(editor).isEmpty());
    store.setWorkspaceRoot(workspace.path());
    expect("workspace reload restores stored link", markers(editor).size() == 1);
    editor->setPlainText("module top;\nendmodule\n");
    drain(coordinator);
    expect("unresolved anchor retains bounded fallback marker",
           markers(editor).size() == 1
               && markers(editor).first().range.startPosition < editor->cachedDocumentLength()
               && markers(editor).first().detail.contains("not currently resolved"));
    QFile metadata(store.storagePath());
    expect("last link can be removed from stored fixture",
           metadata.open(QIODevice::WriteOnly | QIODevice::Truncate)
               && metadata.write("{\"version\":2,\"anchors\":[]}") > 0);
    metadata.close();
    PinloomCodeLinkStore reloaded;
    reloaded.setWorkspaceRoot(workspace.path());
    coordinator.setPinloomCodeLinkStore(&reloaded);
    expect("removing last link clears stale fallback", markers(editor).isEmpty());
    editor->resetHotPathMetricsForTest();
    edit(editor, " ");
    drain(coordinator);
    expect("last-link removal restores no-text path", !reloaded.hasLinksForDocument(file)
           && editor->hotPathMetricsForTest().fullTextMaterializations == 0);
    coordinator.setPinloomCodeLinkStore(nullptr);
}
void testTypeQueries(const QString& file)
{
    auto* index = SemanticIndex::getInstance();
    const QString source = sourceText();
    install(file, source);
    CommandCompletionQuery query;
    query.fileName = file;
    query.documentText = source;
    query.moduleName = "first";
    query.cursorLine = 14;
    query.cursorPosition = source.indexOf("endmodule");
    const QList<CompletionCommandKind> kinds{
        CompletionCommandKind::PackedStructType, CompletionCommandKind::UnpackedStructType,
        CompletionCommandKind::EnumType, CompletionCommandKind::VisibleSymbol,
        CompletionCommandKind::Module, CompletionCommandKind::VisibleSymbol};
    auto compare = [&] {
        const auto groups = CompletionSemanticQuery::commandSymbolRecordGroups(index, query, kinds);
        bool same = groups.size() == kinds.size();
        for (qsizetype i = 0; same && i < kinds.size(); ++i) {
            auto single = query;
            single.commandKind = kinds.at(i);
            same = recordKeys(groups.at(i))
                == recordKeys(CompletionSemanticQuery::commandSymbolRecords(index, single));
        }
        return same;
    };
    expect("batched mixed kinds retain per-kind order and duplicates", compare());
    CompletionService service;
    auto members = names(service.findVisibleStructMemberRecords(query));
    auto enums = names(service.findVisibleEnumValueRecords(query));
    expect("local and imported struct members remain available",
           members.contains("local_field") && members.contains("pkg_field")
               && members.contains("unpacked_field") && !members.contains("second_field"));
    expect("local and imported enum values remain available",
           enums.contains("A_IDLE") && enums.contains("LOCAL_RUN")
               && !enums.contains("SECOND_IDLE"));
    query.moduleName = "second";
    query.cursorLine = source.count('\n');
    expect("module change prepares new query scope", compare());
    members = names(service.findVisibleStructMemberRecords(query));
    expect("second module does not inherit first module types",
           members.contains("second_field") && !members.contains("local_field"));
    query.moduleName.clear(); query.packageName = "types_a";
    expect("package-only batch retains package semantics", compare());
    query.packageName.clear();
    expect("global batch retains kind-specific empty scopes", compare());
    query.moduleName = "first"; query.cursorLine = 5;
    expect("cursor before import preserves per-kind visibility", compare());
    query.cursorLine = 14; query.prefix = "p";
    expect("prefix applies independently to each kind", compare());
    expect("null index preserves group positions",
           CompletionSemanticQuery::commandSymbolRecordGroups(nullptr, query, kinds).size() == kinds.size());
    expect("empty kind list produces no work",
           CompletionSemanticQuery::commandSymbolRecordGroups(index, query, {}).isEmpty());
    query.prefix.clear();
    const QString changed = QString(source).replace("local_field", "fresh_field")
        .replace("LOCAL_RUN", "LOCAL_FRESH");
    install(file, changed, true);
    query.documentText = changed;
    members = names(service.findVisibleStructMemberRecords(query));
    enums = names(service.findVisibleEnumValueRecords(query));
    expect("edited native records invalidate type results",
           members.contains("fresh_field") && !members.contains("local_field")
               && enums.contains("LOCAL_FRESH") && !enums.contains("LOCAL_RUN"));
    expect("shared preparation updates native file state",
           !index->contentAffectsSymbols(file, changed) && index->contentAffectsSymbols(file, source));
    index->clearSemanticState();
    expect("cleared index cannot reuse earlier candidates",
           service.findVisibleStructMemberRecords(query).isEmpty()
               && service.findVisibleEnumValueRecords(query).isEmpty());
}
QStringList relationshipKeys(const QList<SemanticRelationship>& relationships)
{
    QStringList result;
    for (const auto& relation : relationships)
        result.append(QStringLiteral("%1|%2|%3|%4|%5")
            .arg(symbolStableKeyText(relation.fromStableKey), symbolStableKeyText(relation.toStableKey))
            .arg(relation.fromId).arg(relation.toId).arg(int(relation.type)));
    return result;
}
QStringList querySignature(const SemanticIndexSnapshot& snapshot, const QString& file)
{
    QStringList result = recordKeys(snapshot.getSymbolRecords());
    result += recordKeys(snapshot.getSymbolRecords(file));
    result += snapshot.getScopeSymbolNames(file, 14);
    result += snapshot.getCachedFileContent(file);
    for (const auto& record : snapshot.symbolRecordsView()) {
        result += recordKeys(snapshot.getSymbolRecordsByName(record.name));
        result += recordKeys(snapshot.getSymbolRecordsByOwner(record.owner.name));
        result += recordKeys(snapshot.getSymbolRecordsByDeclarationKind(record.declarationKind));
        result += recordKeys({snapshot.getSymbolRecordByStableKey(record.stableKey)});
        result += recordKeys(snapshot.findDefinitionRecords(record.name));
        result += relationshipKeys(snapshot.relationshipsForStableKey(record.stableKey, true));
        result += relationshipKeys(snapshot.relationshipsForStableKey(record.stableKey, false));
    }
    return result;
}
void testDiagnostics(const QString& file)
{
    SlangManager slang;
    const QString source = sourceText();
    auto records = slang.extractSymbolRecords(file, source);
    for (qsizetype i = 0; i < records.size(); ++i)
        records[i].localHandle = int(i) + 100;
    expect("snapshot fixture has symbol identities", records.size() > 2);
    if (records.size() < 2) return;
    SemanticRelationship relation;
    relation.fromStableKey = records.first().stableKey;
    relation.toStableKey = records.last().stableKey;
    relation.fromId = -8; relation.toId = -9;
    relation.type = SymbolRelationshipEngine::CONTAINS;
    relation.evidenceText = "unchanged evidence";
    SemanticDiagnostic first, second, global, replacement;
    first.fileName = file; first.message = "old first";
    second.fileName = file + ".other"; second.message = "old second";
    global.message = "global";
    replacement = first; replacement.message = "replacement";
    const auto snapshot = SemanticIndexSnapshot::fromSymbolRecords(
        records, {relation, relation}, {first, second, global}, {{file, source}});
    expect("fixture binds and deduplicates relationships", snapshot.relationshipCount() == 1);
    const auto before = querySignature(snapshot, file);
    const auto unchanged = [&](const SemanticIndexSnapshot& next) {
        expect("non-diagnostic queries stay unchanged",
               querySignature(next, file) == before
                   && relationshipKeys(next.relationships()) == relationshipKeys(snapshot.relationships())
                   && next.fileContents() == snapshot.fileContents());
        expect("diagnostic snapshot reuses records and indexes",
               next.symbolRecordsView().constData() == snapshot.symbolRecordsView().constData()
                   && next.relationshipsView().constData() == snapshot.relationshipsView().constData()
                   && next.m_symbolRecordIndexesByFile.isSharedWith(snapshot.m_symbolRecordIndexesByFile)
                   && next.m_symbolRecordIndexesByName.isSharedWith(snapshot.m_symbolRecordIndexesByName)
                   && next.m_symbolRecordIndexesByOwner.isSharedWith(snapshot.m_symbolRecordIndexesByOwner)
                   && next.m_symbolRecordIndexesByDeclarationKind.isSharedWith(snapshot.m_symbolRecordIndexesByDeclarationKind)
                   && next.m_symbolRecordIndexByStableKey.isSharedWith(snapshot.m_symbolRecordIndexByStableKey)
                   && next.m_symbolRecordIndexByLocalHandle.isSharedWith(snapshot.m_symbolRecordIndexByLocalHandle)
                   && next.m_relationshipIndexesByFromStableKey.isSharedWith(snapshot.m_relationshipIndexesByFromStableKey)
                   && next.m_relationshipIndexesByToStableKey.isSharedWith(snapshot.m_relationshipIndexesByToStableKey));
    };
    const auto scoped = snapshot.withReplacedDiagnostics(
        {QDir::toNativeSeparators(QFileInfo(file).path() + "/./" + QFileInfo(file).fileName())},
        {replacement});
    expect("scoped replacement preserves other and global diagnostics",
           scoped.diagnostics().size() == 3 && scoped.getDiagnostics(file).size() == 1
               && scoped.getDiagnostics(file).first().message == "replacement"
               && scoped.diagnostics().first().message == "old second");
    unchanged(scoped);
    const auto cleared = snapshot.withReplacedDiagnostics({file}, {});
    expect("scoped clear removes only target diagnostics",
           cleared.diagnostics().size() == 2 && cleared.getDiagnostics(file).isEmpty());
    unchanged(cleared);
    const auto all = snapshot.withReplacedDiagnostics({}, {replacement});
    expect("empty filename list replaces all diagnostics", all.diagnostics().size() == 1);
    unchanged(all);
    const auto empty = snapshot.withReplacedDiagnostics({}, {});
    expect("empty filenames and diagnostics clear all", empty.diagnostics().isEmpty());
    unchanged(empty);
    const auto blank = snapshot.withReplacedDiagnostics({QString()}, {replacement});
    expect("nonempty list of empty filenames appends", blank.diagnostics().size() == 4);
    unchanged(blank);
    expect("original snapshot stays immutable", snapshot.diagnostics().size() == 3
           && snapshot.getDiagnostics(file).first().message == "old first"
           && querySignature(snapshot, file) == before);
    auto rebound = scoped.relationships().first();
    rebound.fromId = -1; rebound.toId = -1;
    expect("copied snapshot retains relationship handle rebinding",
           relationshipKeys({scoped.rebindRelationship(rebound)}) == relationshipKeys(snapshot.relationships()));
    const auto edited = scoped.withReplacedFiles({{file, {}, "module changed; endmodule\n"}}, {file}, {});
    expect("later file replacement detaches from earlier snapshots",
           edited.getSymbolRecords(file).isEmpty() && scoped.symbolRecordCount() == records.size()
               && querySignature(snapshot, file) == before && scoped.getCachedFileContent(file) == source);
}
}
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir workspace;
    testMarkers();
    testTypeQueries(workspace.filePath("types.sv"));
    testDiagnostics(workspace.filePath("diagnostics.sv"));
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
