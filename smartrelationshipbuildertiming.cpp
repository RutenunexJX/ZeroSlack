#include "smartrelationshipbuilder.h"

#include <utility>

namespace {
bool isClockSignalName(const QString& signalName)
{
    const QString lower = signalName.toLower();
    return lower.contains(QLatin1String("clk")) || lower.contains(QLatin1String("clock"));
}

bool isResetSignalName(const QString& signalName)
{
    const QString lower = signalName.toLower();
    return lower == QLatin1String("rst")
        || lower == QLatin1String("reset")
        || lower == QLatin1String("rstn")
        || lower == QLatin1String("rst_n")
        || lower.contains(QLatin1String("reset"))
        || lower.contains(QLatin1String("_rst"));
}
}

void SmartRelationshipBuilder::analyzeAlwaysBlocks(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    for (const TimingSignalInfo& signal : std::as_const(context.relationshipInfo.timingSignals)) {
        if (lineMin >= 0 && (signal.lineNumber - 1 < lineMin || signal.lineNumber - 1 > lineMax))
            continue;

        int signalId = findSymbolIdByName(signal.signalName, context);
        int ownerModuleId = getContainingModuleId(signal.lineNumber, context);
        if (ownerModuleId == -1)
            ownerModuleId = context.currentModuleId;
        if (signalId != -1 && ownerModuleId != -1) {
            addRelationshipWithContext(
                ownerModuleId,
                signalId,
                SymbolRelationshipEngine::READS_FROM,
                QString("Timing sensitivity at line %1").arg(signal.lineNumber),
                80
            );
        }
    }
}

void SmartRelationshipBuilder::analyzeClockResetRelationships(const QString& content, AnalysisContext& context, int lineMin, int lineMax)
{
    if (!m_slangManager)
        return;

    ensureRelationshipInfo(content, context);
    for (const TimingSignalInfo& signal : std::as_const(context.relationshipInfo.timingSignals)) {
        if (lineMin >= 0 && (signal.lineNumber - 1 < lineMin || signal.lineNumber - 1 > lineMax))
            continue;

        int ownerModuleId = getContainingModuleId(signal.lineNumber, context);
        if (ownerModuleId == -1)
            ownerModuleId = context.currentModuleId;
        if (ownerModuleId == -1)
            continue;

        if (signal.edgeSensitive && isClockSignalName(signal.signalName)) {
            int clockId = findSymbolIdByName(signal.signalName, context);
            if (clockId != -1) {
                addRelationshipWithContext(
                    clockId,
                    ownerModuleId,
                    SymbolRelationshipEngine::CLOCKS,
                    QString("Clock domain at line %1").arg(signal.lineNumber),
                    95
                );
            }
        }

        if (isResetSignalName(signal.signalName)) {
            int resetId = findSymbolIdByName(signal.signalName, context);
            if (resetId != -1) {
                addRelationshipWithContext(
                    resetId,
                    ownerModuleId,
                    SymbolRelationshipEngine::RESETS,
                    QString("Reset signal at line %1").arg(signal.lineNumber),
                    90
                );
            }
        }
    }
}
