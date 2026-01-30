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
    // 🚀 符号关系类型枚举
    enum RelationType {
        CONTAINS,        // module包含变量/task/function
        REFERENCES,      // 变量引用另一个变量
        INSTANTIATES,    // module实例化另一个module
        CALLS,          // task/function调用
        INHERITS,        // interface继承
        IMPLEMENTS,      // 实现interface
        ASSIGNS_TO,      // 赋值给某个变量
        READS_FROM,      // 从某个变量读取
        CLOCKS,          // 时钟关系
        RESETS,          // 复位关系
        GENERATES,       // generate语句关系
        CONSTRAINS       // 约束关系
    };
    Q_ENUM(RelationType)

    explicit SymbolRelationshipEngine(QObject *parent = nullptr);
    ~SymbolRelationshipEngine();

    // 🚀 核心关系管理API
    void addRelationship(int fromSymbolId, int toSymbolId, RelationType type,
                        const QString& context = "", int confidence = 100);
    void removeRelationship(int fromSymbolId, int toSymbolId, RelationType type);
    void removeAllRelationships(int symbolId);
    void clearAllRelationships();

    // 🚀 基本查询API
    QList<int> getRelatedSymbols(int symbolId, RelationType type, bool outgoing = true) const;
    QList<int> getAllRelatedSymbols(int symbolId, bool outgoing = true) const;
    bool hasRelationship(int fromSymbolId, int toSymbolId, RelationType type) const;

    // 🚀 高频查询API (针对SV特化)
    QList<int> getModuleChildren(int moduleId) const;              // 获取module包含的所有符号
    QList<int> getSymbolReferences(int symbolId) const;            // 获取引用某符号的所有符号
    QList<int> getSymbolDependencies(int symbolId) const;          // 获取某符号依赖的所有符号
    QList<int> getModuleInstances(int moduleId) const;            // 获取module的所有实例
    QList<int> getTaskCalls(int taskId) const;                    // 获取调用某task的所有位置

    // 🚀 高级查询API
    QList<int> findRelationshipPath(int fromSymbolId, int toSymbolId, int maxDepth = 3) const;
    QList<int> getInfluencedSymbols(int symbolId, int depth = 2) const;
    QList<int> getSymbolHierarchy(int rootSymbolId) const;

    // 🚀 批量操作API
    void buildFileRelationships(const QString& fileName);
    void invalidateFileRelationships(const QString& fileName);
    void rebuildAllRelationships();

    // 🚀 统计和调试API
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
    // 🚀 核心数据结构：邻接表表示的关系图
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
        QList<RelationshipEdge> outgoingEdges;  // 输出边：此符号指向其他符号的关系
        QList<RelationshipEdge> incomingEdges;  // 输入边：其他符号指向此符号的关系
    };

    // 🚀 主要存储结构
    QHash<int, RelationshipNode> relationshipGraph;

    // 🚀 优化索引：按关系类型快速查询
    QHash<RelationType, QList<QPair<int, int>>> relationshipsByType;

    // 🚀 文件级索引：快速失效某个文件的所有关系
    QHash<QString, QSet<int>> symbolsByFile;

    // 🚀 缓存：避免重复计算
    mutable QHash<QPair<int, RelationType>, QList<int>> queryCache;
    mutable bool cacheValid = true;

    // 🚀 辅助方法：按影响范围失效缓存，避免全局 clear
    void invalidateCache();
    void invalidateCacheForRelationship(int fromId, int toId, RelationType type);
    void invalidateCacheForSymbol(int symbolId);
    void addToTypeIndex(int fromId, int toId, RelationType type);
    void removeFromTypeIndex(int fromId, int toId, RelationType type);

    // 🚀 递归查询辅助方法
    void findPathRecursive(int currentId, int targetId, int currentDepth, int maxDepth,
                          QSet<int>& visited, QList<int>& currentPath,
                          QList<QList<int>>& allPaths) const;

    void getInfluencedSymbolsRecursive(int symbolId, int currentDepth, int maxDepth,
                                     QSet<int>& visited, QList<int>& result) const;
};

// 🚀 全局关系类型工具函数
SymbolRelationshipEngine::RelationType stringToRelationshipType(const QString& typeStr);

// 供跨线程/队列信号槽传递 RelationType 使用（配合 main 中 qRegisterMetaType）
Q_DECLARE_METATYPE(SymbolRelationshipEngine::RelationType)

#endif // SYMBOLRELATIONSHIPENGINE_H
