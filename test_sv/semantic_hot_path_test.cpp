#include <QtWidgets>
#include <QtTest>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

#define private public
#include "semanticindexsnapshot.h"
#include "semanticdependencygraph.h"
#undef private
#include "completionsemanticquery.h"
#include "completionservice.h"
#include "completionmodel.h"
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
                   && next.m_symbolsByFile.isSharedWith(snapshot.m_symbolsByFile)
                   && next.m_relationshipsByOwner.isSharedWith(snapshot.m_relationshipsByOwner));
    };
    const auto scoped = snapshot.withReplacedDiagnostics(
        {QDir::toNativeSeparators(QFileInfo(file).path() + "/./" + QFileInfo(file).fileName())},
        {replacement});
    expect("scoped replacement preserves other and global diagnostics",
           scoped.diagnostics().size() == 3 && scoped.getDiagnostics(file).size() == 1
               && scoped.getDiagnostics(file).first().message == "replacement"
               && scoped.getDiagnostics(file + ".other").first().message == "old second"
               && scoped.rawDiagnostics().first().message == "global");
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

void testBoundedModuleTemplates(const QString& file)
{
    QString source;
    for (int i = 0; i < 80; ++i) {
        source += QStringLiteral(
            "module module_%1 #(parameter int W = 8)"
            "(input logic [W-1:0] data, output logic valid); endmodule\n")
                      .arg(i, 3, 10, QLatin1Char('0'));
    }
    install(file, source);
    auto* index = SemanticIndex::getInstance();
    auto candidates = index->getSymbolRecordsByDeclarationKind(
        SymbolTaxonomy::DeclarationKind::Module);
    expect("module popup fixture exceeds visible cap", candidates.size() == 80);
    if (candidates.size() != 80)
        return;
    candidates.append(candidates.first());
    CompletionService service;
    CompletionModel model;
    for (const QString prefix : {QString(), QStringLiteral("module_"),
                                 QStringLiteral("module_079")}) {
        // Compare the retained rows with the eager service result, including
        // its original score ties and first-wins duplicate policy.
        QList<CompletionModel::CompletionItem> expected;
        CompletionModel::CompletionItem header;
        header.score = 1000;
        expected.append(header);
        QSet<QString> seen;
        for (const auto& record : candidates) {
            const auto payload = service.commandSymbolCompletionItem(
                record, CompletionCommandKind::Module, prefix);
            if (seen.contains(payload.uniqueKey))
                continue;
            seen.insert(payload.uniqueKey);
            CompletionModel::CompletionItem item;
            item.text = payload.text;
            item.score = payload.score;
            item.defaultValue = payload.defaultValue;
            item.templateSlots = payload.templateSlots;
            item.selectionStart = payload.selectionStart;
            item.selectionLength = payload.selectionLength;
            expected.append(item);
        }
        std::sort(expected.begin(), expected.end(), [](const auto& a, const auto& b) {
            return a.score > b.score;
        });
        expected = expected.mid(0, 32);
        model.updateSymbolRecordCompletions(candidates, prefix, CompletionCommandKind::Module);
        bool same = model.rowCount() == expected.size();
        for (int row = 0; same && row < expected.size(); ++row) {
            const auto actual = model.getItem(model.index(row, 0));
            const auto& eager = expected.at(row);
            if (eager.text.isEmpty()) {
                same = !actual.selectable;
                continue;
            }
            same = actual.text == eager.text && actual.score == eager.score
                && actual.defaultValue == eager.defaultValue
                && actual.selectionStart == eager.selectionStart
                && actual.selectionLength == eager.selectionLength
                && actual.templateSlots.size() == eager.templateSlots.size();
            for (int slot = 0; same && slot < eager.templateSlots.size(); ++slot) {
                same = actual.templateSlots.at(slot).start == eager.templateSlots.at(slot).start
                    && actual.templateSlots.at(slot).length == eager.templateSlots.at(slot).length;
            }
        }
        expect("bounded module popup preserves eager ranking and insertion payload", same);
    }
    auto first = model.getItem(model.firstSelectableIndex());
    expect("exact module match survives cap even at end of candidates", first.text == "module_079");
    const QString changed = QString(source).replace("data", "fresh_data");
    install(file, changed);
    model.updateSymbolRecordCompletions(
        index->getSymbolRecordsByDeclarationKind(SymbolTaxonomy::DeclarationKind::Module),
        "module_079", CompletionCommandKind::Module);
    first = model.getItem(model.firstSelectableIndex());
    expect("next popup uses newly published module ports",
           first.defaultValue.contains(".fresh_data(") && !first.defaultValue.contains(".data("));
    model.updateSymbolRecordCompletions({}, "module_079", CompletionCommandKind::Module, false);
    expect("empty module query retains non-executable no-match row", !model.firstSelectableIndex().isValid());
    index->clearSemanticState();
}

