#ifndef RELATIONSHIPPROGRESSDIALOG_H
#define RELATIONSHIPPROGRESSDIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QTextEdit>
#include <QSplitter>
#include <QGroupBox>

class RelationshipProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RelationshipProgressDialog(QWidget *parent = nullptr);
    ~RelationshipProgressDialog();

    void startAnalysis(int totalFiles);
    /** 阶段1（符号分析）时更新统计，避免显示“已分析: 0个文件”造成误解 */
    void setSymbolAnalysisProgress(int filesDone, int totalFiles);
    void updateProgress(const QString& fileName, int relationshipsFound);
    void finishAnalysis();
    void showError(const QString& fileName, const QString& error);

    void setShowDetails(bool show);
    void setAutoClose(bool autoClose);
    void setMinimumDuration(int msecs);

    void forceShow();
    bool isDialogVisible() const { return isVisible(); }
    void debugState() const;

    void logProgress(const QString& message);
    QLabel* statusLabel;
    QLabel* currentFileLabel;
    QProgressBar* progressBar;

    struct Config {
        bool showDetails = false;
        bool autoClose = true;
        int minimumDuration = 2000;
        int autoCloseDelay = 3000;
        bool showSpeed = true;
        bool showEstimation = true;
    } config;

public slots:
    void onCancelClicked();
    void onDetailsToggled(bool show);

signals:
    void cancelled();
    void finished();

private slots:
    void updateEstimatedTime();

private:
    QVBoxLayout* mainLayout;
    QHBoxLayout* buttonLayout;
    QHBoxLayout* progressLayout;

    QLabel* speedLabel;
    QLabel* timeLabel;
    QLabel* estimatedLabel;
    QLabel* statsLabel;
    QLabel* fileStatsLabel;
    QGroupBox* detailsGroup;
    QTextEdit* detailsText;
    QPushButton* detailsButton;
    QSplitter* splitter;

    QPushButton* cancelButton;
    QPushButton* pauseButton;

    QTimer* timeUpdateTimer;
    QTimer* estimationTimer;
    QElapsedTimer elapsedTimer;

    struct AnalysisState {
        int totalFiles = 0;
        int processedFiles = 0;
        int totalRelationships = 0;
        int totalErrors = 0;
        qint64 totalFileSize = 0;
        qint64 processedFileSize = 0;
        bool cancelled = false;
        bool paused = false;
        bool finished = false;

        QList<qint64> processingTimes;
        QList<int> fileSizes;
        QList<int> relationshipCounts;
    } state;

    void setupUI();
    void setupConnections();
    void updateStatistics();
    void calculateSpeed();
    void estimateRemainingTime();
    void resizeToFitContent();
    QString formatTime(qint64 seconds);
    QString formatFileSize(qint64 bytes);
    QString formatSpeed(double filesPerSecond);
};

#endif // RELATIONSHIPPROGRESSDIALOG_H
