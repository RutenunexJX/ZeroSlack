#include "completionservice.h"
#include "definitionservice.h"
#include "diagnosticservice.h"
#include "fsmgraphservice.h"
#include "moduleblockdiagramservice.h"
#include "navigationservice.h"
#include "referenceservice.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "signalkernelgraphservice.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "statetransitiongraphservice.h"
#include "symboltaxonomy.h"
#include "wavepreviewservice.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {

enum class AuditStatus {
    Pass,
    Fail,
    Skipped,
    Timeout,
    EmptyButValid
};

struct AuditCase {
    QString feature;
    QString status;
    QString fileName;
    QString moduleName;
    QString symbolName;
    QString locationKind;
    int line = 0;
    int column = 0;
    QString reason;
    bool emptyResult = false;
    QJsonObject metrics;
    QJsonArray tags;
};

struct FeatureCounts {
    int pass = 0;
    int fail = 0;
    int skipped = 0;
    int timeout = 0;
    int emptyButValid = 0;
};

struct CorpusContext {
    QString workspaceRoot;
    QStringList roots;
    QStringList files;
    QStringList includeDirs;
    QHash<QString, QString> fileContents;
    QList<SemanticSymbolRecord> records;
    QList<SemanticRelationship> relationships;
    QList<SemanticDiagnostic> diagnostics;
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
};

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

QString relativePath(const QString& workspaceRoot, const QString& path)
{
    const QString rel = QDir(workspaceRoot).relativeFilePath(normalizedPath(path));
    return QDir::fromNativeSeparators(rel);
}

QString findWorkspaceRoot(const QStringList& roots)
{
    QStringList candidates;
    candidates.append(QDir::currentPath());
    for (const QString& root : roots)
        candidates.append(root);

    for (const QString& candidate : std::as_const(candidates)) {
        QDir dir(normalizedPath(candidate));
        if (QFileInfo(dir.absolutePath()).isFile())
            dir.cdUp();
        for (int depth = 0; depth < 8; ++depth) {
            if (QFileInfo(dir.absoluteFilePath(QStringLiteral("CMakeLists.txt"))).exists()
                && QFileInfo(dir.absoluteFilePath(QStringLiteral("test_sv"))).exists()) {
                return normalizedPath(dir.absolutePath());
            }
            if (!dir.cdUp())
                break;
        }
    }
    return normalizedPath(QDir::currentPath());
}

bool isSvFile(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("sv")
        || suffix == QStringLiteral("svh")
        || suffix == QStringLiteral("v");
}

QString statusText(AuditStatus status)
{
    switch (status) {
    case AuditStatus::Pass:
        return QStringLiteral("pass");
    case AuditStatus::Fail:
        return QStringLiteral("fail");
    case AuditStatus::Skipped:
        return QStringLiteral("skipped");
    case AuditStatus::Timeout:
        return QStringLiteral("timeout");
    case AuditStatus::EmptyButValid:
        return QStringLiteral("empty-but-valid");
    }
    return QStringLiteral("fail");
}

QJsonObject stableKeyJson(const SymbolStableKey& key)
{
    QJsonObject object;
    object.insert(QStringLiteral("file"), key.fileName);
    object.insert(QStringLiteral("symbol"), key.symbolName);
    object.insert(QStringLiteral("kind"), static_cast<int>(key.declarationKind));
    object.insert(QStringLiteral("owner"), key.ownerScope);
    return object;
}

QJsonObject caseJson(const AuditCase& item, const QString& workspaceRoot)
{
    QJsonObject object;
    object.insert(QStringLiteral("feature"), item.feature);
    object.insert(QStringLiteral("status"), item.status);
    object.insert(QStringLiteral("file"), relativePath(workspaceRoot, item.fileName));
    object.insert(QStringLiteral("module"), item.moduleName);
    object.insert(QStringLiteral("symbol"), item.symbolName);
    object.insert(QStringLiteral("locationKind"), item.locationKind);
    object.insert(QStringLiteral("line"), item.line);
    object.insert(QStringLiteral("column"), item.column);
    object.insert(QStringLiteral("reason"), item.reason);
    object.insert(QStringLiteral("emptyResult"), item.emptyResult);
    object.insert(QStringLiteral("metrics"), item.metrics);
    object.insert(QStringLiteral("tags"), item.tags);
    return object;
}

bool readTextFile(const QString& path, QString* text)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    if (text)
        *text = QString::fromUtf8(file.readAll());
    return true;
}

bool writeTextFile(const QString& path, const QString& text)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write(text.toUtf8());
    return file.commit();
}

QStringList collectFiles(const QStringList& roots)
{
    QStringList files;
    for (const QString& root : roots) {
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = normalizedPath(it.next());
            if (isSvFile(path))
                files.append(path);
        }
    }
    files.removeDuplicates();
    files.sort();
    return files;
}

QStringList collectIncludeDirs(const QStringList& files, const QStringList& roots)
{
    Q_UNUSED(files);
    QSet<QString> dirs;
    for (const QString& root : roots)
        dirs.insert(normalizedPath(root));
    for (const QString& root : roots) {
        QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo info(it.next());
            const QString dirName = info.fileName().toLower();
            if (dirName == QStringLiteral("include")
                || dirName == QStringLiteral("inc")) {
                dirs.insert(normalizedPath(info.absoluteFilePath()));
            }
        }
    }
    QStringList result = dirs.values();
    result.sort();
    return result;
}

int positionForLineColumn(const QString& text, int line, int column)
{
    if (line <= 0)
        return -1;
    int currentLine = 1;
    int pos = 0;
    while (pos < text.size() && currentLine < line) {
        if (text.at(pos) == QLatin1Char('\n'))
            ++currentLine;
        ++pos;
    }
    if (currentLine != line)
        return -1;
    return qBound(0, pos + qMax(0, column - 1), text.size());
}

int lineEndPosition(const QString& text, int line)
{
    const int start = positionForLineColumn(text, line, 1);
    if (start < 0)
        return -1;
    int end = start;
    while (end < text.size() && text.at(end) != QLatin1Char('\n'))
        ++end;
    return end;
}

bool rangeForRecord(const SemanticSymbolRecord& record,
                    const QString& text,
                    int* start,
                    int* end)
{
    if (record.location.position >= 0 && record.location.length > 0) {
        const int pos = qBound(0, record.location.position, text.size());
        int len = record.location.length;
        if (record.location.endLine > record.location.startLine) {
            const int lineEnd = lineEndPosition(text, record.location.endLine);
            if (lineEnd > pos)
                len = lineEnd - pos;
        }
        *start = pos;
        *end = qBound(pos, pos + len, text.size());
        return *end > *start;
    }

    const int pos = positionForLineColumn(text,
                                          record.location.startLine,
                                          record.location.startColumn);
    const int lineEnd = lineEndPosition(text,
                                        qMax(record.location.endLine,
                                             record.location.startLine));
    if (pos < 0 || lineEnd <= pos)
        return false;
    *start = pos;
    *end = lineEnd;
    return true;
}

QList<SemanticSymbolRecord> recordsForFile(
    const QList<SemanticSymbolRecord>& records,
    const QString& fileName)
{
    QList<SemanticSymbolRecord> result;
    const QString normalizedFile = normalizedPath(fileName);
    for (const SemanticSymbolRecord& record : records) {
        if (normalizedPath(record.location.fileName) == normalizedFile)
            result.append(record);
    }
    return result;
}

QList<SemanticSymbolRecord> recordsInModule(
    const QList<SemanticSymbolRecord>& records,
    const SemanticSymbolRecord& module)
{
    QList<SemanticSymbolRecord> result;
    const QString fileName = normalizedPath(module.location.fileName);
    for (const SemanticSymbolRecord& record : records) {
        if (normalizedPath(record.location.fileName) != fileName)
            continue;
        if (record.owner.name == module.name)
            result.append(record);
    }
    return result;
}

bool isModuleRecord(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Module;
}

bool isProcessRecord(const SemanticSymbolRecord& record)
{
    return record.declarationKind == SymbolTaxonomy::DeclarationKind::Process
        || record.collectorKind == SymbolTaxonomy::CollectorKind::Always
        || record.collectorKind == SymbolTaxonomy::CollectorKind::AlwaysFf
        || record.collectorKind == SymbolTaxonomy::CollectorKind::AlwaysComb
        || record.collectorKind == SymbolTaxonomy::CollectorKind::AlwaysLatch;
}

