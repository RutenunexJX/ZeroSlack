#include "relationshipprogressdialog.h"
#include <QApplication>
#include <QFileInfo>
#include <QTime>

RelationshipProgressDialog::RelationshipProgressDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setupConnections();

    setModal(true);
    setWindowTitle("符号关系分析进度");
    setMinimumSize(500, 200);
    resize(600, 300);

    timeUpdateTimer = nullptr;
    estimationTimer = nullptr;
}

RelationshipProgressDialog::~RelationshipProgressDialog()
{
    if (timeUpdateTimer) {
        timeUpdateTimer->stop();
        timeUpdateTimer->deleteLater();
        timeUpdateTimer = nullptr;
    }

    if (estimationTimer) {
        estimationTimer->stop();
        estimationTimer->deleteLater();
        estimationTimer = nullptr;
    }
}

void RelationshipProgressDialog::setupUI()
{
    mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    statusLabel = new QLabel("准备开始分析SystemVerilog文件的符号关系...", this);
    statusLabel->setWordWrap(true);
    QFont statusFont = statusLabel->font();
    statusFont.setPointSize(statusFont.pointSize() + 1);
    statusLabel->setFont(statusFont);
    mainLayout->addWidget(statusLabel);

    progressLayout = new QHBoxLayout();
    progressBar = new QProgressBar(this);
    progressBar->setMinimum(0);
    progressBar->setMaximum(100);
    progressBar->setTextVisible(true);
    progressBar->setFormat("%v / %m 文件 (%p%)");

    speedLabel = new QLabel("", this);
    speedLabel->setMinimumWidth(100);
    speedLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    progressLayout->addWidget(progressBar, 1);
    progressLayout->addWidget(speedLabel);
    mainLayout->addLayout(progressLayout);

    currentFileLabel = new QLabel("", this);
    currentFileLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    currentFileLabel->setWordWrap(true);
    mainLayout->addWidget(currentFileLabel);

    QHBoxLayout* timeLayout = new QHBoxLayout();
    estimatedLabel = new QLabel("", this);
    estimatedLabel->setAlignment(Qt::AlignRight);
    timeLayout->addWidget(estimatedLabel);
    mainLayout->addLayout(timeLayout);

    statsLabel = new QLabel("已分析: 0个文件, 发现: 0个关系", this);
    fileStatsLabel = new QLabel("", this);
    mainLayout->addWidget(statsLabel);
    mainLayout->addWidget(fileStatsLabel);

    detailsGroup = new QGroupBox("详细日志", this);
    detailsGroup->setVisible(false);

    QVBoxLayout* detailsLayout = new QVBoxLayout(detailsGroup);
    detailsText = new QTextEdit(detailsGroup);
    detailsText->setMaximumHeight(150);
    detailsText->setReadOnly(true);
    detailsText->setFont(QFont("Consolas", 9));
    detailsLayout->addWidget(detailsText);

    mainLayout->addWidget(detailsGroup);

    buttonLayout = new QHBoxLayout();

    detailsButton = new QPushButton("显示详情", this);
    detailsButton->setCheckable(true);

    pauseButton = new QPushButton("暂停", this);
    pauseButton->setEnabled(false);

    cancelButton = new QPushButton("取消", this);

    buttonLayout->addWidget(detailsButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(pauseButton);
    buttonLayout->addWidget(cancelButton);

    mainLayout->addLayout(buttonLayout);
}

void RelationshipProgressDialog::setupConnections()
{
    connect(cancelButton, &QPushButton::clicked, this, &RelationshipProgressDialog::onCancelClicked);
    connect(detailsButton, &QPushButton::toggled, this, &RelationshipProgressDialog::onDetailsToggled);

    connect(pauseButton, &QPushButton::clicked, this, [this]() {
        state.paused = !state.paused;
        pauseButton->setText(state.paused ? "继续" : "暂停");

        if (state.paused) {
            logProgress("⏸️ 分析已暂停");
            statusLabel->setText("分析已暂停 - 点击'继续'恢复分析");
            if (timeUpdateTimer && timeUpdateTimer->isActive()) {
                timeUpdateTimer->stop();
            }
        } else {
            logProgress("▶️ 分析继续");
            statusLabel->setText("继续分析SystemVerilog文件...");
            if (timeUpdateTimer && !timeUpdateTimer->isActive()) {
                timeUpdateTimer->start();
            }
        }
    });

    timeUpdateTimer = new QTimer(this);
    timeUpdateTimer->setInterval(1000);

    estimationTimer = new QTimer(this);
    estimationTimer->setInterval(2000);
    connect(estimationTimer, &QTimer::timeout, this, &RelationshipProgressDialog::updateEstimatedTime);
}

void RelationshipProgressDialog::startAnalysis(int totalFiles)
{
    if (totalFiles <= 0) {
        totalFiles = 1;
    }

    state = AnalysisState();
    state.totalFiles = totalFiles;
    state.cancelled = false;
    state.finished = false;
        state.paused = false;

    progressBar->setMaximum(totalFiles);
    progressBar->setValue(0);
    progressBar->setFormat(QString("准备中... (0 / %1 文件)").arg(totalFiles));

    statusLabel->setText(QString("正在准备分析 %1 个SystemVerilog文件...").arg(totalFiles));
    currentFileLabel->setText("阶段 1/2: 正在加载符号数据库，请稍候...");
    speedLabel->setText("");
    estimatedLabel->setText("");

    if (config.showDetails) {
        detailsText->clear();
        logProgress(QString("🚀 开始分析 %1 个SV文件").arg(totalFiles));
        logProgress("⏳ 阶段1: 正在加载符号数据库...");
        logProgress("⏳ 阶段1: 初始化关系分析引擎...");
    }

    if (timeUpdateTimer) {
        elapsedTimer.start();
        timeUpdateTimer->start();
    }

    if (estimationTimer) {
        estimationTimer->start();
    }

    pauseButton->setEnabled(true);
    cancelButton->setText("取消");

    forceShow();

    QTimer::singleShot(100, this, [this]() {
        debugState();
    });
}


void RelationshipProgressDialog::setSymbolAnalysisProgress(int filesDone, int totalFiles)
{
    if (statsLabel && totalFiles > 0) {
        statsLabel->setText(QString("阶段1: 已分析 %1/%2 个文件 (符号)").arg(filesDone).arg(totalFiles));
    }
}

void RelationshipProgressDialog::updateProgress(const QString& fileName, int relationshipsFound)
{
    static int updateCount = 0;
    updateCount++;

    if (state.cancelled) {
        return;
    }

    if (state.finished) {
        if (state.processedFiles < state.totalFiles) {
            state.finished = false;
            statusLabel->setText("继续分析SystemVerilog文件...");
        } else {
            return;
        }
    }

    if (relationshipsFound < 0) relationshipsFound = 0;

    state.processedFiles++;
    state.totalRelationships += relationshipsFound;

    if (!isVisible()) {
        forceShow();
    }

    if (state.processedFiles > state.totalFiles) {
        state.totalFiles = state.processedFiles;
        progressBar->setMaximum(state.totalFiles);
    }

    QFileInfo fileInfo(fileName);
    qint64 fileSize = fileInfo.exists() ? fileInfo.size() : 0;
    state.totalFileSize += fileSize;
    state.fileSizes.append(fileSize);
    state.relationshipCounts.append(relationshipsFound);

    progressBar->setValue(state.processedFiles);

    QString shortFileName = fileInfo.fileName();
    if (shortFileName.length() > 45) {
        shortFileName = "..." + shortFileName.right(42);
    }

    QString sizeStr = formatFileSize(fileSize);
    QString currentText = QString("当前: %1 (%2个关系, %3)")
                         .arg(shortFileName)
                         .arg(relationshipsFound)
                         .arg(sizeStr);
    currentFileLabel->setText(currentText);

    updateStatistics();

    if (config.showDetails) {
        QString logMessage = QString("✅ %1: %2个关系")
                           .arg(shortFileName)
                           .arg(relationshipsFound);
        if (relationshipsFound > 100) {
            logMessage += " 🔥";
        }
        logProgress(logMessage);
    }

    if (config.showSpeed) {
        calculateSpeed();
    }

    QApplication::processEvents();

    if (state.processedFiles >= state.totalFiles && !state.finished) {
        QTimer::singleShot(500, this, [this]() {
            finishAnalysis();
        });
    }

}


void RelationshipProgressDialog::finishAnalysis()
{
    if (state.finished) {
        return;
    }

    state.finished = true;

    if (timeUpdateTimer && timeUpdateTimer->isActive()) {
        timeUpdateTimer->stop();
    }
    if (estimationTimer && estimationTimer->isActive()) {
        estimationTimer->stop();
    }

    if (state.cancelled) {
        statusLabel->setText("❌ 符号关系分析已取消");
        logProgress("❌ 分析被用户取消");
    } else {
        statusLabel->setText("✅ 符号关系分析完成!");
        logProgress(QString("🎉 分析完成! 总计发现 %1 个关系").arg(state.totalRelationships));

        currentFileLabel->setText(QString("分析完成 - 总计 %1 个关系")
                                 .arg(state.totalRelationships));
    }

    cancelButton->setText("关闭");
    pauseButton->setEnabled(false);

    emit finished();

    if (config.autoClose && !state.cancelled) {
        QTimer::singleShot(3000, this, [this]() {
            if (state.finished && !state.cancelled) {
                accept();
            }
        });
    }

}

void RelationshipProgressDialog::showError(const QString& fileName, const QString& error)
{
    state.totalErrors++;

    QString shortFileName = QFileInfo(fileName).fileName();
    QString errorMsg = QString("❌ %1: %2").arg(shortFileName, error);

    if (config.showDetails) {
        logProgress(errorMsg);
    }

    fileStatsLabel->setText(QString("错误: %1个文件").arg(state.totalErrors));
}

void RelationshipProgressDialog::updateStatistics()
{
    statsLabel->setText(QString("已分析: %1/%2个文件, 发现: %3个关系")
                       .arg(state.processedFiles)
                       .arg(state.totalFiles)
                       .arg(state.totalRelationships));

    QString totalSizeStr = formatFileSize(state.totalFileSize);
    double avgRelations = state.processedFiles > 0 ?
        (double)state.totalRelationships / state.processedFiles : 0;

    fileStatsLabel->setText(QString("总大小: %1, 平均关系数: %2")
                           .arg(totalSizeStr)
                           .arg(QString::number(avgRelations, 'f', 1)));
}

void RelationshipProgressDialog::calculateSpeed()
{
    if (state.processedFiles <= 0) return;

    qint64 elapsed = elapsedTimer.isValid() ? elapsedTimer.elapsed() : 0;
    if (elapsed <= 0) return;

    double filesPerSecond = (double)state.processedFiles * 1000 / elapsed;
    speedLabel->setText(formatSpeed(filesPerSecond));
}

void RelationshipProgressDialog::updateEstimatedTime()
{
    if (state.processedFiles <= 0 || state.totalFiles <= 0) return;

    qint64 elapsed = elapsedTimer.isValid() ? elapsedTimer.elapsed() : 0;
    if (elapsed <= 0) return;

    qint64 avgTimePerFile = elapsed / state.processedFiles;
    qint64 remainingFiles = state.totalFiles - state.processedFiles;

    if (remainingFiles > 0) {
        qint64 estimatedRemaining = avgTimePerFile * remainingFiles / 1000;
        estimatedLabel->setText(QString("预计剩余: %1").arg(formatTime(estimatedRemaining)));
    } else {
        estimatedLabel->setText("");
    }
}

void RelationshipProgressDialog::onCancelClicked()
{
    if (state.finished) {
        accept();
        return;
    }

    state.cancelled = true;
    emit cancelled();

    statusLabel->setText("正在取消分析...");
    cancelButton->setEnabled(false);
    pauseButton->setEnabled(false);

    logProgress("🛑 用户请求取消分析");
}

void RelationshipProgressDialog::onDetailsToggled(bool show)
{
    config.showDetails = show;
    detailsGroup->setVisible(show);
    detailsButton->setText(show ? "隐藏详情" : "显示详情");

    if (show) {
        int newHeight = height() + 150;
        resize(width(), newHeight);
    } else {
        int newHeight = qMax(200, height() - 150);
        resize(width(), newHeight);
    }
}

void RelationshipProgressDialog::logProgress(const QString& message)
{
    if (!config.showDetails || !detailsText) return;

    QString timestamp = QTime::currentTime().toString("hh:mm:ss");
    QString logLine = QString("[%1] %2").arg(timestamp, message);

    detailsText->append(logLine);

    QTextCursor cursor = detailsText->textCursor();
    cursor.movePosition(QTextCursor::End);
    detailsText->setTextCursor(cursor);
}

QString RelationshipProgressDialog::formatTime(qint64 seconds)
{
    if (seconds < 60) {
        return QString("%1秒").arg(seconds);
    } else if (seconds < 3600) {
        return QString("%1分%2秒").arg(seconds / 60).arg(seconds % 60);
    } else {
        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        int secs = seconds % 60;
        return QString("%1时%2分%3秒").arg(hours).arg(minutes).arg(secs);
    }
}

QString RelationshipProgressDialog::formatFileSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024);
    } else {
        return QString("%1 MB").arg(QString::number((double)bytes / (1024 * 1024), 'f', 1));
    }
}

QString RelationshipProgressDialog::formatSpeed(double filesPerSecond)
{
    if (filesPerSecond < 1.0) {
        return QString("%1/分钟").arg(QString::number(filesPerSecond * 60, 'f', 1));
    } else {
        return QString("%1/秒").arg(QString::number(filesPerSecond, 'f', 1));
    }
}

void RelationshipProgressDialog::setShowDetails(bool show)
{
    config.showDetails = show;
    detailsButton->setChecked(show);
    onDetailsToggled(show);
}

void RelationshipProgressDialog::setAutoClose(bool autoClose)
{
    config.autoClose = autoClose;
}

void RelationshipProgressDialog::setMinimumDuration(int msecs)
{
    config.minimumDuration = msecs;
}

void RelationshipProgressDialog::forceShow()
{
    show();
    raise();
    activateWindow();
    setWindowState(windowState() & ~Qt::WindowMinimized);

    if (parentWidget()) {
        move(parentWidget()->geometry().center() - rect().center());
    }
}

void RelationshipProgressDialog::debugState() const
{
}
