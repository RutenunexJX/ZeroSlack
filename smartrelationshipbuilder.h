#ifndef SMARTRELATIONSHIPBUILDER_H
#define SMARTRELATIONSHIPBUILDER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QHash>
#include <QList>
#include "symbolrelationshipengine.h"
#include "syminfo.h"
#include <QVector>
#include <QSet>

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

    void analyzeFile(const QString& fileName, const QString& content);

    QVector<RelationshipToAdd> computeRelationships(const QString& fileName, const QString& content,
                                                    const QList<sym_list::SymbolInfo>& fileSymbols);
    void analyzeFileIncremental(const QString& fileName, const QString& content,
                               const QList<int>& changedLines);

    void analyzeModuleRelationships(const QString& fileName, const QString& content);
    void analyzeVariableRelationships(const QString& fileName, const QString& content);
    void analyzeTaskFunctionRelationships(const QString& fileName, const QString& content);
    void analyzeAssignmentRelationships(const QString& fileName, const QString& content);
    void analyzeInstantiationRelationships(const QString& fileName, const QString& content);

    void setAnalysisDepth(int depth) { analysisDepth = depth; }
    void setEnableAdvancedAnalysis(bool enable) { enableAdvancedAnalysis = enable; }
    void setConfidenceThreshold(int threshold) { confidenceThreshold = threshold; }

    void cancelAnalysis();
    bool isCancelled() const { return cancelled; }

    void analyzeMultipleFiles(const QStringList& fileNames,
                             const QHash<QString, QString>& fileContents);


signals:
    void analysisCompleted(const QString& fileName, int relationshipsFound);
    void analysisError(const QString& fileName, const QString& error);
    void analysisCancelled();

private:
    SymbolRelationshipEngine* relationshipEngine;
    sym_list* symbolDatabase;

    int analysisDepth = 3;
    bool enableAdvancedAnalysis = true;
    int confidenceThreshold = 50;

    std::atomic<bool> cancelled{false};
    bool checkCancellation(const QString& currentFile = "");

    struct AnalysisPatterns {
        QRegularExpression moduleInstantiation;
        QRegularExpression variableAssignment;
        QRegularExpression variableReference;
        QRegularExpression taskCall;
        QRegularExpression functionCall;
        QRegularExpression alwaysBlock;
        QRegularExpression generateBlock;
    };
    AnalysisPatterns patterns;

    struct AnalysisContext {
        QString currentFileName;
        QString currentModuleName;
        int currentModuleId = -1;
        QHash<QString, int> localSymbolIds;
        QList<sym_list::SymbolInfo> fileSymbols;
        /** 用于 computeRelationships 中不访问 DB */
        QHash<int, sym_list::sym_type_e> symbolIdToType;
    };

    void initializePatterns();
    void setupAnalysisContext(const QString& fileName, AnalysisContext& context);
    void setupAnalysisContextFromSymbols(const QString& fileName,
                                         const QList<sym_list::SymbolInfo>& fileSymbols,
                                         AnalysisContext& context);

    void analyzeModuleInstantiations(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
    void analyzeVariableAssignments(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
    void analyzeVariableReferences(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
    void analyzeTaskFunctionCalls(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
    void analyzeAlwaysBlocks(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
    void analyzeGenerateBlocks(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);

    QStringList extractVariablesFromExpression(const QString& expression);
    int findSymbolIdByName(const QString& symbolName, const AnalysisContext& context);
    QString findContainingModule(int lineNumber, const AnalysisContext& context);
    int getContainingModuleId(int lineNumber, const AnalysisContext& context);
    QSet<int> getAffectedSymbolIds(const QString& content, const QList<int>& changedLines, AnalysisContext& context);
    bool isInCommentOrString(int position, const QString& content);
    int calculateConfidence(const QString& pattern, const QString& match);

    QVector<RelationshipToAdd>* collectResults = nullptr;
    void addRelationshipWithContext(int fromId, int toId,
                                  SymbolRelationshipEngine::RelationType type,
                                  const QString& context, int confidence = 100);

    void analyzeParameterRelationships(const QString& content, AnalysisContext& context);
    void analyzeConstraintRelationships(const QString& content, AnalysisContext& context);
    void analyzeClockResetRelationships(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
};

#endif // SMARTRELATIONSHIPBUILDER_H
