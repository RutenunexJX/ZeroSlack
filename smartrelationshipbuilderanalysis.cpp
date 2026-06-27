#include "smartrelationshipbuilder.h"
#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"

#include <algorithm>

namespace {
QString rootNameForAccessPath(const QString& accessPath)
{
    const int dotIndex = accessPath.indexOf(QLatin1Char('.'));
    return dotIndex < 0 ? accessPath : accessPath.left(dotIndex);
}

QStringList accessPathsOrNames(const QStringList& accessPaths,
                               const QStringList& names)
{
    if (!accessPaths.isEmpty())
        return accessPaths;
    return names;
}
}

void SmartRelationshipBuilder::analyzeModuleInstantiations(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    QSet<QString> emitted;
    for (const ModuleInstantiationInfo& info : std::as_const(context.relationshipInfo.moduleInstantiations)) {
        // lineMin/lineMax are 0-based; info.lineNumber is 1-based
        if (lineMin >= 0 && (info.lineNumber - 1 < lineMin || info.lineNumber - 1 > lineMax))
            continue;

        int moduleTypeHandle = findSymbolLocalHandleByName(info.moduleName, context);
        int ownerModuleHandle =
            getContainingModuleLocalHandle(info.lineNumber, context);
        if (ownerModuleHandle == -1)
            ownerModuleHandle = context.currentModuleLocalHandle;
        if (moduleTypeHandle != -1 && ownerModuleHandle != -1) {
            addRelationshipWithContext(
                ownerModuleHandle,
                moduleTypeHandle,
                SymbolRelationshipEngine::INSTANTIATES,
                QString("Instance: %1 at line %2").arg(info.instanceName).arg(info.lineNumber),
                90,
                info.sourceRange
            );
            emitted.insert(QStringLiteral("%1:%2").arg(ownerModuleHandle).arg(moduleTypeHandle));
        }
    }

    // Workspace-wide Slang symbol extraction can resolve cross-file instances even when
    // the single-file relationship parse treats the module type as unknown.
    for (const SemanticSymbolRecord& record : std::as_const(context.fileSymbolRecords)) {
        if (!SymbolTaxonomy::isInstanceDeclaration(
                semanticMetadataForSymbolRecord(record))
            || record.type.rawTypeText.isEmpty()) {
            continue;
        }
        if (lineMin >= 0
            && (record.location.startLine - 1 < lineMin
                || record.location.startLine - 1 > lineMax))
            continue;

        int moduleTypeHandle =
            findSymbolLocalHandleByName(record.type.rawTypeText, context);
        int ownerModuleHandle =
            getContainingModuleLocalHandle(record.location.startLine, context);
        if (ownerModuleHandle == -1)
            ownerModuleHandle = context.currentModuleLocalHandle;
        if (moduleTypeHandle != -1 && ownerModuleHandle != -1) {
            const QString key =
                QStringLiteral("%1:%2").arg(ownerModuleHandle).arg(moduleTypeHandle);
            if (emitted.contains(key))
                continue;

            addRelationshipWithContext(
                ownerModuleHandle,
                moduleTypeHandle,
                SymbolRelationshipEngine::INSTANTIATES,
                QString("Instance: %1 at line %2")
                    .arg(record.name)
                    .arg(record.location.startLine),
                90,
                SemanticSourceRange{record.location.fileName,
                                    record.location.startLine,
                                    record.location.startColumn,
                                    record.location.endLine,
                                    record.location.endColumn}
            );
            emitted.insert(key);
        }
    }
}

void SmartRelationshipBuilder::analyzeVariableAssignments(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    for (const AssignmentInfo& assignment : std::as_const(context.relationshipInfo.assignments)) {
        if (lineMin >= 0 && (assignment.lineNumber - 1 < lineMin || assignment.lineNumber - 1 > lineMax))
            continue;

        int leftVarHandle =
            findSymbolLocalHandleByName(assignment.leftName,
                                        context,
                                        assignment.lineNumber);
        if (leftVarHandle == -1)
            continue;

        const QStringList rightAccessPaths =
            accessPathsOrNames(assignment.rightAccessPaths,
                               assignment.rightNames);
        for (const QString& rightAccessPath : rightAccessPaths) {
            const QString rightVar = rootNameForAccessPath(rightAccessPath);
            int rightVarHandle = findSymbolLocalHandleByName(rightVar,
                                                             context,
                                                             assignment.lineNumber);
            if (rightVarHandle != -1 && rightVarHandle != leftVarHandle) {
                addRelationshipWithContext(
                    leftVarHandle,
                    rightVarHandle,
                    SymbolRelationshipEngine::REFERENCES,
                    QString("Assignment at line %1").arg(assignment.lineNumber),
                    85,
                    assignment.sourceRange,
                    assignment.leftAccessPath,
                    rightAccessPath
                );

                addRelationshipWithContext(
                    rightVarHandle,
                    leftVarHandle,
                    SymbolRelationshipEngine::ASSIGNS_TO,
                    QString("Assigned to %1 at line %2")
                        .arg(assignment.leftAccessPath.isEmpty()
                                 ? assignment.leftName
                                 : assignment.leftAccessPath)
                        .arg(assignment.lineNumber),
                    85,
                    assignment.sourceRange,
                    rightAccessPath,
                    assignment.leftAccessPath
                );
            }
        }
    }
}

