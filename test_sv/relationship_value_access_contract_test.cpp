#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <cstdio>
#include "slangmanager.h"
#include "semanticindexsnapshot.h"
#include "semanticanalysisinput.h"
#include "smartrelationshipbuilder.h"
#include "signaljourneyservice.h"
#include "signalusagehotspotservice.h"

namespace {
int checks = 0, failures = 0;
void check(bool ok, const QString& label)
{
    ++checks;
    failures += !ok;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
}

bool write(const QString& path, const QString& source)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(source.toUtf8()) == source.toUtf8().size();
}

int lineOf(const QString& source, const QString& marker)
{
    const int position = source.indexOf(marker);
    return position < 0 ? -1 : source.left(position).count('\n') + 1;
}

using Edges = QVector<RelationshipToAdd>;
using Kind = SymbolRelationshipEngine::RelationType;

struct Fixture {
    SlangManager slang;
    QString file, source;
    SemanticIndexSnapshot snapshot;
    RelationshipExtractionInfo facts;
    QHash<int, SemanticSymbolRecord> records;

    bool load(const QString& root, const QString& text, const QString& include = {})
    {
        file = QDir(root).filePath("access.sv");
        source = text;
        QHash<QString, QString> contents{{file, source}};
        if (!include.isEmpty()) {
            const QString header = QDir(root).filePath("shared.svh");
            if (!write(header, include)) return false;
            contents.insert(header, include);
        }
        if (!write(file, source)) return false;
        const auto parsed = slang.analyzeOverlayWorkspace(contents, {file}, {root}, {}, {}, {true, true, true});
        check(parsed.error.isEmpty() && parsed.diagnostics.isEmpty(), "real Slang parse has zero diagnostics");
        if (!parsed.error.isEmpty() || !parsed.diagnostics.isEmpty()) return false;
        snapshot = SemanticIndexSnapshot::fromSymbolRecords(parsed.symbols, {}, {}, contents);
        for (auto it = parsed.relationships.cbegin(); it != parsed.relationships.cend(); ++it)
            if (SemanticInputCapture::pathKey(it.key()) == SemanticInputCapture::pathKey(file)) facts = it.value();
        check(!facts.assignments.isEmpty(), "real capture provides assignment facts");
        for (const auto& record : snapshot.getSymbolRecords()) records.insert(record.localHandle, record);
        return !facts.assignments.isEmpty();
    }

    Edges build(const RelationshipExtractionInfo& input)
    {
        SmartRelationshipBuilder builder(nullptr, &slang);
        return builder.computeRelationships(file, source, snapshot.getSymbolRecords(file), &snapshot, {}, {}, &input);
    }

    QSet<QString> pairs(const Edges& edges, Kind kind, int line, bool from = false)
    {
        QSet<QString> result;
        for (const auto& edge : edges) {
            if (edge.type != kind || edge.evidenceRange.line != line) continue;
            const auto record = records.value(from ? edge.fromId : edge.toId);
            result.insert(record.owner.name + ':' + (from ? edge.fromAccessPath : edge.toAccessPath));
        }
        return result;
    }

    QSet<QString> references(const QList<SemanticValueReference>& references)
    {
        QSet<QString> pairs;
        for (const auto& ref : references) {
            check(ref.hasBinding() && ref.isValid(), "captured path has a source binding");
            pairs.insert(ref.declaringScope.symbolName + ':' + ref.accessPath);
        }
        return pairs;
    }
};

