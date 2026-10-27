#pragma once

#include <zeroslack/semantic/semanticsourcerange.h>
#include <QString>
#include <QStringList>

enum class SignalDependencyKind { Data, Port, Control, Clock, Reference, Coverage, Identity };
enum class SignalStorageBoundary { Combinational, EdgeTriggered, Latch, Unknown };
enum class SignalTraceRole { Write, Read, Port, Control, Clock, Unknown };

struct SignalBitRange {
    bool known = false;
    qint64 low = 0;
    qint64 high = 0;
    bool overlaps(const SignalBitRange& other) const {
        return !known || !other.known || (low <= other.high && other.low <= high);
    }
    QString key() const {
        return known ? QStringLiteral("%1:%2").arg(low).arg(high) : QStringLiteral("?");
    }
};

// Captured from one Slang elaboration and owned by the immutable relationship shard.
// Coverage facts describe an output/limitation; they are never graph edges.
struct SemanticSignalDependency {
    SignalDependencyKind kind = SignalDependencyKind::Data;
    SignalStorageBoundary boundary = SignalStorageBoundary::Unknown;
    QString fromInstance;
    QString toInstance;
    QString fromObject;
    QString toObject;
    SignalBitRange fromBits;
    SignalBitRange toBits;
    QString processId;
    QString processKind;
    SemanticSourceRange boundaryEvidence;
    QStringList limitations;
    bool complete = true;
    SignalTraceRole referenceRole = SignalTraceRole::Unknown;
    QList<SemanticSourceRange> sourceTrail;
    bool bitwiseForward = false;
};