void testWideDependencyTraversal(const QString& file)
{
    QString source = QStringLiteral("module wide;\n");
    for (int i = 0; i < 10000; ++i)
        source += QStringLiteral("logic signal_%1;\n").arg(i);
    source += QStringLiteral("endmodule\n");
    QElapsedTimer timer;
    timer.start();
    const auto facts = SemanticDependencyGraph::extractFacts(file, source);
    const qint64 elapsed = timer.elapsed();
    std::printf("perf.dependency.wide_10000_declarations_ms=%lld\n",
                static_cast<long long>(elapsed));
    expect("wide dependency traversal preserves every identifier",
           !facts.parseError && facts.moduleDeclarations.contains("wide")
               && facts.symbolReferences.size() == 10001
               && facts.symbolReferences.contains("signal_0")
               && facts.symbolReferences.contains("signal_9999"));
    expect("wide dependency traversal avoids repeated ancestor/sibling scans", elapsed < 2500);
}

void testFactoredDependencyQueries(const QString& root)
{
    ProjectSnapshot project;
    project.workspaceRoot = root;
    QHash<QString, QString> contents;
    for (int i = 0; i < 12; ++i) {
        const QString file = QDir(root).filePath(QStringLiteral("graph_%1.sv").arg(i));
        project.systemVerilogFiles.append(file);
        contents.insert(file, QStringLiteral(
            "module m%1; parameter SHARED = %2; typedef logic type_%3; "
            "type_%4 value; localparam V = SHARED; endmodule\n")
            .arg(i).arg(i + 1).arg(i % 4).arg((i + 1) % 4));
    }
    project.allFiles = project.systemVerilogFiles;
    auto graph = SemanticDependencyGraph::build(project, contents);
    auto matchesExpanded = [&](const SemanticDependencyGraph& candidate) {
        auto expanded = candidate;
        for (auto it = expanded.factsByFile.cbegin(); it != expanded.factsByFile.cend(); ++it)
            for (const auto& name : it->symbolReferences)
                for (const auto& provider : expanded.api.value(name))
                    expanded.addDependency(it.key(), provider, SemanticDependencyKind::TypeOrApi);
        const QList<SemanticDependencyKinds> masks{SemanticDependencyKind::All,
            SemanticDependencyKind::TypeOrApi, SemanticDependencyKind::Instantiation,
            SemanticDependencyKind::Package | SemanticDependencyKind::TypeOrApi};
        for (bool reverse : {false, true}) for (bool recursive : {false, true})
            for (auto mask : masks) for (int count : {1, 2, 12}) {
                const auto files = project.systemVerilogFiles.mid(0, count);
                QSet<QString> expected, visited;
                QStringList queue;
                for (const auto& file : files) queue.append(expanded.normalizedPath(file));
                const auto& edges = reverse ? expanded.dependents : expanded.dependencies;
                while (!queue.isEmpty()) {
                    const auto file = queue.takeFirst();
                    if (visited.contains(file)) continue;
                    visited.insert(file);
                    const auto row = edges.value(file);
                    for (auto edge = row.cbegin(); edge != row.cend(); ++edge)
                        if ((edge.value() & mask) && !expected.contains(edge.key())) {
                            expected.insert(edge.key());
                            if (recursive) queue.append(edge.key());
                        }
                }
                if (candidate.reachableFiles(files, mask, recursive, reverse)
                    != candidate.orderedFiles(expected)) return false;
            }
        return true;
    };
    expect("factored API incidence equals expanded edges for both directions, cycles and multiple roots",
           matchesExpanded(graph));
    const QString changed = project.systemVerilogFiles.at(1);
    contents[changed] = "module replacement; parameter UNIQUE = 3; endmodule\n";
    const auto updated = graph.withUpdatedFiles(project, contents, {changed});
    expect("factored API incidence preserves replacement invalidation", matchesExpanded(updated));
    expect("dependency graph updates preserve prior immutable graph", matchesExpanded(graph)
           && graph.fileFacts().value(graph.normalizedPath(changed)).apiDeclarations.contains("SHARED"));

    SemanticShardDirectory<QString, QSet<QString>> memberships;
    memberships.insert("common", {"a", "b"});
    const auto previous = memberships;
    memberships.mutate("common", [](auto& files) { files.remove("a"); files.insert("c"); return true; });
    memberships.mutate("new", [](auto& files) { files.insert("d"); return true; });
    expect("in-place membership update detaches the publication bucket and nested set",
           previous.value("common") == QSet<QString>{"a", "b"} && !previous.contains("new")
           && memberships.value("common") == QSet<QString>{"b", "c"});
    memberships.mutate("common", [](auto&) { return false; });
    expect("removing an empty membership never mutates an older publication",
           !memberships.contains("common") && previous.contains("common"));
}

