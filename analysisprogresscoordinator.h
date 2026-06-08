#ifndef ANALYSISPROGRESSCOORDINATOR_H
#define ANALYSISPROGRESSCOORDINATOR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

class AnalysisScheduler;
class RelationshipProgressDialog;
class SymbolAnalyzer;
class QWidget;
struct WorkspaceRelationshipAnalysisResult;

class AnalysisProgressCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisProgressCoordinator(QWidget* dialogParent, QObject* parent = nullptr);
    ~AnalysisProgressCoordinator() override;

    void connectToScheduler(AnalysisScheduler* scheduler);
    void connectToSymbolAnalyzer(SymbolAnalyzer* analyzer);
    bool isSymbolAnalysisCancelled() const;

signals:
    void statusMessageRequested(const QString& message, int timeoutMs);
    void relationshipAnalysisErrorReported(const QString& fileName, const QString& error);

private:
    QWidget* dialogParent = nullptr;
    RelationshipProgressDialog* progressDialog = nullptr;
    AnalysisScheduler* scheduler = nullptr;
    SymbolAnalyzer* symbolAnalyzer = nullptr;
    std::atomic<bool> symbolAnalysisCancelled{false};

    void showAnalysisProgress(const QStringList& files);
    void showSymbolStageStarted(const QStringList& files);
    void showRelationshipStageStarted(const QStringList& files);
    void showRelationshipAnalysisFinished(const WorkspaceRelationshipAnalysisResult& result);
    void showRelationshipProgress(const QString& fileName, int relationshipsFound);
    void showWorkspaceRelationshipProgress(int processedFiles, int totalFiles);
    void showRelationshipError(const QString& fileName, const QString& error);
    void showRelationshipCancelled();
};

#endif // ANALYSISPROGRESSCOORDINATOR_H
