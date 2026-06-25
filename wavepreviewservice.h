#ifndef WAVEPREVIEWSERVICE_H
#define WAVEPREVIEWSERVICE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <memory>

class SemanticIndexSnapshot;

enum class WavePreviewBlockKind {
    Unknown,
    AlwaysComb,
    AlwaysFf,
    AlwaysLatch,
    AlwaysClocked,
    AlwaysLevel
};

enum class WavePreviewAssignmentKind {
    Continuous,
    Blocking,
    NonBlocking
};

struct WavePreviewEdgeSignal {
    QString signalName;
    QString edge;

    bool isValid() const
    {
        return !signalName.isEmpty();
    }

    QString label() const
    {
        return edge.isEmpty()
            ? signalName
            : edge + QStringLiteral(" ") + signalName;
    }
};

struct WavePreviewAssignment {
    WavePreviewAssignmentKind kind = WavePreviewAssignmentKind::Blocking;
    QString target;
    QString expression;
    QStringList sourceSignals;
    QString trigger;
    QString guardText;
    int blockIndex = -1;
    int cycleOffset = 0;
    int line = 0;
    int column = 0;
    int startPosition = -1;
    int endPosition = -1;

    bool isValid() const
    {
        return !target.isEmpty() && startPosition >= 0 && endPosition >= startPosition;
    }
};

struct WavePreviewSignalContext {
    QString signalName;
    QString direction;
    QString typeText;
    QString declarationText;
    int line = 0;
    int column = 0;

    bool isValid() const
    {
        return !signalName.isEmpty();
    }

    QString label() const
    {
        QStringList parts;
        if (!direction.isEmpty())
            parts.append(direction);
        if (!typeText.isEmpty())
            parts.append(typeText);
        return parts.isEmpty()
            ? QStringLiteral("-")
            : parts.join(QStringLiteral(" "));
    }
};

struct WavePreviewLaneSummary {
    int eventCount = 0;
    int sourceSignalCount = 0;
    int blockCount = 0;
    int maxCycleOffset = 0;
    int continuousEventCount = 0;
    int combinationalEventCount = 0;
    int sequentialEventCount = 0;
    QStringList guardTexts;
    bool hasContinuousEvent = false;
    bool hasCombinationalEvent = false;
    bool hasSequentialEvent = false;

    bool isValid() const
    {
        return eventCount > 0;
    }
};

struct WavePreviewBlock {
    WavePreviewBlockKind kind = WavePreviewBlockKind::Unknown;
    QString trigger;
    QStringList clockSignals;
    QStringList resetSignals;
    QList<WavePreviewEdgeSignal> clockEdgeSignals;
    QList<WavePreviewEdgeSignal> resetEdgeSignals;
    int startLine = 0;
    int endLine = 0;
    int startPosition = -1;
    int endPosition = -1;
    int assignmentCount = 0;

    bool isClocked() const
    {
        return kind == WavePreviewBlockKind::AlwaysFf
            || kind == WavePreviewBlockKind::AlwaysClocked;
    }
};

struct WavePreviewClockResetGroup {
    QStringList clockSignals;
    QStringList resetSignals;
    QList<WavePreviewEdgeSignal> clockEdgeSignals;
    QList<WavePreviewEdgeSignal> resetEdgeSignals;
    QList<int> blockIndexes;
    int assignmentCount = 0;

    bool isValid() const
    {
        return !clockSignals.isEmpty() || !resetSignals.isEmpty();
    }
};

struct WavePreviewLane {
    QString signalName;
    WavePreviewSignalContext context;
    QList<WavePreviewAssignment> assignments;
    WavePreviewLaneSummary summary;

    bool isValid() const
    {
        return !signalName.isEmpty() && !assignments.isEmpty();
    }
};

struct WavePreviewQuery {
    QString fileName;
    QString documentText;
    std::shared_ptr<const SemanticIndexSnapshot> semanticSnapshot;
};

struct WavePreviewReport {
    QList<WavePreviewBlock> blocks;
    QList<WavePreviewClockResetGroup> clockResetGroups;
    QList<WavePreviewSignalContext> signalContexts;
    QList<WavePreviewLane> lanes;
    QStringList warnings;
    int assignmentCount = 0;
    bool available = false;
};

class WavePreviewService
{
public:
    static WavePreviewService* getInstance();

    WavePreviewReport previewForDocument(const WavePreviewQuery& query) const;

private:
    static std::unique_ptr<WavePreviewService> instance;
};

#endif // WAVEPREVIEWSERVICE_H
