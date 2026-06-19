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

        int signalHandle = findSymbolLocalHandleByName(signal.signalName, context);
        int ownerModuleHandle =
            getContainingModuleLocalHandle(signal.lineNumber, context);
        if (ownerModuleHandle == -1)
            ownerModuleHandle = context.currentModuleLocalHandle;
        if (signalHandle != -1 && ownerModuleHandle != -1) {
            addRelationshipWithContext(
                ownerModuleHandle,
                signalHandle,
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

        int ownerModuleHandle =
            getContainingModuleLocalHandle(signal.lineNumber, context);
        if (ownerModuleHandle == -1)
            ownerModuleHandle = context.currentModuleLocalHandle;
        if (ownerModuleHandle == -1)
            continue;

        if (signal.edgeSensitive && isClockSignalName(signal.signalName)) {
            int clockHandle = findSymbolLocalHandleByName(signal.signalName, context);
            if (clockHandle != -1) {
                addRelationshipWithContext(
                    clockHandle,
                    ownerModuleHandle,
                    SymbolRelationshipEngine::CLOCKS,
                    QString("Clock domain at line %1").arg(signal.lineNumber),
                    95
                );
            }
        }

        if (isResetSignalName(signal.signalName)) {
            int resetHandle = findSymbolLocalHandleByName(signal.signalName, context);
            if (resetHandle != -1) {
                addRelationshipWithContext(
                    resetHandle,
                    ownerModuleHandle,
                    SymbolRelationshipEngine::RESETS,
                    QString("Reset signal at line %1").arg(signal.lineNumber),
                    90
                );
            }
        }
    }
}
