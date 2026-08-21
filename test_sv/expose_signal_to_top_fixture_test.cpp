#include "exposesignaltotopservice.h"
#include "formatterservice.h"
#include "hierarchyservice.h"
#include "semanticindexsnapshot.h"
#include "slangmanager.h"
#include "tsdocument.h"

#include <rtledit/text_edit.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
int checks = 0;
int failures = 0;

void check(const QString& name, bool condition)
{
    ++checks;
    failures += condition ? 0 : 1;
    std::printf("[%s] %s\n",
                condition ? "PASS" : "FAIL",
                name.toUtf8().constData());
}

QString norm(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

QString readUtf8(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

bool writeUtf8(const QString& path, const QString& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(text.toUtf8()) == text.toUtf8().size();
}

class MemoryDocuments final : public rtledit::WorkspaceDocumentManager
{
public:
    struct Doc {
        QString text;
        std::uint64_t version = 1;
    };
    QHash<QString, Doc> docs;

    void add(const QString& path, const QString& text)
    {
        docs.insert(norm(path), {text, 1});
    }

    std::optional<rtledit::WorkspaceDocumentSnapshot> snapshot(
        const std::string& path) const override
    {
        const auto found =
            docs.constFind(norm(QString::fromStdString(path)));
        if (found == docs.constEnd())
            return std::nullopt;
        return rtledit::WorkspaceDocumentSnapshot{
            {found->version},
            found->text.toUtf8().toStdString()};
    }

    bool applyTextEdits(
        const std::string& path,
        rtledit::DocumentVersion expectedVersion,
        const std::vector<rtledit::WorkspaceTextEdit>& edits) override
    {
        auto found = docs.find(norm(QString::fromStdString(path)));
        if (found == docs.end()
            || found->version != expectedVersion.value) {
            return false;
        }
        const std::string utf8 = found->text.toUtf8().toStdString();
        const auto edited =
            rtledit::applyTextEditsToString(utf8, edits);
        if (!edited)
            return false;
        found->text = QString::fromUtf8(
            edited->data(), static_cast<qsizetype>(edited->size()));
        ++found->version;
        return true;
    }

    bool restoreSnapshot(
        const std::string& path,
        const rtledit::WorkspaceDocumentSnapshot& snapshot) override
    {
        auto found = docs.find(norm(QString::fromStdString(path)));
        if (found == docs.end())
            return false;
        found->text = QString::fromUtf8(
            snapshot.text.data(),
            static_cast<qsizetype>(snapshot.text.size()));
        found->version = snapshot.version.value;
        return true;
    }
};

struct FixtureSpec {
    QString label;
    QString originalRoot;
    QString activeTop;
    QString sourceModule;
    QStringList compilationFiles;
    QStringList editableFiles;
    QString menuProbeModule;
    QString menuProbeSignal;
};

struct FixtureWorkspace {
    QString tempRoot;
    QStringList orderedFiles;
    QStringList editableFiles;
    QStringList includeDirs;
    QHash<QString, QString> contents;
    QSet<QString> workspaceFiles;
    MemoryDocuments documents;
};

std::optional<FixtureWorkspace> copyFixture(
    const FixtureSpec& spec,
    const QString& tempRoot)
{
    FixtureWorkspace workspace;
    workspace.tempRoot = norm(tempRoot);
    workspace.includeDirs = {norm(spec.originalRoot)};
    for (const QString& relative : spec.compilationFiles) {
        const QString source =
            norm(QDir(spec.originalRoot).filePath(relative));
        const QString target =
            norm(QDir(tempRoot).filePath(relative));
        const QString text = readUtf8(source);
        if (text.isNull()
            || !QFileInfo(source).isFile()
            || !writeUtf8(target, text)) {
            std::fprintf(stderr,
                         "%s: failed to copy %s\n",
                         spec.label.toUtf8().constData(),
                         source.toUtf8().constData());
            return std::nullopt;
        }
        workspace.orderedFiles.append(target);
        workspace.contents.insert(target, text);
        workspace.documents.add(target, text);
    }
    for (const QString& relative : spec.editableFiles) {
        const QString target =
            norm(QDir(tempRoot).filePath(relative));
        if (!workspace.contents.contains(target))
            return std::nullopt;
        workspace.editableFiles.append(target);
        workspace.workspaceFiles.insert(target);
    }
    return workspace;
}

bool isModule(const SemanticSymbolRecord& record)
{
    return record.declarationKind
        == SymbolTaxonomy::DeclarationKind::Module;
}

void assignNativeLocalHandles(
    QList<SemanticSymbolRecord>* records)
{
    if (!records)
        return;
    int nextHandle = 1;
    for (SemanticSymbolRecord& record : *records) {
        if (record.localHandle < 0)
            record.localHandle = nextHandle;
        nextHandle =
            std::max(nextHandle, record.localHandle + 1);
    }
}

bool isInstance(const SemanticSymbolRecord& record)
{
    return record.collectorKind
        == SymbolTaxonomy::CollectorKind::Inst;
}

QList<SemanticRelationship> slangInstantiationRelationships(
    const QList<SemanticSymbolRecord>& records)
{
    QHash<QString, QList<SemanticSymbolRecord>> modulesByName;
    for (const SemanticSymbolRecord& record : records) {
        if (isModule(record))
            modulesByName[record.name].append(record);
    }

    QList<SemanticRelationship> result;
    for (const SemanticSymbolRecord& instance : records) {
        if (!isInstance(instance)
            || instance.owner.kind
                   != SymbolTaxonomy::SymbolOwnerScope::Module
            || instance.owner.name.isEmpty()
            || instance.type.resolvedTypeName.isEmpty()) {
            continue;
        }
        const QList<SemanticSymbolRecord> owners =
            modulesByName.value(instance.owner.name);
        if (owners.size() != 1)
            continue;
        const SemanticSymbolRecord& owner = owners.constFirst();
        SemanticRelationship relationship;
        relationship.fromId = owner.localHandle;
        relationship.toId = instance.localHandle;
        relationship.type =
            SymbolRelationshipEngine::INSTANTIATES;
        relationship.fromStableKey = owner.stableKey;
        relationship.toStableKey = instance.stableKey;
        relationship.provenance =
            RelationshipProvenance::SlangExtracted;
        relationship.confidence = 100;
        relationship.evidenceText =
            QStringLiteral("Slang-resolved module instantiation");
        relationship.evidenceRange.fileName =
            instance.location.fileName;
        relationship.evidenceRange.line =
            instance.location.startLine;
        relationship.evidenceRange.column =
            instance.location.startColumn;
        relationship.evidenceRange.endLine =
            instance.location.endLine;
        relationship.evidenceRange.endColumn =
            instance.location.endColumn;
        relationship.evidenceRange.position =
            instance.location.position;
        relationship.evidenceRange.length =
            instance.location.length;
        result.append(relationship);
    }
    return result;
}

std::shared_ptr<const SemanticIndexSnapshot> snapshot(
    const QList<SemanticSymbolRecord>& records,
    const QList<SemanticDiagnostic>& diagnostics,
    const QHash<QString, QString>& contents)
{
    return std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(
            records,
            slangInstantiationRelationships(records),
            diagnostics,
            contents));
}

bool propagatableInternal(
    SymbolTaxonomy::CollectorKind kind)
{
    using Collector = SymbolTaxonomy::CollectorKind;
    return kind == Collector::Logic
        || kind == Collector::Wire
        || kind == Collector::Reg;
}

bool usableInfo(const SemanticElaboratedSymbolInfo& info)
{
    return info.available
        && info.fixedSize
        && info.integral
        && !info.unpackedArray
        && !info.interfaceType
        && info.bitWidth > 0;
}

EditorSemanticContext contextFor(
    const FixtureSpec& spec,
    const FixtureWorkspace& workspace,
    const SemanticSymbolRecord& signal,
    const QString& instancePath)
{
    EditorSemanticContext context;
    context.fileName = norm(signal.location.fileName);
    context.moduleName = signal.owner.name;
    context.documentText =
        workspace.documents.docs.value(context.fileName).text;
    context.cursorPosition =
        signal.location.position
        + std::max(0, signal.location.length / 2);
    const int lineStart =
        context.documentText.lastIndexOf(
            QLatin1Char('\n'),
            std::max(0, context.cursorPosition - 1))
        + 1;
    int lineEnd =
        context.documentText.indexOf(
            QLatin1Char('\n'), context.cursorPosition);
    if (lineEnd < 0)
        lineEnd = context.documentText.size();
    context.lineText =
        context.documentText.mid(lineStart, lineEnd - lineStart);
    context.lineUpToCursor =
        context.documentText.mid(
            lineStart, context.cursorPosition - lineStart);
    context.cursorLine =
        context.documentText.left(lineStart)
            .count(QLatin1Char('\n'))
        + 1;
    context.column = context.cursorPosition - lineStart;
    context.documentRevision =
        workspace.documents.docs.value(context.fileName).version;
    context.hierarchyInstance = {
        workspace.tempRoot,
        spec.activeTop,
        instancePath};
    return context;
}

void runMenuAvailabilityProbe(
    const FixtureSpec& spec,
    FixtureWorkspace& workspace,
    const QList<SemanticSymbolRecord>& records,
    const DesignHierarchyReport& design,
    SemanticIndex* index,
    HierarchyService* hierarchy)
{
    if (spec.menuProbeModule.isEmpty()
        || spec.menuProbeSignal.isEmpty()) {
        return;
    }

    QList<DesignHierarchyNode> matchingNodes;
    for (const DesignHierarchyNode& node : design.nodes) {
        if (node.inSelectedTop
            && node.moduleType == spec.menuProbeModule) {
            matchingNodes.append(node);
        }
    }
    check(spec.label + QStringLiteral(" menu probe has concrete instance"),
          matchingNodes.size() == 1);
    if (matchingNodes.size() != 1)
        return;

    const DesignHierarchyNode node = matchingNodes.constFirst();
    QList<SemanticSymbolRecord> matchingSignals;
    for (const SemanticSymbolRecord& record : records) {
        if (record.name == spec.menuProbeSignal
            && record.owner.kind
                == SymbolTaxonomy::SymbolOwnerScope::Module
            && record.owner.name == spec.menuProbeModule
            && norm(record.location.fileName)
                == norm(node.definitionFile)
            && propagatableInternal(record.collectorKind)) {
            matchingSignals.append(record);
        }
    }
    check(spec.label + QStringLiteral(" menu probe finds target signal"),
          matchingSignals.size() == 1);
    if (matchingSignals.size() != 1)
        return;

    const EditorSemanticContext context =
        contextFor(spec,
                   workspace,
                   matchingSignals.constFirst(),
                   node.instancePath);
    ExposeSignalToTopService service(index, hierarchy);
    QString unavailableReason;
    const bool menuEnabled =
        service.canOffer(context, &unavailableReason);
    check(spec.label
              + QStringLiteral(" CPLD_PREPROC target enables menu action"),
          menuEnabled);

    const ExposeSignalToTopQuery query{
        context,
        ExposeSignalToTopService::defaultExportedPortName(
            spec.menuProbeSignal),
        workspace.workspaceFiles};
    const ExposeSignalToTopReport preview =
        service.plan(query, workspace.documents);
    const QByteArray previewState = preview.ready()
        ? QByteArrayLiteral("ready")
        : QByteArrayLiteral("blocked");
    std::printf(
        "[INFO] %s menu=%s preview=%s path=%s reason=%s\n",
        spec.menuProbeSignal.toUtf8().constData(),
        menuEnabled ? "enabled" : "disabled",
        previewState.constData(),
        node.instancePath.toUtf8().constData(),
        (preview.ready() ? QStringLiteral("none")
                         : preview.message).toUtf8().constData());
}

QString errorKey(const SemanticDiagnostic& diagnostic)
{
    return diagnostic.codeName
        + QLatin1Char('\n')
        + diagnostic.message;
}

QHash<QString, int> errorCounts(
    const QList<SemanticDiagnostic>& diagnostics)
{
    QHash<QString, int> result;
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != SemanticDiagnostic::Error)
            continue;
        ++result[errorKey(diagnostic)];
    }
    return result;
}

