#include "exposesignaltotopservice.h"

#include "definitionservice.h"
#include "formatterservice.h"
#include "hierarchyservice.h"
#include "saferenameservice.h"
#include "tsdocument.h"
#include "workspaceedittransactionservice.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return {};
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

std::string utf8String(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(),
                       static_cast<std::size_t>(bytes.size()));
}

QString fromUtf8String(const std::string& text)
{
    return QString::fromUtf8(
        text.data(), static_cast<qsizetype>(text.size()));
}

ExposeSignalToTopReport rejected(
    rtledit::ExposeSignalFailureReason reason,
    const QString& message)
{
    ExposeSignalToTopReport report;
    report.failureReason = reason;
    report.message = message;
    if (!message.isEmpty())
        report.blockers.append(message);
    return report;
}

void rejectInto(ExposeSignalToTopReport* report,
                rtledit::ExposeSignalFailureReason reason,
                const QString& message)
{
    if (!report)
        return;
    report->status = ExposeSignalToTopReportStatus::Rejected;
    report->failureReason = reason;
    report->message = message;
    if (!message.isEmpty() && !report->blockers.contains(message))
        report->blockers.append(message);
}

rtledit::SourcePosition utf8PositionAt(
    const QString& text,
    int charOffset)
{
    const int bounded = qBound(0, charOffset, text.size());
    int line = 0;
    int lineStart = 0;
    for (int index = 0; index < bounded; ++index) {
        if (text.at(index) != QLatin1Char('\n'))
            continue;
        ++line;
        lineStart = index + 1;
    }
    const QByteArray columnBytes =
        text.mid(lineStart, bounded - lineStart).toUtf8();
    return rtledit::SourcePosition{
        static_cast<std::size_t>(line),
        static_cast<std::size_t>(columnBytes.size())};
}

rtledit::SourceRange utf8Range(
    const QString& text,
    int startChar,
    int endChar)
{
    const int boundedStart = qBound(0, startChar, text.size());
    const int boundedEnd =
        qBound(boundedStart, endChar, text.size());
    return {
        utf8PositionAt(text, boundedStart),
        utf8PositionAt(text, boundedEnd)};
}

struct ParsedDocument {
    QString fileName;
    QString text;
    rtledit::DocumentVersion version;
    std::unique_ptr<TSDocument> syntax;
};

class ParsedDocumentCache
{
public:
    explicit ParsedDocumentCache(
        rtledit::WorkspaceDocumentManager* manager)
        : documents(manager)
    {
    }

    std::shared_ptr<ParsedDocument> get(const QString& fileName)
    {
        const QString key = normalizedFileName(fileName);
        if (key.isEmpty() || !documents)
            return {};
        const auto existing = parsed.constFind(key);
        if (existing != parsed.constEnd())
            return existing.value();

        const auto snapshot = documents->snapshot(utf8String(key));
        if (!snapshot)
            return {};
        auto result = std::make_shared<ParsedDocument>();
        result->fileName = key;
        result->text = fromUtf8String(snapshot->text);
        result->version = snapshot->version;
        result->syntax = std::make_unique<TSDocument>();
        result->syntax->setText(result->text);
        parsed.insert(key, result);
        return result;
    }

    QList<std::shared_ptr<ParsedDocument>> values() const
    {
        QList<std::shared_ptr<ParsedDocument>> result = parsed.values();
        std::sort(result.begin(), result.end(),
                  [](const auto& lhs, const auto& rhs) {
                      return lhs->fileName < rhs->fileName;
                  });
        return result;
    }

private:
    rtledit::WorkspaceDocumentManager* documents = nullptr;
    QHash<QString, std::shared_ptr<ParsedDocument>> parsed;
};

rtledit::AnchorProvenance treeSitterProvenance(
    std::uint64_t generation)
{
    rtledit::AnchorProvenance provenance;
    provenance.source =
        rtledit::AnchorResolutionSource::TreeSitter;
    provenance.resolver = "ZeroSlack.TSDocument";
    provenance.semanticSnapshotId = std::to_string(generation);
    return provenance;
}

rtledit::StructuredInsertionAnchor insertionAnchor(
    const std::shared_ptr<ParsedDocument>& document,
    int charOffset,
    const QString& prefix,
    const QString& suffix,
    std::uint64_t generation)
{
    rtledit::StructuredInsertionAnchor anchor;
    if (!document || charOffset < 0
        || charOffset > document->text.size()) {
        return anchor;
    }
    anchor.filePath = utf8String(document->fileName);
    anchor.documentVersion = document->version;
    anchor.range = utf8Range(
        document->text, charOffset, charOffset);
    anchor.prefix = utf8String(prefix);
    anchor.suffix = utf8String(suffix);
    anchor.provenance = treeSitterProvenance(generation);
    return anchor;
}

QPair<QString, QString> insertionTemplateParts(
    const QString& insertionText,
    int caretOffset)
{
    if (caretOffset < 0 || caretOffset > insertionText.size())
        return {insertionText, {}};
    return {
        insertionText.left(caretOffset),
        insertionText.mid(caretOffset)};
}

bool sourceMappedName(
    const std::shared_ptr<ParsedDocument>& document,
    const SemanticSymbolRecord& record)
{
    if (!document || record.location.position < 0
        || record.location.length <= 0
        || record.location.position + record.location.length
               > document->text.size()) {
        return false;
    }
    return document->text.mid(
               record.location.position,
               record.location.length)
        == record.name;
}

bool recordInWorkspace(const SemanticSymbolRecord& record,
                       const QSet<QString>& workspaceFiles)
{
    return workspaceFiles.isEmpty()
        || workspaceFiles.contains(
            normalizedFileName(record.location.fileName));
}

QSet<QString> normalizedFiles(const QSet<QString>& files)
{
    QSet<QString> result;
    for (const QString& file : files) {
        const QString clean = normalizedFileName(file);
        if (!clean.isEmpty())
            result.insert(clean);
    }
    return result;
}

bool propagatableCollector(
    SymbolTaxonomy::CollectorKind kind)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    return kind == CollectorKind::Logic
        || kind == CollectorKind::Wire
        || kind == CollectorKind::Reg
        || kind == CollectorKind::PortOutput;
}

