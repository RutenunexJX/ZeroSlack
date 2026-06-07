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
    setWindowTitle("Symbol Relationship Analysis Progress");
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

    statusLabel = new QLabel("Preparing to analyze SystemVerilog symbol relationships...", this);
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
    progressBar->setFormat("%v / %m files (%p%)");

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

    statsLabel = new QLabel("Analyzed: 0 files, Found: 0 relationships", this);
    fileStatsLabel = new QLabel("", this);
    mainLayout->addWidget(statsLabel);
    mainLayout->addWidget(fileStatsLabel);

    detailsGroup = new QGroupBox("Details", this);
    detailsGroup->setVisible(false);

    QVBoxLayout* detailsLayout = new QVBoxLayout(detailsGroup);
    detailsText = new QTextEdit(detailsGroup);
    detailsText->setMaximumHeight(150);
    detailsText->setReadOnly(true);
    detailsText->setFont(QFont("Consolas", 9));
    detailsLayout->addWidget(detailsText);

    mainLayout->addWidget(detailsGroup);

    buttonLayout = new QHBoxLayout();

    detailsButton = new QPushButton("Show Details", this);
    detailsButton->setCheckable(true);

    pauseButton = new QPushButton("Pause", this);
    pauseButton->setEnabled(false);

    cancelButton = new QPushButton("Cancel", this);

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
        pauseButton->setText(state.paused ? "Resume" : "Pause");

        if (state.paused) {
        logProgress("Analysis paused");
            statusLabel->setText("Analysis paused - click Resume to continue");
            if (timeUpdateTimer && timeUpdateTimer->isActive()) {
                timeUpdateTimer->stop();
            }
        } else {
            logProgress("Analysis resumed");
            statusLabel->setText("Continuing SystemVerilog file analysis...");
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
    progressBar->setFormat(QString("Preparing... (0 / %1 files)").arg(totalFiles));

    statusLabel->setText(QString("Preparing to analyze %1 SystemVerilog files...").arg(totalFiles));
    currentFileLabel->setText("Stage 1/2: Loading symbol database...");
    speedLabel->setText("");
    estimatedLabel->setText("");

    if (config.showDetails) {
        detailsText->clear();
        logProgress(QString("Starting analysis for %1 SV files").arg(totalFiles));
        logProgress("Stage 1: Loading symbol database...");
        logProgress("Stage 1: Initializing relationship analyzer...");
    }

    if (timeUpdateTimer) {
        elapsedTimer.start();
        timeUpdateTimer->start();
    }

    if (estimationTimer) {
        estimationTimer->start();
    }

    pauseButton->setEnabled(true);
    cancelButton->setText("Cancel");

    forceShow();

    QTimer::singleShot(100, this, [this]() {
        debugState();
    });
}


void RelationshipProgressDialog::setSymbolAnalysisProgress(int filesDone, int totalFiles)
{
    if (statsLabel && totalFiles > 0) {
        statsLabel->setText(QString("Stage 1: Analyzed %1/%2 files (symbols)").arg(filesDone).arg(totalFiles));
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
            statusLabel->setText("Continuing SystemVerilog file analysis...");
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
    QString currentText = QString("Current: %1 (%2 relationships, %3)")
                         .arg(shortFileName)
                         .arg(relationshipsFound)
                         .arg(sizeStr);
    currentFileLabel->setText(currentText);

    updateStatistics();

    if (config.showDetails) {
        QString logMessage = QString("%1: %2 relationships")
                           .arg(shortFileName)
                           .arg(relationshipsFound);
        if (relationshipsFound > 100) {
            logMessage += " (large)";
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
        statusLabel->setText("Symbol relationship analysis cancelled");
        logProgress("Analysis cancelled by user");
    } else {
        statusLabel->setText("Symbol relationship analysis complete!");
        logProgress(QString("Analysis complete! Found %1 relationships").arg(state.totalRelationships));

        currentFileLabel->setText(QString("Analysis complete - %1 relationships total")
                                 .arg(state.totalRelationships));
    }

    cancelButton->setText("Close");
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
    QString errorMsg = QString("%1: %2").arg(shortFileName, error);

    if (config.showDetails) {
        logProgress(errorMsg);
    }

    fileStatsLabel->setText(QString("Errors: %1 files").arg(state.totalErrors));
}

void RelationshipProgressDialog::updateStatistics()
{
    statsLabel->setText(QString("Analyzed: %1/%2 files, Found: %3 relationships")
                       .arg(state.processedFiles)
                       .arg(state.totalFiles)
                       .arg(state.totalRelationships));

    QString totalSizeStr = formatFileSize(state.totalFileSize);
    double avgRelations = state.processedFiles > 0 ?
        (double)state.totalRelationships / state.processedFiles : 0;

    fileStatsLabel->setText(QString("Total size: %1, Average relationships: %2")
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
        estimatedLabel->setText(QString("Remaining: %1").arg(formatTime(estimatedRemaining)));
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

    statusLabel->setText("Cancelling analysis...");
    cancelButton->setEnabled(false);
    pauseButton->setEnabled(false);

    logProgress("User requested cancellation");
}

void RelationshipProgressDialog::onDetailsToggled(bool show)
{
    config.showDetails = show;
    detailsGroup->setVisible(show);
    detailsButton->setText(show ? "Hide Details" : "Show Details");

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
        return QString("%1s").arg(seconds);
    } else if (seconds < 3600) {
        return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60);
    } else {
        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        int secs = seconds % 60;
        return QString("%1h %2m %3s").arg(hours).arg(minutes).arg(secs);
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
        return QString("%1/min").arg(QString::number(filesPerSecond * 60, 'f', 1));
    } else {
        return QString("%1/s").arg(QString::number(filesPerSecond, 'f', 1));
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