bool noNewErrors(
    const QList<SemanticDiagnostic>& before,
    const QList<SemanticDiagnostic>& after,
    QString* firstNew)
{
    const QHash<QString, int> beforeCounts =
        errorCounts(before);
    const QHash<QString, int> afterCounts =
        errorCounts(after);
    for (auto it = afterCounts.constBegin();
         it != afterCounts.constEnd(); ++it) {
        if (it.value() > beforeCounts.value(it.key())) {
            if (firstNew)
                *firstNew = it.key();
            return false;
        }
    }
    return true;
}

QList<SemanticSymbolRecord> signalCandidates(
    const QList<SemanticSymbolRecord>& records,
    const QString& sourceModule,
    const QString& definitionFile,
    const QString& instancePath)
{
    QList<SemanticSymbolRecord> result;
    for (const SemanticSymbolRecord& record : records) {
        if (record.owner.kind
                != SymbolTaxonomy::SymbolOwnerScope::Module
            || record.owner.name != sourceModule
            || norm(record.location.fileName)
                != norm(definitionFile)
            || !propagatableInternal(record.collectorKind)
            || record.location.position < 0
            || record.location.length <= 0) {
            continue;
        }
        const auto info =
            record.presentation.instanceInfoByPath.constFind(
                instancePath);
        if (info
                == record.presentation.instanceInfoByPath.constEnd()
            || !usableInfo(info.value())) {
            continue;
        }
        result.append(record);
    }
    std::sort(
        result.begin(), result.end(),
        [](const SemanticSymbolRecord& lhs,
           const SemanticSymbolRecord& rhs) {
            if (lhs.location.position != rhs.location.position)
                return lhs.location.position < rhs.location.position;
            return lhs.name < rhs.name;
        });
    return result;
}