const SemanticElaboratedSymbolInfo* instanceTypeInfo(
    const SemanticSymbolRecord& record,
    const QString& instancePath)
{
    const auto found =
        record.presentation.instanceInfoByPath.constFind(instancePath);
    return found == record.presentation.instanceInfoByPath.constEnd()
        ? nullptr : &found.value();
}

bool compatibleType(const SemanticElaboratedSymbolInfo& lhs,
                    const SemanticElaboratedSymbolInfo& rhs)
{
    return lhs.available && rhs.available
        && lhs.fixedSize && rhs.fixedSize
        && lhs.integral && rhs.integral
        && !lhs.unpackedArray && !rhs.unpackedArray
        && !lhs.interfaceType && !rhs.interfaceType
        && lhs.bitWidth > 0
        && lhs.bitWidth == rhs.bitWidth
        && lhs.signedIntegral == rhs.signedIntegral;
}

QString renderedLogicType(
    const SemanticElaboratedSymbolInfo& info)
{
    if (!info.available || !info.fixedSize || !info.integral
        || info.unpackedArray || info.interfaceType
        || info.bitWidth == 0) {
        return {};
    }
    QString result = QStringLiteral("logic");
    if (info.signedIntegral)
        result += QStringLiteral(" signed");
    if (info.bitWidth > 1) {
        result += QStringLiteral(" [%1:0]")
            .arg(info.bitWidth - 1);
    }
    return result;
}

std::optional<SemanticSymbolRecord> exactModuleRecord(
    SemanticIndex* index,
    const QString& moduleName,
    const QString& definitionFile,
    const QSet<QString>& workspaceFiles)
{
    if (!index)
        return std::nullopt;
    QList<SemanticSymbolRecord> matches;
    for (const SemanticSymbolRecord& record
         : index->getSymbolRecordsByName(moduleName)) {
        if (record.declarationKind
                != SymbolTaxonomy::DeclarationKind::Module
            || !recordInWorkspace(record, workspaceFiles)) {
            continue;
        }
        if (!definitionFile.isEmpty()
            && normalizedFileName(record.location.fileName)
                   != normalizedFileName(definitionFile)) {
            continue;
        }
        matches.append(record);
    }
    return matches.size() == 1
        ? std::optional<SemanticSymbolRecord>(matches.constFirst())
        : std::nullopt;
}

std::optional<SemanticSymbolRecord> exactInstanceRecord(
    SemanticIndex* index,
    const DesignHierarchyNode& child,
    const DesignHierarchyNode& parent,
    const QSet<QString>& workspaceFiles)
{
    if (!index)
        return std::nullopt;
    QList<SemanticSymbolRecord> matches;
    for (const SemanticSymbolRecord& record
         : index->getSymbolRecordsByName(child.instanceName)) {
        if (record.declarationKind
                != SymbolTaxonomy::DeclarationKind::Instance
            || record.owner.kind
                != SymbolTaxonomy::SymbolOwnerScope::Module
            || record.owner.name != parent.moduleType
            || record.type.resolvedTypeName != child.moduleType
            || normalizedFileName(record.location.fileName)
                != normalizedFileName(child.instanceFile)
            || !recordInWorkspace(record, workspaceFiles)) {
            continue;
        }
        matches.append(record);
    }
    return matches.size() == 1
        ? std::optional<SemanticSymbolRecord>(matches.constFirst())
        : std::nullopt;
}

struct PortDecision {
    rtledit::ExposeEndpointState state =
        rtledit::ExposeEndpointState::Conflict;
    rtledit::ExposeSignalFailureReason reason =
        rtledit::ExposeSignalFailureReason::NameConflict;
    QString message;
    std::optional<SemanticSymbolRecord> existingPort;
};

QList<SemanticRelationship> assignmentDrivers(
    SemanticIndex* index,
    const SemanticSymbolRecord& outputPort)
{
    QList<SemanticRelationship> result;
    if (!index || !outputPort.stableKey.isValid())
        return result;
    for (const SemanticRelationship& relationship
         : index->relationshipsForStableKey(
               outputPort.stableKey, false)) {
        if (relationship.type
                == SymbolRelationshipEngine::ASSIGNS_TO
            && relationship.toStableKey
                == outputPort.stableKey) {
            result.append(relationship);
        }
    }
    return result;
}

bool isExactSourceBridge(
    const QList<SemanticRelationship>& drivers,
    const SemanticSymbolRecord& signal,
    const SemanticSymbolRecord& outputPort)
{
    if (drivers.isEmpty())
        return false;
    for (const SemanticRelationship& driver : drivers) {
        if (driver.provenance
                != RelationshipProvenance::SlangExtracted
            || !driver.exactValueForward
            || driver.fromStableKey != signal.stableKey
            || driver.toStableKey != outputPort.stableKey
            || driver.fromAccessPath != signal.name
            || driver.toAccessPath != outputPort.name) {
            return false;
        }
    }
    return true;
}

bool allInstanceDriversProvenZero(
    const SemanticSymbolRecord& outputPort,
    const QString& moduleName,
    const QList<DesignHierarchyNode>& allNodes,
    QString* failureMessage)
{
    bool sawInstance = false;
    for (const DesignHierarchyNode& node : allNodes) {
        if (node.moduleType != moduleName)
            continue;
        sawInstance = true;
        const SemanticElaboratedSymbolInfo* info =
            instanceTypeInfo(outputPort, node.instancePath);
        if (!info
            || info->driverPresence
                == SemanticDriverPresenceState::Unknown) {
            if (failureMessage) {
                *failureMessage = QStringLiteral(
                    "Slang did not prove that existing output %1 in "
                    "module %2 is undriven at %3; driver facts are "
                    "incomplete.")
                    .arg(outputPort.name,
                         moduleName,
                         node.instancePath);
            }
            return false;
        }
        if (info->driverPresence
                == SemanticDriverPresenceState::Present
            || info->driverCount != 0) {
            if (failureMessage) {
                *failureMessage = QStringLiteral(
                    "Existing output %1 in module %2 has %3 "
                    "Slang-resolved driver(s) at %4.")
                    .arg(outputPort.name, moduleName)
                    .arg(info->driverCount)
                    .arg(node.instancePath);
            }
            return false;
        }
    }
    if (!sawInstance && failureMessage) {
        *failureMessage = QStringLiteral(
            "Slang has no elaborated driver facts for existing output "
            "%1 in module %2.")
            .arg(outputPort.name, moduleName);
    }
    return sawInstance;
}