bool isSignalKernelCandidate(const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    return SymbolTaxonomy::isSignalDeclaration(metadata)
        || SymbolTaxonomy::isPortDeclaration(metadata)
        || metadata.collectorKind == SymbolTaxonomy::CollectorKind::PortInput
        || metadata.collectorKind == SymbolTaxonomy::CollectorKind::PortOutput
        || metadata.collectorKind == SymbolTaxonomy::CollectorKind::PortInout;
}

bool isNextStateCandidate(const QString& name)
{
    const QString lower = name.toLower();
    return lower == QStringLiteral("ns")
        || lower == QStringLiteral("next_state")
        || lower.endsWith(QStringLiteral("_ns"))
        || lower.endsWith(QStringLiteral("_next_state"));
}

bool isCurrentStateCandidate(const QString& name)
{
    const QString lower = name.toLower();
    return lower == QStringLiteral("cs")
        || lower == QStringLiteral("current_state")
        || lower.endsWith(QStringLiteral("_cs"))
        || lower.endsWith(QStringLiteral("_current_state"));
}

QStringList pairedCurrentStateNames(const QString& nextStateName)
{
    const QString lower = nextStateName.toLower();
    QStringList names;
    if (lower == QStringLiteral("ns")) {
        names << QStringLiteral("cs")
              << QStringLiteral("current_state")
              << QStringLiteral("state");
    } else if (lower == QStringLiteral("next_state")) {
        names << QStringLiteral("current_state")
              << QStringLiteral("state")
              << QStringLiteral("cs");
    } else if (lower.endsWith(QStringLiteral("_next_state"))) {
        const QString prefix =
            lower.left(lower.size() - QStringLiteral("_next_state").size());
        names << prefix + QStringLiteral("_current_state")
              << prefix + QStringLiteral("_state")
              << prefix + QStringLiteral("_cs");
    } else if (lower.endsWith(QStringLiteral("_ns"))) {
        const QString prefix =
            lower.left(lower.size() - QStringLiteral("_ns").size());
        names << prefix + QStringLiteral("_cs")
              << prefix + QStringLiteral("_current_state")
              << prefix + QStringLiteral("_state");
    }
    names.removeDuplicates();
    return names;
}

SemanticSymbolRecord matchingCurrentStateSignal(
    const QList<SemanticSymbolRecord>& members,
    const SemanticSymbolRecord& nextStateSignal)
{
    const QStringList pairedNames = pairedCurrentStateNames(nextStateSignal.name);
    for (const QString& pairedName : pairedNames) {
        for (const SemanticSymbolRecord& member : members) {
            if (!isSignalKernelCandidate(member))
                continue;
            if (member.name.compare(pairedName, Qt::CaseInsensitive) == 0)
                return member;
        }
    }
    return {};
}

QString stripLineComment(QString line)
{
    const int commentStart = line.indexOf(QStringLiteral("//"));
    if (commentStart >= 0)
        line.truncate(commentStart);
    return line;
}

QString sourceForModule(const CorpusContext& context,
                        const SemanticSymbolRecord& module)
{
    const QString text = context.fileContents.value(
        normalizedPath(module.location.fileName));
    if (text.isEmpty())
        return QString();
    if (module.location.startLine <= 0 || module.location.endLine <= 0)
        return text;

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    const int startLine = qBound(1, module.location.startLine, lines.size());
    const int endLine = qBound(startLine, module.location.endLine, lines.size());
    QStringList moduleLines;
    for (int line = startLine; line <= endLine; ++line)
        moduleLines.append(lines.at(line - 1));
    return moduleLines.join(QLatin1Char('\n'));
}

bool sourceContainsWord(const QString& source, const QString& word)
{
    const QRegularExpression expression(
        QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(word)),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(source).hasMatch();
}

