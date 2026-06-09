#ifndef SYMBOLANALYZER_H
#define SYMBOLANALYZER_H

#include <QObject>
#include <QStringList>
#include <QFutureWatcher>
#include <QList>
#include <QVector>
#include <functional>
#include "projectmodel.h"
#include "semanticindex.h"
#include "syminfo.h"

class SlangManager;
class WorkspaceManager;

struct OpenDocumentContent {
    QString fileName;
    QString content;
};

struct WorkspaceFileAnalysis {
    QString fileName;
    QString content;
    QList<sym_list::SymbolInfo> symbols;
};

struct WorkspaceAnalysisResult {
    QVector<WorkspaceFileAnalysis> files;
    QList<SemanticDiagnostic> diagnostics;
    int totalSymbols = 0;
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

    SlangManager* m_slangManager = nullptr;
    QFutureWatcher<WorkspaceAnalysisResult>* workspaceAnalysisWatcher = nullptr;

    QStringList filterSystemVerilogFiles(const QStringList& files) const;
    bool isSystemVerilogFile(const QString &fileName) const;
};

#endif // SYMBOLANALYZER_H
