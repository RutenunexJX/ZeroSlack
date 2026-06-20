#include "smartrelationshipbuilder.h"
#include "semanticcollectoradapter.h"
#include "semanticindexsnapshot.h"
#include "symboltaxonomy.h"

#include <algorithm>

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
                90
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
                90
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
            findSymbolLocalHandleByName(assignment.leftName, context);
        if (leftVarHandle == -1)
            continue;

        for (const QString& rightVar : assignment.rightNames) {
            int rightVarHandle = findSymbolLocalHandleByName(rightVar, context);
            if (rightVarHandle != -1 && rightVarHandle != leftVarHandle) {
                addRelationshipWithContext(
                    leftVarHandle,
                    rightVarHandle,
                    SymbolRelationshipEngine::REFERENCES,
                    QString("Assignment at line %1").arg(assignment.lineNumber),
                    85
                );

                addRelationshipWithContext(
                    rightVarHandle,
                    leftVarHandle,
                    SymbolRelationshipEngine::ASSIGNS_TO,
                    QString("Assigned to %1 at line %2").arg(assignment.leftName).arg(assignment.lineNumber),
                    85
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

        for (const QString& varName : ref.symbolNames) {
            int varHandle = findSymbolLocalHandleByName(varName, context);
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
                    70
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
            findSymbolRecordByName(call.subroutineName, context);
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
                95
            );
        }
    }
}

SemanticSymbolRecord SmartRelationshipBuilder::findSymbolRecordByName(
    const QString& symbolName,
    const AnalysisContext& context)
{
    if (context.localSymbolHandles.contains(symbolName)) {
        const int localHandle = context.localSymbolHandles.value(symbolName);
        for (const SemanticSymbolRecord& record
             : std::as_const(context.fileSymbolRecords)) {
            if (record.localHandle == localHandle)
                return record;
        }

        SemanticSymbolRecord record;
        record.localHandle = localHandle;
        record.name = symbolName;
        return record;
    }

    if (context.snapshot) {
        const QList<SemanticSymbolRecord> definitions =
            context.snapshot->findDefinitionRecords(symbolName);
        if (!definitions.isEmpty()) {
            return definitions.first();
        }
    }

    if (symbolDatabase) {
        const QList<SemanticSymbolRecord> records =
            semanticSymbolRecordsForDatabaseByName(
                symbolDatabase,
                symbolName);
        if (!records.isEmpty())
            return records.first();
    }

    SemanticSymbolRecord missing;
    missing.localHandle = -1;
    return missing;
}

int SmartRelationshipBuilder::findSymbolLocalHandleByName(
    const QString& symbolName,
    const AnalysisContext& context)
{
    return findSymbolRecordByName(symbolName, context).localHandle;
}

void SmartRelationshipBuilder::addRelationshipWithContext(int fromHandle, int toHandle,
                                                        SymbolRelationshipEngine::RelationType type,
                                                        const QString& context, int confidence)
{
    if (confidence < confidenceThreshold)
        return;
    if (collectResults) {
        collectResults->append({fromHandle, toHandle, type, context, confidence});
        return;
    }
    if (relationshipEngine)
        relationshipEngine->addRelationship(fromHandle, toHandle, type, context, confidence);
}

int SmartRelationshipBuilder::getContainingModuleLocalHandle(
    int lineNumber,
    const AnalysisContext& context)
{
    int foundHandle = -1;
    int foundStart = -1;
    for (const SemanticSymbolRecord& record
         : std::as_const(context.fileSymbolRecords)) {
        if (SymbolTaxonomy::isModuleDeclaration(
                semanticMetadataForSymbolRecord(record))
            && record.location.startLine <= lineNumber
            && record.location.endLine >= lineNumber
            && (foundHandle < 0 || record.location.startLine > foundStart)) {
            foundHandle = record.localHandle;
            foundStart = record.location.startLine;
        }
    }
    return foundHandle;
}

QString SmartRelationshipBuilder::findContainingModule(int lineNumber, const AnalysisContext& context)
{
    int localHandle = getContainingModuleLocalHandle(lineNumber, context);
    if (localHandle < 0)
        return QString();
    for (const SemanticSymbolRecord& record
         : std::as_const(context.fileSymbolRecords)) {
        if (record.localHandle == localHandle)
            return record.name;
    }
    return QString();
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
