#ifndef SYMBOLANALYZER_H
#define SYMBOLANALYZER_H

#include <QObject>
#include <QStringList>
#include <QHash>
#include <QList>
#include <QVector>
#include <cstdint>
#include <functional>
#include "projectmodel.h"
#include "semanticindex.h"
#include "syminfo.h"

class WorkspaceManager;
template <typename T>
class QFutureWatcher;

struct OpenDocumentContent {
    QString fileName;
    QString content;
};

struct WorkspaceFileAnalysis {
    QString fileName;
    QString content;
    QList<SemanticSymbolRecord> symbolRecords;
};

struct WorkspaceAnalysisResult {
    QVector<WorkspaceFileAnalysis> files;
    QList<SemanticDiagnostic> diagnostics;
    QStringList protectedFiles;
    int totalSymbols = 0;
    std::uint64_t generation = 0;
};

struct FileAnalysisResult {
    QString fileName;
    QString content;
    QString contentHash;
    QList<SemanticSymbolRecord> symbolRecords;
    QList<SemanticDiagnostic> diagnostics;
    std::uint64_t generation = 0;
};

class SymbolAnalyzer : public QObject
{
    Q_OBJECT

public:
    explicit SymbolAnalyzer(QObject *parent = nullptr);
    ~SymbolAnalyzer();

    // Analysis modes
    void analyzeOpenDocuments(const QList<OpenDocumentContent>& documents);
    void analyzeWorkspace(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled = nullptr);
    void analyzeProject(const ProjectSnapshot& project, std::function<bool()> isCancelled = nullptr);
    void startAnalyzeWorkspaceAsync(WorkspaceManager* workspaceManager, std::function<bool()> isCancelled = nullptr);
    void startAnalyzeProjectAsync(const ProjectSnapshot& project, std::function<bool()> isCancelled = nullptr);
    void analyzeFile(const QString& filePath);
    void analyzeFileContent(const QString& fileName, const QString& content);
    void analyzeFileContentAsync(const QString& fileName, const QString& content);
    void setWorkspaceProtectedFiles(const QStringList& fileNames);
    void cancelWorkspaceAnalysisAndInvalidate();

    // Utility
    bool isAnalysisNeeded(const QString& fileName, const QString& content) const;
    bool hasSignificantChanges(const QString& oldContent, const QString& newContent) const;
    void invalidateCache();

signals:
    void analysisStarted(const QString& fileName);
    void analysisCompleted(const QString& fileName, int symbolsFound);
    void batchAnalysisCompleted(int filesAnalyzed, int totalSymbols);
    void batchProgress(int filesDone, int totalFiles, const QString& currentFileName);

private slots:
    void onWorkspaceAnalysisFinished();

private:
    // Analysis state tracking
    QHash<QString, QString> lastAnalyzedContent;
    QHash<QString, std::uint64_t> fileAnalysisGenerations;
    QStringList workspaceProtectedFiles;
    std::uint64_t workspaceAnalysisGeneration = 0;

    QFutureWatcher<WorkspaceAnalysisResult>* workspaceAnalysisWatcher = nullptr;

    void publishOpenDocumentResults(
        const QStringList& fileNames,
        const QList<SemanticDiagnostic>& diagnostics);
    void updateFileSymbols(
        const QString& fileName,
        const QString& content,
        const QList<SemanticSymbolRecord>& symbolRecords);
    void publishFileAnalysisResult(
        const QString& fileName,
        const QString& content,
        const QList<SemanticSymbolRecord>& symbolRecords,
        const QList<SemanticDiagnostic>& diagnostics);
    int publishWorkspaceAnalysisResult(
        const WorkspaceAnalysisResult& result,
        int totalFiles);
    QString contentHash(const QString& content) const;
    void cancelWorkspaceAnalysisAndWait();
    QStringList filterSystemVerilogFiles(const QStringList& files) const;
    bool isSystemVerilogFile(const QString &fileName) const;
};

#endif // SYMBOLANALYZER_H
