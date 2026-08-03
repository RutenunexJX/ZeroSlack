#include "multisignalpropagationplanner.h"

#include "hierarchyservice.h"
#include "saferenameservice.h"
#include "semanticindexsnapshot.h"
#include "tsdocument.h"
#include "workspaceedittransactionservice.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr char kActionId[] = "signal.propagateBatch";
constexpr char kTreeSitterResolver[] =
    "ZeroSlack.MultiSignalPropagationPlanner/TSDocument";

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

MultiSignalPropagationProposal rejected(
    MultiSignalPropagationFailure failure,
    const QString& message,
    bool dryRun)
{
    MultiSignalPropagationProposal proposal;
    proposal.failure = failure;
    proposal.message = message;
    proposal.dryRun = dryRun;
    if (!message.isEmpty())
        proposal.blockers.append(message);
    return proposal;
}

bool sameToken(const SemanticSnapshotToken& left,
               const SemanticSnapshotToken& right)
{
    return left.isValid()
        && right.isValid()
        && left.revision == right.revision
        && left.snapshot == right.snapshot;
}

struct CapturedDocuments {
    QHash<QString, MultiSignalPropagationDocumentSnapshot> byFile;
};

std::optional<CapturedDocuments> normalizedCapturedDocuments(
    const MultiSignalPropagationQuery& query)
{
    CapturedDocuments result;
    for (auto it = query.documents.constBegin();
         it != query.documents.constEnd(); ++it) {
        MultiSignalPropagationDocumentSnapshot document =
            it.value();
        const QString fileName = normalizedFileName(
            !document.fileName.isEmpty()
                ? document.fileName : it.key());
        if (fileName.isEmpty() || !document.isValid()
            || result.byFile.contains(fileName)) {
            return std::nullopt;
        }
        document.fileName = fileName;
        result.byFile.insert(fileName, document);
    }
    return result;
}

QHash<QString, QString> normalizedSemanticContents(
    const SemanticIndexSnapshot& snapshot)
{
    QHash<QString, QString> result;
    for (auto it = snapshot.fileContentsView().constBegin();
         it != snapshot.fileContentsView().constEnd(); ++it) {
        const QString fileName = normalizedFileName(it.key());
        if (!fileName.isEmpty())
            result.insert(fileName, it.value());
    }
    return result;
}

struct PreparedMember {
    MultiSignalPropagationMemberRequest request;
    QString selectedIdentifier;
    QString memberName;
    QString exportedPortName;
    QString normalizedFile;
    int originalOrder = 0;
};

struct PlannedMember {
    PreparedMember prepared;
    ExposeSignalToTopReport report;
    QString targetInstancePath;
    int retainedStepCount = 0;
    int stableOrder = 0;
};

bool preparedMemberLess(const PreparedMember& left,
                        const PreparedMember& right)
{
    return std::tie(
               left.normalizedFile,
               left.request.context.cursorPosition,
               left.selectedIdentifier,
               left.memberName,
               left.originalOrder)
        < std::tie(
               right.normalizedFile,
               right.request.context.cursorPosition,
               right.selectedIdentifier,
               right.memberName,
            right.originalOrder);
}

bool plannedMemberLess(const PlannedMember& left,
                       const PlannedMember& right)
{
    const SemanticSymbolRecord& leftSignal =
        left.report.signalRecord;
    const SemanticSymbolRecord& rightSignal =
        right.report.signalRecord;
    const QString leftFile =
        normalizedFileName(
            leftSignal.location.fileName);
    const QString rightFile =
        normalizedFileName(
            rightSignal.location.fileName);
    const QString leftKey =
        symbolStableKeyText(
            leftSignal.stableKey);
    const QString rightKey =
        symbolStableKeyText(
            rightSignal.stableKey);
    return std::tie(
               leftFile,
               leftSignal.location.position,
               leftSignal.location.length,
               leftSignal.name,
               leftKey)
        < std::tie(
               rightFile,
               rightSignal.location.position,
               rightSignal.location.length,
               rightSignal.name,
               rightKey);
}

