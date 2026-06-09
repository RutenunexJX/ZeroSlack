#ifndef ANALYSISCOMMANDCOORDINATOR_H
#define ANALYSISCOMMANDCOORDINATOR_H

#include <QObject>
#include <QString>

class AnalysisScheduler;

class AnalysisCommandCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisCommandCoordinator(AnalysisScheduler* scheduler,
                                        QObject* parent = nullptr);
    ~AnalysisCommandCoordinator() override;

    void requestSingleFileRelationshipAnalysis(const QString& fileName,
                                               const QString& content);
    void scheduleOpenFileAnalysis(const QString& fileName, int delayMs);
    void cancelScheduledOpenFileAnalysis(const QString& fileName);
    void cancelRelationshipWork();

private:
    AnalysisScheduler* scheduler = nullptr;
};

#endif // ANALYSISCOMMANDCOORDINATOR_H