void pairedCases(const QString& root, bool sharedInclude)
{
    const QString declarations =
        "typedef struct packed { logic left; logic right; } payload_t;\n"
        "typedef payload_t alias_t;\nalias_t item;\n"
        "typedef struct packed { alias_t inner; } nested_t;\nnested_t box;\n"
        "alias_t items[2];\nlogic sel;\nalias_t clk, rst;\n";
    const QString body = sharedInclude ? "`include \"shared.svh\"\n" : declarations;
    const QString source = "package p_a;\n" + body + "endpackage\npackage p_b;\n" + body +
        "endpackage\nmodule top(output logic out, output p_a::alias_t copy);\n"
        "always_comb begin\n"
        "out = p_a::item.left ^ p_b::item.right; // pair\n"
        "if (p_a::item.left ^ p_b::item.right) out = 1'b1; // condition\n"
        "out = p_a::item.left ^ p_a::item.right ^ p_a::item.left; // multiple\n"
        "out = p_a::box.inner.left ^ p_b::box.inner.right; // nested\n"
        "out = p_a::items[p_b::sel].left; // selector\n"
        "copy = p_a::item; // forward\n"
        "p_a::item.left = p_b::item.right; // lhs\n"
        "end\n"
        "always @(p_a::item.left ^ p_b::item.right) begin end // timing\n"
        "always @(p_a::item.left ^ p_a::item.right) begin end // multi-timing\n"
        "always @(posedge (p_a::clk.left ^ p_b::clk.right)) begin end // clock\n"
        "always @(p_a::rst.left ^ p_b::rst.right) begin end // reset\n"
        "endmodule\n";
    Fixture fixture;
    if (!fixture.load(root, source, sharedInclude ? declarations : QString())) return;
    const auto edges = fixture.build(fixture.facts);
    const QSet<QString> expected{"p_a:item.left", "p_b:item.right"};
    const QSet<QString> multiple{"p_a:item.left", "p_a:item.right"};
    const auto line = [&](const char* marker) { return lineOf(source, marker); };
    for (const auto& assignment : fixture.facts.assignments) {
        if (assignment.lineNumber == line("// pair")) {
            check(fixture.references(assignment.rightReferences) == expected, "assignment capture keeps two bound pairs");
            check(assignment.rightReferences.size() == 2, "different declarations with the same name survive capture");
        }
        if (assignment.lineNumber == line("// multiple")) {
            check(fixture.references(assignment.rightReferences) == multiple, "same declaration retains both member paths");
            check(assignment.rightReferences.size() == 2, "only repeated declaration/path pairs are deduplicated");
        }
    }
    for (const auto& condition : fixture.facts.conditionReferences)
        if (condition.lineNumber == line("// condition"))
            check(fixture.references(condition.references) == expected, "condition capture keeps its bound paths");
    QList<SemanticValueReference> timing;
    for (const auto& signal : fixture.facts.timingSignals)
        if (signal.lineNumber == line("// timing")) timing.append(signal.reference);
    check(fixture.references(timing) == expected && timing.size() == 2, "timing capture keeps its bound paths");

    check(fixture.pairs(edges, SymbolRelationshipEngine::REFERENCES, line("// pair")) == expected,
          "assignment builder does not cross pair declarations and paths");
    check(fixture.pairs(edges, SymbolRelationshipEngine::ASSIGNS_TO, line("// pair"), true) == expected,
          "reverse assignment edges retain the same pairing");
    check(fixture.pairs(edges, SymbolRelationshipEngine::READS_FROM, line("// condition")) == expected,
          "condition builder does not cross pair declarations and paths");
    check(fixture.pairs(edges, SymbolRelationshipEngine::READS_FROM, line("// timing")) == expected,
          "timing builder does not cross pair declarations and paths");
    check(fixture.pairs(edges, SymbolRelationshipEngine::READS_FROM, line("// multi-timing")) == multiple,
          "timing retains multiple members on the same variable");
    check(fixture.pairs(edges, SymbolRelationshipEngine::REFERENCES, line("// multiple")) == multiple,
          "assignment retains multiple members on the same variable");
    check(fixture.pairs(edges, SymbolRelationshipEngine::REFERENCES, line("// nested")) ==
              QSet<QString>{"p_a:box.inner.left", "p_b:box.inner.right"}, "nested alias paths retain their declaring package");
    check(fixture.pairs(edges, SymbolRelationshipEngine::REFERENCES, line("// selector")) ==
              QSet<QString>{"p_a:items.left", "p_b:sel"}, "member capture preserves dynamic selector reads");
    check(fixture.pairs(edges, SymbolRelationshipEngine::CLOCKS, line("// clock"), true) ==
              QSet<QString>{"p_a:clk.left", "p_b:clk.right"}, "clock consumer preserves binding/path pairing");
    check(fixture.pairs(edges, SymbolRelationshipEngine::RESETS, line("// reset"), true) ==
              QSet<QString>{"p_a:rst.left", "p_b:rst.right"}, "reset consumer preserves binding/path pairing");

    int forwarded = 0, memberWrites = 0;
    bool evidence = true, exactForward = true;
    for (const auto& edge : edges) {
        if (edge.type != SymbolRelationshipEngine::REFERENCES && edge.type != SymbolRelationshipEngine::ASSIGNS_TO
            && edge.type != SymbolRelationshipEngine::READS_FROM) continue;
        evidence &= SemanticInputCapture::pathKey(edge.evidenceRange.fileName) == SemanticInputCapture::pathKey(fixture.file)
                    && edge.evidenceRange.column > 0
                    && edge.evidenceRange.endLine >= edge.evidenceRange.line;
        if (edge.type == SymbolRelationshipEngine::ASSIGNS_TO) {
            if (edge.evidenceRange.line == line("// forward")) {
                ++forwarded;
                exactForward &= edge.exactValueForward && edge.fromAccessPath == "item" && edge.toAccessPath == "copy";
            } else exactForward &= !edge.exactValueForward;
            if (edge.evidenceRange.line == line("// lhs")) {
                ++memberWrites;
                check(edge.fromAccessPath == "item.right" && edge.toAccessPath == "item.left"
                      && fixture.records.value(edge.fromId).owner.name == "p_b"
                      && fixture.records.value(edge.toId).owner.name == "p_a", "bound left member preserves reverse edge endpoints");
            }
        }
    }
    check(evidence, "source evidence ranges survive common builder");
    check(forwarded == 1 && exactForward && memberWrites == 1, "exactValueForward remains restricted to one whole value");

    QList<SemanticRelationship> published;
    for (const auto& edge : edges) {
        SemanticRelationship relationship;
        relationship.fromId = edge.fromId;
        relationship.toId = edge.toId;
        relationship.type = edge.type;
        relationship.confidence = edge.confidence;
        relationship.evidenceText = edge.context;
        relationship.evidenceRange = edge.evidenceRange;
        relationship.fromAccessPath = edge.fromAccessPath;
        relationship.toAccessPath = edge.toAccessPath;
        relationship.exactValueForward = edge.exactValueForward;
        relationship.provenance = RelationshipProvenance::Inferred;
        published.append(relationship);
    }
    SemanticIndex index;
    index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(fixture.snapshot.withAdditionalRelationships(published)));
    SignalJourneyService journey(&index);
    SignalUsageHotspotService hotspot(&index);
    SemanticSymbolRecord subject;
    for (const auto& record : fixture.records)
        if (record.name == "item" && record.owner.name == "p_a") subject = record;
    check(subject.isValid(), "downstream reports use the real package declaration");
    for (const auto& member : {QStringLiteral("left"), QStringLiteral("right")}) {
        SignalJourneyQuery query;
        query.signalStableKey = subject.stableKey;
        query.signalName = subject.name;
        query.fileName = subject.location.fileName;
        query.signalAccessPath = "item." + member;
        const auto report = journey.buildSignalJourney(query);
        bool foundPair = false;
        for (const auto& item : report.assignments + report.drivenAssignments + report.reads)
            foundPair |= item.evidenceRange.line == line("// pair");
        check(report.found && foundPair == (member == "left"), "signal journey excludes the other member's assignment");
        SignalUsageHotspotQuery hotspotQuery;
        hotspotQuery.signalStableKey = subject.stableKey;
        hotspotQuery.signalName = subject.name;
        hotspotQuery.fileName = subject.location.fileName;
        hotspotQuery.signalAccessPath = query.signalAccessPath;
        const auto hotspots = hotspot.buildSignalUsageHotspot(hotspotQuery);
        bool foundCondition = false;
        for (const auto& item : hotspots.items)
            foundCondition |= item.evidenceRange.line == line("// condition");
        check(hotspots.found && foundCondition == (member == "left"), "hotspot report excludes the other member's condition");
        // Snapshot relationship identity coalesces repeated endpoint/type/path
        // edges. Publish the timing event separately to exercise that consumer
        // without changing the existing relationship identity/evidence policy.
        QList<SemanticRelationship> timingPublication;
        for (const auto& relationship : published)
            if (relationship.evidenceRange.line == line("// timing")) timingPublication.append(relationship);
        index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(fixture.snapshot.withAdditionalRelationships(timingPublication)));
        const auto timingHotspots = hotspot.buildSignalUsageHotspot(hotspotQuery);
        bool foundTiming = false;
        for (const auto& item : timingHotspots.items) foundTiming |= item.evidenceRange.line == line("// timing");
        check(timingHotspots.found && foundTiming == (member == "left"), "hotspot report excludes the other member's timing");
        index.setSnapshot(std::make_shared<SemanticIndexSnapshot>(fixture.snapshot.withAdditionalRelationships(published)));
    }

    // Poison the legacy projections: bound production facts must not consult them.
    auto poisoned = fixture.facts;
    for (auto& assignment : poisoned.assignments) {
        assignment.leftAccessPath = "wrong.left";
        assignment.rightAccessPaths = {"wrong.right"};
    }
    for (auto& condition : poisoned.conditionReferences) condition.symbolAccessPaths = {"wrong.condition"};
    for (auto& signal : poisoned.timingSignals) signal.signalAccessPath = "wrong.timing";
    const auto rebuilt = fixture.build(poisoned);
    check(fixture.pairs(rebuilt, SymbolRelationshipEngine::REFERENCES, line("// pair")) == expected
          && fixture.pairs(rebuilt, SymbolRelationshipEngine::ASSIGNS_TO, line("// lhs")) == QSet<QString>{"p_a:item.left"}
          && fixture.pairs(rebuilt, SymbolRelationshipEngine::READS_FROM, line("// condition")) == expected
          && fixture.pairs(rebuilt, SymbolRelationshipEngine::READS_FROM, line("// timing")) == expected,
          "bound consumers ignore stale independent path projections");
}

