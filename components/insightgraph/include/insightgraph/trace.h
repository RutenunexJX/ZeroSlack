#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace insightgraph {
enum class TraceStatus { Ready, DefinitionOnly, Unavailable, Cancelled, Partial, Stale };
struct TraceGroupData {
    QString objectId;
    bool dataPath = false;
};
struct TracePageData {
    QString subjectId;
    int direction = 0;
    quint64 semanticRevision = 0;
    TraceStatus status = TraceStatus::Unavailable;
    QList<TraceGroupData> groups;
    QStringList limitations;
    int nextCursor = -1, scanned = 0;
};
struct TraceBranch {
    int groupIndex = -1;
    QString objectId, path;
    bool dataPath = false;
};
struct TraceProjection {
    TracePageData page;
    QList<TraceBranch> branches;
    QString statusText;
};
enum class FeedbackKind { Combinational, Sequential, Uncertain };
struct FeedbackFindingData {
    QString id;
    FeedbackKind kind = FeedbackKind::Uncertain;
    QString kindDescription;
    QStringList fromDisplayNames, limitations;
};
struct FeedbackSummary {
    QString id, label, toolTip;
    FeedbackKind kind = FeedbackKind::Uncertain;
};
QString traceStatusName(TraceStatus status);
TraceProjection projectTracePage(const TracePageData& page, const QString& parentPath);
QList<FeedbackSummary> projectFeedback(const QList<FeedbackFindingData>& findings);
}