bool allGeneratedSnippetsFormatterStable(
    const ExposeSignalToTopReport& report)
{
    for (const rtledit::WorkspaceTextEdit& edit
         : report.planResult.plan.workspaceEdit.edits) {
        const QString text = QString::fromUtf8(
            edit.newText.data(),
            static_cast<qsizetype>(edit.newText.size()));
        const FormatterReport once =
            FormatterService::getInstance()->formatSnippet(
                text, FormatterProfile::Structured);
        const FormatterReport twice =
            FormatterService::getInstance()->formatSnippet(
                once.formattedText,
                FormatterProfile::Structured);
        if (once.formattedText != text
            || twice.formattedText != once.formattedText) {
            return false;
        }
    }
    return true;
}

bool hasOutputPort(
    const QList<SemanticSymbolRecord>& records,
    const QString& module,
    const QString& name)
{
    for (const SemanticSymbolRecord& record : records) {
        if (record.name == name
            && record.owner.kind
                == SymbolTaxonomy::SymbolOwnerScope::Module
            && record.owner.name == module
            && record.collectorKind
                == SymbolTaxonomy::CollectorKind::PortOutput) {
            return true;
        }
    }
    return false;
}

std::optional<SemanticSymbolRecord> instanceRecord(
    const QList<SemanticSymbolRecord>& records,
    const QString& parentModule,
    const QString& instanceName)
{
    QList<SemanticSymbolRecord> matches;
    for (const SemanticSymbolRecord& record : records) {
        if (isInstance(record)
            && record.name == instanceName
            && record.owner.kind
                == SymbolTaxonomy::SymbolOwnerScope::Module
            && record.owner.name == parentModule) {
            matches.append(record);
        }
    }
    if (matches.size() != 1)
        return std::nullopt;
    return matches.constFirst();
}

