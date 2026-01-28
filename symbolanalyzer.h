#ifndef SYMBOLANALYZER_H
#define SYMBOLANALYZER_H

#include <QObject>
#include <QTimer>
#include <QStringList>
#include "syminfo.h"

class MyCodeEditor;
class TabManager;
class WorkspaceManager;

class SymbolAnalyzer : public QObject
{
    Q_OBJECT

public:
    explicit SymbolAnalyzer(QObject *parent = nullptr);
    ~SymbolAnalyzer();

    // Analysis modes
    void analyzeOpenTabs(TabManager* tabManager);
    void analyzeWorkspace(WorkspaceManager* workspaceManager);
    void analyzeFile(const QString& filePath);
    void analyzeEditor(MyCodeEditor* editor, bool incremental = false);

    // Analysis control
    void scheduleIncrementalAnalysis(MyCodeEditor* editor, int delay = 1000);
    void scheduleSignificantAnalysis(MyCodeEditor* editor, int delay = 2000);
    void cancelScheduledAnalysis(MyCodeEditor* editor);

    // Utility
    bool isAnalysisNeeded(const QString& fileName, const QString& content) const;
    void invalidateCache();

signals:
    void analysisStarted(const QString& fileName);
    void analysisCompleted(const QString& fileName, int symbolsFound);
    void batchAnalysisCompleted(int filesAnalyzed, int totalSymbols);

private slots:
    void onIncrementalAnalysisTimer();
    void onSignificantAnalysisTimer();

private:
    // Timers for delayed analysis
    QHash<MyCodeEditor*, QTimer*> incrementalTimers;
    QHash<MyCodeEditor*, QTimer*> significantTimers;

    // Analysis state tracking
    QHash<QString, QString> lastAnalyzedContent;

    // Helper methods
    std::unique_ptr<MyCodeEditor> createBackgroundEditor(const QString& filePath);
    QStringList filterSystemVerilogFiles(const QStringList& files) const;
    void cleanupTimer(MyCodeEditor* editor);
    bool hasSignificantChanges(const QString& oldContent, const QString& newContent) const;
    bool isSystemVerilogFile(const QString &fileName) const;
};

#endif // SYMBOLANALYZER_H