bool hasSingleExactContinuousDriver(
    const SemanticSymbolRecord& outputPort,
    const QString& moduleName,
    const QList<DesignHierarchyNode>& allNodes,
    QString* failureMessage)
{
    bool sawInstance = false;
    for (const DesignHierarchyNode& node : allNodes) {
        if (node.moduleType != moduleName)
            continue;
        sawInstance = true;
        const SemanticElaboratedSymbolInfo* info =
            instanceTypeInfo(outputPort, node.instancePath);
        if (!info
            || info->driverPresence
                != SemanticDriverPresenceState::Present
            || info->driverCount != 1
            || info->continuousDriverCount != 1
            || info->proceduralDriverCount != 0
            || info->portConnectionDriverCount != 0) {
            if (failureMessage) {
                *failureMessage = QStringLiteral(
                    "Existing source output %1 in module %2 does not "
                    "have exactly one Slang-resolved direct continuous "
                    "assignment driver at %3.")
                    .arg(outputPort.name,
                         moduleName,
                         node.instancePath);
            }
            return false;
        }
    }
    if (!sawInstance && failureMessage) {
        *failureMessage = QStringLiteral(
            "Slang has no elaborated driver summary for existing source "
            "output %1 in module %2.")
            .arg(outputPort.name, moduleName);
    }
    return sawInstance;
}

PortDecision existingPortDecision(
    SemanticIndex* index,
    const QString& moduleName,
    const QString& portName,
    const SemanticElaboratedSymbolInfo& sourceInfo,
    const QList<DesignHierarchyNode>& allNodes,
    const QSet<QString>& workspaceFiles)
{
    QList<SemanticSymbolRecord> collisions;
    for (const SemanticSymbolRecord& record
         : index->getSymbolRecordsByName(portName)) {
        if (record.owner.kind
                == SymbolTaxonomy::SymbolOwnerScope::Module
            && record.owner.name == moduleName
            && recordInWorkspace(record, workspaceFiles)) {
            collisions.append(record);
        }
    }
    if (collisions.isEmpty())
        return {rtledit::ExposeEndpointState::Add,
                rtledit::ExposeSignalFailureReason::None, {}};
    if (collisions.size() != 1) {
        return {rtledit::ExposeEndpointState::Conflict,
                rtledit::ExposeSignalFailureReason::NameConflict,
                QStringLiteral(
                    "Multiple declarations named \"%1\" exist in module %2.")
                    .arg(portName, moduleName)};
    }

    const SemanticSymbolRecord& existing = collisions.constFirst();
    if (existing.collectorKind
            != SymbolTaxonomy::CollectorKind::PortOutput) {
        const bool port =
            existing.declarationKind
            == SymbolTaxonomy::DeclarationKind::Port;
        return {
            rtledit::ExposeEndpointState::Conflict,
            port ? rtledit::ExposeSignalFailureReason::DirectionConflict
                 : rtledit::ExposeSignalFailureReason::NameConflict,
            port
                ? QStringLiteral(
                      "Existing port %1 in module %2 is not an output.")
                      .arg(portName, moduleName)
                : QStringLiteral(
                      "Name %1 already exists in module %2.")
                      .arg(portName, moduleName)};
    }

    for (const DesignHierarchyNode& node : allNodes) {
        if (node.moduleType != moduleName)
            continue;
        const auto* info =
            instanceTypeInfo(existing, node.instancePath);
        if (!info || !compatibleType(*info, sourceInfo)) {
            return {
                rtledit::ExposeEndpointState::Conflict,
                rtledit::ExposeSignalFailureReason::TypeMismatch,
                QStringLiteral(
                    "Existing output %1 in module %2 has an incompatible "
                    "Slang effective type at %3.")
                    .arg(portName, moduleName, node.instancePath)};
        }
    }
    PortDecision decision{
        rtledit::ExposeEndpointState::Reuse,
        rtledit::ExposeSignalFailureReason::None, {}};
    decision.existingPort = existing;
    return decision;
}

rtledit::ExposeSignalPortSite buildPortSite(
    const QString& moduleName,
    const QString& instancePath,
    const QString& portName,
    const QString& renderedType,
    const PortDecision& decision,
    const SemanticSymbolRecord& moduleRecord,
    ParsedDocumentCache* documents,
    std::uint64_t generation)
{
    rtledit::ExposeSignalPortSite site;
    site.state = decision.state;
    site.moduleName = utf8String(moduleName);
    site.instancePath = utf8String(instancePath);
    site.portName = utf8String(portName);
    site.renderedDataType = utf8String(renderedType);
    site.conflictReason = decision.reason;
    site.conflictMessage = utf8String(decision.message);
    if (decision.state != rtledit::ExposeEndpointState::Add)
        return site;

    const auto document = documents->get(moduleRecord.location.fileName);
    if (!document)
        return site;
    const TSPortAppendTarget target =
        document->syntax->portAppendTarget(
            moduleRecord.location.position);
    if (!target.ok())
        return site;
    const auto parts = insertionTemplateParts(
        target.insertText,
        target.caretCharAfterEdit - target.insertChar);
    site.insertion = insertionAnchor(
        document, target.insertChar,
        parts.first, parts.second, generation);
    if (target.needsTrailingComma) {
        site.delimiterInsertion = insertionAnchor(
            document, target.trailingCommaInsertChar,
            {}, {}, generation);
    }
    return site;
}

QStringList uniqueSorted(QStringList values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()),
                 values.end());
    return values;
}

bool fileReadOnly(const QString& fileName)
{
    const QFileInfo info(fileName);
    return info.exists() && !info.isWritable();
}

} // namespace

ExposeSignalToTopService::ExposeSignalToTopService(
    SemanticIndex* semanticIndex,
    HierarchyService* hierarchyService)
    : index(semanticIndex)
    , hierarchy(hierarchyService)
{
}

SemanticIndex* ExposeSignalToTopService::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

HierarchyService* ExposeSignalToTopService::hierarchyService() const
{
    return hierarchy ? hierarchy : HierarchyService::getInstance();
}

QString ExposeSignalToTopService::defaultExportedPortName(
    const QString& signalName)
{
    return signalName.isEmpty()
        ? QString() : signalName + QStringLiteral("_out");
}

