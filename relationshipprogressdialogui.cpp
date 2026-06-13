#include "relationshipprogressdialog.h"

#include <QFont>

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
