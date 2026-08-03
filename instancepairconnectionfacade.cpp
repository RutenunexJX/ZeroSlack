#include "instancepairconnectionfacade.h"

#include "definitionservice.h"
#include "saferenameservice.h"
#include "semanticindexsnapshot.h"
#include "tsdocument.h"
#include "workspaceedittransactionservice.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QMap>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr char kActionId[] = "signal.connectInstancePair";
constexpr char kResolver[] =
    "ZeroSlack.InstancePairConnectionFacade/TSDocument";

QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return {};
    return QDir::cleanPath(
        QDir::fromNativeSeparators(
            QFileInfo(fileName).absoluteFilePath()));
}

std::string utf8String(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(
        bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
}

QString fromUtf8String(const std::string& text)
{
    return QString::fromUtf8(
        text.data(), static_cast<qsizetype>(text.size()));
}

QStringList uniqueSorted(QStringList values)
{
    std::sort(values.begin(), values.end());
    values.erase(
        std::unique(values.begin(), values.end()),
        values.end());
    return values;
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

bool sameToken(const SemanticSnapshotToken& left,
               const SemanticSnapshotToken& right)
{
    return left.isValid()
        && right.isValid()
        && left.revision == right.revision
        && left.snapshot == right.snapshot;
}

InstancePairConnectionAnalysis rejectedAnalysis(
    const InstancePairConnectionQuery& query,
    InstancePairConnectionFailure failure,
    const QString& message)
{
    InstancePairConnectionAnalysis result;
    result.query = query;
    result.failure = failure;
    result.message = message;
    if (!message.isEmpty())
        result.blockers.append(message);
    return result;
}

InstancePairConnectionProposal rejectedProposal(
    const InstancePairConnectionAnalysis& analysis,
    InstancePairConnectionFailure failure,
    const QString& message)
{
    InstancePairConnectionProposal result;
    result.failure = failure;
    result.message = message;
    result.dryRun = analysis.query.dryRun;
    result.blockView = analysis.blockView;
    result.leftSignal = analysis.leftSignal;
    result.connectionName =
        analysis.query.connectionName.trimmed();
    result.renderedSignalType =
        analysis.renderedSignalType;
    result.steps = analysis.steps;
    if (!message.isEmpty())
        result.blockers.append(message);
    return result;
}

std::optional<QHash<QString, InstancePairDocumentSnapshot>>
normalizedCapturedDocuments(
    const InstancePairConnectionQuery& query)
{
    QHash<QString, InstancePairDocumentSnapshot> result;
    for (auto it = query.documents.constBegin();
         it != query.documents.constEnd(); ++it) {
        InstancePairDocumentSnapshot document = it.value();
        const QString fileName = normalizedFileName(
            document.fileName.isEmpty()
                ? it.key() : document.fileName);
        document.fileName = fileName;
        if (!document.isValid()
            || result.contains(fileName)) {
            return std::nullopt;
        }
        result.insert(fileName, std::move(document));
    }
    return result;
}

QHash<QString, QString> normalizedSemanticContents(
    const SemanticIndexSnapshot& snapshot)
{
    QHash<QString, QString> result;
    for (auto it = snapshot.fileContentsView().constBegin();
         it != snapshot.fileContentsView().constEnd(); ++it) {
        const QString fileName =
            normalizedFileName(it.key());
        if (!fileName.isEmpty())
            result.insert(fileName, it.value());
    }
    return result;
}

bool validateCapturedDocuments(
    const InstancePairConnectionQuery& query,
    const QHash<QString, InstancePairDocumentSnapshot>& captured,
    const rtledit::WorkspaceDocumentManager& documents,
    InstancePairConnectionFailure* failure,
    QString* message)
{
    auto fail = [&](InstancePairConnectionFailure reason,
                    const QString& text) {
        if (failure)
            *failure = reason;
        if (message)
            *message = text;
        return false;
    };

    if (!query.semanticToken.isValid()) {
        return fail(
            InstancePairConnectionFailure::MissingSemanticSnapshot,
            QStringLiteral(
                "A captured Slang semantic snapshot is required."));
    }

    QSet<QString> required =
        normalizedFiles(query.workspaceFiles);
    if (required.isEmpty()) {
        for (auto it = captured.constBegin();
             it != captured.constEnd(); ++it) {
            required.insert(it.key());
        }
    }
    if (required.isEmpty()) {
        return fail(
            InstancePairConnectionFailure::MissingDocumentSnapshot,
            QStringLiteral(
                "No workspace document snapshots were captured."));
    }

    const QHash<QString, QString> semanticContents =
        normalizedSemanticContents(
            *query.semanticToken.snapshot);
    for (const QString& fileName : std::as_const(required)) {
        const auto found = captured.constFind(fileName);
        if (found == captured.constEnd()) {
            return fail(
                InstancePairConnectionFailure::
                    MissingDocumentSnapshot,
                QStringLiteral(
                    "No captured live document exists for %1.")
                    .arg(fileName));
        }
        if (found->unsaved) {
            return fail(
                InstancePairConnectionFailure::
                    StaleSemanticSource,
                QStringLiteral(
                    "High-risk instance connection requires saved "
                    "source matching the Slang snapshot: %1.")
                    .arg(fileName));
        }
        const auto live =
            documents.snapshot(utf8String(fileName));
        if (!live) {
            return fail(
                InstancePairConnectionFailure::
                    MissingDocumentSnapshot,
                QStringLiteral(
                    "The live document is unavailable: %1.")
                    .arg(fileName));
        }
        if (live->version.value != found->revision
            || fromUtf8String(live->text)
                   != found->text) {
            return fail(
                InstancePairConnectionFailure::
                    StaleDocumentRevision,
                QStringLiteral(
                    "The live document changed after capture: %1.")
                    .arg(fileName));
        }
        if (!found->syntax
            || found->syntax->text() != found->text) {
            return fail(
                InstancePairConnectionFailure::
                    InvalidTreeSnapshot,
                QStringLiteral(
                    "The Tree-sitter tree does not describe the "
                    "captured revision: %1.")
                    .arg(fileName));
        }
        if (found->syntax->hasError()) {
            return fail(
                InstancePairConnectionFailure::SyntaxError,
                QStringLiteral(
                    "The captured SystemVerilog syntax is incomplete: %1.")
                    .arg(fileName));
        }
        const auto semantic =
            semanticContents.constFind(fileName);
        if (semantic == semanticContents.constEnd()
            || semantic.value() != found->text) {
            return fail(
                InstancePairConnectionFailure::
                    StaleSemanticSource,
                QStringLiteral(
                    "The saved source no longer matches the Slang "
                    "snapshot: %1.")
                    .arg(fileName));
        }
    }

    for (const SemanticDiagnostic& diagnostic :
         query.semanticToken.snapshot->diagnostics()) {
        if (diagnostic.severity
                != SemanticDiagnostic::Error
            || !required.contains(
                normalizedFileName(
                    diagnostic.fileName))) {
            continue;
        }
        return fail(
            InstancePairConnectionFailure::SemanticErrors,
            QStringLiteral(
                "The captured Slang snapshot contains an error in %1.")
                .arg(normalizedFileName(
                    diagnostic.fileName)));
    }
    return true;
}

const InstancePairDocumentSnapshot* capturedDocument(
    const QHash<QString, InstancePairDocumentSnapshot>& documents,
    const QString& fileName)
{
    const auto found =
        documents.constFind(normalizedFileName(fileName));
    return found == documents.constEnd()
        ? nullptr : &found.value();
}

rtledit::SourcePosition utf8PositionAt(
    const QString& text,
    int charOffset)
{
    const int bounded =
        qBound(0, charOffset, text.size());
    int line = 0;
    int lineStart = 0;
    for (int index = 0; index < bounded; ++index) {
        if (text.at(index) != QLatin1Char('\n'))
            continue;
        ++line;
        lineStart = index + 1;
    }
    return {
        static_cast<std::size_t>(line),
        static_cast<std::size_t>(
            text.mid(lineStart, bounded - lineStart)
                .toUtf8().size())};
}

rtledit::SourceRange utf8Range(
    const QString& text,
    int startChar,
    int endChar)
{
    const int start =
        qBound(0, startChar, text.size());
    const int end =
        qBound(start, endChar, text.size());
    return {
        utf8PositionAt(text, start),
        utf8PositionAt(text, end)};
}

rtledit::AnchorProvenance treeSitterProvenance(
    std::uint64_t generation)
{
    rtledit::AnchorProvenance result;
    result.source =
        rtledit::AnchorResolutionSource::TreeSitter;
    result.resolver = kResolver;
    result.semanticSnapshotId =
        std::to_string(generation);
    return result;
}

rtledit::StructuredInsertionAnchor insertionAnchor(
    const InstancePairDocumentSnapshot& document,
    int charOffset,
    const QString& prefix,
    const QString& suffix,
    std::uint64_t generation)
{
    rtledit::StructuredInsertionAnchor result;
    if (charOffset < 0
        || charOffset > document.text.size()) {
        return result;
    }
    result.filePath = utf8String(document.fileName);
    result.documentVersion = {document.revision};
    result.range =
        utf8Range(document.text, charOffset, charOffset);
    result.prefix = utf8String(prefix);
    result.suffix = utf8String(suffix);
    result.provenance =
        treeSitterProvenance(generation);
    return result;
}

QPair<QString, QString> insertionTemplateParts(
    const QString& insertionText,
    int caretOffset)
{
    if (caretOffset < 0
        || caretOffset > insertionText.size()) {
        return {insertionText, {}};
    }
    return {
        insertionText.left(caretOffset),
        insertionText.mid(caretOffset)};
}

bool sourceMappedName(
    const InstancePairDocumentSnapshot* document,
    const SemanticSymbolRecord& record)
{
    if (!document || record.location.position < 0
        || record.location.length <= 0
        || record.location.position
                   + record.location.length
               > document->text.size()) {
        return false;
    }
    return document->text.mid(
               record.location.position,
               record.location.length)
        == record.name;
}

bool recordInWorkspace(
    const SemanticSymbolRecord& record,
    const QSet<QString>& workspaceFiles)
{
    return workspaceFiles.isEmpty()
        || workspaceFiles.contains(
            normalizedFileName(
                record.location.fileName));
}

bool propagatableCollector(
    SymbolTaxonomy::CollectorKind kind)
{
    using Collector = SymbolTaxonomy::CollectorKind;
    return kind == Collector::Logic
        || kind == Collector::Wire
        || kind == Collector::Reg
        || kind == Collector::PortOutput;
}

bool localSignalCollector(
    SymbolTaxonomy::CollectorKind kind)
{
    using Collector = SymbolTaxonomy::CollectorKind;
    return kind == Collector::Logic
        || kind == Collector::Wire
        || kind == Collector::Reg;
}

bool compatibleType(
    const SemanticElaboratedSymbolInfo& left,
    const SemanticElaboratedSymbolInfo& right)
{
    return left.available && right.available
        && left.fixedSize && right.fixedSize
        && left.integral && right.integral
        && !left.unpackedArray && !right.unpackedArray
        && !left.interfaceType && !right.interfaceType
        && left.bitWidth > 0
        && left.bitWidth == right.bitWidth
        && left.signedIntegral == right.signedIntegral;
}

QString renderedLogicType(
    const SemanticElaboratedSymbolInfo& info)
{
    if (!info.available || !info.fixedSize
        || !info.integral || info.unpackedArray
        || info.interfaceType || info.bitWidth == 0) {
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

const SemanticElaboratedSymbolInfo* instanceTypeInfo(
    const SemanticSymbolRecord& record,
    const QString& instancePath)
{
    const auto found =
        record.presentation.instanceInfoByPath.constFind(
            instancePath);
    return found
                == record.presentation.instanceInfoByPath.constEnd()
        ? nullptr : &found.value();
}

std::optional<SemanticSymbolRecord> exactModuleRecord(
    SemanticIndex* index,
    const DesignHierarchyNode& node,
    const QSet<QString>& workspaceFiles)
{
    if (!index)
        return std::nullopt;
    QList<SemanticSymbolRecord> matches;
    for (const SemanticSymbolRecord& record :
         index->getSymbolRecordsByName(node.moduleType)) {
        if (record.declarationKind
                != SymbolTaxonomy::DeclarationKind::Module
            || normalizedFileName(
                   record.location.fileName)
                   != normalizedFileName(
                       node.definitionFile)
            || !recordInWorkspace(
                record, workspaceFiles)) {
            continue;
        }
        matches.append(record);
    }
    return matches.size() == 1
        ? std::optional<SemanticSymbolRecord>(
              matches.constFirst())
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
    for (const SemanticSymbolRecord& record :
         index->getSymbolRecordsByName(
             child.instanceName)) {
        if (record.declarationKind
                != SymbolTaxonomy::DeclarationKind::Instance
            || record.owner.kind
                != SymbolTaxonomy::SymbolOwnerScope::Module
            || record.owner.name != parent.moduleType
            || record.type.resolvedTypeName
                != child.moduleType
            || normalizedFileName(
                   record.location.fileName)
                   != normalizedFileName(
                       child.instanceFile)
            || !recordInWorkspace(
                record, workspaceFiles)) {
            continue;
        }
        matches.append(record);
    }
    return matches.size() == 1
        ? std::optional<SemanticSymbolRecord>(
              matches.constFirst())
        : std::nullopt;
}

std::optional<SemanticSymbolRecord> selectedSignalRecord(
    SemanticIndex* semantic,
    const EditorSemanticContext& context,
    const InstancePairDocumentSnapshot& document)
{
    const TSIdentifierTarget identifier =
        document.syntax->identifierAt(
            context.cursorPosition);
    if (!identifier.ok())
        return std::nullopt;

    QList<SemanticSymbolRecord> exact;
    for (const SemanticSymbolRecord& candidate :
         semantic->getSymbolRecordsByName(
             identifier.text)) {
        if (candidate.owner.kind
                != SymbolTaxonomy::SymbolOwnerScope::Module
            || candidate.owner.name != context.moduleName
            || normalizedFileName(
                   candidate.location.fileName)
                   != document.fileName) {
            continue;
        }
        if (context.cursorPosition
                >= candidate.location.position
            && context.cursorPosition
                <= candidate.location.position
                       + candidate.location.length) {
            exact.append(candidate);
        }
    }
    if (exact.size() == 1)
        return exact.constFirst();

    DefinitionQuery query;
    query.symbolName = identifier.text;
    query.fileName = document.fileName;
    query.moduleName = context.moduleName;
    query.linePrefixBeforeCursor =
        context.lineUpToCursor;
    query.cursorLine = context.cursorLine;
    query.cursorColumn = context.column;
    const DefinitionResult definition =
        DefinitionService(semantic).resolveDefinition(query);
    return definition.found
        ? std::optional<SemanticSymbolRecord>(
              definition.symbolRecord)
        : std::nullopt;
}

bool ancestorEdges(
    const DesignHierarchyNode& source,
    const DesignHierarchyNode& ancestor,
    const QHash<QString, DesignHierarchyNode>& nodesById,
    QList<QPair<DesignHierarchyNode, DesignHierarchyNode>>* edges)
{
    if (!edges)
        return false;
    edges->clear();
    DesignHierarchyNode current = source;
    QSet<QString> visited;
    while (current.id != ancestor.id) {
        if (current.id.isEmpty()
            || current.parentId.isEmpty()
            || visited.contains(current.id)
            || !nodesById.contains(
                current.parentId)) {
            return false;
        }
        visited.insert(current.id);
        const DesignHierarchyNode parent =
            nodesById.value(current.parentId);
        edges->append({current, parent});
        current = parent;
    }
    return true;
}

std::optional<DesignHierarchyNode> lowestCommonAncestor(
    const DesignHierarchyNode& left,
    const DesignHierarchyNode& right,
    const QHash<QString, DesignHierarchyNode>& nodesById)
{
    QSet<QString> leftAncestors;
    DesignHierarchyNode current = left;
    QSet<QString> visited;
    while (!current.id.isEmpty()
           && !visited.contains(current.id)) {
        visited.insert(current.id);
        leftAncestors.insert(current.id);
        if (current.parentId.isEmpty())
            break;
        if (!nodesById.contains(current.parentId))
            return std::nullopt;
        current = nodesById.value(current.parentId);
    }

    current = right;
    visited.clear();
    while (!current.id.isEmpty()
           && !visited.contains(current.id)) {
        if (leftAncestors.contains(current.id))
            return current;
        visited.insert(current.id);
        if (current.parentId.isEmpty())
            break;
        if (!nodesById.contains(current.parentId))
            return std::nullopt;
        current = nodesById.value(current.parentId);
    }
    return std::nullopt;
}

QList<InstancePairBlockPortView> portViews(
    const SemanticIndexSnapshot& snapshot,
    const DesignHierarchyNode& node)
{
    QList<InstancePairBlockPortView> result;
    for (const SemanticSymbolRecord& record :
         snapshot.symbolRecordsView()) {
        if (record.declarationKind
                != SymbolTaxonomy::DeclarationKind::Port
            || record.owner.kind
                != SymbolTaxonomy::SymbolOwnerScope::Module
            || record.owner.name != node.moduleType
            || normalizedFileName(
                   record.location.fileName)
                   != normalizedFileName(
                       node.definitionFile)) {
            continue;
        }
        InstancePairBlockPortView view;
        view.name = record.name;
        view.direction = record.collectorKind;
        view.record = record;
        if (const auto* info =
                instanceTypeInfo(
                    record, node.instancePath)) {
            view.effectiveType = *info;
        }
        result.append(std::move(view));
    }
    std::sort(
        result.begin(), result.end(),
        [](const auto& left, const auto& right) {
            return std::tie(
                       left.record.location.position,
                       left.name)
                < std::tie(
                       right.record.location.position,
                       right.name);
        });
    return result;
}

InstancePairBlockSideView blockSide(
    InstancePairSide side,
    const DesignHierarchyNode& node,
    const SemanticIndexSnapshot& snapshot)
{
    InstancePairBlockSideView result;
    result.side = side;
    result.instancePath = node.instancePath;
    result.instanceName = node.instanceName;
    result.moduleName = node.moduleType;
    result.definitionFile =
        normalizedFileName(node.definitionFile);
    result.instanceFile =
        normalizedFileName(node.instanceFile);
    result.ports = portViews(snapshot, node);
    return result;
}

bool nodeTypeIs(TSNode node, const char* expected)
{
    return !ts_node_is_null(node)
        && std::string_view(ts_node_type(node))
               == expected;
}

bool subtreeHasNodeType(
    TSNode root,
    const char* expected)
{
    if (nodeTypeIs(root, expected))
        return true;
    const uint32_t count =
        ts_node_child_count(root);
    for (uint32_t index = 0;
         index < count; ++index) {
        if (subtreeHasNodeType(
                ts_node_child(root, index),
                expected)) {
            return true;
        }
    }
    return false;
}

bool instanceUsesWildcard(
    const TSDocument& syntax,
    int charOffset)
{
    TSNode root = syntax.rootNode();
    const int bounded =
        qBound(0, charOffset, syntax.text().size());
    const uint32_t byte =
        static_cast<uint32_t>(bounded) * 2u;
    TSNode node =
        ts_node_named_descendant_for_byte_range(
            root, byte, byte);
    while (!ts_node_is_null(node)
           && !nodeTypeIs(
               node, "module_instantiation")) {
        node = ts_node_parent(node);
    }
    return !ts_node_is_null(node)
        && subtreeHasNodeType(node, ".*");
}

std::optional<InstancePairResolvedConnectionSite>
resolveConnectionSite(
    SemanticIndex* semantic,
    const DesignHierarchyNode& child,
    const DesignHierarchyNode& parent,
    const QString& portName,
    const QSet<QString>& workspaceFiles,
    const QHash<QString, InstancePairDocumentSnapshot>& documents,
    std::uint64_t generation,
    InstancePairConnectionFailure* failure,
    QString* message)
{
    auto reject = [&](InstancePairConnectionFailure reason,
                      const QString& text)
        -> std::optional<InstancePairResolvedConnectionSite> {
        if (failure)
            *failure = reason;
        if (message)
            *message = text;
        return std::nullopt;
    };

    const auto instance = exactInstanceRecord(
        semantic, child, parent, workspaceFiles);
    if (!instance) {
        return reject(
            InstancePairConnectionFailure::AmbiguousHierarchy,
            QStringLiteral(
                "Instance %1 has no unique Slang source mapping.")
                .arg(child.instancePath));
    }
    const auto* document = capturedDocument(
        documents, instance->location.fileName);
    if (!document
        || !sourceMappedName(document, *instance)) {
        return reject(
            InstancePairConnectionFailure::MissingSource,
            QStringLiteral(
                "Instance %1 has no stable saved-source mapping.")
                .arg(child.instancePath));
    }
    if (QFileInfo(document->fileName).exists()
        && !QFileInfo(document->fileName).isWritable()) {
        return reject(
            InstancePairConnectionFailure::ReadOnlyFile,
            QStringLiteral(
                "Instance source is read-only: %1")
                .arg(document->fileName));
    }

    const TSNamedPortConnectionTarget target =
        document->syntax->namedPortConnectionTarget(
            instance->location.position, portName);
    InstancePairResolvedConnectionSite result;
    result.child = child;
    result.parent = parent;
    result.instanceRecord = *instance;
    if (target.status
        == TSNamedPortConnectionStatus::
            PositionalConnections) {
        return reject(
            InstancePairConnectionFailure::OrderedConnection,
            QStringLiteral(
                "Instance %1 uses ordered or mixed port connections.")
                .arg(child.instancePath));
    }
    if (target.status
        == TSNamedPortConnectionStatus::AlreadyConnected) {
        if (target.existingActual != portName) {
            return reject(
                InstancePairConnectionFailure::NameConflict,
                QStringLiteral(
                    "Existing connection .%1(%2) at %3 is incompatible.")
                    .arg(
                        portName,
                        target.existingActual,
                        child.instancePath));
        }
        result.state =
            rtledit::ExposeEndpointState::Reuse;
        return result;
    }
    if (!target.canInsert()) {
        if (instanceUsesWildcard(
                *document->syntax,
                instance->location.position)) {
            return reject(
                InstancePairConnectionFailure::WildcardConnection,
                QStringLiteral(
                    "Instance %1 uses wildcard port connections.")
                    .arg(child.instancePath));
        }
        return reject(
            InstancePairConnectionFailure::
                UnsupportedConnectionSyntax,
            QStringLiteral(
                "Instance %1 has no precise named-port anchor.")
                .arg(child.instancePath));
    }
    result.state = rtledit::ExposeEndpointState::Add;
    result.insertion = insertionAnchor(
        *document, target.insertChar,
        target.prefix, target.suffix,
        generation);
    if (target.needsTrailingComma) {
        result.delimiterInsertion = insertionAnchor(
            *document,
            target.trailingCommaInsertChar,
            {}, {}, generation);
    }
    return result;
}

std::optional<InstancePairResolvedPortSite>
resolveInputPortSite(
    SemanticIndex* semantic,
    const DesignHierarchyNode& node,
    const QString& portName,
    const SemanticElaboratedSymbolInfo& sourceType,
    const QSet<QString>& workspaceFiles,
    const QHash<QString, InstancePairDocumentSnapshot>& documents,
    std::uint64_t generation,
    InstancePairConnectionFailure* failure,
    QString* message)
{
    auto reject = [&](InstancePairConnectionFailure reason,
                      const QString& text)
        -> std::optional<InstancePairResolvedPortSite> {
        if (failure)
            *failure = reason;
        if (message)
            *message = text;
        return std::nullopt;
    };

    const auto module =
        exactModuleRecord(
            semantic, node, workspaceFiles);
    if (!module) {
        return reject(
            InstancePairConnectionFailure::MissingSource,
            QStringLiteral(
                "Slang did not provide a unique definition for module %1.")
                .arg(node.moduleType));
    }
    const auto* document = capturedDocument(
        documents, module->location.fileName);
    if (!document
        || !sourceMappedName(document, *module)
        || document->syntax->enclosingModuleName(
               module->location.position)
               != node.moduleType) {
        return reject(
            InstancePairConnectionFailure::MissingSource,
            QStringLiteral(
                "Module %1 has no stable saved-source mapping.")
                .arg(node.moduleType));
    }
    if (QFileInfo(document->fileName).exists()
        && !QFileInfo(document->fileName).isWritable()) {
        return reject(
            InstancePairConnectionFailure::ReadOnlyFile,
            QStringLiteral(
                "Module source is read-only: %1")
                .arg(document->fileName));
    }

    QList<SemanticSymbolRecord> collisions;
    for (const SemanticSymbolRecord& record :
         semantic->getSymbolRecordsByName(portName)) {
        if (record.owner.kind
                == SymbolTaxonomy::SymbolOwnerScope::Module
            && record.owner.name == node.moduleType
            && recordInWorkspace(
                record, workspaceFiles)) {
            collisions.append(record);
        }
    }

    InstancePairResolvedPortSite result;
    result.node = node;
    result.moduleRecord = *module;
    if (!collisions.isEmpty()) {
        if (collisions.size() != 1) {
            return reject(
                InstancePairConnectionFailure::NameConflict,
                QStringLiteral(
                    "Multiple declarations named %1 exist in module %2.")
                    .arg(portName, node.moduleType));
        }
        const SemanticSymbolRecord existing =
            collisions.constFirst();
        if (existing.declarationKind
                != SymbolTaxonomy::DeclarationKind::Port
            || existing.collectorKind
                != SymbolTaxonomy::CollectorKind::PortInput) {
            return reject(
                existing.declarationKind
                        == SymbolTaxonomy::DeclarationKind::Port
                    ? InstancePairConnectionFailure::
                          DirectionConflict
                    : InstancePairConnectionFailure::
                          NameConflict,
                existing.declarationKind
                        == SymbolTaxonomy::DeclarationKind::Port
                    ? QStringLiteral(
                          "Existing port %1 in module %2 is not an input.")
                          .arg(portName, node.moduleType)
                    : QStringLiteral(
                          "Name %1 already exists in module %2.")
                          .arg(portName, node.moduleType));
        }
        const auto* existingType =
            instanceTypeInfo(
                existing, node.instancePath);
        if (!existingType
            || !compatibleType(
                sourceType, *existingType)) {
            return reject(
                InstancePairConnectionFailure::TypeMismatch,
                QStringLiteral(
                    "Existing input %1 in module %2 has an incompatible "
                    "Slang effective type.")
                    .arg(portName, node.moduleType));
        }
        if (!sourceMappedName(document, existing)) {
            return reject(
                InstancePairConnectionFailure::MissingSource,
                QStringLiteral(
                    "Existing input %1 has no stable source mapping.")
                    .arg(portName));
        }
        result.state =
            rtledit::ExposeEndpointState::Reuse;
        result.existingPort = existing;
        return result;
    }

    const TSPortAppendTarget target =
        document->syntax->portAppendTarget(
            module->location.position);
    if (!target.ok()) {
        return reject(
            InstancePairConnectionFailure::UnsupportedPortList,
            QStringLiteral(
                "Module %1 does not have a precise editable ANSI port list.")
                .arg(node.moduleType));
    }
    const auto parts = insertionTemplateParts(
        target.insertText,
        target.caretCharAfterEdit
            - target.insertChar);
    result.state = rtledit::ExposeEndpointState::Add;
    result.insertion = insertionAnchor(
        *document, target.insertChar,
        parts.first, parts.second,
        generation);
    if (target.needsTrailingComma) {
        result.delimiterInsertion = insertionAnchor(
            *document,
            target.trailingCommaInsertChar,
            {}, {}, generation);
    }
    return result;
}

bool stableKeyEquals(
    const SymbolStableKey& left,
    const SymbolStableKey& right)
{
    return left.isValid() && right.isValid()
        && left == right;
}

std::optional<InstancePairResolvedLocalSignalSite>
resolveLocalSignalSite(
    SemanticIndex* semantic,
    const DesignHierarchyNode& lca,
    const DesignHierarchyNode& left,
    const SemanticSymbolRecord& leftSignal,
    const SemanticElaboratedSymbolInfo& sourceType,
    const QString& signalName,
    const InstancePairResolvedConnectionSite* leftConnection,
    const QSet<QString>& workspaceFiles,
    const QHash<QString, InstancePairDocumentSnapshot>& documents,
    std::uint64_t generation,
    InstancePairConnectionFailure* failure,
    QString* message)
{
    auto reject = [&](InstancePairConnectionFailure reason,
                      const QString& text)
        -> std::optional<InstancePairResolvedLocalSignalSite> {
        if (failure)
            *failure = reason;
        if (message)
            *message = text;
        return std::nullopt;
    };

    const auto module =
        exactModuleRecord(
            semantic, lca, workspaceFiles);
    if (!module) {
        return reject(
            InstancePairConnectionFailure::MissingSource,
            QStringLiteral(
                "Slang did not provide a unique LCA module definition."));
    }
    const auto* document = capturedDocument(
        documents, module->location.fileName);
    if (!document
        || !sourceMappedName(document, *module)) {
        return reject(
            InstancePairConnectionFailure::MissingSource,
            QStringLiteral(
                "The LCA module has no stable saved-source mapping."));
    }
    if (QFileInfo(document->fileName).exists()
        && !QFileInfo(document->fileName).isWritable()) {
        return reject(
            InstancePairConnectionFailure::ReadOnlyFile,
            QStringLiteral(
                "The LCA module source is read-only: %1")
                .arg(document->fileName));
    }

    QList<SemanticSymbolRecord> collisions;
    for (const SemanticSymbolRecord& record :
         semantic->getSymbolRecordsByName(signalName)) {
        if (record.owner.kind
                == SymbolTaxonomy::SymbolOwnerScope::Module
            && record.owner.name == lca.moduleType
            && recordInWorkspace(
                record, workspaceFiles)) {
            collisions.append(record);
        }
    }

    InstancePairResolvedLocalSignalSite result;
    result.moduleRecord = *module;
    result.bridgeRequired =
        left.id == lca.id
        && leftSignal.name != signalName;
    if (!collisions.isEmpty()) {
        if (collisions.size() != 1) {
            return reject(
                InstancePairConnectionFailure::NameConflict,
                QStringLiteral(
                    "Multiple LCA declarations are named %1.")
                    .arg(signalName));
        }
        const SemanticSymbolRecord existing =
            collisions.constFirst();
        if (left.id == lca.id
            && signalName == leftSignal.name
            && stableKeyEquals(
                existing.stableKey,
                leftSignal.stableKey)) {
            result.state =
                rtledit::ExposeEndpointState::Reuse;
            result.existingSignal = existing;
            result.selectedSourceSignal = true;
            result.bridgeRequired = false;
            return result;
        }
        if (!localSignalCollector(
                existing.collectorKind)
            || existing.declarationKind
                != SymbolTaxonomy::DeclarationKind::Signal) {
            return reject(
                existing.declarationKind
                        == SymbolTaxonomy::DeclarationKind::Port
                    ? InstancePairConnectionFailure::
                          DirectionConflict
                    : InstancePairConnectionFailure::
                          NameConflict,
                QStringLiteral(
                    "LCA name %1 is not a reusable local signal.")
                    .arg(signalName));
        }
        const auto* existingType =
            instanceTypeInfo(
                existing, lca.instancePath);
        if (!existingType
            || !compatibleType(
                sourceType, *existingType)) {
            return reject(
                InstancePairConnectionFailure::TypeMismatch,
                QStringLiteral(
                    "Existing LCA signal %1 has an incompatible "
                    "Slang effective type.")
                    .arg(signalName));
        }
        if (!sourceMappedName(document, existing)) {
            return reject(
                InstancePairConnectionFailure::MissingSource,
                QStringLiteral(
                    "Existing LCA signal %1 has no stable source mapping.")
                    .arg(signalName));
        }

        const bool exactExistingLeftConnection =
            leftConnection
            && leftConnection->state
                == rtledit::ExposeEndpointState::Reuse;
        const bool provenUndriven =
            existingType->driverPresence
                == SemanticDriverPresenceState::ProvenZero
            && existingType->driverCount == 0;
        const bool provenSinglePortDriver =
            exactExistingLeftConnection
            && existingType->driverPresence
                == SemanticDriverPresenceState::Present
            && existingType->driverCount == 1
            && existingType->portConnectionDriverCount == 1
            && existingType->continuousDriverCount == 0
            && existingType->proceduralDriverCount == 0;
        if (!provenUndriven
            && !provenSinglePortDriver) {
            return reject(
                InstancePairConnectionFailure::DriverConflict,
                QStringLiteral(
                    "Existing LCA signal %1 is not Slang-proven "
                    "undriven by this connection.")
                    .arg(signalName));
        }
        result.state =
            rtledit::ExposeEndpointState::Reuse;
        result.existingSignal = existing;
    } else {
        const TSSignalInsertTarget target =
            document->syntax->signalInsertTarget(
                module->location.position);
        if (!target.ok()) {
            return reject(
                InstancePairConnectionFailure::
                    UnsupportedPortList,
                QStringLiteral(
                    "The LCA module has no precise local-signal "
                    "insertion anchor."));
        }
        const auto parts = insertionTemplateParts(
            target.insertText,
            target.caretCharAfterEdit
                - target.insertChar);
        result.state =
            rtledit::ExposeEndpointState::Add;
        result.insertion = insertionAnchor(
            *document, target.insertChar,
            parts.first, parts.second,
            generation);
    }

    if (result.bridgeRequired
        && result.state
            == rtledit::ExposeEndpointState::Reuse) {
        TSSignalInsertTarget bridge =
            document->syntax->sourceBridgeInsertTarget(
                leftSignal.location.position);
        if (!bridge.ok()) {
            bridge =
                document->syntax->signalInsertTarget(
                    module->location.position);
        }
        if (!bridge.ok()) {
            return reject(
                InstancePairConnectionFailure::
                    UnsupportedPortList,
                QStringLiteral(
                    "The LCA source bridge has no Tree-sitter anchor."));
        }
        const auto parts = insertionTemplateParts(
            bridge.insertText,
            bridge.caretCharAfterEdit
                - bridge.insertChar);
        result.bridgeInsertion = insertionAnchor(
            *document, bridge.insertChar,
            parts.first, parts.second,
            generation);
    }
    return result;
}

bool moduleHasSingleElaboratedContext(
    const DesignHierarchyReport& design,
    const DesignHierarchyNode& selected)
{
    int count = 0;
    const QString definition =
        normalizedFileName(selected.definitionFile);
    for (const DesignHierarchyNode& node :
         design.nodes) {
        if (node.moduleType == selected.moduleType
            && normalizedFileName(
                   node.definitionFile)
                   == definition) {
            ++count;
        }
    }
    return count == 1;
}

rtledit::ExposeEndpointState sourcePortState(
    const ExposeSignalToTopReport& report)
{
    for (const rtledit::TextEditProvenance& provenance :
         report.planResult.plan.workspaceEdit.provenance) {
        if (provenance.anchorName == "port.insert"
            && !provenance.hierarchyStepIndex) {
            return rtledit::ExposeEndpointState::Add;
        }
    }
    return rtledit::ExposeEndpointState::Reuse;
}

struct PendingEdit {
    rtledit::WorkspaceTextEdit edit;
    rtledit::TextEditProvenance provenance;
    int role = 0;
};

rtledit::TextEditProvenance provenance(
    const InstancePairConnectionAnalysis& analysis,
    const rtledit::StructuredInsertionAnchor& anchor,
    const char* anchorName,
    const char* description,
    std::optional<std::size_t> step)
{
    rtledit::TextEditProvenance result;
    result.actionId = kActionId;
    result.anchorName = anchorName;
    result.description = description;
    result.anchor = anchor.provenance;
    result.signalQualifiedName =
        utf8String(
            analysis.blockView.left.instancePath
            + QLatin1Char('.')
            + analysis.leftSignal.name);
    result.sourceInstancePath =
        utf8String(
            analysis.blockView.left.instancePath);
    result.hierarchyStepIndex = step;
    result.sourceFilePath = utf8String(
        normalizedFileName(
            analysis.leftSignal.location.fileName));
    const auto* source = capturedDocument(
        analysis.capturedDocuments,
        analysis.leftSignal.location.fileName);
    if (source) {
        result.sourceRange = utf8Range(
            source->text,
            analysis.leftSignal.location.position,
            analysis.leftSignal.location.position
                + analysis.leftSignal.location.length);
    }
    return result;
}

void appendInsertion(
    std::vector<PendingEdit>* result,
    const InstancePairConnectionAnalysis& analysis,
    const rtledit::StructuredInsertionAnchor& anchor,
    const std::string& generatedText,
    const char* anchorName,
    const char* description,
    int role,
    std::optional<std::size_t> step = std::nullopt)
{
    if (!result || !anchor.present())
        return;
    result->push_back({
        {
            anchor.filePath,
            anchor.documentVersion,
            anchor.range,
            anchor.expectedText,
            anchor.prefix
                + generatedText
                + anchor.suffix},
        provenance(
            analysis, anchor, anchorName,
            description, step),
        role});
}

std::string indentFromAnchor(
    const rtledit::StructuredInsertionAnchor& anchor)
{
    const std::string& prefix = anchor.prefix;
    const std::size_t line =
        prefix.find_last_of("\r\n");
    const std::string candidate =
        line == std::string::npos
        ? prefix : prefix.substr(line + 1);
    for (char character : candidate) {
        if (character != ' '
            && character != '\t') {
            return {};
        }
    }
    return candidate;
}

rtledit::SemanticEditIntent semanticIntent(
    const InstancePairConnectionAnalysis& analysis)
{
    if (analysis.leftBranchReport.ready()) {
        return analysis.leftBranchReport
            .planResult.plan.workspaceEdit.intent;
    }
    rtledit::SemanticEditIntent intent;
    intent.kind =
        rtledit::SemanticEditKind::ExposeSignalToTop;
    intent.target.kind =
        analysis.leftSignal.declarationKind
                == SymbolTaxonomy::DeclarationKind::Port
            ? rtledit::SemanticObjectKind::Port
            : rtledit::SemanticObjectKind::Signal;
    intent.target.qualifiedName = utf8String(
        analysis.blockView.left.instancePath
        + QLatin1Char('.')
        + analysis.leftSignal.name);
    intent.target.ownerScope =
        utf8String(analysis.leftSignal.owner.name);
    intent.target.filePath = utf8String(
        normalizedFileName(
            analysis.leftSignal.location.fileName));
    const auto* source = capturedDocument(
        analysis.capturedDocuments,
        analysis.leftSignal.location.fileName);
    if (source) {
        intent.target.range = utf8Range(
            source->text,
            analysis.leftSignal.location.position,
            analysis.leftSignal.location.position
                + analysis.leftSignal.location.length);
    }
    intent.target.signatureHash =
        utf8String(
            analysis.leftSignal.stableKey.toString());
    return intent;
}

} // namespace

bool InstancePairDocumentSnapshot::isValid() const
{
    return !fileName.isEmpty()
        && revision > 0
        && syntax
        && syntax->text() == text;
}

bool InstancePairConnectionAnalysis::ready() const
{
    return status == InstancePairConnectionStatus::Ready
        && failure == InstancePairConnectionFailure::None
        && query.semanticToken.isValid()
        && blockView.semanticGeneration
               == query.semanticToken.revision
        && leftSignal.isValid()
        && !renderedSignalType.isEmpty();
}

bool InstancePairConnectionProposal::ready() const
{
    return status == InstancePairConnectionStatus::Ready
        && failure == InstancePairConnectionFailure::None
        && workspaceEdit.riskLevel
               == rtledit::RiskLevel::High
        && workspaceEdit.previewPolicy
               == rtledit::PreviewPolicy::Diff
        && transaction.ready()
        && !transaction.previewConfirmed
        && sourceDiff.built();
}

InstancePairConnectionFacade::
InstancePairConnectionFacade(
    SemanticIndex* semanticIndex,
    HierarchyService* hierarchyService)
    : index(semanticIndex),
      hierarchy(hierarchyService)
{
}

SemanticIndex*
InstancePairConnectionFacade::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

HierarchyService*
InstancePairConnectionFacade::hierarchyService() const
{
    return hierarchy
        ? hierarchy : HierarchyService::getInstance();
}

InstancePairConnectionAnalysis
InstancePairConnectionFacade::analyze(
    const InstancePairConnectionQuery& originalQuery,
    rtledit::WorkspaceDocumentManager& documents) const
{
    InstancePairConnectionQuery query = originalQuery;
    query.leftInstancePath =
        query.leftInstancePath.trimmed();
    query.rightInstancePath =
        query.rightInstancePath.trimmed();
    query.connectionName =
        query.connectionName.trimmed();
    query.workspaceFiles =
        normalizedFiles(query.workspaceFiles);

    if (query.leftInstancePath.isEmpty()
        || query.rightInstancePath.isEmpty()
        || query.connectionName.isEmpty()
        || !SafeRenameService::isValidIdentifier(
            query.connectionName)) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::InvalidRequest,
            QStringLiteral(
                "Two exact instance paths and a valid connection "
                "identifier are required."));
    }
    if (query.leftInstancePath
        == query.rightInstancePath) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::SameInstance,
            QStringLiteral(
                "The left and right selections identify the same instance."));
    }
    if (!query.leftSignalContext
             .hierarchyInstance.isBound()
        || query.leftSignalContext
               .hierarchyInstance.instancePath
               != query.leftInstancePath) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::InvalidRequest,
            QStringLiteral(
                "The selected signal is not bound to the exact left "
                "instance context."));
    }

    SemanticIndex* semantic = semanticIndex();
    HierarchyService* hierarchyApi =
        hierarchyService();
    if (!semantic || !hierarchyApi
        || !query.semanticToken.isValid()) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                MissingSemanticSnapshot,
            QStringLiteral(
                "A captured Slang semantic snapshot is required."));
    }
    if (!sameToken(
            query.semanticToken,
            semantic->snapshotToken())) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                StaleSemanticGeneration,
            QStringLiteral(
                "The Slang semantic generation changed before analysis."));
    }

    const auto normalizedDocuments =
        normalizedCapturedDocuments(query);
    if (!normalizedDocuments) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                InvalidTreeSnapshot,
            QStringLiteral(
                "Captured document snapshots are missing, duplicated, "
                "or structurally inconsistent."));
    }
    query.documents = *normalizedDocuments;
    InstancePairConnectionFailure documentFailure =
        InstancePairConnectionFailure::None;
    QString documentMessage;
    if (!validateCapturedDocuments(
            query, *normalizedDocuments,
            documents, &documentFailure,
            &documentMessage)) {
        return rejectedAnalysis(
            query, documentFailure,
            documentMessage);
    }

    const QString contextFile =
        normalizedFileName(
            query.leftSignalContext.fileName);
    const auto contextDocument =
        normalizedDocuments->constFind(
            contextFile);
    if (contextDocument
            == normalizedDocuments->constEnd()) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                MissingDocumentSnapshot,
            QStringLiteral(
                "The selected signal document was not captured."));
    }
    if (query.leftSignalContext.documentRevision
            != contextDocument->revision
        || query.leftSignalContext.documentText
            != contextDocument->text) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                StaleDocumentRevision,
            QStringLiteral(
                "The selected signal context is stale."));
    }
    query.leftSignalContext.fileName =
        contextFile;

    QStringList roots =
        hierarchyApi->inferDesignTopModules(
            query.workspaceFiles);
    const QString activeTop =
        query.leftSignalContext
            .hierarchyInstance.activeTopModule;
    if (activeTop.isEmpty()) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::InvalidRequest,
            QStringLiteral(
                "The selected hierarchy has no active design top."));
    }
    if (!roots.contains(activeTop))
        roots.append(activeTop);
    roots = uniqueSorted(roots);
    const DesignHierarchyReport design =
        hierarchyApi->getDesignHierarchyReport(
            roots, activeTop,
            query.workspaceFiles);
    if (design.snapshotGeneration == 0
        || design.snapshotGeneration
            != query.semanticToken.revision
        || design.snapshotGeneration
            != semantic->snapshotRevision()) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                StaleSemanticGeneration,
            QStringLiteral(
                "The hierarchy and Slang semantic generations differ."));
    }

    QList<DesignHierarchyNode> leftMatches;
    QList<DesignHierarchyNode> rightMatches;
    QHash<QString, DesignHierarchyNode> nodesById;
    for (const DesignHierarchyNode& node :
         design.nodes) {
        nodesById.insert(node.id, node);
        if (!node.inSelectedTop)
            continue;
        if (node.instancePath
            == query.leftInstancePath) {
            leftMatches.append(node);
        }
        if (node.instancePath
            == query.rightInstancePath) {
            rightMatches.append(node);
        }
    }
    if (leftMatches.size() != 1
        || rightMatches.size() != 1) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                InstanceNotFound,
            QStringLiteral(
                "Both selected instance paths must resolve uniquely "
                "inside the active top."));
    }
    const DesignHierarchyNode left =
        leftMatches.constFirst();
    const DesignHierarchyNode right =
        rightMatches.constFirst();
    if (left.rootId != right.rootId
        || left.rootModule != right.rootModule) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                DifferentDesignRoots,
            QStringLiteral(
                "The selected instances do not belong to one design root."));
    }
    const auto lcaResult =
        lowestCommonAncestor(
            left, right, nodesById);
    if (!lcaResult) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                AmbiguousHierarchy,
            QStringLiteral(
                "The selected instances have no unique common ancestor."));
    }
    const DesignHierarchyNode lca = *lcaResult;
    QList<QPair<DesignHierarchyNode, DesignHierarchyNode>>
        leftEdges;
    QList<QPair<DesignHierarchyNode, DesignHierarchyNode>>
        rightEdges;
    if (!ancestorEdges(
            left, lca, nodesById, &leftEdges)
        || !ancestorEdges(
            right, lca, nodesById, &rightEdges)) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                AmbiguousHierarchy,
            QStringLiteral(
                "The hierarchy paths to the common ancestor are not "
                "contiguous."));
    }

    QMap<QString, DesignHierarchyNode> touchedModules;
    auto addTouched = [&](const DesignHierarchyNode& node) {
        const QString key =
            node.moduleType + QLatin1Char('|')
            + normalizedFileName(
                node.definitionFile);
        touchedModules.insert(key, node);
    };
    addTouched(lca);
    for (const auto& edge : std::as_const(leftEdges))
        addTouched(edge.first);
    for (const auto& edge : std::as_const(rightEdges))
        addTouched(edge.first);
    for (auto it = touchedModules.constBegin();
         it != touchedModules.constEnd(); ++it) {
        if (!moduleHasSingleElaboratedContext(
                design, it.value())) {
            return rejectedAnalysis(
                query,
                InstancePairConnectionFailure::
                    MultiInstanceContext,
                QStringLiteral(
                    "Module %1 participates in more than one "
                    "elaborated instance; a source edit cannot isolate "
                    "the selected pair.")
                    .arg(it.value().moduleType));
        }
    }

    const auto signal = selectedSignalRecord(
        semantic,
        query.leftSignalContext,
        contextDocument.value());
    if (!signal || !signal->isValid()) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::SignalNotFound,
            QStringLiteral(
                "Slang did not resolve the selected left signal."));
    }
    if (signal->owner.kind
            != SymbolTaxonomy::SymbolOwnerScope::Module
        || signal->owner.name != left.moduleType
        || !propagatableCollector(
            signal->collectorKind)
        || !sourceMappedName(
            &contextDocument.value(), *signal)) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                SignalNotPropagatable,
            QStringLiteral(
                "The selected identifier is not a stably mapped "
                "module signal or output in the left instance."));
    }
    const auto* signalType =
        instanceTypeInfo(
            *signal, left.instancePath);
    if (!signalType) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                UnsupportedSignalType,
            QStringLiteral(
                "Slang has no effective type for the selected "
                "left instance."));
    }
    const QString renderedType =
        renderedLogicType(*signalType);
    if (renderedType.isEmpty()) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                UnsupportedSignalType,
            QStringLiteral(
                "Only fixed-width integral, packed Slang signal types "
                "can be connected."));
    }

    InstancePairBlockView blockView;
    blockView.semanticGeneration =
        design.snapshotGeneration;
    blockView.activeTopModule = activeTop;
    blockView.left = blockSide(
        InstancePairSide::Left, left,
        *query.semanticToken.snapshot);
    blockView.right = blockSide(
        InstancePairSide::Right, right,
        *query.semanticToken.snapshot);
    blockView.lcaInstancePath =
        lca.instancePath;
    blockView.lcaModuleName =
        lca.moduleType;
    blockView.leftPathToLca.append(
        left.instancePath);
    for (const auto& edge :
         std::as_const(leftEdges)) {
        blockView.leftPathToLca.append(
            edge.second.instancePath);
    }
    blockView.rightPathFromLca.append(
        lca.instancePath);
    for (auto it = rightEdges.crbegin();
         it != rightEdges.crend(); ++it) {
        blockView.rightPathFromLca.append(
            it->first.instancePath);
    }

    ExposeSignalToTopReport leftReport;
    InstancePairResolvedConnectionSite
        leftLcaConnection;
    QList<InstancePairConnectionStepView> steps;
    if (!leftEdges.isEmpty()) {
        const DesignHierarchyNode leftBranchRoot =
            leftEdges.constLast().first;
        ExposeSignalToTopQuery leftQuery;
        leftQuery.context =
            query.leftSignalContext;
        leftQuery.exportedPortName =
            query.connectionName;
        leftQuery.workspaceFiles =
            query.workspaceFiles;
        leftQuery.targetAncestorInstancePath =
            leftBranchRoot.instancePath;
        leftReport =
            ExposeSignalToTopService(
                semantic, hierarchyApi)
                .plan(
                    leftQuery, documents);
        if (!sameToken(
                query.semanticToken,
                semantic->snapshotToken())) {
            return rejectedAnalysis(
                query,
                InstancePairConnectionFailure::
                    StaleSemanticGeneration,
                QStringLiteral(
                    "The Slang semantic generation changed while "
                    "analyzing the left branch."));
        }
        if (!leftReport.ready()) {
            return rejectedAnalysis(
                query,
                InstancePairConnectionFailure::
                    LeftPropagationRejected,
                QStringLiteral(
                    "Left output propagation was rejected: %1")
                    .arg(leftReport.message));
        }
        if (!stableKeyEquals(
                leftReport.signalRecord.stableKey,
                signal->stableKey)
            || leftReport.exportedPortName
                != query.connectionName) {
            return rejectedAnalysis(
                query,
                InstancePairConnectionFailure::
                    NameConflict,
                QStringLiteral(
                    "The left output planner could not preserve the "
                    "requested connection identity."));
        }

        InstancePairConnectionFailure connectionFailure =
            InstancePairConnectionFailure::None;
        QString connectionMessage;
        const auto connection =
            resolveConnectionSite(
                semantic,
                leftEdges.constLast().first,
                lca,
                query.connectionName,
                query.workspaceFiles,
                *normalizedDocuments,
                design.snapshotGeneration,
                &connectionFailure,
                &connectionMessage);
        if (!connection) {
            return rejectedAnalysis(
                query, connectionFailure,
                connectionMessage);
        }
        leftLcaConnection = *connection;

        for (const auto& pathStep :
             leftReport.planResult.plan.path.steps) {
            InstancePairConnectionStepView view;
            view.side = InstancePairSide::Left;
            view.direction =
                InstancePairFlowDirection::TowardLca;
            view.childInstancePath =
                fromUtf8String(
                    pathStep.childInstancePath);
            view.childModule =
                fromUtf8String(
                    pathStep.childModuleName);
            view.parentInstancePath =
                fromUtf8String(
                    pathStep.parentInstancePath);
            view.parentModule =
                fromUtf8String(
                    pathStep.parentModuleName);
            view.portName = query.connectionName;
            view.portState =
                pathStep.parentPort.state;
            view.connectionState =
                pathStep.connection.state;
            steps.append(std::move(view));
        }
        InstancePairConnectionStepView lcaStep;
        lcaStep.side = InstancePairSide::Left;
        lcaStep.direction =
            InstancePairFlowDirection::TowardLca;
        lcaStep.childInstancePath =
            leftLcaConnection.child.instancePath;
        lcaStep.childModule =
            leftLcaConnection.child.moduleType;
        lcaStep.parentInstancePath =
            lca.instancePath;
        lcaStep.parentModule = lca.moduleType;
        lcaStep.portName = query.connectionName;
        lcaStep.portState =
            leftReport.planResult.plan.path.steps.empty()
                ? sourcePortState(leftReport)
                : leftReport.planResult.plan.path.steps
                      .back().parentPort.state;
        lcaStep.connectionState =
            leftLcaConnection.state;
        steps.append(std::move(lcaStep));
    }

    QList<InstancePairResolvedPortSite> rightPorts;
    QList<InstancePairResolvedConnectionSite>
        rightConnections;
    for (const auto& edge :
         std::as_const(rightEdges)) {
        InstancePairConnectionFailure portFailure =
            InstancePairConnectionFailure::None;
        QString portMessage;
        const auto port = resolveInputPortSite(
            semantic, edge.first,
            query.connectionName,
            *signalType,
            query.workspaceFiles,
            *normalizedDocuments,
            design.snapshotGeneration,
            &portFailure, &portMessage);
        if (!port) {
            return rejectedAnalysis(
                query, portFailure,
                portMessage);
        }

        InstancePairConnectionFailure connectionFailure =
            InstancePairConnectionFailure::None;
        QString connectionMessage;
        const auto connection =
            resolveConnectionSite(
                semantic, edge.first, edge.second,
                query.connectionName,
                query.workspaceFiles,
                *normalizedDocuments,
                design.snapshotGeneration,
                &connectionFailure,
                &connectionMessage);
        if (!connection) {
            return rejectedAnalysis(
                query, connectionFailure,
                connectionMessage);
        }
        rightPorts.append(*port);
        rightConnections.append(*connection);
    }
    for (int index = rightEdges.size() - 1;
         index >= 0; --index) {
        const auto& edge = rightEdges.at(index);
        InstancePairConnectionStepView view;
        view.side = InstancePairSide::Right;
        view.direction =
            InstancePairFlowDirection::AwayFromLca;
        view.childInstancePath =
            edge.first.instancePath;
        view.childModule =
            edge.first.moduleType;
        view.parentInstancePath =
            edge.second.instancePath;
        view.parentModule =
            edge.second.moduleType;
        view.portName = query.connectionName;
        view.portState =
            rightPorts.at(index).state;
        view.connectionState =
            rightConnections.at(index).state;
        steps.append(std::move(view));
    }

    InstancePairConnectionFailure localFailure =
        InstancePairConnectionFailure::None;
    QString localMessage;
    const auto local = resolveLocalSignalSite(
        semantic, lca, left,
        *signal, *signalType,
        query.connectionName,
        leftEdges.isEmpty()
            ? nullptr : &leftLcaConnection,
        query.workspaceFiles,
        *normalizedDocuments,
        design.snapshotGeneration,
        &localFailure, &localMessage);
    if (!local) {
        return rejectedAnalysis(
            query, localFailure,
            localMessage);
    }

    if (!validateCapturedDocuments(
            query, *normalizedDocuments,
            documents, &documentFailure,
            &documentMessage)) {
        return rejectedAnalysis(
            query, documentFailure,
            documentMessage);
    }
    if (!sameToken(
            query.semanticToken,
            semantic->snapshotToken())) {
        return rejectedAnalysis(
            query,
            InstancePairConnectionFailure::
                StaleSemanticGeneration,
            QStringLiteral(
                "The Slang semantic generation changed before "
                "analysis completed."));
    }

    InstancePairConnectionAnalysis result;
    result.status =
        InstancePairConnectionStatus::Ready;
    result.failure =
        InstancePairConnectionFailure::None;
    result.message = QStringLiteral(
        "Instance-pair connection analysis is ready.");
    result.query = query;
    result.capturedDocuments =
        *normalizedDocuments;
    result.blockView = std::move(blockView);
    result.leftSignal = *signal;
    result.signalType = *signalType;
    result.renderedSignalType =
        renderedType;
    result.leftBranchReport =
        std::move(leftReport);
    result.localSignal = *local;
    result.leftLcaConnection =
        std::move(leftLcaConnection);
    result.rightPorts =
        std::move(rightPorts);
    result.rightConnections =
        std::move(rightConnections);
    result.steps = std::move(steps);
    return result;
}