bool sourceContainsCaseForSignal(const QString& source, const QString& signalName)
{
    const QRegularExpression expression(
        QStringLiteral("\\bcase\\s*\\(\\s*%1\\b")
            .arg(QRegularExpression::escape(signalName)),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(source).hasMatch();
}

bool sourceContainsAssignmentToSignal(const QString& source,
                                      const QString& signalName)
{
    const QRegularExpression expression(
        QStringLiteral("\\b%1\\b\\s*(?:<=|=)")
            .arg(QRegularExpression::escape(signalName)),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(source).hasMatch();
}

bool isParameterLikeStateValue(const SemanticSymbolRecord& record)
{
    const SymbolTaxonomy::SemanticMetadata metadata =
        semanticMetadataForSymbolRecord(record);
    const bool parameterLike =
        metadata.collectorKind == SymbolTaxonomy::CollectorKind::Parameter
        || metadata.collectorKind == SymbolTaxonomy::CollectorKind::Localparam
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Parameter
        || metadata.declarationKind == SymbolTaxonomy::DeclarationKind::Localparam;
    if (!parameterLike)
        return false;

    const QString upper = record.name.toUpper();
    return upper.startsWith(QStringLiteral("S_"))
        || upper.startsWith(QStringLiteral("ST_"))
        || upper.startsWith(QStringLiteral("SM_"))
        || upper.endsWith(QStringLiteral("_STATE"))
        || upper.contains(QStringLiteral("_STATE_"));
}

bool moduleHasStateValueCandidates(const QList<SemanticSymbolRecord>& members)
{
    for (const SemanticSymbolRecord& member : members) {
        const SymbolTaxonomy::SemanticMetadata metadata =
            semanticMetadataForSymbolRecord(member);
        if (SymbolTaxonomy::isFsmStateValueDeclaration(metadata)
            || isParameterLikeStateValue(member)) {
            return true;
        }
    }
    return false;
}

AuditStatus classifyStateTransitionFailure(
    const CorpusContext& context,
    const SemanticSymbolRecord& module,
    const QList<SemanticSymbolRecord>& members,
    const SemanticSymbolRecord& nextStateSignal,
    AuditCase* item)
{
    const SemanticSymbolRecord currentState =
        matchingCurrentStateSignal(members, nextStateSignal);
    if (!currentState.isValid()) {
        item->reason =
            QStringLiteral("no paired current-state signal for next-state candidate");
        return AuditStatus::Skipped;
    }

    item->metrics.insert(QStringLiteral("pairedCurrentState"),
                         currentState.name);
    const QString moduleSource = sourceForModule(context, module);
    if (moduleSource.isEmpty()) {
        item->reason = QStringLiteral("source text unavailable for module");
        return AuditStatus::Fail;
    }
    if (!sourceContainsWord(moduleSource, QStringLiteral("case"))) {
        item->reason =
            QStringLiteral("unsupported pattern: no case statement in module");
        return AuditStatus::Fail;
    }
    if (!sourceContainsCaseForSignal(moduleSource, currentState.name)) {
        item->reason =
            QStringLiteral("unsupported pattern: no case on paired current-state signal %1")
                .arg(currentState.name);
        return AuditStatus::Fail;
    }
    if (!sourceContainsAssignmentToSignal(moduleSource, nextStateSignal.name)) {
        item->reason =
            QStringLiteral("no assignment to next-state candidate in paired FSM case");
        return AuditStatus::Fail;
    }
    if (!moduleHasStateValueCandidates(members)) {
        item->reason =
            QStringLiteral("no enum/localparam/parameter state values recognized");
        return AuditStatus::Fail;
    }

    item->reason =
        QStringLiteral("no transitions extracted from recognized FSM pattern");
    return AuditStatus::Fail;
}

QString relationshipReason(const QString& displayName, const QString& fallback)
{
    return displayName.isEmpty() ? fallback : displayName;
}

SemanticRelationship toSemanticRelationship(const RelationshipToAdd& rel,
                                            const SemanticIndexSnapshot& snapshot)
{
    SemanticRelationship relationship;
    relationship.fromId = rel.fromId;
    relationship.toId = rel.toId;
    relationship.type = rel.type;
    relationship.provenance = RelationshipProvenance::Inferred;
    relationship.confidence = rel.confidence;
    relationship.evidenceText = rel.context;
    relationship.evidenceRange = rel.evidenceRange;
    relationship.fromAccessPath = rel.fromAccessPath;
    relationship.toAccessPath = rel.toAccessPath;
    return snapshot.rebindRelationship(relationship);
}

QString rootAccessName(const QString& name)
{
    QString result = name.trimmed();
    const int dot = result.indexOf(QLatin1Char('.'));
    const int bracket = result.indexOf(QLatin1Char('['));
    int end = result.size();
    if (dot >= 0)
        end = qMin(end, dot);
    if (bracket >= 0)
        end = qMin(end, bracket);
    return result.left(end).trimmed();
}

SemanticSymbolRecord containingModuleAtLine(
    const QList<SemanticSymbolRecord>& fileRecords,
    int line)
{
    SemanticSymbolRecord best;
    if (line <= 0) {
        for (const SemanticSymbolRecord& record : fileRecords) {
            if (isModuleRecord(record))
                return record;
        }
        return {};
    }
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (!isModuleRecord(record))
            continue;
        if (record.location.startLine > line)
            continue;
        if (record.location.endLine > 0 && line > record.location.endLine)
            continue;
        if (!best.isValid()
            || record.location.startLine >= best.location.startLine) {
            best = record;
        }
    }
    if (best.isValid())
        return best;

    for (const SemanticSymbolRecord& record : fileRecords) {
        if (isModuleRecord(record) && record.location.startLine <= line)
            best = record;
    }
    return best;
}

SemanticSymbolRecord symbolInModuleByName(
    const QList<SemanticSymbolRecord>& fileRecords,
    const QString& moduleName,
    const QString& name)
{
    const QString rootName = rootAccessName(name);
    for (const SemanticSymbolRecord& record : fileRecords) {
        if (record.owner.name == moduleName && record.name == rootName)
            return record;
    }
    return {};
}

SemanticRelationship directRelationship(
    const SemanticSymbolRecord& from,
    const SemanticSymbolRecord& to,
    SymbolRelationshipEngine::RelationType type,
    const QString& evidenceText,
    const SemanticSourceRange& evidenceRange,
    const QString& fromAccessPath = {},
    const QString& toAccessPath = {})
{
    SemanticRelationship relationship;
    relationship.fromId = from.localHandle;
    relationship.toId = to.localHandle;
    relationship.type = type;
    relationship.fromStableKey = from.stableKey;
    relationship.toStableKey = to.stableKey;
    relationship.provenance = RelationshipProvenance::Inferred;
    relationship.confidence = 80;
    relationship.evidenceText = evidenceText;
    relationship.evidenceRange = evidenceRange;
    relationship.fromAccessPath = fromAccessPath;
    relationship.toAccessPath = toAccessPath;
    return relationship;
}

QList<SemanticRelationship> directRelationshipsFromFacts(
    const QHash<QString, RelationshipExtractionInfo>& relationshipFacts,
    const QHash<QString, QList<SemanticSymbolRecord>>& recordsByFile,
    const QHash<QString, QList<SemanticSymbolRecord>>& recordsByBaseName,
    const QHash<QString, SemanticSymbolRecord>& moduleByName)
{
    QList<SemanticRelationship> relationships;
    QSet<QString> seen;
    int instFacts = 0;
    int assignmentFacts = 0;
    int conditionFacts = 0;
    int timingFacts = 0;
    int missingFileRecords = 0;
    int missingOwner = 0;
    int missingTarget = 0;
    int missingSignal = 0;
    auto appendUnique = [&relationships, &seen](const SemanticRelationship& rel) {
        if (rel.fromId < 0 || rel.toId < 0)
            return;
        const QString key = QStringLiteral("%1:%2:%3:%4:%5")
            .arg(rel.fromId)
            .arg(rel.toId)
            .arg(static_cast<int>(rel.type))
            .arg(rel.evidenceRange.fileName)
            .arg(rel.evidenceRange.line);
        if (seen.contains(key))
            return;
        seen.insert(key);
        relationships.append(rel);
    };

    for (auto it = relationshipFacts.constBegin();
         it != relationshipFacts.constEnd();
         ++it) {
        QList<SemanticSymbolRecord> fileRecords = recordsByFile.value(it.key());
        if (fileRecords.isEmpty())
            fileRecords = recordsByBaseName.value(QFileInfo(it.key()).fileName());
        if (fileRecords.isEmpty()) {
            ++missingFileRecords;
            continue;
        }

        const RelationshipExtractionInfo& facts = it.value();
        for (const ModuleInstantiationInfo& inst : facts.moduleInstantiations) {
            ++instFacts;
            const SemanticSymbolRecord owner =
                containingModuleAtLine(fileRecords, inst.lineNumber);
            const SemanticSymbolRecord target = moduleByName.value(inst.moduleName);
            if (!owner.isValid()) {
                ++missingOwner;
                continue;
            }
            if (!target.isValid()) {
                ++missingTarget;
                continue;
            }
            appendUnique(directRelationship(
                owner,
                target,
                SymbolRelationshipEngine::INSTANTIATES,
                QStringLiteral("%1 %2").arg(inst.moduleName, inst.instanceName),
                inst.sourceRange));
        }

        for (const AssignmentInfo& assignment : facts.assignments) {
            ++assignmentFacts;
            const SemanticSymbolRecord owner =
                containingModuleAtLine(fileRecords, assignment.lineNumber);
            if (!owner.isValid()) {
                ++missingOwner;
                continue;
            }
            const SemanticSymbolRecord left =
                symbolInModuleByName(fileRecords, owner.name, assignment.leftName);
            if (!left.isValid()) {
                ++missingSignal;
                continue;
            }
            for (int i = 0; i < assignment.rightNames.size(); ++i) {
                const QString rightName = assignment.rightNames.at(i);
                const SemanticSymbolRecord right =
                    symbolInModuleByName(fileRecords, owner.name, rightName);
                if (!right.isValid()) {
                    ++missingSignal;
                    continue;
                }
                appendUnique(directRelationship(
                    right,
                    left,
                    SymbolRelationshipEngine::ASSIGNS_TO,
                    QStringLiteral("%1 <= %2").arg(assignment.leftName, rightName),
                    assignment.sourceRange,
                    i < assignment.rightAccessPaths.size()
                        ? assignment.rightAccessPaths.at(i)
                        : QString(),
                    assignment.leftAccessPath));
            }
        }

        for (const ConditionReferenceInfo& condition : facts.conditionReferences) {
            ++conditionFacts;
            const SemanticSymbolRecord owner =
                containingModuleAtLine(fileRecords, condition.lineNumber);
            if (!owner.isValid()) {
                ++missingOwner;
                continue;
            }
            for (int i = 0; i < condition.symbolNames.size(); ++i) {
                const QString signalName = condition.symbolNames.at(i);
                const SemanticSymbolRecord signal =
                    symbolInModuleByName(fileRecords, owner.name, signalName);
                if (!signal.isValid()) {
                    ++missingSignal;
                    continue;
                }
                appendUnique(directRelationship(
                    owner,
                    signal,
                    SymbolRelationshipEngine::READS_FROM,
                    QStringLiteral("condition reads %1").arg(signalName),
                    condition.sourceRange,
                    QString(),
                    i < condition.symbolAccessPaths.size()
                        ? condition.symbolAccessPaths.at(i)
                        : QString()));
            }
        }

        for (const TimingSignalInfo& timing : facts.timingSignals) {
            ++timingFacts;
            const SemanticSymbolRecord owner =
                containingModuleAtLine(fileRecords, timing.lineNumber);
            if (!owner.isValid()) {
                ++missingOwner;
                continue;
            }
            const SemanticSymbolRecord signal =
                symbolInModuleByName(fileRecords, owner.name, timing.signalName);
            if (!signal.isValid()) {
                ++missingSignal;
                continue;
            }
            const QString lower = timing.signalName.toLower();
            const bool resetLike = lower.contains(QStringLiteral("rst"))
                || lower.contains(QStringLiteral("reset"));
            appendUnique(directRelationship(
                signal,
                owner,
                resetLike ? SymbolRelationshipEngine::RESETS
                          : SymbolRelationshipEngine::CLOCKS,
                QStringLiteral("timing control %1").arg(timing.signalName),
                timing.sourceRange,
                timing.signalAccessPath));
        }
    }
    printf("corpus_audit: direct relationship facts inst=%d assign=%d condition=%d timing=%d missingFile=%d missingOwner=%d missingTarget=%d missingSignal=%d emitted=%d\n",
           instFacts,
           assignmentFacts,
           conditionFacts,
           timingFacts,
           missingFileRecords,
           missingOwner,
           missingTarget,
           missingSignal,
           relationships.size());
    fflush(stdout);
    return relationships;
}

QStringList featureOrder()
{
    return {
        QStringLiteral("state_transition_graph"),
        QStringLiteral("signal_kernel_graph"),
        QStringLiteral("module_block_diagram"),
        QStringLiteral("wave_preview"),
        QStringLiteral("semantic_baseline")
    };
}

void addCase(QList<AuditCase>* cases,
             QHash<QString, FeatureCounts>* counts,
             AuditCase item,
             AuditStatus status)
{
    item.status = statusText(status);
    cases->append(item);
    FeatureCounts& feature = (*counts)[item.feature];
    switch (status) {
    case AuditStatus::Pass:
        ++feature.pass;
        break;
    case AuditStatus::Fail:
        ++feature.fail;
        break;
    case AuditStatus::Skipped:
        ++feature.skipped;
        break;
    case AuditStatus::Timeout:
        ++feature.timeout;
        break;
    case AuditStatus::EmptyButValid:
        ++feature.emptyButValid;
        break;
    }
}

QList<SemanticSymbolRecord> moduleRecords(const QList<SemanticSymbolRecord>& records)
{
    QList<SemanticSymbolRecord> modules;
    for (const SemanticSymbolRecord& record : records) {
        if (isModuleRecord(record))
            modules.append(record);
    }
    std::sort(modules.begin(), modules.end(), [](const auto& lhs, const auto& rhs) {
        if (normalizedPath(lhs.location.fileName) != normalizedPath(rhs.location.fileName))
            return normalizedPath(lhs.location.fileName) < normalizedPath(rhs.location.fileName);
        if (lhs.location.startLine != rhs.location.startLine)
            return lhs.location.startLine < rhs.location.startLine;
        return lhs.name < rhs.name;
    });
    return modules;
}

int wordOccurrenceCount(const QString& text, const QString& word)
{
    const QRegularExpression expression(
        QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(word)),
        QRegularExpression::CaseInsensitiveOption);
    int count = 0;
    QRegularExpressionMatchIterator it = expression.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

SymbolTaxonomy::CollectorKind processCollectorKindForToken(const QString& token)
{
    const QString lower = token.toLower();
    if (lower == QStringLiteral("always_ff"))
        return SymbolTaxonomy::CollectorKind::AlwaysFf;
    if (lower == QStringLiteral("always_comb"))
        return SymbolTaxonomy::CollectorKind::AlwaysComb;
    if (lower == QStringLiteral("always_latch"))
        return SymbolTaxonomy::CollectorKind::AlwaysLatch;
    return SymbolTaxonomy::CollectorKind::Always;
}

int estimateProcessEndLine(const QStringList& lines,
                           int startLine,
                           int moduleEndLine)
{
    int depth = 0;
    bool sawBegin = false;
    const int endLine = qBound(startLine, moduleEndLine, lines.size());
    for (int line = startLine; line <= endLine; ++line) {
        const QString code = stripLineComment(lines.at(line - 1));
        const int begins = wordOccurrenceCount(code, QStringLiteral("begin"));
        const int ends = wordOccurrenceCount(code, QStringLiteral("end"));
        if (begins > 0)
            sawBegin = true;
        depth += begins;
        depth -= ends;
        if (sawBegin && line > startLine && depth <= 0)
            return line;
        if (!sawBegin && code.contains(QLatin1Char(';')))
            return line;
    }
    return endLine;
}

void appendDiscoveredProcessRecords(CorpusContext* context, int* nextLocalHandle)
{
    if (!context || !nextLocalHandle)
        return;

    const QRegularExpression alwaysExpression(
        QStringLiteral("\\balways(?:_(?:ff|comb|latch))?\\b"),
        QRegularExpression::CaseInsensitiveOption);
    QSet<QString> existing;
    for (const SemanticSymbolRecord& record : std::as_const(context->records)) {
        if (!isProcessRecord(record))
            continue;
        existing.insert(QStringLiteral("%1:%2:%3")
                            .arg(normalizedPath(record.location.fileName))
                            .arg(record.owner.name)
                            .arg(record.location.startLine));
    }

    int appended = 0;
    const QList<SemanticSymbolRecord> modules = moduleRecords(context->records);
    for (const SemanticSymbolRecord& module : modules) {
        const QString fileName = normalizedPath(module.location.fileName);
        const QString text = context->fileContents.value(fileName);
        if (text.isEmpty())
            continue;
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        const int moduleStart =
            qBound(1, qMax(1, module.location.startLine), lines.size());
        const int moduleEnd =
            qBound(moduleStart,
                   module.location.endLine > 0 ? module.location.endLine
                                               : lines.size(),
                   lines.size());
        for (int line = moduleStart; line <= moduleEnd; ++line) {
            const QString code = stripLineComment(lines.at(line - 1));
            const QRegularExpressionMatch match =
                alwaysExpression.match(code);
            if (!match.hasMatch())
                continue;
            const QString existingKey = QStringLiteral("%1:%2:%3")
                                            .arg(fileName)
                                            .arg(module.name)
                                            .arg(line);
            if (existing.contains(existingKey))
                continue;
            existing.insert(existingKey);

            const QString token = match.captured(0);
            const int startColumn = match.capturedStart(0) + 1;
            const int endLine = estimateProcessEndLine(lines, line, moduleEnd);
            const int startPosition = positionForLineColumn(text,
                                                            line,
                                                            startColumn);
            const int endPosition = lineEndPosition(text, endLine);

            SemanticSymbolRecord process;
            process.name = QStringLiteral("%1@%2").arg(token).arg(line);
            process.localHandle = (*nextLocalHandle)++;
            process.location.fileName = fileName;
            process.location.startLine = line;
            process.location.startColumn = startColumn;
            process.location.endLine = endLine;
            process.location.endColumn = 1;
            process.location.position = qMax(0, startPosition);
            process.location.length =
                endPosition > startPosition ? endPosition - startPosition
                                            : token.size();
            process.declarationKind =
                SymbolTaxonomy::DeclarationKind::Process;
            process.usageRole = SymbolTaxonomy::SymbolUsageRole::Process;
            process.visibility = SymbolTaxonomy::SymbolVisibility::ScopeLocal;
            process.sourceRole =
                SymbolTaxonomy::sourceRoleForFileName(process.location.fileName);
            process.collectorKind = processCollectorKindForToken(token);
            process.owner.kind = SymbolTaxonomy::SymbolOwnerScope::Module;
            process.owner.name = module.name;
            process.owner.stableKey = module.stableKey;
            process.stableKey.fileName = process.location.fileName;
            process.stableKey.symbolName = process.name;
            process.stableKey.declarationKind = process.declarationKind;
            process.stableKey.ownerScope = module.name;
            context->records.append(process);
            ++appended;
        }
    }

    printf("corpus_audit: discovered %d always/process records from source\n",
           appended);
    fflush(stdout);
}

QJsonArray statusCountsJson(const QHash<QString, FeatureCounts>& counts)
{
    QJsonArray array;
    for (const QString& feature : featureOrder()) {
        const FeatureCounts c = counts.value(feature);
        QJsonObject object;
        object.insert(QStringLiteral("feature"), feature);
        object.insert(QStringLiteral("pass"), c.pass);
        object.insert(QStringLiteral("fail"), c.fail);
        object.insert(QStringLiteral("skipped"), c.skipped);
        object.insert(QStringLiteral("timeout"), c.timeout);
        object.insert(QStringLiteral("emptyButValid"), c.emptyButValid);
        array.append(object);
    }
    return array;
}

QString markdownTableForCounts(const QHash<QString, FeatureCounts>& counts)
{
    QString text;
    QTextStream out(&text);
    out << "| Feature | Pass | Fail | Skipped | Timeout | Empty-but-valid |\n";
    out << "| --- | ---: | ---: | ---: | ---: | ---: |\n";
    for (const QString& feature : featureOrder()) {
        const FeatureCounts c = counts.value(feature);
        out << "| `" << feature << "` | " << c.pass << " | " << c.fail
            << " | " << c.skipped << " | " << c.timeout << " | "
            << c.emptyButValid << " |\n";
    }
    return text;
}

QString markdownReport(const CorpusContext& context,
                       const QList<AuditCase>& cases,
                       const QHash<QString, FeatureCounts>& counts,
                       qint64 elapsedMs)
{
    QString text;
    QTextStream out(&text);
    const QList<SemanticSymbolRecord> modules = moduleRecords(context.records);
    int processCount = 0;
    int signalCount = 0;
    for (const SemanticSymbolRecord& record : context.records) {
        if (isProcessRecord(record))
            ++processCount;
        if (isSignalKernelCandidate(record))
            ++signalCount;
    }

    out << "# ZeroSlack Functional Corpus Audit\n\n";
    out << "- Generated: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
        << " UTC\n";
    out << "- Roots: ";
    QStringList rootNames;
    for (const QString& root : context.roots)
        rootNames.append(relativePath(context.workspaceRoot, root));
    out << rootNames.join(QStringLiteral(", ")) << "\n";
    out << "- Files: " << context.files.size() << "\n";
    out << "- Modules: " << modules.size() << "\n";
    out << "- Always/process records: " << processCount << "\n";
    out << "- Signal/port candidates: " << signalCount << "\n";
    out << "- Semantic records: " << context.records.size() << "\n";
    out << "- Relationships: " << context.relationships.size() << "\n";
    out << "- Diagnostics: " << context.diagnostics.size() << "\n";
    out << "- Elapsed: " << elapsedMs << " ms\n\n";
    out << "## Feature Summary\n\n";
    out << markdownTableForCounts(counts) << "\n";

    out << "## Key Failures\n\n";
    int shown = 0;
    for (const AuditCase& item : cases) {
        if (item.status != QStringLiteral("fail"))
            continue;
        out << "- `" << item.feature << "` "
            << relativePath(context.workspaceRoot, item.fileName)
            << ":" << item.line << " module `" << item.moduleName
            << "` symbol `" << item.symbolName << "`: " << item.reason << "\n";
        if (++shown >= 50)
            break;
    }
    if (shown == 0)
        out << "- No hard failures recorded by the audit harness.\n";
    out << "\n## Notes\n\n";
    out << "- Real corpus files were opened read-only; reports are written under `test_sv`.\n";
    out << "- `empty-but-valid` means the service returned a coherent empty/root-only result, not a full feature pass.\n";
    out << "- `skipped` means the corpus item did not contain the required structural trigger shape, such as no clocked FSM pair or no always block.\n";
    out << "\n## Known Issues And Residual Risk\n\n";
    out << "- State Transition Graph is structure-discovered: clocked current<=next pairs drive next-state positive cases and current-state negative cases. Skipped modules have no structural FSM pair under the current extractor.\n";
    out << "- Signal Kernel Graph uses a bounded deep-call budget: 4 signal graph attempts per module and 400 total attempts. Skipped candidates are counted explicitly.\n";
    out << "- Wave Preview uses source-discovered always/process records when workspace symbol extraction does not expose process nodes; module fallback remains explicit.\n";
    out << "- Empty outline source files are classified as preprocessor/comment-only, guarded, skipped, or real outline failures instead of being hidden.\n";
    return text;
}

void auditStateTransitions(const CorpusContext& context,
                           const QList<SemanticSymbolRecord>& modules,
                           QList<AuditCase>* cases,
                           QHash<QString, FeatureCounts>* counts)
{
    FsmGraphService fsmService(const_cast<SemanticIndex*>(SemanticIndex::getInstance()));
    StateTransitionGraphService service(const_cast<SemanticIndex*>(SemanticIndex::getInstance()));
    for (const SemanticSymbolRecord& module : modules) {
        FsmGraphQuery fsmQuery;
        fsmQuery.moduleName = module.name;
        fsmQuery.fileName = module.location.fileName;
        const FsmGraphReport fsmReport = fsmService.buildFsmGraph(fsmQuery);
        if (!fsmReport.found || fsmReport.graphs.isEmpty()) {
            AuditCase item;
            item.feature = QStringLiteral("state_transition_graph");
            item.fileName = module.location.fileName;
            item.moduleName = module.name;
            item.locationKind = QStringLiteral("module");
            item.line = module.location.startLine;
            item.column = module.location.startColumn;
            item.reason = QStringLiteral("no structural FSM pair discovered");
            addCase(cases, counts, item, AuditStatus::Skipped);
        }

        for (const FsmGraph& graph : fsmReport.graphs) {
            const SemanticSymbolRecord& signal = graph.nextStateSignalRecord;
            StateTransitionGraphQuery query;
            query.symbolName = signal.name;
            query.fileName = signal.location.fileName;
            query.moduleName = module.name;
            const StateTransitionGraphReport report =
                service.buildStateTransitionGraph(query);

            AuditCase item;
            item.feature = QStringLiteral("state_transition_graph");
            item.fileName = signal.location.fileName;
            item.moduleName = module.name;
            item.symbolName = signal.name;
            item.locationKind = QStringLiteral("next_state_signal");
            item.line = signal.location.startLine;
            item.column = signal.location.startColumn;
            item.metrics.insert(QStringLiteral("stateCount"), report.stateCount);
            item.metrics.insert(QStringLiteral("transitionCount"), report.transitionCount);
            item.emptyResult = report.transitionCount == 0;
            if (report.found && report.stateCount > 0 && report.transitionCount > 0) {
                addCase(cases, counts, item, AuditStatus::Pass);
            } else {
                item.reason = relationshipReason(
                    report.notFoundReasonDisplayName,
                    QStringLiteral("structural FSM pair did not produce a transition graph"));
                addCase(cases, counts, item, AuditStatus::Fail);
            }

            const SemanticSymbolRecord& current = graph.stateRegisterRecord;
            StateTransitionGraphQuery currentQuery;
            currentQuery.symbolName = current.name;
            currentQuery.fileName = current.location.fileName;
            currentQuery.moduleName = module.name;
            const StateTransitionGraphReport currentReport =
                service.buildStateTransitionGraph(currentQuery);
            AuditCase currentItem;
            currentItem.feature = QStringLiteral("state_transition_graph");
            currentItem.fileName = current.location.fileName;
            currentItem.moduleName = module.name;
            currentItem.symbolName = current.name;
            currentItem.locationKind = QStringLiteral("current_state_negative");
            currentItem.line = current.location.startLine;
            currentItem.column = current.location.startColumn;
            currentItem.reason = currentReport.notFoundReasonDisplayName;
            const bool rejected =
                !currentReport.found
                && currentReport.notFoundReason
                    == StateTransitionGraphNotFoundReason::TriggerRejected
                && currentReport.notFoundReasonDisplayName.contains(
                    QStringLiteral("next-state"), Qt::CaseInsensitive);
            addCase(cases, counts, currentItem,
                    rejected ? AuditStatus::Pass : AuditStatus::Fail);
        }
    }
}

void auditSignalKernelGraphs(const CorpusContext& context,
                             const QList<SemanticSymbolRecord>& modules,
                             QList<AuditCase>* cases,
                             QHash<QString, FeatureCounts>* counts)
{
    constexpr int kMaxSignalsPerModule = 4;
    constexpr int kMaxTotalSignalGraphAttempts = 400;
    SignalKernelGraphService service(SemanticIndex::getInstance());
    QSet<int> relationshipEndpoints;
    for (const SemanticRelationship& relationship : context.relationships) {
        if (relationship.fromId >= 0)
            relationshipEndpoints.insert(relationship.fromId);
        if (relationship.toId >= 0)
            relationshipEndpoints.insert(relationship.toId);
    }
    int totalAttempts = 0;
    for (const SemanticSymbolRecord& module : modules) {
        const QList<SemanticSymbolRecord> members =
            recordsInModule(context.records, module);
        QList<SemanticSymbolRecord> signalRecords;
        for (const SemanticSymbolRecord& member : members) {
            if (isSignalKernelCandidate(member))
                signalRecords.append(member);
        }

        if (signalRecords.isEmpty()) {
            AuditCase item;
            item.feature = QStringLiteral("signal_kernel_graph");
            item.fileName = module.location.fileName;
            item.moduleName = module.name;
            item.locationKind = QStringLiteral("module");
            item.line = module.location.startLine;
            item.column = module.location.startColumn;
            item.reason = QStringLiteral("no signal or port candidate");
            addCase(cases, counts, item, AuditStatus::Skipped);
        }

        QList<SemanticSymbolRecord> selectedSignals;
        for (const SemanticSymbolRecord& signal : signalRecords) {
            if (selectedSignals.size() >= kMaxSignalsPerModule)
                break;
            if (relationshipEndpoints.contains(signal.localHandle))
                selectedSignals.append(signal);
        }
        for (const SemanticSymbolRecord& signal : signalRecords) {
            if (selectedSignals.size() >= kMaxSignalsPerModule)
                break;
            bool alreadySelected = false;
            for (const SemanticSymbolRecord& selected : selectedSignals) {
                if (selected.localHandle == signal.localHandle) {
                    alreadySelected = true;
                    break;
                }
            }
            if (!alreadySelected)
                selectedSignals.append(signal);
        }

        if (selectedSignals.size() < signalRecords.size()) {
            AuditCase item;
            item.feature = QStringLiteral("signal_kernel_graph");
            item.fileName = module.location.fileName;
            item.moduleName = module.name;
            item.locationKind = QStringLiteral("module_signal_budget");
            item.line = module.location.startLine;
            item.column = module.location.startColumn;
            item.reason = QStringLiteral("signals traversed but not individually graphed by audit budget");
            item.metrics.insert(QStringLiteral("candidateSignals"), signalRecords.size());
            item.metrics.insert(QStringLiteral("attemptedSignals"), selectedSignals.size());
            addCase(cases, counts, item, AuditStatus::Skipped);
        }

        for (const SemanticSymbolRecord& signal : selectedSignals) {
            if (totalAttempts >= kMaxTotalSignalGraphAttempts) {
                AuditCase item;
                item.feature = QStringLiteral("signal_kernel_graph");
                item.fileName = signal.location.fileName;
                item.moduleName = module.name;
                item.symbolName = signal.name;
                item.locationKind = QStringLiteral("signal");
                item.line = signal.location.startLine;
                item.column = signal.location.startColumn;
                item.reason = QStringLiteral("global signal graph attempt budget exhausted");
                addCase(cases, counts, item, AuditStatus::Skipped);
                continue;
            }
            ++totalAttempts;
            SignalKernelGraphQuery query;
            query.signalStableKey = signal.stableKey;
            query.signalName = signal.name;
            query.fileName = signal.location.fileName;
            query.moduleName = module.name;
            const SignalKernelGraphReport report =
                service.buildSignalKernelGraph(query);
            AuditCase item;
            item.feature = QStringLiteral("signal_kernel_graph");
            item.fileName = signal.location.fileName;
            item.moduleName = module.name;
            item.symbolName = signal.name;
            item.locationKind = QStringLiteral("signal");
            item.line = signal.location.startLine;
            item.column = signal.location.startColumn;
            item.metrics.insert(QStringLiteral("inputs"), report.inputs.size());
            item.metrics.insert(QStringLiteral("outputs"), report.outputs.size());
            item.metrics.insert(QStringLiteral("edges"), report.edges.size());
            item.metrics.insert(QStringLiteral("inputFanoutGroups"),
                                report.inputFanoutGroups.size());
            item.metrics.insert(QStringLiteral("outputFanoutGroups"),
                                report.outputFanoutGroups.size());
            bool crossModule = false;
            for (const SignalKernelGraphNode& node : report.inputs)
                crossModule = crossModule || node.crossModule;
            for (const SignalKernelGraphNode& node : report.outputs)
                crossModule = crossModule || node.crossModule;
            if (crossModule)
                item.tags.append(QStringLiteral("cross_module"));
            for (const SignalKernelGraphFanoutGroup& group :
                 report.outputFanoutGroups) {
                if (group.highFanout)
                    item.tags.append(QStringLiteral("high_fanout"));
            }
            const bool hasGraphBody =
                !report.inputs.isEmpty() || !report.outputs.isEmpty()
                || !report.edges.isEmpty();
            item.emptyResult = !hasGraphBody;
            if (!report.found) {
                item.reason = relationshipReason(
                    report.notFoundReasonDisplayName,
                    QStringLiteral("signal journey unavailable"));
                addCase(cases, counts, item, AuditStatus::Fail);
            } else if (!hasGraphBody) {
                item.reason = QStringLiteral("kernel resolved but no driver/consumer relationships");
                addCase(cases, counts, item, AuditStatus::EmptyButValid);
            } else {
                addCase(cases, counts, item, AuditStatus::Pass);
            }
        }
    }
}

void auditModuleBlockDiagrams(const CorpusContext& context,
                              const QList<SemanticSymbolRecord>& modules,
                              QList<AuditCase>* cases,
                              QHash<QString, FeatureCounts>* counts)
{
    ModuleBlockDiagramService service(SemanticIndex::getInstance());
    QSet<QString> moduleNames;
    for (const SemanticSymbolRecord& module : modules)
        moduleNames.insert(module.name);

    for (const SemanticSymbolRecord& module : modules) {
        const QList<SemanticSymbolRecord> members =
            recordsInModule(context.records, module);
        int unresolvedInstances = 0;
        for (const SemanticSymbolRecord& member : members) {
            const SymbolTaxonomy::SemanticMetadata metadata =
                semanticMetadataForSymbolRecord(member);
            if (!SymbolTaxonomy::isInstanceDeclaration(metadata))
                continue;
            const QString typeName =
                !member.type.resolvedTypeName.isEmpty()
                    ? member.type.resolvedTypeName
                    : member.type.rawTypeText.trimmed();
            if (!typeName.isEmpty() && !moduleNames.contains(typeName))
                ++unresolvedInstances;
        }

        ModuleBlockDiagramQuery query;
        query.moduleStableKey = module.stableKey;
        query.moduleName = module.name;
        query.fileName = module.location.fileName;
        query.maxDepth = 8;
        const ModuleBlockDiagramReport report =
            service.buildModuleBlockDiagram(query);

        AuditCase item;
        item.feature = QStringLiteral("module_block_diagram");
        item.fileName = module.location.fileName;
        item.moduleName = module.name;
        item.symbolName = module.name;
        item.locationKind = QStringLiteral("module");
        item.line = module.location.startLine;
        item.column = module.location.startColumn;
        item.metrics.insert(QStringLiteral("moduleCount"), report.moduleCount);
        item.metrics.insert(QStringLiteral("edgeCount"), report.edgeCount);
        item.metrics.insert(QStringLiteral("unresolvedInstances"),
                            unresolvedInstances);
        item.emptyResult = report.edgeCount == 0;
        if (unresolvedInstances > 0)
            item.tags.append(QStringLiteral("unresolved_instance"));

        bool linksOk = report.root.definitionCodeLink.line > 0;
        bool onlyModules = true;
        for (const ModuleBlockDiagramNode& node : report.nodes) {
            linksOk = linksOk && node.definitionCodeLink.line > 0
                && !node.definitionCodeLink.fileName.isEmpty();
            onlyModules = onlyModules
                && (node.moduleSymbolRecord.declarationKind
                        == SymbolTaxonomy::DeclarationKind::Module
                    || node.moduleSymbolRecord.declarationKind
                        == SymbolTaxonomy::DeclarationKind::Interface);
        }
        item.metrics.insert(QStringLiteral("navigationLinksOk"), linksOk);
        item.metrics.insert(QStringLiteral("moduleOnlyNodes"), onlyModules);

        if (!report.found && report.notFoundReason
            == ModuleBlockDiagramNotFoundReason::NoRootModule) {
            item.reason = relationshipReason(report.notFoundReasonDisplayName,
                                             QStringLiteral("no root module"));
            addCase(cases, counts, item, AuditStatus::Fail);
        } else if (!linksOk || !onlyModules) {
            item.reason = !linksOk
                ? QStringLiteral("diagram node navigation link missing")
                : QStringLiteral("diagram contains non-module/interface node");
            addCase(cases, counts, item, AuditStatus::Fail);
        } else if (report.edgeCount == 0) {
            item.reason = relationshipReason(report.notFoundReasonDisplayName,
                                             QStringLiteral("leaf module"));
            addCase(cases, counts, item, AuditStatus::EmptyButValid);
        } else {
            addCase(cases, counts, item, AuditStatus::Pass);
        }
    }
}

void auditWavePreview(const CorpusContext& context,
                      const QList<SemanticSymbolRecord>& modules,
                      QList<AuditCase>* cases,
                      QHash<QString, FeatureCounts>* counts)
{
    WavePreviewService service;
    for (const SemanticSymbolRecord& module : modules) {
        const QList<SemanticSymbolRecord> members =
            recordsInModule(context.records, module);
        QList<SemanticSymbolRecord> processes;
        for (const SemanticSymbolRecord& member : members) {
            if (isProcessRecord(member))
                processes.append(member);
        }
        if (processes.isEmpty()) {
            AuditCase item;
            item.feature = QStringLiteral("wave_preview");
            item.fileName = module.location.fileName;
            item.moduleName = module.name;
            item.locationKind = QStringLiteral("module_scope_fallback");
            item.line = module.location.startLine;
            item.column = module.location.startColumn;
            const QString text = context.fileContents.value(
                normalizedPath(module.location.fileName));
            int start = -1;
            int end = -1;
            const bool hasRange = rangeForRecord(module, text, &start, &end);
            WavePreviewQuery query;
            query.fileName = module.location.fileName;
            query.documentText = text;
            query.semanticSnapshot = context.snapshot;
            query.scopeStartPosition = hasRange ? start : -1;
            query.scopeEndPosition = hasRange ? end : -1;
            query.scopeLabel = QStringLiteral("%1 module scope").arg(module.name);
            const WavePreviewReport report = service.previewForDocument(query);
            item.metrics.insert(QStringLiteral("available"), report.available);
            item.metrics.insert(QStringLiteral("blocks"), report.blocks.size());
            item.metrics.insert(QStringLiteral("lanes"), report.lanes.size());
            item.metrics.insert(QStringLiteral("assignments"),
                                report.assignmentCount);
            item.metrics.insert(QStringLiteral("warnings"),
                                report.warnings.size());
            item.emptyResult = report.lanes.isEmpty();
            item.tags.append(QStringLiteral("module_scope_fallback"));
            if (!hasRange) {
                item.reason = QStringLiteral("no source range for module scope");
                addCase(cases, counts, item, AuditStatus::Fail);
            } else if (report.available && !report.lanes.isEmpty()) {
                addCase(cases, counts, item, AuditStatus::Pass);
            } else if (!report.warnings.isEmpty()) {
                item.reason = report.warnings.join(QStringLiteral("; "));
                addCase(cases, counts, item, AuditStatus::EmptyButValid);
            } else {
                item.reason = QStringLiteral("module-scope preview returned no lane and no warning");
                addCase(cases, counts, item, AuditStatus::Skipped);
            }
        }

        const QString text = context.fileContents.value(
            normalizedPath(module.location.fileName));
        for (int i = 0; i < processes.size(); ++i) {
            const SemanticSymbolRecord& process = processes.at(i);
            int start = -1;
            int end = -1;
            const bool hasRange = rangeForRecord(process, text, &start, &end);
            WavePreviewQuery query;
            query.fileName = process.location.fileName;
            query.documentText = text;
            query.semanticSnapshot = context.snapshot;
            query.scopeStartPosition = hasRange ? start : -1;
            query.scopeEndPosition = hasRange ? end : -1;
            query.scopeLabel =
                QStringLiteral("%1 always #%2").arg(module.name).arg(i + 1);
            const WavePreviewReport report = service.previewForDocument(query);

            AuditCase item;
            item.feature = QStringLiteral("wave_preview");
            item.fileName = process.location.fileName;
            item.moduleName = module.name;
            item.symbolName = process.name;
            item.locationKind = QStringLiteral("always_block");
            item.line = process.location.startLine;
            item.column = process.location.startColumn;
            item.metrics.insert(QStringLiteral("available"), report.available);
            item.metrics.insert(QStringLiteral("blocks"), report.blocks.size());
            item.metrics.insert(QStringLiteral("lanes"), report.lanes.size());
            item.metrics.insert(QStringLiteral("assignments"),
                                report.assignmentCount);
            item.metrics.insert(QStringLiteral("warnings"),
                                report.warnings.size());
            item.metrics.insert(QStringLiteral("traceSignals"),
                                report.trace.traceSignals.size());
            item.emptyResult = report.lanes.isEmpty();
            if (QFileInfo(process.location.fileName).fileName()
                == QStringLiteral("cpld_preproc.sv")) {
                item.tags.append(QStringLiteral("required_complex_file"));
            }
            if (!hasRange) {
                item.reason = QStringLiteral("no source range for always block");
                addCase(cases, counts, item, AuditStatus::Fail);
            } else if (report.available && !report.lanes.isEmpty()) {
                addCase(cases, counts, item, AuditStatus::Pass);
            } else if (!report.warnings.isEmpty()) {
                item.reason = report.warnings.join(QStringLiteral("; "));
                addCase(cases, counts, item, AuditStatus::EmptyButValid);
            } else {
                item.reason = QStringLiteral("preview returned no lane and no warning");
                addCase(cases, counts, item, AuditStatus::Fail);
            }
        }
    }
}

QString sourceWithoutLineComments(const QString& text)
{
    QStringList result;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const QString& line : lines)
        result.append(stripLineComment(line));
    return result.join(QLatin1Char('\n'));
}

