#ifndef SMARTRELATIONSHIPBUILDER_H
#define SMARTRELATIONSHIPBUILDER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QRegExp>
#include <QHash>
#include <QList>
#include "symbolrelationshipengine.h"
#include "syminfo.h"
#include <QVector>

// 用于异步分析：在后台计算关系，在主线程应用
struct RelationshipToAdd {
    int fromId;
    int toId;
    SymbolRelationshipEngine::RelationType type;
    QString context;
    int confidence;
};

class SmartRelationshipBuilder : public QObject
{
    Q_OBJECT

public:
    explicit SmartRelationshipBuilder(SymbolRelationshipEngine* engine,
                                    sym_list* symbolDatabase,
                                    QObject *parent = nullptr);
    ~SmartRelationshipBuilder();

    // 🚀 主要分析接口（主线程同步，可能阻塞）
    void analyzeFile(const QString& fileName, const QString& content);

    // 🚀 异步友好：在后台线程中调用，仅计算不写引擎；主线程用 applyRelationshipResults 写回
    QVector<RelationshipToAdd> computeRelationships(const QString& fileName, const QString& content,
                                                    const QList<sym_list::SymbolInfo>& fileSymbols);
    void analyzeFileIncremental(const QString& fileName, const QString& content,
                               const QList<int>& changedLines);

    // 🚀 特定关系类型分析
    void analyzeModuleRelationships(const QString& fileName, const QString& content);
    void analyzeVariableRelationships(const QString& fileName, const QString& content);
    void analyzeTaskFunctionRelationships(const QString& fileName, const QString& content);
    void analyzeAssignmentRelationships(const QString& fileName, const QString& content);
    void analyzeInstantiationRelationships(const QString& fileName, const QString& content);

    // 🚀 配置选项
    void setAnalysisDepth(int depth) { analysisDepth = depth; }
    void setEnableAdvancedAnalysis(bool enable) { enableAdvancedAnalysis = enable; }
    void setConfidenceThreshold(int threshold) { confidenceThreshold = threshold; }

    void cancelAnalysis();
    bool isCancelled() const { return cancelled; }

    // 🚀 批量分析方法（支持取消）
    void analyzeMultipleFiles(const QStringList& fileNames,
                             const QHash<QString, QString>& fileContents);


signals:
    void analysisCompleted(const QString& fileName, int relationshipsFound);
    void analysisError(const QString& fileName, const QString& error);
    void analysisCancelled();

private:
    SymbolRelationshipEngine* relationshipEngine;
    sym_list* symbolDatabase;

    // 🚀 分析配置
    int analysisDepth = 3;              // 分析深度
    bool enableAdvancedAnalysis = true;  // 启用高级分析
    int confidenceThreshold = 50;       // 置信度阈值

    std::atomic<bool> cancelled{false};  // 线程安全的取消标志
    bool checkCancellation(const QString& currentFile = "");

    // 🚀 缓存的正则表达式
    struct AnalysisPatterns {
        QRegExp moduleInstantiation;     // module实例化模式
        QRegExp variableAssignment;      // 变量赋值模式
        QRegExp variableReference;       // 变量引用模式
        QRegExp taskCall;               // task调用模式
        QRegExp functionCall;           // function调用模式
        QRegExp alwaysBlock;            // always块模式
        QRegExp generateBlock;          // generate块模式
    };
    AnalysisPatterns patterns;

    // 🚀 分析上下文
    struct AnalysisContext {
        QString currentFileName;
        QString currentModuleName;
        int currentModuleId = -1;
        QHash<QString, int> localSymbolIds;  // 当前文件的符号名到ID映射
        QList<sym_list::SymbolInfo> fileSymbols;
        QHash<int, sym_list::sym_type_e> symbolIdToType;  // 用于 computeRelationships 中不访问 DB
    };

    // 🚀 初始化方法
    void initializePatterns();
    void setupAnalysisContext(const QString& fileName, AnalysisContext& context);
    void setupAnalysisContextFromSymbols(const QString& fileName,
                                         const QList<sym_list::SymbolInfo>& fileSymbols,
                                         AnalysisContext& context);

    // 🚀 核心分析方法
    void analyzeModuleInstantiations(const QString& content, AnalysisContext& context);
    void analyzeVariableAssignments(const QString& content, AnalysisContext& context);
    void analyzeVariableReferences(const QString& content, AnalysisContext& context);
    void analyzeTaskFunctionCalls(const QString& content, AnalysisContext& context);
    void analyzeAlwaysBlocks(const QString& content, AnalysisContext& context);
    void analyzeGenerateBlocks(const QString& content, AnalysisContext& context);

    // 🚀 辅助分析方法
    QStringList extractVariablesFromExpression(const QString& expression);
    int findSymbolIdByName(const QString& symbolName, const AnalysisContext& context);
    QString findContainingModule(int lineNumber, const AnalysisContext& context);
    bool isInCommentOrString(int position, const QString& content);
    int calculateConfidence(const QString& pattern, const QString& match);

    // 🚀 关系建立方法（当 collectResults 非空时只收集不写引擎）
    QVector<RelationshipToAdd>* collectResults = nullptr;
    void addRelationshipWithContext(int fromId, int toId,
                                  SymbolRelationshipEngine::RelationType type,
                                  const QString& context, int confidence = 100);

    // 🚀 特殊分析：SystemVerilog高级特性
    void analyzeInterfaceRelationships(const QString& content, AnalysisContext& context);
    void analyzeParameterRelationships(const QString& content, AnalysisContext& context);
    void analyzeConstraintRelationships(const QString& content, AnalysisContext& context);
    void analyzeClockResetRelationships(const QString& content, AnalysisContext& context);
};

#endif // SMARTRELATIONSHIPBUILDER_H