void SmartRelationshipBuilder::analyzeVariableReferences(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    for (const ConditionReferenceInfo& ref : std::as_const(context.relationshipInfo.conditionReferences)) {
        if (lineMin >= 0 && (ref.lineNumber - 1 < lineMin || ref.lineNumber - 1 > lineMax))
            continue;

        const QStringList referenceAccessPaths =
            accessPathsOrNames(ref.symbolAccessPaths, ref.symbolNames);
        for (const QString& referenceAccessPath : referenceAccessPaths) {
            const QString varName = rootNameForAccessPath(referenceAccessPath);
            int varHandle = findSymbolLocalHandleByName(varName,
                                                        context,
                                                        ref.lineNumber);
            int ownerModuleHandle =
                getContainingModuleLocalHandle(ref.lineNumber, context);
            if (ownerModuleHandle == -1)
                ownerModuleHandle = context.currentModuleLocalHandle;
            if (varHandle != -1 && ownerModuleHandle != -1) {
                addRelationshipWithContext(
                    ownerModuleHandle,
                    varHandle,
                    SymbolRelationshipEngine::READS_FROM,
                    QString("Condition check at line %1").arg(ref.lineNumber),
                    70,
                    ref.sourceRange,
                    QString(),
                    referenceAccessPath
                );
            }
        }
    }
}

void SmartRelationshipBuilder::analyzeTaskFunctionCalls(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    for (const SubroutineCallInfo& call : std::as_const(context.relationshipInfo.subroutineCalls)) {
        if (lineMin >= 0 && (call.lineNumber - 1 < lineMin || call.lineNumber - 1 > lineMax))
            continue;

        const SemanticSymbolRecord taskRecord =
            findSymbolRecordByName(call.subroutineName,
                                   context,
                                   call.lineNumber);
        const int taskHandle = taskRecord.localHandle;
        if (taskHandle == -1)
            continue;

        if (!SymbolTaxonomy::isSubroutineDeclaration(
                semanticMetadataForSymbolRecord(taskRecord)))
            continue;

        int ownerModuleHandle =
            getContainingModuleLocalHandle(call.lineNumber, context);
        if (ownerModuleHandle == -1)
            ownerModuleHandle = context.currentModuleLocalHandle;
        if (ownerModuleHandle != -1) {
            addRelationshipWithContext(
                ownerModuleHandle,
                taskHandle,
                SymbolRelationshipEngine::CALLS,
                QString("Called at line %1").arg(call.lineNumber),
                95,
                call.sourceRange
            );
        }
    }
}

SemanticSymbolRecord SmartRelationshipBuilder::findSymbolRecordByName(
    const QString& symbolName,
    const AnalysisContext& context,
    int lineNumber)
{
    const QString cacheKey =
        QStringLiteral("%1:%2").arg(symbolName).arg(lineNumber);
    const auto cacheIt = context.symbolRecordLookupCache.constFind(cacheKey);
    if (cacheIt != context.symbolRecordLookupCache.constEnd())
        return cacheIt.value();

    auto cacheAndReturn = [&context, &cacheKey](
                              const SemanticSymbolRecord& record) {
        context.symbolRecordLookupCache.insert(cacheKey, record);
        return record;
    };

    QList<SemanticSymbolRecord> candidates =
        context.recordsByName.value(symbolName);
    if (!candidates.isEmpty()) {
        const QString containingModule =
            lineNumber > 0 ? findContainingModule(lineNumber, context) : QString();
        auto score = [&containingModule, &context, lineNumber](
                         const SemanticSymbolRecord& record) {
            int value = 0;
            if (!containingModule.isEmpty()
                && record.owner.name == containingModule) {
                value += 100;
            }
            if (lineNumber > 0
                && record.location.startLine <= lineNumber
                && (record.location.endLine <= 0
                    || record.location.endLine >= lineNumber)) {
                value += 50;
            }
            if (!context.currentModuleName.isEmpty()
                && record.owner.name == context.currentModuleName) {
                value += 10;
            }
            return value;
        };

        SemanticSymbolRecord best = candidates.first();
        int bestScore = score(best);
        for (int i = 1; i < candidates.size(); ++i) {
            const SemanticSymbolRecord& candidate = candidates.at(i);
            const int candidateScore = score(candidate);
            const bool better =
                candidateScore > bestScore
                || (candidateScore == bestScore
                    && candidate.location.startLine < best.location.startLine)
                || (candidateScore == bestScore
                    && candidate.location.startLine == best.location.startLine
                    && candidate.localHandle < best.localHandle);
            if (better) {
                best = candidate;
                bestScore = candidateScore;
            }
        }
        return cacheAndReturn(best);
    }

    if (context.localSymbolHandles.contains(symbolName)) {
        const int localHandle = context.localSymbolHandles.value(symbolName);
        const auto it = context.recordsByLocalHandle.constFind(localHandle);
        if (it != context.recordsByLocalHandle.constEnd())
            return cacheAndReturn(it.value());

        SemanticSymbolRecord record;
        record.localHandle = localHandle;
        record.name = symbolName;
        return cacheAndReturn(record);
    }

    if (context.snapshot) {
        const QList<SemanticSymbolRecord> definitions =
            context.snapshot->findDefinitionRecords(symbolName);
        if (!definitions.isEmpty()) {
            return cacheAndReturn(definitions.first());
        }
    }

    if (!context.snapshot && m_symbolRecordProvider) {
        const QList<SemanticSymbolRecord> records = m_symbolRecordProvider(QString());
        for (const SemanticSymbolRecord& record : records) {
            if (record.name == symbolName)
                return cacheAndReturn(record);
        }
    }

    SemanticSymbolRecord missing;
    missing.localHandle = -1;
    return cacheAndReturn(missing);
}