int retainedStepCount(
    const ExposeSignalToTopReport& report,
    const QString& requestedTarget,
    QString* resolvedTarget)
{
    const QString target = requestedTarget.isEmpty()
        ? report.targetInstancePath : requestedTarget;
    if (resolvedTarget)
        *resolvedTarget = target;
    if (target == report.sourceInstancePath)
        return 0;

    int result = -1;
    for (const ExposeSignalHierarchyStepView& step :
         report.hierarchySteps) {
        if (step.parentInstancePath != target)
            continue;
        if (result >= 0)
            return -1;
        result = step.index + 1;
    }
    return result;
}

const rtledit::TextEditProvenance* provenanceForEdit(
    const rtledit::WorkspaceEditPlan& plan,
    std::size_t editIndex)
{
    for (const rtledit::TextEditProvenance& provenance :
         plan.provenance) {
        if (provenance.editIndex == editIndex)
            return &provenance;
    }
    return nullptr;
}

struct InsertionEnvelope {
    std::string prefix;
    std::string suffix;
    bool valid = false;
};

InsertionEnvelope anchorEnvelope(
    const rtledit::StructuredInsertionAnchor& anchor)
{
    return {anchor.prefix, anchor.suffix, anchor.present()};
}

InsertionEnvelope sourcePortEnvelope(
    const PlannedMember& member,
    const CapturedDocuments& documents)
{
    const QString fileName = normalizedFileName(
        member.report.signalRecord.location.fileName);
    const auto found = documents.byFile.constFind(fileName);
    if (found == documents.byFile.constEnd()
        || !found->syntax) {
        return {};
    }

    const TSPortAppendTarget target =
        found->syntax->portAppendTarget(
            member.report.signalRecord.location.position);
    if (!target.ok()
        || target.insertChar < 0
        || target.caretCharAfterEdit < target.insertChar) {
        return {};
    }
    const int caretOffset =
        target.caretCharAfterEdit - target.insertChar;
    if (caretOffset > target.insertText.size()) {
        return {
            utf8String(target.insertText),
            {},
            true};
    }
    return {
        utf8String(target.insertText.left(caretOffset)),
        utf8String(target.insertText.mid(caretOffset)),
        true};
}

InsertionEnvelope insertionEnvelope(
    const PlannedMember& member,
    const rtledit::TextEditProvenance& provenance,
    const CapturedDocuments& documents)
{
    if (provenance.anchorName == "port.insert") {
        if (!provenance.hierarchyStepIndex)
            return sourcePortEnvelope(member, documents);
        const std::size_t index =
            *provenance.hierarchyStepIndex;
        const auto& steps =
            member.report.planResult.plan.path.steps;
        if (index >= steps.size())
            return {};
        return anchorEnvelope(
            steps[index].parentPort.insertion);
    }
    if (provenance.anchorName == "connection.insert") {
        if (!provenance.hierarchyStepIndex)
            return {};
        const std::size_t index =
            *provenance.hierarchyStepIndex;
        const auto& steps =
            member.report.planResult.plan.path.steps;
        if (index >= steps.size())
            return {};
        return anchorEnvelope(
            steps[index].connection.insertion);
    }
    return {};
}

bool isBatchInsertionAnchor(const std::string& anchorName)
{
    return anchorName == "port.insert"
        || anchorName == "connection.insert";
}

bool isDelimiterAnchor(const std::string& anchorName)
{
    return anchorName == "port.previousDelimiter"
        || anchorName == "connection.previousDelimiter";
}

bool structuralPrefixProvidesComma(
    const std::string& prefix)
{
    for (const char character : prefix) {
        if (character == ' ' || character == '\t'
            || character == '\r' || character == '\n') {
            continue;
        }
        return character == ',';
    }
    return false;
}

bool insertCommaBeforeSuffix(
    rtledit::WorkspaceTextEdit* edit,
    const InsertionEnvelope& envelope)
{
    if (!edit || !envelope.valid
        || edit->newText.size()
               < envelope.prefix.size()
                    + envelope.suffix.size()
        || edit->newText.compare(
               0, envelope.prefix.size(),
               envelope.prefix) != 0) {
        return false;
    }
    const std::size_t suffixStart =
        edit->newText.size() - envelope.suffix.size();
    if (edit->newText.compare(
            suffixStart, envelope.suffix.size(),
            envelope.suffix) != 0) {
        return false;
    }
    edit->newText.insert(suffixStart, 1, ',');
    return true;
}