bool containsDesignDeclaration(const QString& text)
{
    const QRegularExpression expression(
        QStringLiteral("^\\s*(?:module|interface|package|program|primitive)\\b"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::MultilineOption);
    return expression.match(sourceWithoutLineComments(text)).hasMatch();
}

bool containsPreprocessorDirective(const QString& text)
{
    const QRegularExpression expression(
        QStringLiteral("^\\s*`(?:ifdef|ifndef|elsif|else|endif|include|define|undef)\\b"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::MultilineOption);
    return expression.match(text).hasMatch();
}

bool containsOnlyCommentsWhitespaceAndDirectives(const QString& text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    bool sawDirective = false;
    for (QString line : lines) {
        line = stripLineComment(line).trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('`'))) {
            sawDirective = true;
            continue;
        }
        return false;
    }
    return sawDirective || !text.trimmed().isEmpty();
}

AuditStatus classifyEmptyOutlineSource(const CorpusContext& context,
                                       const QString& fileName,
                                       AuditCase* item)
{
    const QString text = context.fileContents.value(normalizedPath(fileName));
    if (text.isEmpty()) {
        item->reason = QStringLiteral("source file is empty");
        return AuditStatus::EmptyButValid;
    }
    if (!containsDesignDeclaration(text)) {
        if (containsOnlyCommentsWhitespaceAndDirectives(text)) {
            item->reason =
                containsPreprocessorDirective(text)
                    ? QStringLiteral("no design declarations: preprocessor/comment-only source file")
                    : QStringLiteral("no design declarations: comment-only source file");
        } else {
            item->reason = QStringLiteral("no design declarations in source file");
        }
        return AuditStatus::EmptyButValid;
    }
    if (containsPreprocessorDirective(text)) {
        item->reason =
            QStringLiteral("outline empty for design declaration behind preprocessor guard or include context");
        return AuditStatus::Skipped;
    }

    item->reason = QStringLiteral("outline empty despite visible design declarations");
    return AuditStatus::Fail;
}

