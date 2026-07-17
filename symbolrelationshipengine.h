#ifndef SYMBOLRELATIONSHIPENGINE_H
#define SYMBOLRELATIONSHIPENGINE_H

#include <QObject>
#include <QHash>
#include <QSet>
#include <QList>
#include <QString>
#include <QPair>
#include <QStringList>
#include <functional>
#include <memory>

#include "semanticsourcerange.h"

struct SemanticSymbolRecord;

class SymbolRelationshipEngine : public QObject
{
    Q_OBJECT

public:
    using SymbolRecordProvider =
        std::function<QList<SemanticSymbolRecord>(const QString& fileName)>;

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

    struct RelationshipEdgeMetadata {
        bool found = false;
        QString context;
        int confidence = 0;
        SemanticSourceRange evidenceRange;
    };

    explicit SymbolRelationshipEngine(QObject *parent = nullptr);
    explicit SymbolRelationshipEngine(
        SymbolRecordProvider symbolRecordProvider,
        QObject *parent = nullptr);
    ~SymbolRelationshipEngine();

    void setSymbolRecordProvider(SymbolRecordProvider symbolRecordProvider);

    void addRelationship(int fromSymbolId, int toSymbolId, RelationType type,
                        const QString& context = "", int confidence = 100,
                        const SemanticSourceRange& evidenceRange = {});
    void removeAllRelationships(int symbolId);
    void clearAllRelationships();

    QList<int> getRelatedSymbols(int symbolId, RelationType type, bool outgoing = true) const;
    RelationshipEdgeMetadata getRelationshipMetadata(
        int fromSymbolId,
        int toSymbolId,
        RelationType type) const;
    bool hasRelationship(int fromSymbolId, int toSymbolId, RelationType type) const;

    void beginUpdate();
    void endUpdate();
    void buildFileRelationships(const QString& fileName);
    void invalidateFileRelationships(const QString& fileName);
    void rebuildAllRelationships();

    int getRelationshipCount() const;

signals:
    void relationshipAdded(int fromSymbolId, int toSymbolId, RelationType type);
    void relationshipsCleared();

private slots:
    void emitRelationshipAddedQueued(int fromSymbolId, int toSymbolId, int typeAsInt);

private:
    struct RelationshipEdge {
        int targetId;
        RelationType type;
        QString context;
        int confidence;
        SemanticSourceRange evidenceRange;

        RelationshipEdge(int target,
                         RelationType t,
                         const QString& ctx = "",
                         int conf = 100,
                         const SemanticSourceRange& range = {})
            : targetId(target),
              type(t),
              context(ctx),
              confidence(conf),
              evidenceRange(range) {}

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
    SymbolRecordProvider m_symbolRecordProvider;

    mutable QHash<QString, QList<int>> queryCache;
    mutable bool cacheValid = true;
    int updateDepth = 0;

    void invalidateCache();
    void invalidateCacheForRelationship(int fromId, int toId, RelationType type);
    void invalidateCacheForSymbol(int symbolId);
    void addToTypeIndex(int fromId, int toId, RelationType type);
    void removeFromTypeIndex(int fromId, int toId, RelationType type);
    QList<SemanticSymbolRecord> symbolRecords(const QString& fileName = QString()) const;
};

Q_DECLARE_METATYPE(SymbolRelationshipEngine::RelationType)

#endif // SYMBOLRELATIONSHIPENGINE_H