int SmartRelationshipBuilder::findSymbolLocalHandleByName(
    const QString& symbolName,
    const AnalysisContext& context,
    int lineNumber)
{
    return findSymbolRecordByName(symbolName, context, lineNumber).localHandle;
}

void SmartRelationshipBuilder::addRelationshipWithContext(int fromHandle, int toHandle,
                                                        SymbolRelationshipEngine::RelationType type,
                                                        const QString& context,
                                                        int confidence,
                                                        const SemanticSourceRange& evidenceRange,
                                                        const QString& fromAccessPath,
                                                        const QString& toAccessPath)
{
    if (confidence < confidenceThreshold)
        return;
    if (collectResults) {
        collectResults->append(
            {fromHandle,
             toHandle,
             type,
             context,
             confidence,
             evidenceRange,
             fromAccessPath,
             toAccessPath});
        return;
    }
    if (relationshipEngine)
        relationshipEngine->addRelationship(
            fromHandle, toHandle, type, context, confidence, evidenceRange);
}

int SmartRelationshipBuilder::getContainingModuleLocalHandle(
    int lineNumber,
    const AnalysisContext& context)
{
    const auto cached = context.containingModuleHandleByLine.constFind(lineNumber);
    if (cached != context.containingModuleHandleByLine.constEnd())
        return cached.value();

    int foundHandle = -1;
    int foundStart = -1;
    for (const SemanticSymbolRecord& record
         : std::as_const(context.moduleRecords)) {
        if (record.location.startLine <= lineNumber
            && record.location.endLine >= lineNumber
            && (foundHandle < 0 || record.location.startLine > foundStart)) {
            foundHandle = record.localHandle;
            foundStart = record.location.startLine;
        }
    }
    context.containingModuleHandleByLine.insert(lineNumber, foundHandle);
    return foundHandle;
}

QString SmartRelationshipBuilder::findContainingModule(int lineNumber, const AnalysisContext& context)
{
    int localHandle = getContainingModuleLocalHandle(lineNumber, context);
    if (localHandle < 0)
        return QString();
    const auto it = context.recordsByLocalHandle.constFind(localHandle);
    return it == context.recordsByLocalHandle.constEnd()
        ? QString()
        : it.value().name;
}

QSet<int> SmartRelationshipBuilder::getAffectedSymbolLocalHandles(
    const QString& content,
    const QList<int>& changedLines,
    AnalysisContext& context)
{
    QSet<int> affectedHandles;
    if (changedLines.isEmpty())
        return affectedHandles;

    QStringList lines = content.split('\n');
    int numLines = lines.size();
    int minChanged = *std::min_element(changedLines.begin(), changedLines.end());
    int maxChanged = *std::max_element(changedLines.begin(), changedLines.end());
    int minLine = qMax(0, minChanged - 2);
    int maxLine = qMin(numLines - 1, maxChanged + 2);

    for (const SemanticSymbolRecord& record
         : std::as_const(context.fileSymbolRecords)) {
        if (record.location.startLine >= minLine
            && record.location.startLine <= maxLine) {
            affectedHandles.insert(record.localHandle);
        }
    }
    for (int lineNum : changedLines) {
        int moduleHandle = getContainingModuleLocalHandle(lineNum, context);
        if (moduleHandle >= 0)
            affectedHandles.insert(moduleHandle);
    }
    return affectedHandles;
}

void SmartRelationshipBuilder::analyzeParameterRelationships(const QString& content, AnalysisContext& context)
{
    Q_UNUSED(content)
    Q_UNUSED(context)
}

void SmartRelationshipBuilder::analyzeConstraintRelationships(const QString& content, AnalysisContext& context)
{
    Q_UNUSED(content)
    Q_UNUSED(context)
}
