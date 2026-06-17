#include "smartrelationshipbuilder.h"
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

        int moduleTypeId = findSymbolIdByName(info.moduleName, context);
        int ownerModuleId = getContainingModuleId(info.lineNumber, context);
        if (ownerModuleId == -1)
            ownerModuleId = context.currentModuleId;
        if (moduleTypeId != -1 && ownerModuleId != -1) {
            addRelationshipWithContext(
                ownerModuleId,
                moduleTypeId,
                SymbolRelationshipEngine::INSTANTIATES,
                QString("Instance: %1 at line %2").arg(info.instanceName).arg(info.lineNumber),
                90
            );
            emitted.insert(QStringLiteral("%1:%2").arg(ownerModuleId).arg(moduleTypeId));
        }
    }

    // Workspace-wide Slang symbol extraction can resolve cross-file instances even when
    // the single-file relationship parse treats the module type as unknown.
    for (const sym_list::SymbolInfo& symbol : std::as_const(context.fileSymbols)) {
        if (!SymbolTaxonomy::isInstanceDeclaration(symbol)
            || symbol.dataType.isEmpty()) {
            continue;
        }
        if (lineMin >= 0 && (symbol.startLine - 1 < lineMin || symbol.startLine - 1 > lineMax))
            continue;

        int moduleTypeId = findSymbolIdByName(symbol.dataType, context);
        int ownerModuleId = getContainingModuleId(symbol.startLine, context);
        if (ownerModuleId == -1)
            ownerModuleId = context.currentModuleId;
        if (moduleTypeId != -1 && ownerModuleId != -1) {
            const QString key = QStringLiteral("%1:%2").arg(ownerModuleId).arg(moduleTypeId);
            if (emitted.contains(key))
                continue;

            addRelationshipWithContext(
                ownerModuleId,
                moduleTypeId,
                SymbolRelationshipEngine::INSTANTIATES,
                QString("Instance: %1 at line %2").arg(symbol.symbolName).arg(symbol.startLine),
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

        int leftVarId = findSymbolIdByName(assignment.leftName, context);
        if (leftVarId == -1)
            continue;

        for (const QString& rightVar : assignment.rightNames) {
            int rightVarId = findSymbolIdByName(rightVar, context);
            if (rightVarId != -1 && rightVarId != leftVarId) {
                addRelationshipWithContext(
                    leftVarId,
                    rightVarId,
                    SymbolRelationshipEngine::REFERENCES,
                    QString("Assignment at line %1").arg(assignment.lineNumber),
                    85
                );

                addRelationshipWithContext(
                    rightVarId,
                    leftVarId,
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
            int varId = findSymbolIdByName(varName, context);
            int ownerModuleId = getContainingModuleId(ref.lineNumber, context);
            if (ownerModuleId == -1)
                ownerModuleId = context.currentModuleId;
            if (varId != -1 && ownerModuleId != -1) {
                addRelationshipWithContext(
                    ownerModuleId,
                    varId,
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

        int taskId = findSymbolIdByName(call.subroutineName, context);
        if (taskId == -1)
            continue;

        sym_list::sym_type_e taskType = sym_list::sym_user;
        if (context.symbolIdToType.contains(taskId))
            taskType = context.symbolIdToType[taskId];
        else {
            if (context.snapshot)
                taskType = context.snapshot->getSymbolById(taskId).symbolType;
            else if (symbolDatabase)
                taskType = symbolDatabase->getSymbolById(taskId).symbolType;
        }

        if (!SymbolTaxonomy::isSubroutineDeclaration(taskType))
            continue;

        int ownerModuleId = getContainingModuleId(call.lineNumber, context);
        if (ownerModuleId == -1)
            ownerModuleId = context.currentModuleId;
        if (ownerModuleId != -1) {
            addRelationshipWithContext(
                ownerModuleId,
                taskId,
                SymbolRelationshipEngine::CALLS,
                QString("Called at line %1").arg(call.lineNumber),
                95
            );
        }
    }
}

int SmartRelationshipBuilder::findSymbolIdByName(const QString& symbolName, const AnalysisContext& context)
{
    if (context.localSymbolIds.contains(symbolName)) {
        return context.localSymbolIds[symbolName];
    }

    if (context.snapshot) {
        const int snapshotId = context.snapshot->findSymbolId(symbolName);
        if (snapshotId >= 0)
            return snapshotId;
    }

    if (!symbolDatabase)
        return -1;

    int id = symbolDatabase->findSymbolIdByName(symbolName);
    if (id >= 0)
        return id;

    return -1;
}

void SmartRelationshipBuilder::addRelationshipWithContext(int fromId, int toId,
                                                        SymbolRelationshipEngine::RelationType type,
                                                        const QString& context, int confidence)
{
    if (confidence < confidenceThreshold)
        return;
    if (collectResults) {
        collectResults->append({fromId, toId, type, context, confidence});
        return;
    }
    if (relationshipEngine)
        relationshipEngine->addRelationship(fromId, toId, type, context, confidence);
}

int SmartRelationshipBuilder::getContainingModuleId(int lineNumber, const AnalysisContext& context)
{
    int foundId = -1;
    int foundStart = -1;
    for (const sym_list::SymbolInfo& s : context.fileSymbols) {
        if (SymbolTaxonomy::isModuleDeclaration(s)
            && s.startLine <= lineNumber
            && s.endLine >= lineNumber
            && (foundId < 0 || s.startLine > foundStart)) {
            foundId = s.symbolId;
            foundStart = s.startLine;
        }
    }
    return foundId;
}

QString SmartRelationshipBuilder::findContainingModule(int lineNumber, const AnalysisContext& context)
{
    int id = getContainingModuleId(lineNumber, context);
    if (id < 0)
        return QString();
    for (const sym_list::SymbolInfo& s : context.fileSymbols) {
        if (s.symbolId == id)
            return s.symbolName;
    }
    return QString();
}

QSet<int> SmartRelationshipBuilder::getAffectedSymbolIds(const QString& content, const QList<int>& changedLines, AnalysisContext& context)
{
    QSet<int> affectedIds;
    if (changedLines.isEmpty())
        return affectedIds;

    QStringList lines = content.split('\n');
    int numLines = lines.size();
    int minChanged = *std::min_element(changedLines.begin(), changedLines.end());
    int maxChanged = *std::max_element(changedLines.begin(), changedLines.end());
    int minLine = qMax(0, minChanged - 2);
    int maxLine = qMin(numLines - 1, maxChanged + 2);

    for (const sym_list::SymbolInfo& s : context.fileSymbols) {
        if (s.startLine >= minLine && s.startLine <= maxLine)
            affectedIds.insert(s.symbolId);
    }
    for (int lineNum : changedLines) {
        int mid = getContainingModuleId(lineNum, context);
        if (mid >= 0)
            affectedIds.insert(mid);
    }
    return affectedIds;
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