struct PendingEdit {
    rtledit::WorkspaceTextEdit edit;
    rtledit::TextEditProvenance provenance;
    InsertionEnvelope envelope;
    int memberOrder = 0;
    int sourceOrder = 0;
};

struct EditIdentity {
    std::string filePath;
    rtledit::DocumentVersion version;
    rtledit::SourceRange range;
    std::string expectedText;
    std::string newText;
    std::string anchorName;
    std::string signalQualifiedName;

    bool operator<(const EditIdentity& other) const
    {
        return std::tie(
                   filePath, version.value,
                   range.start.line, range.start.column,
                   range.end.line, range.end.column,
                   expectedText, newText, anchorName,
                   signalQualifiedName)
            < std::tie(
                   other.filePath, other.version.value,
                   other.range.start.line,
                   other.range.start.column,
                   other.range.end.line,
                   other.range.end.column,
                   other.expectedText, other.newText,
                   other.anchorName,
                   other.signalQualifiedName);
    }
};

EditIdentity identityOf(const PendingEdit& pending,
                        bool includeSignal)
{
    return {
        pending.edit.filePath,
        pending.edit.expectedDocumentVersion,
        pending.edit.range,
        pending.edit.expectedText,
        pending.edit.newText,
        pending.provenance.anchorName,
        includeSignal
            ? pending.provenance.signalQualifiedName
            : std::string{}};
}

struct InsertionGroupKey {
    std::string filePath;
    std::uint64_t version = 0;
    rtledit::SourceRange range;
    std::string anchorName;

    bool operator<(const InsertionGroupKey& other) const
    {
        return std::tie(
                   filePath, version,
                   range.start.line, range.start.column,
                   range.end.line, range.end.column,
                   anchorName)
            < std::tie(
                   other.filePath, other.version,
                   other.range.start.line,
                   other.range.start.column,
                   other.range.end.line,
                   other.range.end.column,
                   other.anchorName);
    }
};

int anchorRoleOrder(const std::string& anchorName)
{
    if (isDelimiterAnchor(anchorName))
        return 0;
    if (anchorName == "port.insert")
        return 1;
    if (anchorName == "connection.insert")
        return 2;
    if (anchorName == "source.bridge")
        return 3;
    return 4;
}

bool pendingEditLess(const PendingEdit& left,
                     const PendingEdit& right)
{
    const int leftRole =
        anchorRoleOrder(left.provenance.anchorName);
    const int rightRole =
        anchorRoleOrder(right.provenance.anchorName);
    return std::tie(
               left.edit.filePath,
               left.edit.range.start.line,
               left.edit.range.start.column,
               left.edit.range.end.line,
               left.edit.range.end.column,
               leftRole,
               left.memberOrder,
               left.sourceOrder,
               left.edit.newText)
        < std::tie(
               right.edit.filePath,
               right.edit.range.start.line,
               right.edit.range.start.column,
               right.edit.range.end.line,
               right.edit.range.end.column,
               rightRole,
               right.memberOrder,
               right.sourceOrder,
               right.edit.newText);
}