void compatibilityCases(const QString& root)
{
    Fixture fixture;
    const QString source = "module top(input logic a, output logic b);\n"
        "always_comb begin\nb = a;\nif (a) b = 1'b1;\nend\nalways @(a) begin end\nendmodule\n";
    if (!fixture.load(root, source)) return;
    const auto assignment = lineOf(source, "b = a"), condition = lineOf(source, "if (a)"), timing = lineOf(source, "always @(a)");
    auto legacy = fixture.facts;
    for (auto& fact : legacy.assignments) { fact.leftReference = {}; fact.rightReferences.clear(); }
    for (auto& fact : legacy.conditionReferences) fact.references.clear();
    for (auto& fact : legacy.timingSignals) fact.reference = {};
    const auto legacyEdges = fixture.build(legacy);
    check(fixture.pairs(legacyEdges, SymbolRelationshipEngine::REFERENCES, assignment) == QSet<QString>{"top:a"}
          && fixture.pairs(legacyEdges, SymbolRelationshipEngine::READS_FROM, condition) == QSet<QString>{"top:a"}
          && fixture.pairs(legacyEdges, SymbolRelationshipEngine::READS_FROM, timing) == QSet<QString>{"top:a"},
          "legacy facts with no binding retain name fallback");

    // Start with real capture; corrupt its source or path without removing binding.
    for (int mode = 0; mode < 3; ++mode) {
        auto unresolved = fixture.facts;
        auto corrupt = [mode](SemanticValueReference& ref) {
            if (mode == 0) ref.location.position += 10000;
            if (mode == 1) ref.location = {};
            if (mode == 2) ref.accessPath.clear();
        };
        for (auto& fact : unresolved.assignments) for (auto& ref : fact.rightReferences) corrupt(ref);
        for (auto& fact : unresolved.conditionReferences) for (auto& ref : fact.references) corrupt(ref);
        for (auto& fact : unresolved.timingSignals) corrupt(fact.reference);
        const auto edges = fixture.build(unresolved);
        check(fixture.pairs(edges, SymbolRelationshipEngine::REFERENCES, assignment).isEmpty()
              && fixture.pairs(edges, SymbolRelationshipEngine::READS_FROM, condition).isEmpty()
              && fixture.pairs(edges, SymbolRelationshipEngine::READS_FROM, timing).isEmpty(),
              QString("unresolved captured binding/path does not fall back to a name (case %1)").arg(mode));
    }
    auto unresolvedLeft = fixture.facts;
    for (auto& fact : unresolvedLeft.assignments) fact.leftReference.location = {};
    check(fixture.pairs(fixture.build(unresolvedLeft), SymbolRelationshipEngine::REFERENCES, assignment).isEmpty(),
          "unresolved bound assignment target does not fall back to a name");
}
} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    for (const auto& name : {"explicit", "shared", "compatibility"}) QDir(root.path()).mkdir(name);
    pairedCases(root.filePath("explicit"), false);
    pairedCases(root.filePath("shared"), true);
    compatibilityCases(root.filePath("compatibility"));
    std::printf("Value access contract: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
