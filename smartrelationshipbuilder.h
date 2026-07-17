#ifndef SMARTRELATIONSHIPBUILDER_H
#define SMARTRELATIONSHIPBUILDER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QList>
#include <functional>
#include "slangmanager.h"
#include "symbolrelationshipengine.h"
#include <QVector>

class SemanticIndexSnapshot;
struct SemanticSymbolRecord;

struct RelationshipToAdd {
    int fromId;
    int toId;
    SymbolRelationshipEngine::RelationType type;
    QString context;
    int confidence;
    SemanticSourceRange evidenceRange;
    QString fromAccessPath;
    QString toAccessPath;
};

class SmartRelationshipBuilder : public QObject
{
    Q_OBJECT

public:
    using SymbolRecordProvider =
        std::function<QList<SemanticSymbolRecord>(const QString& fileName)>;

    explicit SmartRelationshipBuilder(SymbolRelationshipEngine* engine,
                                    SlangManager* slangManager,
                                    SymbolRecordProvider symbolRecordProvider = {},
                                    QObject *parent = nullptr);
    ~SmartRelationshipBuilder();

    QVector<RelationshipToAdd> computeRelationships(
        const QString& fileName,
        const QString& content,
        const QList<SemanticSymbolRecord>& fileSymbolRecords,
        const SemanticIndexSnapshot* snapshot);
    QVector<RelationshipToAdd> computeRelationships(
        const QString& fileName,
        const QString& content,
        const QList<SemanticSymbolRecord>& fileSymbolRecords,
        const SemanticIndexSnapshot* snapshot,
        const QStringList& includeDirs,
        const QHash<QString, QString>& defines,
        const RelationshipExtractionInfo* precomputedRelationshipInfo = nullptr);
    QHash<QString, RelationshipExtractionInfo> extractWorkspaceRelationshipInfo(
        const QStringList& filePaths,
        const QStringList& includeDirs,
        const QHash<QString, QString>& defines) const;

    void cancelAnalysis();
    void resetCancellation();
    bool isCancelled() const { return cancelled; }

signals:
    void analysisError(const QString& fileName, const QString& error);
    void analysisCancelled();

private:
    SymbolRelationshipEngine* relationshipEngine;
    SlangManager* m_slangManager = nullptr;
    SymbolRecordProvider m_symbolRecordProvider;

    bool enableAdvancedAnalysis = true;
    int confidenceThreshold = 50;

    std::atomic<bool> cancelled{false};
    bool checkCancellation(const QString& currentFile = "");

    struct AnalysisContext {
        QString currentFileName;
        QString currentModuleName;
        int currentModuleLocalHandle = -1;
        QHash<QString, int> localSymbolHandles;
        QHash<QString, QList<SemanticSymbolRecord>> recordsByName;
        QHash<int, SemanticSymbolRecord> recordsByLocalHandle;
        mutable QHash<QString, SemanticSymbolRecord> symbolRecordLookupCache;
        mutable QHash<int, int> containingModuleHandleByLine;
        QList<SemanticSymbolRecord> moduleRecords;
        QList<SemanticSymbolRecord> fileSymbolRecords;
        RelationshipExtractionInfo relationshipInfo;
        bool relationshipInfoLoaded = false;
        bool textualAssignmentFallbackLoaded = false;
        const SemanticIndexSnapshot* snapshot = nullptr;
        QStringList includeDirs;
        QHash<QString, QString> defines;
    };

    void setupAnalysisContextFromRecords(
        const QString& fileName,
        const QList<SemanticSymbolRecord>& fileSymbolRecords,
        const SemanticIndexSnapshot* snapshot,
        AnalysisContext& context);
    void ensureRelationshipInfo(const QString& content, AnalysisContext& context);

    void analyzeModuleInstantiations(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
    void analyzeVariableAssignments(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
    void analyzeVariableReferences(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
    void analyzeTaskFunctionCalls(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
    void analyzeAlwaysBlocks(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);

    SemanticSymbolRecord findSymbolRecordByName(
        const QString& symbolName,
        const AnalysisContext& context,
        int lineNumber = -1);
    int findSymbolLocalHandleByName(const QString& symbolName,
                                    const AnalysisContext& context,
                                    int lineNumber = -1);
    QString findContainingModule(int lineNumber, const AnalysisContext& context);
    int getContainingModuleLocalHandle(int lineNumber,
                                       const AnalysisContext& context);

    QVector<RelationshipToAdd>* collectResults = nullptr;
    void addRelationshipWithContext(int fromHandle, int toHandle,
                                  SymbolRelationshipEngine::RelationType type,
                                  const QString& context, int confidence = 100,
                                  const SemanticSourceRange& evidenceRange = {},
                                  const QString& fromAccessPath = {},
                                  const QString& toAccessPath = {});

    void analyzeParameterRelationships(const QString& content, AnalysisContext& context);
    void analyzeConstraintRelationships(const QString& content, AnalysisContext& context);
    void analyzeClockResetRelationships(const QString& content, AnalysisContext& context, int lineMin = -1, int lineMax = -1);
};

#endif // SMARTRELATIONSHIPBUILDER_H
