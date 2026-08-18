#ifndef WAVESIMULATIONCOORDINATOR_H
#define WAVESIMULATIONCOORDINATOR_H

#include "wavesimulationconfiguration.h"
#include "wavesimulationpreparationservice.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QPointer>
#include <atomic>
#include <memory>

class QProcess;
template <typename T>
class QFutureWatcher;

enum class WaveSimulationStage {
    Idle,
    Preparing,
    ImportingTarget,
    ExportingStimulus,
    Running,
    OpeningResult,
    Complete,
    Failed,
    Cancelled
};

struct WaveSimulationRunRequest {
    WaveSimulationPreparationRequest preparation;
    WaveSimulationToolPaths tools;
};

class ZEROSLACK_API WaveSimulationCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit WaveSimulationCoordinator(QObject* parent = nullptr);
    ~WaveSimulationCoordinator() override;

    bool start(const WaveSimulationRunRequest& request,
               QString* failureReason = nullptr);
    void cancel();
    bool isRunning() const;
    WaveSimulationStage stage() const;

signals:
    void stageChanged(WaveSimulationStage stage,
                      const QString& message);
    void finished(bool success,
                  const QString& resultProjectPath,
                  const QString& message);

private:
    static constexpr qsizetype kMaximumProcessOutput = 512 * 1024;

    WaveSimulationRunRequest currentRequest;
    WaveSimulationPreparationResult prepared;
    QFutureWatcher<WaveSimulationPreparationResult>* watcher = nullptr;
    QPointer<QProcess> process;
    std::shared_ptr<std::atomic_bool> cancellation;
    QByteArray standardOutput;
    QByteArray standardError;
    WaveSimulationStage currentStage = WaveSimulationStage::Idle;
    bool active = false;
    bool processTerminalHandled = false;

    void setStage(WaveSimulationStage next,
                  const QString& message);
    void handlePreparationFinished();
    void startProcess(WaveSimulationStage stage,
                      const QString& program,
                      const QStringList& arguments);
    void handleProcessFinished(int exitCode,
                               int exitStatus);
    void handleProcessError();
    void startNextStage();
    void openResultWindow();
    void fail(const QString& message);
    void finishCancelled();
    void clearProcess();
    void appendBounded(QByteArray* destination,
                       const QByteArray& content);
};

#endif // WAVESIMULATIONCOORDINATOR_H
