#ifndef INSIGHTGRAPHCORE_H
#define INSIGHTGRAPHCORE_H

#include "zeroslackexport.h"

#include <QHash>
#include <QList>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>
#include <QVariantMap>

struct SymbolStableKey;
struct SignalKernelGraphReport;
struct ModuleBlockDiagramReport;
struct SignalUsageHotspotReport;
struct StateTransitionGraphReport;
struct FsmGraphReport;

enum class InsightGraphDomain : quint8 {
    Ast,
    Symbol,
    Hierarchy,
    Connection,
    DataFlow,
    Fsm,
    Hotspot
};

struct ZEROSLACK_API InsightGraphNode {
    QString nodeId;
    QString displayName;
    QString detail;
    QString sourceFile;
    int sourceLine = 0;
    InsightGraphDomain domain = InsightGraphDomain::Symbol;
    int heatScore = 0;
    QVariantMap attributes;

    bool isValid() const;
};

struct ZEROSLACK_API InsightGraphEdge {
    QString edgeId;
    QString fromNodeId;
    QString toNodeId;
    QString displayName;
    InsightGraphDomain domain = InsightGraphDomain::Connection;
    int weight = 1;
    QVariantMap attributes;

    bool isValid() const;
};

struct ZEROSLACK_API InsightGraphFact {
    InsightGraphDomain domain = InsightGraphDomain::Symbol;
    QString nodeId;
    QString relatedNodeId;
    QString symbolName;
    QString detail;
    QString sourceFile;
    QString ownerScope;
    QString relation;
    int sourceLine = 0;
    int weight = 1;
    QVariantMap attributes;
};

struct ZEROSLACK_API InsightGraphStatistics {
    int astCount = 0;
    int symbolCount = 0;
    int hierarchyCount = 0;
    int connectionCount = 0;
    int dataFlowCount = 0;
    int fsmCount = 0;
    int hotspotCount = 0;
    QHash<QString, int> hotspotScores;
};

struct ZEROSLACK_API InsightGraphDraft {
    QString workspaceId;
    QString documentId;
    quint64 documentRevision = 0;
    quint64 semanticRevision = 0;
    QString contextKey;
    QString layoutHint;
    QList<InsightGraphFact> facts;
    QList<InsightGraphNode> nodes;
    QList<InsightGraphEdge> edges;
    QVariantMap metadata;
};

struct ZEROSLACK_API InsightGraphSnapshot {
    QString channelId;
    QString workspaceId;
    QString documentId;
    quint64 documentRevision = 0;
    quint64 semanticRevision = 0;
    quint64 generation = 0;
    QString contextKey;
    QString layoutHint;
    QHash<QString, InsightGraphNode> nodes;
    QHash<QString, InsightGraphEdge> edges;
    InsightGraphStatistics statistics;
    QVariantMap metadata;
    QByteArray contentFingerprint;

    bool isValid() const;
    bool isEmpty() const;
};

struct ZEROSLACK_API InsightGraphDiff {
    QStringList addedNodeIds;
    QStringList removedNodeIds;
    QStringList updatedNodeIds;
    QStringList addedEdgeIds;
    QStringList removedEdgeIds;
    QStringList updatedEdgeIds;
    bool contextChanged = false;
    bool layoutInvalidated = false;
    bool metadataChanged = false;

    bool isEmpty() const;
    bool topologyChanged() const;
};

struct ZEROSLACK_API InsightGraphUpdate {
    InsightGraphSnapshot previousSnapshot;
    InsightGraphSnapshot snapshot;
    InsightGraphDiff diff;
};

class ZEROSLACK_API InsightGraphCore
{
public:
    InsightGraphCore() = default;

    static QString stableSymbolId(const SymbolStableKey& key);
    static QString stableSymbolId(const QString& sourceFile,
                                  const QString& ownerScope,
                                  int declarationKind,
                                  const QString& symbolName);
    static QString stableSyntheticId(const QString& domain,
                                     const QString& context,
                                     const QString& identity);
    static QString stableEdgeId(const QString& fromNodeId,
                                const QString& toNodeId,
                                const QString& relation,
                                const QString& discriminator = {});

    InsightGraphUpdate update(const QString& channelId,
                              const InsightGraphDraft& draft);
    InsightGraphSnapshot snapshot(const QString& channelId) const;
    void clear(const QString& channelId);
    void clear();

    static InsightGraphDiff diff(const InsightGraphSnapshot& previous,
                                 const InsightGraphSnapshot& current);

    static InsightGraphDraft fromSignalKernelGraph(
        const SignalKernelGraphReport& report);
    static InsightGraphDraft fromModuleBlockDiagram(
        const ModuleBlockDiagramReport& report);
    static InsightGraphDraft fromSignalUsageHotspot(
        const SignalUsageHotspotReport& report);
    static InsightGraphDraft fromStateTransitionGraph(
        const StateTransitionGraphReport& report);
    static InsightGraphDraft fromFsmGraph(const FsmGraphReport& report);

private:
    mutable QReadWriteLock lock;
    QHash<QString, InsightGraphSnapshot> snapshots;
    QHash<QString, quint64> generationCounters;

    static InsightGraphSnapshot buildSnapshot(
        const QString& channelId,
        const InsightGraphDraft& draft,
        quint64 generation);
};

ZEROSLACK_API bool operator==(const InsightGraphNode& lhs,
                              const InsightGraphNode& rhs);
ZEROSLACK_API bool operator!=(const InsightGraphNode& lhs,
                              const InsightGraphNode& rhs);
ZEROSLACK_API bool operator==(const InsightGraphEdge& lhs,
                              const InsightGraphEdge& rhs);
ZEROSLACK_API bool operator!=(const InsightGraphEdge& lhs,
                              const InsightGraphEdge& rhs);

#endif // INSIGHTGRAPHCORE_H
