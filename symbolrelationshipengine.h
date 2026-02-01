#ifndef SYMBOLRELATIONSHIPENGINE_H
#define SYMBOLRELATIONSHIPENGINE_H

#include <QObject>
#include <QHash>
#include <QSet>
#include <QList>
#include <QString>
#include <QPair>
#include <QStringList>
#include <memory>

class SymbolRelationshipEngine : public QObject
{
    Q_OBJECT

public:
    enum RelationType {
        CONTAINS,
        REFERENCES,
        INSTANTIATES,
        CALLS,
        INHERITS,
        IMPLEMENTS,
        ASSIGNS_TO,
        READS_FROM,
        CLOCKS,
        RESETS,
        GENERATES,
        CONSTRAINS
    };
    Q_ENUM(RelationType)

    explicit SymbolRelationshipEngine(QObject *parent = nullptr);
    ~SymbolRelationshipEngine();

    void addRelationship(int fromSymbolId, int toSymbolId, RelationType type,
                        const QString& context = "", int confidence = 100);
    void removeRelationship(int fromSymbolId, int toSymbolId, RelationType type);
    void removeAllRelationships(int symbolId);
    void clearAllRelationships();

    QList<int> getRelatedSymbols(int symbolId, RelationType type, bool outgoing = true) const;
    QList<int> getAllRelatedSymbols(int symbolId, bool outgoing = true) const;
    bool hasRelationship(int fromSymbolId, int toSymbolId, RelationType type) const;

    QList<int> getModuleChildren(int moduleId) const;
    QList<int> getSymbolReferences(int symbolId) const;
    QList<int> getSymbolDependencies(int symbolId) const;
    QList<int> getModuleInstances(int moduleId) const;
    QList<int> getTaskCalls(int taskId) const;

    QList<int> findRelationshipPath(int fromSymbolId, int toSymbolId, int maxDepth = 3) const;
    QList<int> getInfluencedSymbols(int symbolId, int depth = 2) const;
    QList<int> getSymbolHierarchy(int rootSymbolId) const;

    /** 批量提交开始：在 endUpdate 之前不调用 invalidateCache，避免 O(N^2) */
    void beginUpdate();
    /** 批量提交结束：按需失效缓存一次 */
    void endUpdate();
    void buildFileRelationships(const QString& fileName);
    void invalidateFileRelationships(const QString& fileName);
    void rebuildAllRelationships();

    int getRelationshipCount() const;
    int getRelationshipCount(RelationType type) const;
    QStringList getRelationshipSummary() const;
    void printRelationshipGraph(int symbolId, int depth = 2) const;

    QString relationshipTypeToString(RelationType type) const;

signals:
    void relationshipAdded(int fromSymbolId, int toSymbolId, RelationType type);
    void relationshipRemoved(int fromSymbolId, int toSymbolId, RelationType type);
    void relationshipsCleared();

private slots:
    /** 供非主线程调用 addRelationship 时在主线程发射 relationshipAdded，避免排队传递 RelationType */
    void emitRelationshipAddedQueued(int fromSymbolId, int toSymbolId, int typeAsInt);

private:
    struct RelationshipEdge {
        int targetId;
        RelationType type;
        QString context;
        int confidence;

        RelationshipEdge(int target, RelationType t, const QString& ctx = "", int conf = 100)
            : targetId(target), type(t), context(ctx), confidence(conf) {}

        bool operator==(const RelationshipEdge& other) const {
            return targetId == other.targetId && type == other.type;
        }
    };

    struct RelationshipNode {
        QList<RelationshipEdge> outgoingEdges;
        QList<RelationshipEdge> incomingEdges;
    };

    QHash<int, RelationshipNode> relationshipGraph;
    QHash<RelationType, QList<QPair<int, int>>> relationshipsByType;
    QHash<QString, QSet<int>> symbolsByFile;

    mutable QHash<QPair<int, RelationType>, QList<int>> queryCache;
    mutable bool cacheValid = true;
    /** 批量提交深度，>0 时不执行 per-item 的 invalidateCache* */
    int updateDepth = 0;

    void invalidateCache();
    void invalidateCacheForRelationship(int fromId, int toId, RelationType type);
    void invalidateCacheForSymbol(int symbolId);
    void addToTypeIndex(int fromId, int toId, RelationType type);
    void removeFromTypeIndex(int fromId, int toId, RelationType type);

    void findPathRecursive(int currentId, int targetId, int currentDepth, int maxDepth,
                          QSet<int>& visited, QList<int>& currentPath,
                          QList<QList<int>>& allPaths) const;

    void getInfluencedSymbolsRecursive(int symbolId, int currentDepth, int maxDepth,
                                     QSet<int>& visited, QList<int>& result) const;
};

SymbolRelationshipEngine::RelationType stringToRelationshipType(const QString& typeStr);

// 供跨线程/队列信号槽传递 RelationType 使用（配合 main 中 qRegisterMetaType）
Q_DECLARE_METATYPE(SymbolRelationshipEngine::RelationType)

#endif // SYMBOLRELATIONSHIPENGINE_H
