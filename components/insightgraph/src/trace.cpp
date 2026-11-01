#include <insightgraph/trace.h>

namespace insightgraph {
QString traceStatusName(TraceStatus status)
{
    switch (status) {
    case TraceStatus::Ready: return QStringLiteral("Ready");
    case TraceStatus::DefinitionOnly: return QStringLiteral("Definition only — choose an instance to follow dependencies");
    case TraceStatus::Partial: return QStringLiteral("Partial");
    case TraceStatus::Cancelled: return QStringLiteral("Cancelled");
    case TraceStatus::Stale: return QStringLiteral("Stale — refresh or select the signal again");
    default: return QStringLiteral("Unavailable");
    }
}
TraceProjection projectTracePage(const TracePageData& page, const QString& parentPath)
{
    TraceProjection result;
    result.page = page;
    result.statusText = page.status == TraceStatus::Ready ? QString() : traceStatusName(page.status);
    for (int i = 0; i < page.groups.size(); ++i) {
        const auto& group = page.groups[i];
        result.branches.append({i, group.objectId,
            parentPath + QLatin1Char('/') + group.objectId
                + (group.dataPath ? "/data" : "/related"), group.dataPath});
    }
    return result;
}
QList<FeedbackSummary> projectFeedback(const QList<FeedbackFindingData>& findings)
{
    QList<FeedbackSummary> result;
    for (const auto& finding : findings) {
        QStringList names;
        for (const auto& name : finding.fromDisplayNames)
            if (!names.contains(name)) names << name;
        const QString kind = finding.kind == FeedbackKind::Combinational ? QStringLiteral("Combinational")
            : finding.kind == FeedbackKind::Sequential ? QStringLiteral("Sequential") : QStringLiteral("Uncertain");
        result.append({finding.id, kind + QStringLiteral(" — ") + names.join(QStringLiteral(" → ")),
            finding.kindDescription + QStringLiteral("\n") + finding.limitations.join('\n'), finding.kind});
    }
    return result;
}
}
