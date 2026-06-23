#ifndef WAVEPREVIEWSERVICE_H
#define WAVEPREVIEWSERVICE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <memory>

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

struct WavePreviewAssignment {
    WavePreviewAssignmentKind kind = WavePreviewAssignmentKind::Blocking;
    QString target;
    QString expression;
    QStringList sourceSignals;
    QString trigger;
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

struct WavePreviewBlock {
    WavePreviewBlockKind kind = WavePreviewBlockKind::Unknown;
    QString trigger;
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

struct WavePreviewLane {
    QString signalName;
    QList<WavePreviewAssignment> assignments;

    bool isValid() const
    {
        return !signalName.isEmpty() && !assignments.isEmpty();
    }
};

struct WavePreviewQuery {
    QString fileName;
    QString documentText;
};

struct WavePreviewReport {
    QList<WavePreviewBlock> blocks;
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
