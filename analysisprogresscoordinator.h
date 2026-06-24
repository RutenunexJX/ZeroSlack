#ifndef ANALYSISPROGRESSCOORDINATOR_H
#define ANALYSISPROGRESSCOORDINATOR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

class AnalysisScheduler;
class QWidget;
struct WorkspaceAnalysisRequestTelemetry;
struct WorkspaceRelationshipAnalysisResult;

class AnalysisProgressCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisProgressCoordinator(QWidget* dialogParent, QObject* parent = nullptr);
    ~AnalysisProgressCoordinator() override;

    void connectToScheduler(AnalysisScheduler* scheduler);
    void handleWorkspaceSymbolProgress(int filesDone,
                                       int totalFiles,
                                       const QString& currentFileName);
    void handleWorkspaceAnalysisRequestQueued(
        const WorkspaceAnalysisRequestTelemetry& telemetry);
    void handleWorkspaceAnalysisRequestResolved(
        const WorkspaceAnalysisRequestTelemetry& telemetry);
    bool isSymbolAnalysisCancelled() const;

signals:
    void statusMessageRequested(const QString& message, int timeoutMs);
    void relationshipAnalysisErrorReported(const QString& fileName, const QString& error);

private:
    QWidget* dialogParent = nullptr;
    AnalysisScheduler* scheduler = nullptr;
    std::atomic<bool> symbolAnalysisCancelled{false};
    int lastSymbolProgressCheckpoint = 0;
    int lastRelationshipProgressCheckpoint = 0;

    void showAnalysisProgress(const QStringList& files);
    void showSymbolStageStarted(const QStringList& files);
    void showRelationshipStageStarted(const QStringList& files);
    void showRelationshipAnalysisFinished(const WorkspaceRelationshipAnalysisResult& result);
    void showRelationshipProgress(const QString& fileName, int relationshipsFound);
    void showWorkspaceRelationshipProgress(int processedFiles, int totalFiles);
    void showRelationshipError(const QString& fileName, const QString& error);
    void showRelationshipCancelled();
    void showWorkspaceRelationshipCancelled();
    void logProgressCheckpoint(const QString& label,
                               int processedFiles,
                               int totalFiles,
                               const QString& currentFileName,
                               int* lastCheckpoint);
};

#endif // ANALYSISPROGRESSCOORDINATOR_H