bool sourceBridgeResolvedBySlang(
    const QHash<QString, RelationshipExtractionInfo>& facts,
    const QString& fileName,
    const QString& exportedName,
    const QString& signalName)
{
    const RelationshipExtractionInfo fileFacts =
        facts.value(norm(fileName));
    for (const AssignmentInfo& assignment
         : fileFacts.assignments) {
        const bool left =
            assignment.leftName == exportedName
            || assignment.leftAccessPath == exportedName;
        const bool right =
            assignment.rightNames.contains(signalName)
            || assignment.rightAccessPaths.contains(signalName);
        if (left && right)
            return true;
    }
    return false;
}

bool connectionsComplete(
    const ExposeSignalToTopReport& report,
    const QList<SemanticSymbolRecord>& afterRecords,
    const MemoryDocuments& documents)
{
    for (const ExposeSignalHierarchyStepView& step
         : report.hierarchySteps) {
        const auto record =
            instanceRecord(afterRecords,
                           step.parentModule,
                           step.childInstance);
        if (!record)
            return false;
        const QString fileName =
            norm(record->location.fileName);
        if (!documents.docs.contains(fileName))
            return false;
        TSDocument syntax;
        syntax.setText(documents.docs.value(fileName).text);
        const TSNamedPortConnectionTarget connection =
            syntax.namedPortConnectionTarget(
                record->location.position,
                report.exportedPortName);
        if (connection.status
                != TSNamedPortConnectionStatus::AlreadyConnected
            || connection.existingActual
                != report.exportedPortName) {
            return false;
        }
    }
    return true;
}