void testLineCommentEdits()
{
    auto treeShape = [](const TSDocument& document) {
        QByteArray result;
        std::function<void(TSNode)> append = [&](TSNode node) {
            result += ts_node_type(node);
            result += ':' + QByteArray::number(ts_node_start_byte(node));
            result += ':' + QByteArray::number(ts_node_end_byte(node));
            result += ts_node_is_missing(node) ? '?' : ';';
            for (uint32_t i = 0; i < ts_node_child_count(node); ++i)
                append(ts_node_child(node, i));
        };
        append(document.rootNode());
        return result;
    };
    auto edit = [](TSDocument& document, QString& text, int position,
                   int removed, const QString& inserted) {
        DocumentChange change;
        change.position = position;
        change.removedLength = removed;
        change.removedText = text.mid(position, removed);
        change.insertedText = inserted;
        change.oldLength = text.size();
        change.newLength = text.size() - removed + inserted.size();
        change.startLine = text.left(position).count(QLatin1Char('\n'));
        change.startColumn = position - text.lastIndexOf(QLatin1Char('\n'), position - 1) - 1;
        change.oldEndLine = change.startLine + change.removedText.count(QLatin1Char('\n'));
        change.newEndLine = change.startLine + inserted.count(QLatin1Char('\n'));
        change.lineDelta = change.newEndLine - change.oldEndLine;
        document.applyEdit(change);
        text.replace(position, removed, inserted);
    };
    struct Case { const char* name; QString marked; int removed; QString inserted; bool fast; };
    const QList<Case> cases{
        {"comment interior", "module m; // ab|cd\nlogic x; endmodule\n", 0, " / * endmodule 中文", true},
        {"comment end", "module m; // text|\nlogic x; endmodule\n", 0, "more", true},
        {"comment at EOF", "module m; endmodule // text|", 0, "more", true},
        {"empty comment", "module m; //|\nendmodule\n", 0, "text", true},
        {"delete comment body", "module m; //|text\nendmodule\n", 4, "", true},
        {"replace comment text", "module m; // a|bc\nendmodule\n", 2, "logic x;", true},
        {"newline reparses", "module m; // a|bc\nendmodule\n", 0, "\nlogic x;", false},
        {"CR reparses", "module m; // a|bc\nendmodule\n", 0, "\r", false},
        {"slash boundary reparses", "module m; /|/ text\nendmodule\n", 1, "", false},
        {"block comment reparses", "module m; /* a|bc */ endmodule\n", 0, "d", false},
        {"continuation reparses", "module m; // a|bc\nendmodule\n", 0, "\\", false},
        {"existing backslash reparses", "module m; // a\\b|c\nendmodule\n", 0, "d", false}
    };
    for (const auto& value : cases) {
        QString text = value.marked;
        const int position = text.indexOf(QLatin1Char('|'));
        text.remove(position, 1);
        TSDocument document;
        document.setText(text);
        document.resetTextStorageMetricsForTest();
        edit(document, text, position, value.removed, value.inserted);
        const auto metrics = document.textStorageMetricsForTest();
        TSDocument fresh;
        fresh.setText(text);
        expect(value.name, document.text() == text && treeShape(document) == treeShape(fresh)
            && metrics.structurePreservingEditCount == (value.fast ? 1u : 0u)
            && metrics.syntaxParseCount == (value.fast ? 0u : 1u));
    }

    QString text = QStringLiteral("module m; // text\nlogic value; endmodule\n");
    const QString original = text;
    int position = text.indexOf(QLatin1Char('\n'));
    TSDocument document;
    document.setText(text);
    document.resetTextStorageMetricsForTest();
    for (int i = 0; i < 100; ++i)
        edit(document, text, position++, 0, QStringLiteral("a"));
    for (int i = 0; i < 100; ++i)
        edit(document, text, --position, 1, QString());
    TSDocument fresh;
    fresh.setText(original);
    expect("comment typing and deletion retain an exact live syntax tree",
           text == original && treeShape(document) == treeShape(fresh)
               && document.textStorageMetricsForTest().structurePreservingEditCount == 200
               && document.textStorageMetricsForTest().syntaxParseCount == 0);
    edit(document, text, position, 0, QStringLiteral("\nlogic added;"));
    fresh.setText(text);
    expect("structural edit after comment burst matches a fresh parse",
           treeShape(document) == treeShape(fresh));

    MyCodeEditor editor;
    editor.setPlainText(original);
    QTextCursor cursor(editor.document());
    cursor.setPosition(original.indexOf(QLatin1Char('\n')));
    editor.setTextCursor(cursor);
    QTest::keyClicks(&editor, "comment typing");
    const QString typed = editor.toPlainText();
    editor.undo();
    fresh.setText(original);
    expect("comment typing remains one undo group", editor.toPlainText() == original
        && treeShape(*editor.syntaxDocument()) == treeShape(fresh));
    editor.redo();
    fresh.setText(typed);
    expect("comment redo restores text and comment classification",
           editor.toPlainText() == typed
               && editor.syntaxDocument()->isCommentAt(original.indexOf("//") + 3)
               && treeShape(*editor.syntaxDocument()) == treeShape(fresh));
}
}
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir workspace;
    testMarkers();
    testTypeQueries(workspace.filePath("types.sv"));
    testDiagnostics(workspace.filePath("diagnostics.sv"));
    testBoundedModuleTemplates(workspace.filePath("module_templates.sv"));
    testWideDependencyTraversal(workspace.filePath("wide.sv"));
    testLineCommentEdits();
    testFactoredDependencyQueries(workspace.path());
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