InstancePairConnectionProposal
InstancePairConnectionFacade::plan(
    const InstancePairConnectionAnalysis& analysis,
    rtledit::WorkspaceDocumentManager& documents) const
{
    if (!analysis.ready()) {
        return rejectedProposal(
            analysis,
            InstancePairConnectionFailure::InvalidRequest,
            QStringLiteral(
                "A successful instance-pair analysis is required."));
    }
    SemanticIndex* semantic = semanticIndex();
    if (!semantic
        || !sameToken(
            analysis.query.semanticToken,
            semantic->snapshotToken())) {
        return rejectedProposal(
            analysis,
            InstancePairConnectionFailure::
                StaleSemanticGeneration,
            QStringLiteral(
                "The Slang semantic generation changed before planning."));
    }

    InstancePairConnectionFailure documentFailure =
        InstancePairConnectionFailure::None;
    QString documentMessage;
    if (!validateCapturedDocuments(
            analysis.query,
            analysis.capturedDocuments,
            documents, &documentFailure,
            &documentMessage)) {
        return rejectedProposal(
            analysis, documentFailure,
            documentMessage);
    }

    std::vector<PendingEdit> pending;
    if (analysis.leftBranchReport.ready()) {
        const rtledit::WorkspaceEditPlan& leftPlan =
            analysis.leftBranchReport
                .planResult.plan.workspaceEdit;
        for (std::size_t index = 0;
             index < leftPlan.edits.size(); ++index) {
            const auto found = std::find_if(
                leftPlan.provenance.begin(),
                leftPlan.provenance.end(),
                [index](const auto& item) {
                    return item.editIndex == index;
                });
            if (found == leftPlan.provenance.end()) {
                return rejectedProposal(
                    analysis,
                    InstancePairConnectionFailure::
                        LeftPropagationRejected,
                    QStringLiteral(
                        "The left output plan has an edit without "
                        "structural provenance."));
            }
            PendingEdit item;
            item.edit = leftPlan.edits[index];
            item.provenance = *found;
            item.provenance.actionId = kActionId;
            item.provenance.anchor.resolver =
                kResolver;
            item.provenance.description =
                "Instance-pair left output: "
                + item.provenance.description;
            item.role = 10;
            pending.push_back(std::move(item));
        }
    }

    const std::string connectionName =
        utf8String(
            analysis.query.connectionName);
    const std::string renderedType =
        utf8String(
            analysis.renderedSignalType);
    if (analysis.localSignal.state
        == rtledit::ExposeEndpointState::Add) {
        std::string generated =
            renderedType + " " + connectionName + ";";
        if (analysis.localSignal.bridgeRequired) {
            generated += "\n";
            generated += indentFromAnchor(
                analysis.localSignal.insertion);
            generated += "assign " + connectionName
                + " = "
                + utf8String(
                    analysis.leftSignal.name)
                + ";";
        }
        appendInsertion(
            &pending, analysis,
            analysis.localSignal.insertion,
            generated,
            "lca.signal.insert",
            "Declare the LCA-local connection signal.",
            20);
    } else if (analysis.localSignal.bridgeRequired) {
        appendInsertion(
            &pending, analysis,
            analysis.localSignal.bridgeInsertion,
            "assign " + connectionName
                + " = "
                + utf8String(
                    analysis.leftSignal.name)
                + ";",
            "lca.source.bridge",
            "Bridge the ancestor-left source to the LCA signal.",
            21);
    }

    std::size_t stepIndex = 0;
    if (!analysis.leftLcaConnection.child.id.isEmpty()) {
        const auto& site =
            analysis.leftLcaConnection;
        if (site.state
            == rtledit::ExposeEndpointState::Add) {
            if (site.delimiterInsertion.present()) {
                appendInsertion(
                    &pending, analysis,
                    site.delimiterInsertion,
                    ",",
                    "left.connection.previousDelimiter",
                    "Terminate the previous named connection.",
                    30, stepIndex);
            }
            appendInsertion(
                &pending, analysis,
                site.insertion,
                "." + connectionName
                    + "(" + connectionName + ")",
                "left.connection.insert",
                "Connect the left branch output to the LCA signal.",
                31, stepIndex);
        }
        ++stepIndex;
    }

    for (int index = 0;
         index < analysis.rightPorts.size(); ++index) {
        const auto& port =
            analysis.rightPorts.at(index);
        const auto& connection =
            analysis.rightConnections.at(index);
        if (port.state
            == rtledit::ExposeEndpointState::Add) {
            if (port.delimiterInsertion.present()) {
                appendInsertion(
                    &pending, analysis,
                    port.delimiterInsertion,
                    ",",
                    "right.port.previousDelimiter",
                    "Terminate the previous ANSI port.",
                    40, stepIndex);
            }
            appendInsertion(
                &pending, analysis,
                port.insertion,
                "input " + renderedType
                    + " " + connectionName,
                "right.port.insert",
                "Add the right-branch input port.",
                41, stepIndex);
        }
        if (connection.state
            == rtledit::ExposeEndpointState::Add) {
            if (connection.delimiterInsertion.present()) {
                appendInsertion(
                    &pending, analysis,
                    connection.delimiterInsertion,
                    ",",
                    "right.connection.previousDelimiter",
                    "Terminate the previous named connection.",
                    42, stepIndex);
            }
            appendInsertion(
                &pending, analysis,
                connection.insertion,
                "." + connectionName
                    + "(" + connectionName + ")",
                "right.connection.insert",
                "Connect the LCA signal through the right branch.",
                43, stepIndex);
        }
        ++stepIndex;
    }

    std::stable_sort(
        pending.begin(), pending.end(),
        [](const PendingEdit& left,
           const PendingEdit& right) {
            return std::tie(
                       left.edit.filePath,
                       left.edit.range.start.line,
                       left.edit.range.start.column,
                       left.edit.range.end.line,
                       left.edit.range.end.column,
                       left.role,
                       left.edit.newText)
                < std::tie(
                       right.edit.filePath,
                       right.edit.range.start.line,
                       right.edit.range.start.column,
                       right.edit.range.end.line,
                       right.edit.range.end.column,
                       right.role,
                       right.edit.newText);
        });

    std::vector<rtledit::WorkspaceTextEdit> edits;
    std::vector<rtledit::TextEditProvenance>
        editProvenance;
    edits.reserve(pending.size());
    editProvenance.reserve(pending.size());
    for (std::size_t index = 0;
         index < pending.size(); ++index) {
        pending[index].provenance.editIndex = index;
        edits.push_back(
            std::move(pending[index].edit));
        editProvenance.push_back(
            std::move(pending[index].provenance));
    }

    InstancePairConnectionProposal proposal;
    proposal.blockView = analysis.blockView;
    proposal.leftSignal = analysis.leftSignal;
    proposal.connectionName =
        analysis.query.connectionName;
    proposal.renderedSignalType =
        analysis.renderedSignalType;
    proposal.steps = analysis.steps;
    proposal.dryRun = analysis.query.dryRun;
    if (edits.empty()) {
        proposal.status =
            InstancePairConnectionStatus::NoChanges;
        proposal.failure =
            InstancePairConnectionFailure::None;
        proposal.message = QStringLiteral(
            "The selected instances are already connected.");
        return proposal;
    }

    proposal.workspaceEdit =
        rtledit::makeWorkspaceEditPlan(
            semanticIntent(analysis),
            rtledit::RiskLevel::High,
            rtledit::PreviewPolicy::Diff,
            std::move(edits),
            std::move(editProvenance));
    proposal.workspaceEdit.semanticSnapshot = {
        std::to_string(
            analysis.query.semanticToken.revision)};
    std::set<std::string> semanticFiles;
    for (const QString& file :
         std::as_const(
             analysis.query.workspaceFiles)) {
        semanticFiles.insert(utf8String(file));
    }
    proposal.workspaceEdit.semanticIndexFilePaths.assign(
        semanticFiles.begin(), semanticFiles.end());

    proposal.transaction =
        WorkspaceEditTransactionService::getInstance()
            ->prepare(
                proposal.workspaceEdit,
                rtledit::SemanticIndexSnapshot{
                    std::to_string(
                        analysis.query.semanticToken
                            .revision)},
                documents,
                analysis.query.dryRun);
    if (!proposal.transaction.ready()) {
        return rejectedProposal(
            analysis,
            InstancePairConnectionFailure::
                TransactionPreparationFailed,
            QStringLiteral(
                "The atomic High+Diff workspace preview could not be "
                "prepared."));
    }
    if (!sameToken(
            analysis.query.semanticToken,
            semantic->snapshotToken())) {
        return rejectedProposal(
            analysis,
            InstancePairConnectionFailure::
                StaleSemanticGeneration,
            QStringLiteral(
                "The Slang semantic generation changed before the "
                "preview was finalized."));
    }
    proposal.sourceDiff =
        proposal.transaction.sourceDiff;
    proposal.renderedDiff = fromUtf8String(
        rtledit::renderWorkspaceEditSourceDiffHunks(
            proposal.sourceDiff));
    proposal.status =
        InstancePairConnectionStatus::Ready;
    proposal.failure =
        InstancePairConnectionFailure::None;
    proposal.message = QStringLiteral(
        "Instance-pair High+Diff preview is ready.");
    return proposal;
}