bool provenanceComplete(
    const ExposeSignalToTopReport& report)
{
    const auto& editPlan =
        report.planResult.plan.workspaceEdit;
    if (editPlan.edits.empty()
        || editPlan.edits.size()
               != editPlan.provenance.size()) {
        return false;
    }
    for (const rtledit::TextEditProvenance& item
         : editPlan.provenance) {
        if (item.actionId != "signal.exposeToTop"
            || item.anchor.source
                != rtledit::AnchorResolutionSource::TreeSitter
            || item.signalQualifiedName.empty()
            || item.sourceInstancePath
                != report.sourceInstancePath.toStdString()
            || item.sourceFilePath.empty()
            || !(item.sourceRange.start
                 <= item.sourceRange.end)) {
            return false;
        }
    }
    return true;
}

void runFixture(const FixtureSpec& spec,
                const QString& tempRoot)
{
    std::printf("\n-- %s --\n",
                spec.label.toUtf8().constData());
    const auto copied = copyFixture(spec, tempRoot);
    check(spec.label + QStringLiteral(" copied to temporary workspace"),
          copied.has_value());
    if (!copied)
        return;
    FixtureWorkspace workspace = *copied;

    SlangManager slang;
    QList<SemanticSymbolRecord> records =
        slang.extractOverlayWorkspaceSymbolRecords(
            workspace.contents,
            workspace.includeDirs,
            {},
            nullptr,
            nullptr,
            workspace.orderedFiles);
    assignNativeLocalHandles(&records);
    const QList<SemanticDiagnostic> beforeDiagnostics =
        slang.extractOverlayWorkspaceDiagnostics(
            workspace.contents,
            workspace.includeDirs,
            {},
            nullptr,
            workspace.orderedFiles);
    check(spec.label + QStringLiteral(" Slang records available"),
          !records.isEmpty());

    SemanticIndex index;
    index.setSnapshot(snapshot(
        records, beforeDiagnostics, workspace.contents));
    HierarchyService hierarchy(&index);
    const DesignHierarchyReport design =
        hierarchy.getDesignHierarchyReport(
            spec.activeTop, workspace.workspaceFiles);
    runMenuAvailabilityProbe(spec,
                             workspace,
                             records,
                             design,
                             &index,
                             &hierarchy);
    QList<DesignHierarchyNode> sourceNodes;
    for (const DesignHierarchyNode& node : design.nodes) {
        if (node.inSelectedTop
            && node.moduleType == spec.sourceModule) {
            sourceNodes.append(node);
        }
    }
    check(spec.label + QStringLiteral(" unique concrete source instance"),
          sourceNodes.size() == 1);
    if (sourceNodes.size() != 1) {
        std::fprintf(stderr,
                     "%s: hierarchy nodes=%d unresolved=%s\n",
                     spec.label.toUtf8().constData(),
                     design.nodes.size(),
                     design.unresolvedModules.join(QLatin1Char(','))
                         .toUtf8().constData());
        return;
    }
    const DesignHierarchyNode sourceNode =
        sourceNodes.constFirst();
    check(spec.label + QStringLiteral(" path has at least two propagation steps"),
          sourceNode.instancePath.count(QLatin1Char('.')) >= 2);

    const QList<SemanticSymbolRecord> candidates =
        signalCandidates(records,
                         spec.sourceModule,
                         sourceNode.definitionFile,
                         sourceNode.instancePath);
    check(spec.label + QStringLiteral(" has Slang-typed internal candidates"),
          !candidates.isEmpty());
    if (candidates.isEmpty())
        return;

    ExposeSignalToTopService service(&index, &hierarchy);
    ExposeSignalToTopReport report;
    SemanticSymbolRecord selectedSignal;
    QStringList rejections;
    for (const SemanticSymbolRecord& candidate : candidates) {
        const EditorSemanticContext context =
            contextFor(spec,
                       workspace,
                       candidate,
                       sourceNode.instancePath);
        const QString exported =
            ExposeSignalToTopService::defaultExportedPortName(
                candidate.name);
        const ExposeSignalToTopQuery query{
            context, exported, workspace.workspaceFiles};
        const ExposeSignalToTopReport attempt =
            service.plan(query, workspace.documents);
        if (attempt.ready()) {
            report = attempt;
            selectedSignal = candidate;
            break;
        }
        rejections.append(
            QStringLiteral("%1: %2")
                .arg(candidate.name, attempt.message));
    }
    check(spec.label + QStringLiteral(" production plan ready"),
          report.ready());
    if (!report.ready()) {
        std::fprintf(stderr,
                     "%s: no candidate planned:\n%s\n",
                     spec.label.toUtf8().constData(),
                     rejections.join(QLatin1Char('\n'))
                         .toUtf8().constData());
        return;
    }

    const EditorSemanticContext repeatContext =
        contextFor(spec,
                   workspace,
                   selectedSignal,
                   sourceNode.instancePath);
    const ExposeSignalToTopQuery repeatQuery{
        repeatContext,
        report.exportedPortName,
        workspace.workspaceFiles};
    const ExposeSignalToTopReport repeated =
        service.plan(repeatQuery, workspace.documents);
    check(spec.label + QStringLiteral(" deterministic formatted preview"),
          repeated.ready()
              && repeated.renderedDiff == report.renderedDiff);
    check(spec.label + QStringLiteral(" preview spans every path definition"),
          report.hierarchySteps.size() >= 2
              && report.affectedModules.size()
                     == report.hierarchySteps.size() + 1
              && report.sourceDiff.files.size() >= 3);
    check(spec.label + QStringLiteral(" provenance is complete"),
          provenanceComplete(report));
    check(spec.label + QStringLiteral(" formatter output is stable"),
          allGeneratedSnippetsFormatterStable(report));

    const ExposeSignalToTopApplyReport applied =
        service.apply(report, workspace.documents);
    check(spec.label + QStringLiteral(" atomic apply succeeds"),
          applied.applied());
    if (!applied.applied())
        return;

    QHash<QString, QString> afterContents = workspace.contents;
    for (auto it = workspace.documents.docs.constBegin();
         it != workspace.documents.docs.constEnd(); ++it) {
        afterContents.insert(it.key(), it->text);
    }
    const QList<SemanticSymbolRecord> afterRecords =
        slang.extractOverlayWorkspaceSymbolRecords(
            afterContents,
            workspace.includeDirs,
            {},
            nullptr,
            nullptr,
            workspace.orderedFiles);
    const QList<SemanticDiagnostic> afterDiagnostics =
        slang.extractOverlayWorkspaceDiagnostics(
            afterContents,
            workspace.includeDirs,
            {},
            nullptr,
            workspace.orderedFiles);
    QString firstNewError;
    const bool clean =
        noNewErrors(beforeDiagnostics,
                    afterDiagnostics,
                    &firstNewError);
    check(spec.label + QStringLiteral(" Slang reports no new errors"),
          clean);
    if (!clean) {
        std::fprintf(stderr,
                     "%s: first new Slang error: %s\n",
                     spec.label.toUtf8().constData(),
                     firstNewError.toUtf8().constData());
    }

    bool allPorts = true;
    for (const QString& module : report.affectedModules) {
        allPorts = allPorts
            && hasOutputPort(afterRecords,
                             module,
                             report.exportedPortName);
    }
    check(spec.label + QStringLiteral(" Slang resolves every propagated output"),
          allPorts);
    check(spec.label + QStringLiteral(" Tree-sitter resolves every named connection"),
          connectionsComplete(report,
                              afterRecords,
                              workspace.documents));

    const QHash<QString, RelationshipExtractionInfo> afterFacts =
        slang.extractOverlayWorkspaceRelationshipInfo(
            afterContents,
            workspace.includeDirs,
            {},
            nullptr,
            workspace.orderedFiles);
    const bool sourceBridgeResolved =
        sourceBridgeResolvedBySlang(
            afterFacts,
            selectedSignal.location.fileName,
            report.exportedPortName,
            selectedSignal.name);
    check(spec.label + QStringLiteral(" Slang resolves the source bridge"),
          sourceBridgeResolved);
    if (!sourceBridgeResolved) {
        std::fprintf(
            stderr,
            "%s: selected candidate name=%s owner=%s file=%s position=%d "
            "length=%d type=%s exported=%s\n",
            spec.label.toUtf8().constData(),
            selectedSignal.name.toUtf8().constData(),
            selectedSignal.owner.name.toUtf8().constData(),
            selectedSignal.location.fileName.toUtf8().constData(),
            selectedSignal.location.position,
            selectedSignal.location.length,
            selectedSignal.presentation.defaultInfo.resolvedTypeText.toUtf8().constData(),
            report.exportedPortName.toUtf8().constData());
        const auto& editPlan =
            report.planResult.plan.workspaceEdit;
        for (std::size_t index = 0;
             index < editPlan.edits.size()
             && index < editPlan.provenance.size();
             ++index) {
            if (editPlan.provenance[index].anchorName
                != "source.bridge") {
                continue;
            }
            const auto& edit = editPlan.edits[index];
            std::fprintf(
                stderr,
                "%s: source.bridge edit file=%s range=%zu:%zu-%zu:%zu "
                "newText=<<<%s>>>\n",
                spec.label.toUtf8().constData(),
                edit.filePath.c_str(),
                edit.range.start.line,
                edit.range.start.column,
                edit.range.end.line,
                edit.range.end.column,
                edit.newText.c_str());
        }
        const QString sourceFile =
            norm(selectedSignal.location.fileName);
        const RelationshipExtractionInfo sourceFacts =
            afterFacts.value(sourceFile);
        for (const AssignmentInfo& assignment
             : sourceFacts.assignments) {
            std::fprintf(
                stderr,
                "%s: Slang assignment file=%s line=%d left=%s "
                "leftPath=%s rights=[%s] rightPaths=[%s]\n",
                spec.label.toUtf8().constData(),
                assignment.sourceRange.fileName.toUtf8().constData(),
                assignment.lineNumber,
                assignment.leftName.toUtf8().constData(),
                assignment.leftAccessPath.toUtf8().constData(),
                assignment.rightNames.join(QLatin1Char(',')).toUtf8().constData(),
                assignment.rightAccessPaths.join(QLatin1Char(',')).toUtf8().constData());
        }
        std::fprintf(
            stderr,
            "%s: applied source module text file=%s\n<<<\n%s\n>>>\n",
            spec.label.toUtf8().constData(),
            sourceFile.toUtf8().constData(),
            workspace.documents.docs.value(sourceFile).text.toUtf8().constData());
    }
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <test_sv/new> <test_sv/huge_prj>\n",
                     argv[0]);
        return 2;
    }

    QTemporaryDir temp;
    check(QStringLiteral("temporary fixture root"), temp.isValid());
    if (!temp.isValid())
        return 1;

    const FixtureSpec newFixture{
        QStringLiteral("test_sv/new"),
        norm(QString::fromLocal8Bit(argv[1])),
        QStringLiteral("rtl_top"),
        QStringLiteral("cpld_board_id_rx"),
        {
            QStringLiteral("PKG_global.sv"),
            QStringLiteral(
                "elec_phy_import/phy/cpld_board_id_rx.sv"),
            QStringLiteral(
                "elec_phy_import/phy/cpld_preproc.sv"),
            QStringLiteral(
                "elec_phy_import/phy/cpld_top.sv"),
            QStringLiteral(
                "elec_phy_import/phy/phy_top.sv"),
            QStringLiteral(
                "elec_phy_import/top/rtl_top.sv")
        },
        {
            QStringLiteral(
                "elec_phy_import/phy/cpld_board_id_rx.sv"),
            QStringLiteral(
                "elec_phy_import/phy/cpld_preproc.sv"),
            QStringLiteral(
                "elec_phy_import/phy/cpld_top.sv"),
            QStringLiteral(
                "elec_phy_import/phy/phy_top.sv"),
            QStringLiteral(
                "elec_phy_import/top/rtl_top.sv")
        },
        QStringLiteral("cpld_preproc"),
        QStringLiteral("c0_oc_event_st_cnt")};
    runFixture(newFixture,
               QDir(temp.path()).filePath(QStringLiteral("new")));

    const FixtureSpec hugeFixture{
        QStringLiteral("test_sv/huge_prj"),
        norm(QString::fromLocal8Bit(argv[2])),
        QStringLiteral("axi_bridge"),
        QStringLiteral("vendor_ip_axi_gm"),
        {
            QStringLiteral("Axi/vendor_ip_axi_gm.sv"),
            QStringLiteral(
                "Bridge/inbound/vendor_ip_bridge_ib.sv"),
            QStringLiteral("Axi/axi_bridge.sv")
        },
        {
            QStringLiteral("Axi/vendor_ip_axi_gm.sv"),
            QStringLiteral(
                "Bridge/inbound/vendor_ip_bridge_ib.sv"),
            QStringLiteral("Axi/axi_bridge.sv")
        }};
    runFixture(hugeFixture,
               QDir(temp.path()).filePath(QStringLiteral("huge")));

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