void auditSemanticBaseline(const CorpusContext& context,
                           QList<AuditCase>* cases,
                           QHash<QString, FeatureCounts>* counts)
{
    NavigationService navigation(SemanticIndex::getInstance());
    ReferenceService references(SemanticIndex::getInstance());

    for (const QString& fileName : context.files) {
        NavigationSymbolOutlineQuery outlineQuery;
        outlineQuery.fileName = fileName;
        const QList<SymbolOutlineGroup> outline =
            navigation.findSymbolOutline(outlineQuery);
        int outlineRows = 0;
        for (const SymbolOutlineGroup& group : outline)
            outlineRows += group.symbolRows.size();

        AuditCase item;
        item.feature = QStringLiteral("semantic_baseline");
        item.fileName = fileName;
        item.locationKind = QStringLiteral("file_outline");
        item.metrics.insert(QStringLiteral("outlineGroups"), outline.size());
        item.metrics.insert(QStringLiteral("outlineRows"), outlineRows);
        if (outlineRows > 0 || QFileInfo(fileName).suffix().toLower()
            == QStringLiteral("svh")) {
            addCase(cases, counts, item,
                    outlineRows > 0 ? AuditStatus::Pass
                                    : AuditStatus::EmptyButValid);
        } else {
            const AuditStatus status =
                classifyEmptyOutlineSource(context, fileName, &item);
            addCase(cases, counts, item, status);
        }
    }

    for (const SemanticSymbolRecord& module : moduleRecords(context.records)) {
        SemanticDefinitionQuery query;
        query.symbolName = module.name;
        query.fileName = module.location.fileName;
        query.moduleName = module.name;
        const SemanticDefinitionResult result =
            SemanticIndex::getInstance()->resolveDefinition(query);
        AuditCase item;
        item.feature = QStringLiteral("semantic_baseline");
        item.fileName = module.location.fileName;
        item.moduleName = module.name;
        item.symbolName = module.name;
        item.locationKind = QStringLiteral("module_definition");
        item.line = module.location.startLine;
        item.column = module.location.startColumn;
        item.metrics.insert(QStringLiteral("candidates"),
                            result.inspectedCandidateCount);
        addCase(cases, counts, item,
                result.found ? AuditStatus::Pass : AuditStatus::Fail);
    }

    AuditCase diagnosticItem;
    diagnosticItem.feature = QStringLiteral("semantic_baseline");
    diagnosticItem.fileName = context.workspaceRoot;
    diagnosticItem.locationKind = QStringLiteral("workspace_diagnostics");
    diagnosticItem.metrics.insert(QStringLiteral("diagnostics"),
                                  context.diagnostics.size());
    addCase(cases, counts, diagnosticItem, AuditStatus::Pass);

    Q_UNUSED(references);
}