bool buildPendingEdits(
    const QList<PlannedMember>& members,
    const CapturedDocuments& documents,
    std::vector<PendingEdit>* result,
    QString* failure)
{
    if (!result)
        return false;

    int sourceOrder = 0;
    for (const PlannedMember& member : members) {
        const rtledit::WorkspaceEditPlan& plan =
            member.report.planResult.plan.workspaceEdit;
        for (std::size_t editIndex = 0;
             editIndex < plan.edits.size(); ++editIndex) {
            const rtledit::TextEditProvenance* sourceProvenance =
                provenanceForEdit(plan, editIndex);
            if (!sourceProvenance) {
                if (failure) {
                    *failure = QStringLiteral(
                        "A member plan has an edit without provenance.");
                }
                return false;
            }
            if (sourceProvenance->hierarchyStepIndex
                && *sourceProvenance->hierarchyStepIndex
                       >= static_cast<std::size_t>(
                           member.retainedStepCount)) {
                continue;
            }

            PendingEdit pending;
            pending.edit = plan.edits[editIndex];
            pending.provenance = *sourceProvenance;
            pending.provenance.actionId = kActionId;
            pending.provenance.anchor.resolver =
                kTreeSitterResolver;
            pending.provenance.description =
                "Batch signal propagation: "
                + pending.provenance.description;
            pending.memberOrder = member.stableOrder;
            pending.sourceOrder = sourceOrder++;
            if (isBatchInsertionAnchor(
                    pending.provenance.anchorName)) {
                pending.envelope = insertionEnvelope(
                    member, pending.provenance, documents);
                if (!pending.envelope.valid) {
                    if (failure) {
                        *failure = QStringLiteral(
                            "A Tree-sitter insertion envelope could not "
                            "be recovered from the member plan.");
                    }
                    return false;
                }
            }
            result->push_back(std::move(pending));
        }
    }

    std::stable_sort(
        result->begin(), result->end(), pendingEditLess);

    // A batch sees the same original delimiter and module definition from
    // every member. Identical delimiter edits and identical per-signal edits
    // are applied once; no semantic decision is reconstructed from text.
    std::set<EditIdentity> seenDelimiters;
    std::set<EditIdentity> seenSameSignalEdits;
    std::vector<PendingEdit> deduplicated;
    deduplicated.reserve(result->size());
    for (PendingEdit& pending : *result) {
        if (isDelimiterAnchor(
                pending.provenance.anchorName)) {
            if (!seenDelimiters.insert(
                    identityOf(pending, false)).second) {
                continue;
            }
        } else if (!seenSameSignalEdits.insert(
                       identityOf(pending, true)).second) {
            continue;
        }
        deduplicated.push_back(std::move(pending));
    }
    *result = std::move(deduplicated);

    std::map<InsertionGroupKey, std::vector<std::size_t>>
        insertionGroups;
    for (std::size_t index = 0;
         index < result->size(); ++index) {
        const PendingEdit& pending = result->at(index);
        if (!isBatchInsertionAnchor(
                pending.provenance.anchorName)) {
            continue;
        }
        insertionGroups[{
            pending.edit.filePath,
            pending.edit.expectedDocumentVersion.value,
            pending.edit.range,
            pending.provenance.anchorName}].push_back(index);
    }

    for (const auto& [key, indexes] : insertionGroups) {
        Q_UNUSED(key);
        for (std::size_t position = 0;
             position + 1 < indexes.size(); ++position) {
            PendingEdit& current =
                result->at(indexes[position]);
            const PendingEdit& next =
                result->at(indexes[position + 1]);
            if (structuralPrefixProvidesComma(
                    next.envelope.prefix)) {
                continue;
            }
            if (!insertCommaBeforeSuffix(
                    &current.edit, current.envelope)) {
                if (failure) {
                    *failure = QStringLiteral(
                        "A generated member insertion no longer matches "
                        "its Tree-sitter prefix/suffix envelope.");
                }
                return false;
            }
        }
    }

    return true;
}

