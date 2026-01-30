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

    // 🚀 核心控制方法
    void startAnalysis(int totalFiles);
    /** 阶段1（符号分析）时更新统计，避免显示“已分析: 0个文件”造成误解 */
    void setSymbolAnalysisProgress(int filesDone, int totalFiles);
    void updateProgress(const QString& fileName, int relationshipsFound);
    void finishAnalysis();
    void showError(const QString& fileName, const QString& error);

    // 🚀 配置选项
    void setShowDetails(bool show);
    void setAutoClose(bool autoClose);
    void setMinimumDuration(int msecs);

    void forceShow();  // 强制显示对话框
    bool isDialogVisible() const { return isVisible(); }
    void debugState() const;  // 调试状态信息

    void logProgress(const QString& message);
    QLabel* statusLabel;
    QLabel* currentFileLabel;
    QProgressBar* progressBar;

    // 🚀 配置选项
    struct Config {
        bool showDetails = false;
        bool autoClose = true;
        int minimumDuration = 2000;  // 2秒
        int autoCloseDelay = 3000;   // 3秒后自动关闭
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
    //void updateElapsedTime();
    void updateEstimatedTime();

private:
    // 🚀 UI组件
    QVBoxLayout* mainLayout;
    QHBoxLayout* buttonLayout;
    QHBoxLayout* progressLayout;

    // 🚀 进度显示
    QLabel* speedLabel;          // 分析速度

    // 🚀 时间显示
    QLabel* timeLabel;
    QLabel* estimatedLabel;      // 预估剩余时间

    // 🚀 统计显示
    QLabel* statsLabel;
    QLabel* fileStatsLabel;      // 文件大小统计

    // 🚀 详细信息区域
    QGroupBox* detailsGroup;
    QTextEdit* detailsText;      // 详细日志
    QPushButton* detailsButton;  // 显示/隐藏详情按钮
    QSplitter* splitter;

    // 🚀 控制按钮
    QPushButton* cancelButton;
    QPushButton* pauseButton;    // 暂停/继续按钮

    // 🚀 计时器
    QTimer* timeUpdateTimer;
    QTimer* estimationTimer;
    QElapsedTimer elapsedTimer;

    // 🚀 状态数据
    struct AnalysisState {
        int totalFiles = 0;
        int processedFiles = 0;
        int totalRelationships = 0;
        int totalErrors = 0;
        qint64 totalFileSize = 0;    // 已处理文件大小
        qint64 processedFileSize = 0;
        bool cancelled = false;
        bool paused = false;
        bool finished = false;

        // 性能统计
        QList<qint64> processingTimes;  // 每个文件的处理时间
        QList<int> fileSizes;           // 文件大小列表
        QList<int> relationshipCounts;  // 每个文件的关系数
    } state;



    // 🚀 内部方法
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