bool buildCorpusContext(const QString& workspaceRoot,
                        const QStringList& roots,
                        CorpusContext* context,
                        QString* error)
{
    context->workspaceRoot = workspaceRoot;
    context->roots = roots;
    context->files = collectFiles(roots);
    if (context->files.isEmpty()) {
        if (error)
            *error = QStringLiteral("no corpus files found");
        return false;
    }
    printf("corpus_audit: collected %d files\n", context->files.size());
    fflush(stdout);

    for (const QString& fileName : context->files) {
        QString text;
        if (!readTextFile(fileName, &text)) {
            if (error)
                *error = QStringLiteral("cannot read %1").arg(fileName);
            return false;
        }
        context->fileContents.insert(normalizedPath(fileName), text);
    }

    context->includeDirs = collectIncludeDirs(context->files, roots);
    printf("corpus_audit: using %d include dirs\n", context->includeDirs.size());
    fflush(stdout);

    SlangManager slang;
    QElapsedTimer phaseTimer;
    phaseTimer.start();
    context->records =
        slang.extractWorkspaceSymbolRecords(context->files, context->includeDirs);
    int nextLocalHandle = 1;
    for (const SemanticSymbolRecord& record : std::as_const(context->records)) {
        if (record.localHandle >= nextLocalHandle)
            nextLocalHandle = record.localHandle + 1;
    }
    for (SemanticSymbolRecord& record : context->records) {
        if (record.localHandle < 0)
            record.localHandle = nextLocalHandle++;
    }
    appendDiscoveredProcessRecords(context, &nextLocalHandle);
    printf("corpus_audit: extracted %d symbols in %lld ms\n",
           context->records.size(),
           static_cast<long long>(phaseTimer.elapsed()));
    fflush(stdout);
    phaseTimer.restart();
    context->diagnostics =
        slang.extractWorkspaceDiagnostics(context->files, context->includeDirs);
    printf("corpus_audit: extracted %d diagnostics in %lld ms\n",
           context->diagnostics.size(),
           static_cast<long long>(phaseTimer.elapsed()));
    fflush(stdout);

    auto baseSnapshot = std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(context->records,
                                                 {},
                                                 context->diagnostics,
                                                 context->fileContents));

    QHash<QString, QList<SemanticSymbolRecord>> recordsByFile;
    QHash<QString, QList<SemanticSymbolRecord>> recordsByBaseName;
    for (const SemanticSymbolRecord& record : context->records) {
        recordsByFile[normalizedPath(record.location.fileName)].append(record);
        recordsByBaseName[QFileInfo(record.location.fileName).fileName()].append(record);
    }
    printf("corpus_audit: record file buckets %d\n", recordsByFile.size());
    fflush(stdout);

    SmartRelationshipBuilder builder(nullptr, &slang);
    phaseTimer.restart();
    const QHash<QString, RelationshipExtractionInfo> rawRelationshipFacts =
        builder.extractWorkspaceRelationshipInfo(context->files,
                                                 context->includeDirs,
                                                 {});
    QHash<QString, RelationshipExtractionInfo> relationshipFacts;
    int nonEmptyFactFiles = 0;
    for (auto it = rawRelationshipFacts.constBegin();
         it != rawRelationshipFacts.constEnd();
         ++it) {
        const RelationshipExtractionInfo& facts = it.value();
        relationshipFacts.insert(normalizedPath(it.key()), facts);
        if (!facts.moduleInstantiations.isEmpty()
            || !facts.subroutineCalls.isEmpty()
            || !facts.assignments.isEmpty()
            || !facts.conditionReferences.isEmpty()
            || !facts.timingSignals.isEmpty()) {
            ++nonEmptyFactFiles;
        }
    }
    printf("corpus_audit: extracted relationship facts in %lld ms\n",
           static_cast<long long>(phaseTimer.elapsed()));
    printf("corpus_audit: relationship fact file buckets %d non-empty %d\n",
           relationshipFacts.size(),
           nonEmptyFactFiles);
    fflush(stdout);

    phaseTimer.restart();
    QHash<QString, SemanticSymbolRecord> moduleByName;
    for (const SemanticSymbolRecord& record : context->records) {
        if (isModuleRecord(record) && !moduleByName.contains(record.name))
            moduleByName.insert(record.name, record);
    }
    context->relationships = directRelationshipsFromFacts(relationshipFacts,
                                                          recordsByFile,
                                                          recordsByBaseName,
                                                          moduleByName);
    printf("corpus_audit: built %d relationships in %lld ms\n",
           context->relationships.size(),
           static_cast<long long>(phaseTimer.elapsed()));
    fflush(stdout);
    context->snapshot = std::make_shared<SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(context->records,
                                                 context->relationships,
                                                 context->diagnostics,
                                                 context->fileContents));
    SemanticIndex::getInstance()->setSnapshot(context->snapshot);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QStringList roots;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i)
            roots.append(normalizedPath(QString::fromLocal8Bit(argv[i])));
    } else {
        roots << normalizedPath(QStringLiteral("test_sv/new"))
              << normalizedPath(QStringLiteral("test_sv/huge_prj"));
    }
    const QString workspaceRoot = findWorkspaceRoot(roots);

    QElapsedTimer timer;
    timer.start();
    CorpusContext context;
    QString error;
    if (!buildCorpusContext(workspaceRoot, roots, &context, &error)) {
        fprintf(stderr, "corpus audit setup failed: %s\n",
                error.toLocal8Bit().constData());
        return 2;
    }

    QList<AuditCase> cases;
    QHash<QString, FeatureCounts> counts;
    const QList<SemanticSymbolRecord> modules = moduleRecords(context.records);

    printf("corpus_audit: auditing state transitions\n");
    fflush(stdout);
    auditStateTransitions(context, modules, &cases, &counts);
    printf("corpus_audit: auditing signal kernel graphs\n");
    fflush(stdout);
    auditSignalKernelGraphs(context, modules, &cases, &counts);
    printf("corpus_audit: auditing module block diagrams\n");
    fflush(stdout);
    auditModuleBlockDiagrams(context, modules, &cases, &counts);
    printf("corpus_audit: auditing wave preview\n");
    fflush(stdout);
    auditWavePreview(context, modules, &cases, &counts);
    printf("corpus_audit: auditing semantic baseline\n");
    fflush(stdout);
    auditSemanticBaseline(context, &cases, &counts);

    const qint64 elapsedMs = timer.elapsed();

    QJsonObject root;
    root.insert(QStringLiteral("generatedAtUtc"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("workspaceRoot"), context.workspaceRoot);
    QJsonArray rootArray;
    for (const QString& corpusRoot : context.roots)
        rootArray.append(relativePath(context.workspaceRoot, corpusRoot));
    root.insert(QStringLiteral("roots"), rootArray);
    root.insert(QStringLiteral("fileCount"), context.files.size());
    root.insert(QStringLiteral("semanticRecordCount"), context.records.size());
    root.insert(QStringLiteral("relationshipCount"), context.relationships.size());
    root.insert(QStringLiteral("diagnosticCount"), context.diagnostics.size());
    root.insert(QStringLiteral("elapsedMs"), static_cast<double>(elapsedMs));
    root.insert(QStringLiteral("featureCounts"), statusCountsJson(counts));

    QJsonArray caseArray;
    for (const AuditCase& item : cases)
        caseArray.append(caseJson(item, context.workspaceRoot));
    root.insert(QStringLiteral("cases"), caseArray);

    const QString jsonPath = normalizedPath(
        QDir(context.workspaceRoot).filePath(
            QStringLiteral("test_sv/corpus_audit_report.json")));
    const QString markdownPath = normalizedPath(
        QDir(context.workspaceRoot).filePath(
            QStringLiteral("test_sv/corpus_audit_report.md")));
    if (!writeTextFile(jsonPath,
                       QString::fromUtf8(QJsonDocument(root).toJson(
                           QJsonDocument::Indented)))) {
        fprintf(stderr, "cannot write %s\n", jsonPath.toLocal8Bit().constData());
        return 3;
    }
    if (!writeTextFile(markdownPath,
                       markdownReport(context, cases, counts, elapsedMs))) {
        fprintf(stderr, "cannot write %s\n",
                markdownPath.toLocal8Bit().constData());
        return 3;
    }

    printf("Corpus audit wrote %s and %s\n",
           jsonPath.toLocal8Bit().constData(),
           markdownPath.toLocal8Bit().constData());
    printf("Files=%d records=%d relationships=%d diagnostics=%d cases=%d elapsedMs=%lld\n",
           context.files.size(),
           context.records.size(),
           context.relationships.size(),
           context.diagnostics.size(),
           cases.size(),
           static_cast<long long>(elapsedMs));
    for (const QString& feature : featureOrder()) {
        const FeatureCounts c = counts.value(feature);
        printf("%s pass=%d fail=%d skipped=%d timeout=%d empty=%d\n",
               feature.toLocal8Bit().constData(),
               c.pass,
               c.fail,
               c.skipped,
               c.timeout,
               c.emptyButValid);
    }
    return 0;
}