bool validateCapturedDocuments(
    const MultiSignalPropagationQuery& query,
    const CapturedDocuments& captured,
    const rtledit::WorkspaceDocumentManager& documents,
    MultiSignalPropagationFailure* failure,
    QString* message)
{
    QSet<QString> requestedFiles;
    for (const QString& file : query.workspaceFiles) {
        const QString normalized = normalizedFileName(file);
        if (!normalized.isEmpty())
            requestedFiles.insert(normalized);
    }
    for (const MultiSignalPropagationMemberRequest& member :
         query.members) {
        const QString normalized =
            normalizedFileName(member.context.fileName);
        if (!normalized.isEmpty())
            requestedFiles.insert(normalized);
    }

    const QHash<QString, QString> semanticContents =
        normalizedSemanticContents(*query.semanticToken.snapshot);
    for (const QString& fileName : requestedFiles) {
        const auto found = captured.byFile.constFind(fileName);
        if (found == captured.byFile.constEnd()) {
            if (failure)
                *failure = MultiSignalPropagationFailure::
                    MissingDocumentSnapshot;
            if (message) {
                *message = QStringLiteral(
                    "No captured live document exists for %1.")
                    .arg(fileName);
            }
            return false;
        }

        const auto live =
            documents.snapshot(utf8String(fileName));
        if (!live) {
            if (failure)
                *failure = MultiSignalPropagationFailure::
                    MissingDocumentSnapshot;
            if (message) {
                *message = QStringLiteral(
                    "The live document is unavailable: %1.")
                    .arg(fileName);
            }
            return false;
        }
        if (live->version.value != found->revision
            || fromUtf8String(live->text) != found->text) {
            if (failure)
                *failure = MultiSignalPropagationFailure::
                    StaleDocumentRevision;
            if (message) {
                *message = QStringLiteral(
                    "The live document changed after capture: %1.")
                    .arg(fileName);
            }
            return false;
        }
        if (!found->syntax
            || found->syntax->text() != found->text) {
            if (failure)
                *failure = MultiSignalPropagationFailure::
                    InvalidTreeSnapshot;
            if (message) {
                *message = QStringLiteral(
                    "The Tree-sitter tree does not describe the captured "
                    "revision: %1.")
                    .arg(fileName);
            }
            return false;
        }
        if (found->syntax->hasError()) {
            if (failure)
                *failure = MultiSignalPropagationFailure::SyntaxError;
            if (message) {
                *message = QStringLiteral(
                    "The captured SystemVerilog syntax is incomplete: %1.")
                    .arg(fileName);
            }
            return false;
        }
        if (!found->unsaved) {
            const auto semantic =
                semanticContents.constFind(fileName);
            if (semantic == semanticContents.constEnd()
                || semantic.value() != found->text) {
                if (failure)
                    *failure = MultiSignalPropagationFailure::
                        StaleSemanticSource;
                if (message) {
                    *message = QStringLiteral(
                        "The saved source no longer matches the Slang "
                        "snapshot: %1.")
                        .arg(fileName);
                }
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool MultiSignalPropagationDocumentSnapshot::isValid() const
{
    return !fileName.isEmpty()
        && revision > 0
        && syntax
        && syntax->text() == text;
}

bool MultiSignalPropagationProposal::ready() const
{
    return status == MultiSignalPropagationStatus::Ready
        && failure == MultiSignalPropagationFailure::None
        && workspaceEdit.riskLevel
               == rtledit::RiskLevel::High
        && workspaceEdit.previewPolicy
               == rtledit::PreviewPolicy::Diff
        && transaction.ready()
        && sourceDiff.built();
}

MultiSignalPropagationPlanner::MultiSignalPropagationPlanner(
    SemanticIndex* semanticIndex,
    HierarchyService* hierarchyService)
    : index(semanticIndex),
      hierarchy(hierarchyService)
{
}

SemanticIndex*
MultiSignalPropagationPlanner::semanticIndex() const
{
    return index ? index : SemanticIndex::getInstance();
}

HierarchyService*
MultiSignalPropagationPlanner::hierarchyService() const
{
    return hierarchy ? hierarchy : HierarchyService::getInstance();
}

MultiSignalPropagationProposal
MultiSignalPropagationPlanner::plan(
    const MultiSignalPropagationQuery& query,
    rtledit::WorkspaceDocumentManager& documents) const
{
    if (query.members.size() < 2) {
        return rejected(
            MultiSignalPropagationFailure::InvalidRequest,
            QStringLiteral(
                "Batch propagation requires at least two selected signals."),
            query.dryRun);
    }
    SemanticIndex* semantic = semanticIndex();
    HierarchyService* hierarchyApi = hierarchyService();
    if (!semantic || !hierarchyApi
        || !query.semanticToken.isValid()) {
        return rejected(
            MultiSignalPropagationFailure::MissingSemanticSnapshot,
            QStringLiteral(
                "A captured Slang semantic snapshot is required."),
            query.dryRun);
    }
    if (!sameToken(
            query.semanticToken, semantic->snapshotToken())) {
        return rejected(
            MultiSignalPropagationFailure::StaleSemanticGeneration,
            QStringLiteral(
                "The Slang semantic generation changed before analysis."),
            query.dryRun);
    }
    if (query.mode == MultiSignalPropagationMode::PortGroup
        && !SafeRenameService::isValidIdentifier(
            query.groupName.trimmed())) {
        return rejected(
            MultiSignalPropagationFailure::InvalidGroupName,
            QStringLiteral(
                "The port-group name is not a valid SystemVerilog "
                "identifier."),
            query.dryRun);
    }

    const auto captured =
        normalizedCapturedDocuments(query);
    if (!captured) {
        return rejected(
            MultiSignalPropagationFailure::InvalidTreeSnapshot,
            QStringLiteral(
                "Captured document snapshots are missing, duplicated, "
                "or structurally inconsistent."),
            query.dryRun);
    }
    MultiSignalPropagationFailure documentFailure =
        MultiSignalPropagationFailure::None;
    QString documentMessage;
    if (!validateCapturedDocuments(
            query, *captured, documents,
            &documentFailure, &documentMessage)) {
        return rejected(
            documentFailure, documentMessage, query.dryRun);
    }

    QList<PreparedMember> preparedMembers;
    preparedMembers.reserve(query.members.size());
    for (int memberIndex = 0;
         memberIndex < query.members.size(); ++memberIndex) {
        const MultiSignalPropagationMemberRequest& request =
            query.members.at(memberIndex);
        const QString fileName =
            normalizedFileName(request.context.fileName);
        const auto document =
            captured->byFile.constFind(fileName);
        if (document == captured->byFile.constEnd()) {
            return rejected(
                MultiSignalPropagationFailure::
                    MissingDocumentSnapshot,
                QStringLiteral(
                    "The selected signal document was not captured: %1.")
                    .arg(fileName),
                query.dryRun);
        }
        if (request.context.documentRevision
                    != document->revision
            || request.context.documentText
                    != document->text) {
            return rejected(
                MultiSignalPropagationFailure::
                    StaleDocumentRevision,
                QStringLiteral(
                    "A selected signal context is stale: %1.")
                    .arg(fileName),
                query.dryRun);
        }
        const TSIdentifierTarget identifier =
            document->syntax->identifierAt(
                request.context.cursorPosition);
        if (!identifier.ok()) {
            return rejected(
                MultiSignalPropagationFailure::InvalidRequest,
                QStringLiteral(
                    "A selected position is not a Tree-sitter "
                    "SystemVerilog identifier."),
                query.dryRun);
        }

        PreparedMember prepared;
        prepared.request = request;
        prepared.request.context.fileName = fileName;
        prepared.selectedIdentifier = identifier.text;
        prepared.memberName =
            request.groupMemberName.trimmed().isEmpty()
            ? identifier.text
            : request.groupMemberName.trimmed();
        prepared.normalizedFile = fileName;
        prepared.originalOrder = memberIndex;
        if (!SafeRenameService::isValidIdentifier(
                prepared.memberName)) {
            return rejected(
                MultiSignalPropagationFailure::
                    InvalidMemberName,
                QStringLiteral(
                    "Port-group member \"%1\" is not a valid "
                    "SystemVerilog identifier.")
                    .arg(prepared.memberName),
                query.dryRun);
        }
        if (query.mode
                == MultiSignalPropagationMode::PortGroup) {
            prepared.exportedPortName =
                query.groupName.trimmed()
                + QLatin1Char('_') + prepared.memberName;
        } else {
            prepared.exportedPortName =
                request.exportedPortName.trimmed();
            if (prepared.exportedPortName.isEmpty()) {
                prepared.exportedPortName =
                    ExposeSignalToTopService::
                        defaultExportedPortName(
                            identifier.text);
            }
        }
        if (!SafeRenameService::isValidIdentifier(
                prepared.exportedPortName)) {
            return rejected(
                MultiSignalPropagationFailure::
                    InvalidMemberName,
                QStringLiteral(
                    "Generated port \"%1\" is not a valid "
                    "SystemVerilog identifier.")
                    .arg(prepared.exportedPortName),
                query.dryRun);
        }
        preparedMembers.append(std::move(prepared));
    }
    std::sort(
        preparedMembers.begin(),
        preparedMembers.end(),
        preparedMemberLess);

    QSet<QString> requestedPortNames;
    for (const PreparedMember& prepared :
         std::as_const(preparedMembers)) {
        if (requestedPortNames.contains(
                prepared.exportedPortName)) {
            return rejected(
                MultiSignalPropagationFailure::
                    DuplicatePortName,
                QStringLiteral(
                    "Multiple members resolve to port \"%1\".")
                    .arg(prepared.exportedPortName),
                query.dryRun);
        }
        requestedPortNames.insert(
            prepared.exportedPortName);
    }

    ExposeSignalToTopService singleSignal(
        semantic, hierarchyApi);
    QList<PlannedMember> plannedMembers;
    plannedMembers.reserve(preparedMembers.size());
    QSet<QString> stableSignalKeys;
    QString commonSourceInstance;
    QString commonTargetInstance;
    for (const PreparedMember& prepared :
         std::as_const(preparedMembers)) {
        const ExposeSignalToTopQuery singleQuery{
            prepared.request.context,
            prepared.exportedPortName,
            query.workspaceFiles};
        ExposeSignalToTopReport report =
            singleSignal.plan(singleQuery, documents);
        if (!sameToken(
                query.semanticToken,
                semantic->snapshotToken())) {
            return rejected(
                MultiSignalPropagationFailure::
                    StaleSemanticGeneration,
                QStringLiteral(
                    "The Slang semantic generation changed while "
                    "analyzing batch members."),
                query.dryRun);
        }
        if (!report.ready()) {
            return rejected(
                MultiSignalPropagationFailure::
                    SingleSignalRejected,
                QStringLiteral("%1: %2")
                    .arg(prepared.selectedIdentifier,
                         report.message),
                query.dryRun);
        }
        if (report.exportedPortName
                != prepared.exportedPortName) {
            return rejected(
                MultiSignalPropagationFailure::
                    IncompatibleMemberPlans,
                QStringLiteral(
                    "The single-signal planner changed the requested "
                    "group member port name."),
                query.dryRun);
        }

        const QString stableKey =
            symbolStableKeyText(
                report.signalRecord.stableKey);
        if (stableKey.isEmpty()
            || stableSignalKeys.contains(stableKey)) {
            return rejected(
                MultiSignalPropagationFailure::
                    DuplicateSignal,
                QStringLiteral(
                    "The same Slang signal was selected more than once."),
                query.dryRun);
        }
        stableSignalKeys.insert(stableKey);

        QString resolvedTarget;
        const int keepSteps = retainedStepCount(
            report,
            query.targetAncestorInstancePath,
            &resolvedTarget);
        if (keepSteps < 0) {
            return rejected(
                MultiSignalPropagationFailure::
                    TargetNotAncestor,
                QStringLiteral(
                    "Target \"%1\" is not an exact ancestor of %2.")
                    .arg(resolvedTarget,
                         report.sourceInstancePath),
                query.dryRun);
        }
        if (commonSourceInstance.isEmpty()) {
            commonSourceInstance =
                report.sourceInstancePath;
            commonTargetInstance = resolvedTarget;
        } else if (commonSourceInstance
                       != report.sourceInstancePath
                   || commonTargetInstance
                       != resolvedTarget) {
            return rejected(
                MultiSignalPropagationFailure::
                    InconsistentSourceInstance,
                QStringLiteral(
                    "All selected signals must belong to the same "
                    "concrete instance and target ancestor."),
                query.dryRun);
        }

        PlannedMember member;
        member.prepared = prepared;
        member.report = std::move(report);
        member.targetInstancePath =
            resolvedTarget;
        member.retainedStepCount = keepSteps;
        plannedMembers.append(std::move(member));
    }
    std::sort(
        plannedMembers.begin(),
        plannedMembers.end(),
        plannedMemberLess);
    for (int index = 0;
         index < plannedMembers.size(); ++index) {
        plannedMembers[index].stableOrder = index;
    }

    std::vector<PendingEdit> pending;
    QString mergeFailure;
    if (!buildPendingEdits(
            plannedMembers, *captured,
            &pending, &mergeFailure)) {
        return rejected(
            MultiSignalPropagationFailure::
                IncompatibleMemberPlans,
            mergeFailure,
            query.dryRun);
    }

    MultiSignalPropagationProposal proposal;
    proposal.dryRun = query.dryRun;
    proposal.sourceInstancePath =
        commonSourceInstance;
    proposal.targetAncestorInstancePath =
        commonTargetInstance;
    if (query.mode
            == MultiSignalPropagationMode::PortGroup) {
        proposal.portGroup.groupName =
            query.groupName.trimmed();
        proposal.portGroup.namingRule =
            QStringLiteral("<group>_<member>");
    }

    std::vector<rtledit::WorkspaceTextEdit> edits;
    std::vector<rtledit::TextEditProvenance> provenance;
    edits.reserve(pending.size());
    provenance.reserve(pending.size());
    for (std::size_t index = 0;
         index < pending.size(); ++index) {
        pending[index].provenance.editIndex = index;
        edits.push_back(
            std::move(pending[index].edit));
        provenance.push_back(
            std::move(pending[index].provenance));
    }

    for (const PlannedMember& member :
         std::as_const(plannedMembers)) {
        MultiSignalPropagationMemberView view;
        view.signal = member.report.signalRecord;
        view.sourceSignalName =
            member.report.signalRecord.name;
        view.memberName =
            member.prepared.memberName;
        view.exportedPortName =
            member.prepared.exportedPortName;
        view.sourceInstancePath =
            member.report.sourceInstancePath;
        view.targetAncestorInstancePath =
            member.targetInstancePath;
        view.retainedHierarchyStepCount =
            member.retainedStepCount;
        proposal.members.append(std::move(view));
        if (query.mode
                == MultiSignalPropagationMode::PortGroup) {
            proposal.portGroup.orderedMembers.append(
                member.prepared.memberName);
            proposal.portGroup.orderedPortNames.append(
                member.prepared.exportedPortName);
        }
    }

    if (edits.empty()) {
        proposal.status =
            MultiSignalPropagationStatus::NoChanges;
        proposal.failure =
            MultiSignalPropagationFailure::None;
        proposal.message = QStringLiteral(
            "Every selected signal is already connected to the "
            "requested ancestor.");
        return proposal;
    }

    rtledit::SemanticEditIntent intent =
        plannedMembers.constFirst()
            .report.planResult.plan.workspaceEdit.intent;
    proposal.workspaceEdit =
        rtledit::makeWorkspaceEditPlan(
            std::move(intent),
            rtledit::RiskLevel::High,
            rtledit::PreviewPolicy::Diff,
            std::move(edits),
            std::move(provenance));
    proposal.workspaceEdit.semanticSnapshot = {
        std::to_string(query.semanticToken.revision)};

    std::set<std::string> semanticFiles;
    for (const PlannedMember& member :
         std::as_const(plannedMembers)) {
        for (const std::string& file :
             member.report.planResult.plan.workspaceEdit
                 .semanticIndexFilePaths) {
            semanticFiles.insert(file);
        }
    }
    proposal.workspaceEdit.semanticIndexFilePaths.assign(
        semanticFiles.begin(), semanticFiles.end());

    proposal.transaction =
        WorkspaceEditTransactionService::getInstance()
            ->prepare(
                proposal.workspaceEdit,
                rtledit::SemanticIndexSnapshot{
                    std::to_string(
                        query.semanticToken.revision)},
                documents,
                query.dryRun);
    if (!proposal.transaction.ready()) {
        return rejected(
            MultiSignalPropagationFailure::
                TransactionPreparationFailed,
            QStringLiteral(
                "The atomic workspace diff could not be prepared."),
            query.dryRun);
    }
    if (!sameToken(
            query.semanticToken,
            semantic->snapshotToken())) {
        return rejected(
            MultiSignalPropagationFailure::
                StaleSemanticGeneration,
            QStringLiteral(
                "The Slang semantic generation changed before the "
                "batch preview was finalized."),
            query.dryRun);
    }

    proposal.sourceDiff =
        proposal.transaction.sourceDiff;
    proposal.renderedDiff = fromUtf8String(
        rtledit::renderWorkspaceEditSourceDiffHunks(
            proposal.sourceDiff));
    proposal.status =
        MultiSignalPropagationStatus::Ready;
    proposal.failure =
        MultiSignalPropagationFailure::None;
    proposal.message = QStringLiteral(
        "Batch signal propagation preview is ready.");
    return proposal;
}