bool ExposeSignalToTopService::canOffer(
    const EditorSemanticContext& context,
    QString* unavailableReason) const
{
    auto fail = [&](const QString& message) {
        if (unavailableReason)
            *unavailableReason = message;
        return false;
    };
    if (!context.hierarchyInstance.isBound()) {
        return fail(QStringLiteral(
            "Select the signal from a concrete design-hierarchy instance."));
    }
    if (context.documentText.isEmpty()
        || context.cursorPosition < 0) {
        return fail(QStringLiteral(
            "The current document snapshot is unavailable."));
    }
    TSDocument syntax;
    syntax.setText(context.documentText);
    const TSIdentifierTarget identifier =
        syntax.identifierAt(context.cursorPosition);
    if (!identifier.ok()) {
        return fail(QStringLiteral(
            "Select a SystemVerilog signal identifier."));
    }

    DefinitionQuery query;
    query.symbolName = identifier.text;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.linePrefixBeforeCursor = context.lineUpToCursor;
    query.cursorLine = context.cursorLine;
    query.cursorColumn = context.column;
    DefinitionService definitions(semanticIndex());
    const DefinitionResult definition =
        definitions.resolveDefinition(query);
    if (!definition.found
        || definition.symbolRecord.owner.kind
            != SymbolTaxonomy::SymbolOwnerScope::Module
        || !propagatableCollector(
            definition.symbolRecord.collectorKind)) {
        return fail(QStringLiteral(
            "The selected identifier is not a propagatable module signal."));
    }
    if (unavailableReason)
        unavailableReason->clear();
    return true;
}

ExposeSignalToTopReport ExposeSignalToTopService::plan(
    const ExposeSignalToTopQuery& query,
    rtledit::WorkspaceDocumentManager& documentManager) const
{
    SemanticIndex* semantic = semanticIndex();
    HierarchyService* hierarchyApi = hierarchyService();
    if (!semantic || !hierarchyApi)
        return rejected(
            rtledit::ExposeSignalFailureReason::InvalidRequest,
            QStringLiteral("Semantic services are unavailable."));
    if (!query.context.hierarchyInstance.isBound())
        return rejected(
            rtledit::ExposeSignalFailureReason::SignalNotInSelectedInstance,
            QStringLiteral(
                "Select the signal from a concrete design-hierarchy instance."));

    ParsedDocumentCache documents(&documentManager);
    const auto contextDocument =
        documents.get(query.context.fileName);
    if (!contextDocument)
        return rejected(
            rtledit::ExposeSignalFailureReason::MissingSource,
            QStringLiteral("The current source document is unavailable."));
    if (query.context.documentRevision > 0
        && contextDocument->version.value
            != query.context.documentRevision) {
        return rejected(
            rtledit::ExposeSignalFailureReason::DocumentVersionConflict,
            QStringLiteral(
                "The current document version changed before planning."));
    }
    if (!query.context.documentText.isEmpty()
        && query.context.documentText != contextDocument->text) {
        return rejected(
            rtledit::ExposeSignalFailureReason::DocumentVersionConflict,
            QStringLiteral(
                "The editor context no longer matches the document snapshot."));
    }

    const TSIdentifierTarget identifier =
        contextDocument->syntax->identifierAt(
            query.context.cursorPosition);
    if (!identifier.ok())
        return rejected(
            rtledit::ExposeSignalFailureReason::SignalNotFound,
            QStringLiteral("No structural signal identifier is selected."));

    SemanticSymbolRecord signal;
    QList<SemanticSymbolRecord> exactDeclarations;
    for (const SemanticSymbolRecord& candidate
         : semantic->getSymbolRecordsByName(identifier.text)) {
        if (candidate.owner.kind
                != SymbolTaxonomy::SymbolOwnerScope::Module
            || candidate.owner.name != query.context.moduleName
            || normalizedFileName(candidate.location.fileName)
                != contextDocument->fileName) {
            continue;
        }
        if (query.context.cursorPosition
                >= candidate.location.position
            && query.context.cursorPosition
                <= candidate.location.position
                    + candidate.location.length) {
            exactDeclarations.append(candidate);
        }
    }
    if (exactDeclarations.size() == 1) {
        signal = exactDeclarations.constFirst();
    } else {
        DefinitionQuery definitionQuery;
        definitionQuery.symbolName = identifier.text;
        definitionQuery.fileName = query.context.fileName;
        definitionQuery.moduleName = query.context.moduleName;
        definitionQuery.linePrefixBeforeCursor =
            query.context.lineUpToCursor;
        definitionQuery.cursorLine = query.context.cursorLine;
        definitionQuery.cursorColumn = query.context.column;
        DefinitionService definitions(semantic);
        const DefinitionResult definition =
            definitions.resolveDefinition(definitionQuery);
        if (definition.found)
            signal = definition.symbolRecord;
    }
    if (!signal.isValid())
        return rejected(
            rtledit::ExposeSignalFailureReason::SignalNotFound,
            QStringLiteral(
                "Slang did not resolve the selected signal definition."));
    if (signal.owner.kind
            != SymbolTaxonomy::SymbolOwnerScope::Module
        || signal.owner.name != query.context.moduleName
        || !propagatableCollector(signal.collectorKind)) {
        return rejected(
            rtledit::ExposeSignalFailureReason::SignalNotPropagatable,
            QStringLiteral(
                "The selected declaration is not a module-scope internal "
                "signal or output port."));
    }
    if (!sourceMappedName(contextDocument, signal))
        return rejected(
            rtledit::ExposeSignalFailureReason::MacroExpansion,
            QStringLiteral(
                "The Slang signal location cannot be mapped to the current "
                "source buffer."));

    QSet<QString> workspaceFiles =
        normalizedFiles(query.workspaceFiles);
    if (workspaceFiles.isEmpty())
        workspaceFiles.insert(contextDocument->fileName);
    QStringList roots =
        hierarchyApi->inferDesignTopModules(workspaceFiles);
    const QString activeTop =
        query.context.hierarchyInstance.activeTopModule;
    if (!roots.contains(activeTop))
        roots.append(activeTop);
    roots = uniqueSorted(roots);
    const DesignHierarchyReport design =
        hierarchyApi->getDesignHierarchyReport(
            roots, activeTop, workspaceFiles);
    if (design.snapshotGeneration == 0
        || design.snapshotGeneration
            != semantic->snapshotRevision()) {
        return rejected(
            rtledit::ExposeSignalFailureReason::StaleSemanticGeneration,
            QStringLiteral(
                "The design hierarchy and semantic index generations differ."));
    }

    QList<DesignHierarchyNode> selectedNodes;
    QList<DesignHierarchyNode> targetNodes;
    for (const DesignHierarchyNode& node : design.nodes) {
        if (node.inSelectedTop
            && node.instancePath
                == query.context.hierarchyInstance.instancePath) {
            selectedNodes.append(node);
        }
        if (!node.inSelectedTop)
            continue;
        if (!query.targetAncestorInstancePath.isEmpty()) {
            if (node.instancePath
                == query.targetAncestorInstancePath) {
                targetNodes.append(node);
            }
        } else if (node.isTop
                   && node.rootModule == activeTop) {
            targetNodes.append(node);
        }
    }
    if (selectedNodes.size() != 1
        || targetNodes.size() != 1) {
        return rejected(
            rtledit::ExposeSignalFailureReason::AmbiguousHierarchy,
            QStringLiteral(
                "The selected instance or target ancestor is not unique."));
    }
    const DesignHierarchyNode sourceNode =
        selectedNodes.constFirst();
    const DesignHierarchyNode targetNode =
        targetNodes.constFirst();
    if (sourceNode.moduleType != signal.owner.name)
        return rejected(
            rtledit::ExposeSignalFailureReason::SignalNotInSelectedInstance,
            QStringLiteral(
                "The signal owner does not match the selected elaborated "
                "instance."));

    QHash<QString, DesignHierarchyNode> nodesById;
    for (const DesignHierarchyNode& node : design.nodes)
        nodesById.insert(node.id, node);
    QList<QPair<DesignHierarchyNode, DesignHierarchyNode>> path;
    DesignHierarchyNode current = sourceNode;
    QSet<QString> visited;
    while (current.id != targetNode.id) {
        if (current.id.isEmpty()
            || current.parentId.isEmpty()
            || visited.contains(current.id)
            || !nodesById.contains(current.parentId)) {
            return rejected(
                rtledit::ExposeSignalFailureReason::TargetNotAncestor,
                QStringLiteral(
                    "The active top is not an unambiguous ancestor of the "
                    "selected instance."));
        }
        visited.insert(current.id);
        const DesignHierarchyNode parent =
            nodesById.value(current.parentId);
        path.append({current, parent});
        current = parent;
    }

    const auto* selectedInfo =
        instanceTypeInfo(signal, sourceNode.instancePath);
    if (!selectedInfo || !selectedInfo->available)
        return rejected(
            rtledit::ExposeSignalFailureReason::SignalNotInSelectedInstance,
            QStringLiteral(
                "Slang has no effective type for the selected instance."));
    if (selectedInfo->interfaceType)
        return rejected(
            rtledit::ExposeSignalFailureReason::UnsupportedInterface,
            QStringLiteral(
                "Interface and modport signals are not supported."));
    if (selectedInfo->unpackedArray)
        return rejected(
            rtledit::ExposeSignalFailureReason::UnsupportedUnpackedArray,
            QStringLiteral(
                "Unpacked array signals are not supported."));
    const QString renderedType = renderedLogicType(*selectedInfo);
    if (renderedType.isEmpty())
        return rejected(
            rtledit::ExposeSignalFailureReason::SignalNotPropagatable,
            QStringLiteral(
                "Slang did not provide a fixed-width integral signal type."));

    QStringList sourceInstances;
    for (const DesignHierarchyNode& node : design.nodes) {
        if (node.moduleType != sourceNode.moduleType)
            continue;
        sourceInstances.append(node.instancePath);
        const auto* info =
            instanceTypeInfo(signal, node.instancePath);
        if (!info || !compatibleType(*selectedInfo, *info)) {
            return rejected(
                rtledit::ExposeSignalFailureReason::ParameterWidthConflict,
                QStringLiteral(
                    "Module %1 has an incompatible effective signal width "
                    "or type at instance %2.")
                    .arg(sourceNode.moduleType, node.instancePath));
        }
    }

    QString exportedPortName =
        query.exportedPortName.trimmed();
    const bool sourceIsOutput =
        signal.collectorKind
        == SymbolTaxonomy::CollectorKind::PortOutput;
    if (sourceIsOutput)
        exportedPortName = signal.name;
    if (!SafeRenameService::isValidIdentifier(exportedPortName))
        return rejected(
            rtledit::ExposeSignalFailureReason::InvalidRequest,
            QStringLiteral(
                "The exported port name is not a valid SystemVerilog identifier."));

    ExposeSignalToTopReport report;
    report.signalRecord = signal;
    report.sourceInstancePath = sourceNode.instancePath;
    report.targetInstancePath = targetNode.instancePath;
    report.exportedPortName = exportedPortName;

    QMap<QString, QString> definitionFileByModule;
    QMap<QString, QString> representativePathByModule;
    definitionFileByModule.insert(
        sourceNode.moduleType,
        normalizedFileName(sourceNode.definitionFile));
    representativePathByModule.insert(
        sourceNode.moduleType, sourceNode.instancePath);
    for (const auto& edge : std::as_const(path)) {
        const DesignHierarchyNode& parent = edge.second;
        const QString definitionFile =
            normalizedFileName(parent.definitionFile);
        if (definitionFileByModule.contains(parent.moduleType)
            && definitionFileByModule.value(parent.moduleType)
                != definitionFile) {
            return rejected(
                rtledit::ExposeSignalFailureReason::AmbiguousHierarchy,
                QStringLiteral(
                    "Module %1 resolves to multiple definitions.")
                    .arg(parent.moduleType));
        }
        definitionFileByModule.insert(
            parent.moduleType, definitionFile);
        representativePathByModule.insert(
            parent.moduleType, parent.instancePath);
    }

    QMap<QString, PortDecision> decisions;
    QMap<QString, SemanticSymbolRecord> moduleRecords;
    QMap<QString, rtledit::ExposeSignalPortSite> portSites;
    for (auto it = definitionFileByModule.constBegin();
         it != definitionFileByModule.constEnd(); ++it) {
        const auto moduleRecord = exactModuleRecord(
            semantic, it.key(), it.value(), workspaceFiles);
        if (!moduleRecord) {
            return rejected(
                rtledit::ExposeSignalFailureReason::MissingSource,
                QStringLiteral(
                    "Slang did not provide a unique source definition for "
                    "module %1.")
                    .arg(it.key()));
        }
        const auto document =
            documents.get(moduleRecord->location.fileName);
        if (!document)
            return rejected(
                rtledit::ExposeSignalFailureReason::MissingSource,
                QStringLiteral(
                    "Source file for module %1 is unavailable.")
                    .arg(it.key()));
        if (fileReadOnly(document->fileName))
            return rejected(
                rtledit::ExposeSignalFailureReason::ReadOnlyFile,
                QStringLiteral("Source file is read-only: %1")
                    .arg(document->fileName));
        if (!sourceMappedName(document, *moduleRecord)
            || document->syntax->enclosingModuleName(
                   moduleRecord->location.position)
                != it.key()) {
            return rejected(
                rtledit::ExposeSignalFailureReason::MacroExpansion,
                QStringLiteral(
                    "Module %1 has no stable Tree-sitter source mapping.")
                    .arg(it.key()));
        }

        PortDecision decision;
        if (sourceIsOutput
            && it.key() == sourceNode.moduleType) {
            decision = PortDecision{
                rtledit::ExposeEndpointState::Reuse,
                rtledit::ExposeSignalFailureReason::None, {}};
            decision.existingPort = signal;
        } else {
            decision = existingPortDecision(
                semantic, it.key(), exportedPortName,
                *selectedInfo, design.nodes, workspaceFiles);
        }
        if (decision.state
            == rtledit::ExposeEndpointState::Conflict) {
            return rejected(decision.reason, decision.message);
        }
        if (it.key() != sourceNode.moduleType
            && decision.state
                == rtledit::ExposeEndpointState::Reuse
            && decision.existingPort) {
            QString driverFailure;
            if (allInstanceDriversProvenZero(
                    *decision.existingPort,
                    it.key(),
                    design.nodes,
                    &driverFailure)) {
                // Proven undriven in every elaborated instance.
            } else {
                return rejected(
                    rtledit::ExposeSignalFailureReason::DriverConflict,
                    driverFailure);
            }
        }
        decisions.insert(it.key(), decision);
        moduleRecords.insert(it.key(), *moduleRecord);
        rtledit::ExposeSignalPortSite site = buildPortSite(
            it.key(), representativePathByModule.value(it.key()),
            exportedPortName, renderedType, decision,
            *moduleRecord, &documents, design.snapshotGeneration);
        if (decision.state == rtledit::ExposeEndpointState::Add
            && !site.insertion.present()) {
            return rejected(
                rtledit::ExposeSignalFailureReason::UnsupportedPortList,
                QStringLiteral(
                    "Module %1 does not have a precise editable ANSI port list.")
                    .arg(it.key()));
        }
        portSites.insert(it.key(), std::move(site));
    }

    // Adding a port affects every instantiation of the definition. Verify
    // every such instance is named-port syntax before producing any edit.
    QSet<QString> inspectedInstantiationSites;
    for (const DesignHierarchyNode& node : design.nodes) {
        if (node.isTop
            || !decisions.contains(node.moduleType)
            || decisions.value(node.moduleType).state
                != rtledit::ExposeEndpointState::Add) {
            continue;
        }
        if (!nodesById.contains(node.parentId))
            continue;
        const DesignHierarchyNode parent =
            nodesById.value(node.parentId);
        const auto instanceRecord = exactInstanceRecord(
            semantic, node, parent, workspaceFiles);
        if (!instanceRecord)
            return rejected(
                rtledit::ExposeSignalFailureReason::UnsupportedGenerateSourceMap,
                QStringLiteral(
                    "Instance %1 has no unique Slang source mapping.")
                    .arg(node.instancePath));
        const QString siteKey =
            normalizedFileName(instanceRecord->location.fileName)
            + QLatin1Char(':')
            + QString::number(instanceRecord->location.position);
        if (inspectedInstantiationSites.contains(siteKey))
            continue;
        inspectedInstantiationSites.insert(siteKey);
        const auto document =
            documents.get(instanceRecord->location.fileName);
        if (!document || !sourceMappedName(document, *instanceRecord))
            return rejected(
                rtledit::ExposeSignalFailureReason::MacroExpansion,
                QStringLiteral(
                    "Instance %1 cannot be mapped to editable source.")
                    .arg(node.instancePath));
        const TSNamedPortConnectionTarget target =
            document->syntax->namedPortConnectionTarget(
                instanceRecord->location.position,
                exportedPortName);
        if (target.status
            == TSNamedPortConnectionStatus::PositionalConnections) {
            return rejected(
                rtledit::ExposeSignalFailureReason::PositionalInstantiation,
                QStringLiteral(
                    "Adding port %1 would affect positional instance %2.")
                    .arg(exportedPortName, node.instancePath));
        }
        if (target.status
                == TSNamedPortConnectionStatus::AlreadyConnected
            || target.status
                == TSNamedPortConnectionStatus::NoInstantiation
            || target.status
                == TSNamedPortConnectionStatus::NoClearConnectionPoint) {
            return rejected(
                rtledit::ExposeSignalFailureReason::UnsupportedInstanceConnection,
                QStringLiteral(
                    "Instance %1 has no precise compatible named-port "
                    "connection structure.")
                    .arg(node.instancePath));
        }
    }

    rtledit::ResolvedExposeSignalToTop resolved;
    resolved.semanticGeneration = design.snapshotGeneration;
    resolved.signalName = utf8String(signal.name);
    resolved.sourceModuleName = utf8String(sourceNode.moduleType);
    resolved.sourceInstancePath =
        utf8String(sourceNode.instancePath);
    resolved.targetAncestorInstancePath =
        utf8String(targetNode.instancePath);
    resolved.renderedPortDataType = utf8String(renderedType);
    resolved.bitWidth =
        static_cast<std::size_t>(selectedInfo->bitWidth);
    resolved.sourcePort =
        portSites.value(sourceNode.moduleType);
    resolved.sourceBridge.moduleName =
        utf8String(sourceNode.moduleType);
    resolved.sourceBridge.outputPortName =
        utf8String(exportedPortName);
    resolved.sourceBridge.signalName =
        utf8String(signal.name);

    if (sourceIsOutput) {
        resolved.sourceBridge.state =
            rtledit::ExposeEndpointState::Reuse;
    } else if (resolved.sourcePort.state
               == rtledit::ExposeEndpointState::Add) {
        resolved.sourceBridge.state =
            rtledit::ExposeEndpointState::Add;
        const TSSignalInsertTarget bridgeTarget =
            contextDocument->syntax->sourceBridgeInsertTarget(
                signal.location.position);
        if (!bridgeTarget.ok())
            return rejected(
                rtledit::ExposeSignalFailureReason::MissingStructuralAnchor,
                QStringLiteral(
                    "The source module bridge insertion point is unavailable."));
        const auto parts =
            insertionTemplateParts(
                bridgeTarget.insertText,
                bridgeTarget.caretCharAfterEdit
                    - bridgeTarget.insertChar);
        resolved.sourceBridge.insertion = insertionAnchor(
            contextDocument, bridgeTarget.insertChar,
            parts.first, parts.second,
            design.snapshotGeneration);
    } else {
        const PortDecision sourcePortDecision =
            decisions.value(sourceNode.moduleType);
        if (!sourcePortDecision.existingPort) {
            resolved.sourceBridge.state =
                rtledit::ExposeEndpointState::Conflict;
            resolved.sourceBridge.conflictReason =
                rtledit::ExposeSignalFailureReason::DriverConflict;
            resolved.sourceBridge.conflictMessage =
                "The existing source output has no stable semantic identity.";
        } else {
            const QList<SemanticRelationship> drivers =
                assignmentDrivers(
                    semantic, *sourcePortDecision.existingPort);
            QString driverSummaryFailure;
            const bool exactRelationship =
                isExactSourceBridge(
                    drivers,
                    signal,
                    *sourcePortDecision.existingPort);
            const bool exactDriverSummary =
                hasSingleExactContinuousDriver(
                    *sourcePortDecision.existingPort,
                    sourceNode.moduleType,
                    design.nodes,
                    &driverSummaryFailure);
            if (exactRelationship && exactDriverSummary) {
                resolved.sourceBridge.state =
                    rtledit::ExposeEndpointState::Reuse;
            } else {
                resolved.sourceBridge.state =
                    rtledit::ExposeEndpointState::Conflict;
                resolved.sourceBridge.conflictReason =
                    rtledit::ExposeSignalFailureReason::DriverConflict;
                if (drivers.isEmpty()) {
                    resolved.sourceBridge.conflictMessage =
                        "The existing source output has no Slang-proven "
                        "identity assignment; automatic reuse is unsafe.";
                } else if (!exactRelationship) {
                    resolved.sourceBridge.conflictMessage =
                        "The existing source output is driven by a "
                        "different or non-identity Slang expression.";
                } else {
                    resolved.sourceBridge.conflictMessage =
                        utf8String(driverSummaryFailure);
                }
            }
        }
    }

    for (int index = 0; index < path.size(); ++index) {
        const DesignHierarchyNode& child = path.at(index).first;
        const DesignHierarchyNode& parent = path.at(index).second;
        const auto instanceRecord = exactInstanceRecord(
            semantic, child, parent, workspaceFiles);
        if (!instanceRecord)
            return rejected(
                rtledit::ExposeSignalFailureReason::UnsupportedGenerateSourceMap,
                QStringLiteral(
                    "Hierarchy step %1 has no unique Slang source mapping.")
                    .arg(child.instancePath));
        const auto document =
            documents.get(instanceRecord->location.fileName);
        if (!document || fileReadOnly(document->fileName))
            return rejected(
                fileReadOnly(instanceRecord->location.fileName)
                    ? rtledit::ExposeSignalFailureReason::ReadOnlyFile
                    : rtledit::ExposeSignalFailureReason::MissingSource,
                QStringLiteral(
                    "Hierarchy step source is unavailable or read-only: %1")
                    .arg(instanceRecord->location.fileName));
        if (!sourceMappedName(document, *instanceRecord))
            return rejected(
                rtledit::ExposeSignalFailureReason::MacroExpansion,
                QStringLiteral(
                    "Hierarchy step %1 is not stably source-mapped.")
                    .arg(child.instancePath));

        const TSNamedPortConnectionTarget target =
            document->syntax->namedPortConnectionTarget(
                instanceRecord->location.position,
                exportedPortName);
        rtledit::ExposeSignalConnectionSite connection;
        connection.childModuleName =
            utf8String(child.moduleType);
        connection.childInstanceName =
            utf8String(child.instanceName);
        connection.childInstancePath =
            utf8String(child.instancePath);
        connection.parentModuleName =
            utf8String(parent.moduleType);
        connection.parentInstancePath =
            utf8String(parent.instancePath);
        connection.portName = utf8String(exportedPortName);
        connection.expressionName =
            utf8String(exportedPortName);
        if (target.status
            == TSNamedPortConnectionStatus::PositionalConnections) {
            return rejected(
                rtledit::ExposeSignalFailureReason::PositionalInstantiation,
                QStringLiteral(
                    "Hierarchy instance %1 uses positional connections.")
                    .arg(child.instancePath));
        }
        if (target.status
            == TSNamedPortConnectionStatus::AlreadyConnected) {
            if (target.existingActual != exportedPortName) {
                return rejected(
                    rtledit::ExposeSignalFailureReason::ConnectionConflict,
                    QStringLiteral(
                        "Existing connection .%1(%2) at %3 is incompatible.")
                        .arg(exportedPortName,
                             target.existingActual,
                             child.instancePath));
            }
            connection.state =
                rtledit::ExposeEndpointState::Reuse;
        } else if (target.canInsert()) {
            connection.state =
                rtledit::ExposeEndpointState::Add;
            connection.insertion = insertionAnchor(
                document, target.insertChar,
                target.prefix, target.suffix,
                design.snapshotGeneration);
            if (target.needsTrailingComma) {
                connection.delimiterInsertion = insertionAnchor(
                    document, target.trailingCommaInsertChar,
                    {}, {}, design.snapshotGeneration);
            }
        } else {
            return rejected(
                rtledit::ExposeSignalFailureReason::UnsupportedInstanceConnection,
                QStringLiteral(
                    "Hierarchy instance %1 has no precise named-port anchor.")
                    .arg(child.instancePath));
        }

        rtledit::SignalPropagationStep step;
        step.index = static_cast<std::size_t>(index);
        step.childModuleName = utf8String(child.moduleType);
        step.childInstanceName = utf8String(child.instanceName);
        step.childInstancePath = utf8String(child.instancePath);
        step.parentModuleName = utf8String(parent.moduleType);
        step.parentInstancePath = utf8String(parent.instancePath);
        step.instanceSourceFilePath =
            utf8String(document->fileName);
        step.instanceSourceRange = utf8Range(
            document->text,
            instanceRecord->location.position,
            instanceRecord->location.position
                + instanceRecord->location.length);
        step.parentPort = portSites.value(parent.moduleType);
        step.connection = std::move(connection);
        resolved.path.steps.push_back(step);

        ExposeSignalHierarchyStepView view;
        view.index = index;
        view.childInstancePath = child.instancePath;
        view.childModule = child.moduleType;
        view.childInstance = child.instanceName;
        view.parentInstancePath = parent.instancePath;
        view.parentModule = parent.moduleType;
        view.sourceFile = document->fileName;
        report.hierarchySteps.append(view);
    }

    for (auto it = definitionFileByModule.constBegin();
         it != definitionFileByModule.constEnd(); ++it) {
        rtledit::AffectedModuleImpact impact;
        impact.moduleName = utf8String(it.key());
        impact.definitionFilePath = utf8String(it.value());
        for (const DesignHierarchyNode& node : design.nodes) {
            if (node.moduleType != it.key())
                continue;
            impact.instancePaths.push_back(
                utf8String(node.instancePath));
            report.affectedInstancePaths.append(
                node.instancePath);
        }
        std::sort(impact.instancePaths.begin(),
                  impact.instancePaths.end());
        impact.instancePaths.erase(
            std::unique(impact.instancePaths.begin(),
                        impact.instancePaths.end()),
            impact.instancePaths.end());
        resolved.impact.push_back(std::move(impact));
        report.affectedModules.append(it.key());
        report.affectedFiles.append(it.value());
    }
    report.affectedModules =
        uniqueSorted(report.affectedModules);
    report.affectedFiles =
        uniqueSorted(report.affectedFiles);
    report.affectedInstancePaths =
        uniqueSorted(report.affectedInstancePaths);

    const rtledit::SourceRange signalRange = utf8Range(
        contextDocument->text,
        signal.location.position,
        signal.location.position + signal.location.length);
    rtledit::ExposeSignalToTopRequest request;
    request.signalSemanticId.kind = sourceIsOutput
        ? rtledit::SemanticObjectKind::Port
        : rtledit::SemanticObjectKind::Signal;
    request.signalSemanticId.qualifiedName =
        utf8String(sourceNode.instancePath
                   + QLatin1Char('.') + signal.name);
    request.signalSemanticId.ownerScope =
        utf8String(signal.owner.name);
    request.signalSemanticId.filePath =
        utf8String(contextDocument->fileName);
    request.signalSemanticId.range = signalRange;
    request.signalSemanticId.signatureHash =
        utf8String(signal.stableKey.toString());
    request.sourceInstancePath =
        utf8String(sourceNode.instancePath);
    request.targetAncestorInstancePath =
        utf8String(targetNode.instancePath);
    request.exportedPortName =
        utf8String(exportedPortName);
    request.semanticGeneration =
        design.snapshotGeneration;

    for (const auto& document : documents.values()) {
        request.documentVersions.push_back(
            rtledit::CapturedDocumentVersion{
                utf8String(document->fileName),
                document->version});
    }
    for (const QString& file : std::as_const(workspaceFiles))
        resolved.semanticIndexFilePaths.push_back(utf8String(file));

    report.planResult =
        rtledit::planExposeSignalToTop(request, resolved);
    if (!report.planResult.ready()) {
        rejectInto(
            &report,
            report.planResult.failureReason,
            fromUtf8String(report.planResult.message));
        return report;
    }

    // Formatting remains a single ZeroSlack fact source. Only generated
    // snippets are passed through the shared snippet formatter; anchors,
    // semantic decisions, and multi-file application remain in rtleditcore.
    auto& workspaceEdit =
        report.planResult.plan.workspaceEdit;
    for (std::size_t index = 0;
         index < workspaceEdit.edits.size()
         && index < workspaceEdit.provenance.size();
         ++index) {
        const std::string& anchorName =
            workspaceEdit.provenance[index].anchorName;
        if (anchorName != "port.insert"
            && anchorName != "source.bridge"
            && anchorName != "connection.insert") {
            continue;
        }
        const FormatterReport formatted =
            FormatterService::getInstance()->formatSnippet(
                fromUtf8String(workspaceEdit.edits[index].newText));
        workspaceEdit.edits[index].newText =
            utf8String(formatted.formattedText);
    }

    report.transaction =
        WorkspaceEditTransactionService::getInstance()->prepare(
            report.planResult.plan.workspaceEdit,
            rtledit::SemanticIndexSnapshot{
                std::to_string(design.snapshotGeneration)},
            documentManager);
    report.sourceDiff = report.transaction.sourceDiff;
    if (!report.sourceDiff.built()) {
        rejectInto(
            &report,
            rtledit::ExposeSignalFailureReason::DocumentVersionConflict,
            QStringLiteral(
                "The workspace diff could not be built from the captured "
                "document versions: %1")
                .arg(fromUtf8String(report.sourceDiff.message)));
        return report;
    }
    report.renderedDiff = fromUtf8String(
        rtledit::renderWorkspaceEditSourceDiffHunks(
            report.sourceDiff));
    report.status = ExposeSignalToTopReportStatus::Ready;
    report.failureReason =
        rtledit::ExposeSignalFailureReason::None;
    report.message = fromUtf8String(report.planResult.message);
    return report;
}

ExposeSignalToTopApplyReport ExposeSignalToTopService::apply(
    const ExposeSignalToTopReport& report,
    rtledit::WorkspaceDocumentManager& documents) const
{
    ExposeSignalToTopApplyReport result;
    if (!report.ready()) {
        result.result.status = rtledit::PlanApplyStatus::Invalid;
        result.message = QStringLiteral(
            "Expose signal plan is not ready.");
        return result;
    }
    const std::uint64_t generation =
        semanticIndex()->snapshotRevision();
    result.transactionResult =
        WorkspaceEditTransactionService::getInstance()
            ->applyConfirmed(
                report.transaction,
                rtledit::SemanticIndexSnapshot{
                    std::to_string(generation)},
                documents);
    result.result = result.transactionResult.applyResult;
    result.message =
        fromUtf8String(result.transactionResult.message);
    return result;
}
