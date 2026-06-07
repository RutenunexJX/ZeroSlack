#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "completionmanager.h"

#include "tabmanager.h"
#include "workspacemanager.h"
#include "projectmodel.h"
#include "modemanager.h"
#include "symbolanalyzer.h"
#include "analysisscheduler.h"
#include "navigationmanager.h"
#include "navigationwidget.h"
#include "diagnosticservice.h"
#include "referenceservice.h"
#include "relationshipservice.h"
#include "symbolrelationshipengine.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "syminfo.h"
#include "version.h"
#include <QtConcurrent/QtConcurrent>
#include <QLabel>
#include <QStatusBar>

#include <QMessageBox>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextStream>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QSet>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setCentralWidget(ui->tabWidget);

    relationshipEngine = std::make_unique<SymbolRelationshipEngine>(this);

    tabManager = std::unique_ptr<TabManager>(new TabManager(ui->tabWidget, this));
    workspaceManager = std::unique_ptr<WorkspaceManager>(new WorkspaceManager(this));
    modeManager = std::unique_ptr<ModeManager>(new ModeManager(ui->tabWidget, this));
    symbolAnalyzer = std::unique_ptr<SymbolAnalyzer>(new SymbolAnalyzer(this));
    navigationManager = std::unique_ptr<NavigationManager>(new NavigationManager(this));  // NEW
    analysisScheduler = std::unique_ptr<AnalysisScheduler>(new AnalysisScheduler(this));

    setupRelationshipEngine();
    setupNavigationPane();
    setupProblemsPane();
    setupReferencesPane();
    setupRelationshipsPane();
    setupManagerConnections();
    connectNavigationSignals();

    setWindowTitle(QStringLiteral("ZeroSlack  %1").arg(QLatin1String(APP_VERSION)));
    if (statusBar()) {
        QLabel* versionLabel = new QLabel(
            QStringLiteral("v%1").arg(QLatin1String(APP_VERSION)), this);
        versionLabel->setToolTip(
            QStringLiteral("ZeroSlack %1\nBuilt at %2")
                .arg(QLatin1String(APP_VERSION), QLatin1String(APP_BUILD_TIME)));
        versionLabel->setStyleSheet(QStringLiteral("color:#888; margin-right:6px;"));
        statusBar()->addPermanentWidget(versionLabel);
    }
}

MainWindow::~MainWindow()
{
    if (analysisScheduler) {
        analysisScheduler->cancelWorkspaceRelationshipAnalysis();
    }
    if (relationshipEngine) {
        relationshipEngine->clearAllRelationships();
    }

    if (progressDialog) {
        progressDialog->deleteLater();
    }
    delete ui;
}

void MainWindow::setupManagerConnections()
{
    analysisScheduler->setDocumentModel(tabManager->getDocumentModel());
    analysisScheduler->setProjectModel(workspaceManager->getProjectModel());
    analysisScheduler->setSymbolAnalyzer(symbolAnalyzer.get());
    analysisScheduler->setOpenFileContentProvider([this](const QString& fileName) {
        return tabManager ? tabManager->getPlainTextFromOpenFile(fileName) : QString();
    });
    analysisScheduler->setWorkspaceOpenProvider([this]() {
        return workspaceManager && workspaceManager->isWorkspaceOpen();
    });
    analysisScheduler->setWorkspaceSymbolCancelProvider([this]() {
        return symbolAnalysisCancelled.load();
    });
    analysisScheduler->setRelationshipAnalysisCallback(
        [this](const QString& fileName, const QString& content) {
            submitSingleFileRelationshipAnalysis(fileName, content);
        });
    analysisScheduler->setWorkspaceRelationshipAnalysisCallback(
        [this](const ProjectSnapshot& project,
               std::shared_ptr<const SemanticIndexSnapshot> baseSnapshot) {
            WorkspaceRelationshipAnalysisResult result;
            result.baseSnapshot = baseSnapshot;
            result.semanticSnapshot = baseSnapshot;
            if (!relationshipBuilder)
                return result;

            relationshipBuilder->resetCancellation();
            const QStringList svFiles = project.systemVerilogFiles;
            result.fileRelationships.reserve(svFiles.size());
            QList<SemanticRelationship> snapshotRelationships =
                baseSnapshot ? baseSnapshot->relationships() : QList<SemanticRelationship>();
            QSet<QString> seenRelationships;
            for (const SemanticRelationship& relationship : std::as_const(snapshotRelationships)) {
                seenRelationships.insert(QStringLiteral("%1:%2:%3")
                                             .arg(relationship.fromId)
                                             .arg(relationship.toId)
                                             .arg(static_cast<int>(relationship.type)));
            }
            for (const QString& filePath : svFiles) {
                if (relationshipBuilder->isCancelled())
                    break;
                QFile file(filePath);
                if (!file.open(QIODevice::ReadOnly | QFile::Text))
                    continue;
                const QString content = QTextStream(&file).readAll();
                const QList<sym_list::SymbolInfo> fs =
                    baseSnapshot ? baseSnapshot->getSymbols(filePath) : QList<sym_list::SymbolInfo>();
                const QVector<RelationshipToAdd> relationships =
                    relationshipBuilder->computeRelationships(filePath, content, fs, baseSnapshot.get());
                result.fileRelationships.append({filePath, relationships});
                for (const RelationshipToAdd& relationship : relationships) {
                    if (relationship.fromId < 0 || relationship.toId < 0)
                        continue;
                    const QString key = QStringLiteral("%1:%2:%3")
                                            .arg(relationship.fromId)
                                            .arg(relationship.toId)
                                            .arg(static_cast<int>(relationship.type));
                    if (seenRelationships.contains(key))
                        continue;
                    seenRelationships.insert(key);
                    snapshotRelationships.append({relationship.fromId,
                                                  relationship.toId,
                                                  relationship.type});
                }
            }
            if (baseSnapshot) {
                result.semanticSnapshot = std::make_shared<SemanticIndexSnapshot>(
                    baseSnapshot->getSymbols(),
                    snapshotRelationships,
                    baseSnapshot->diagnostics(),
                    baseSnapshot->fileContents());
            }
            return result;
        });
    analysisScheduler->setWorkspaceRelationshipCancelCallback([this]() {
        if (relationshipBuilder)
            relationshipBuilder->cancelAnalysis();
    });
    connect(analysisScheduler.get(), &AnalysisScheduler::documentRefreshRequested,
            this, [this](const QString& fileName) {
                MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
                if (editor && editor->getFileName() == fileName) {
                    editor->refreshScopeAndCurrentLineHighlight();
                    QTimer::singleShot(0, editor, [editor]() {
                        editor->refreshScopeAndCurrentLineHighlight();
                    });
                }
            });

    connect(tabManager.get(), &TabManager::activeTabChanged,
            this, [](MyCodeEditor* editor) { Q_UNUSED(editor); });

    connect(tabManager.get(), &TabManager::tabClosed,
            this, [this](const QString& fileName) {
                symbolAnalyzer->analyzeOpenTabs(tabManager.get());

                if (relationshipEngine) {
                    relationshipEngine->invalidateFileRelationships(fileName);
                }
            });

    connect(workspaceManager.get(), &WorkspaceManager::fileChanged,
            this, [this](const QString& filePath) {
                if (analysisScheduler)
                    analysisScheduler->handleExternalFileChanged(filePath, kFileChangeDebounceMs);
            });

    connect(analysisScheduler.get(), &AnalysisScheduler::workspaceSymbolAnalysisStarted,
            this, [this](const ProjectSnapshot& project, int totalFiles) {
                Q_UNUSED(totalFiles)
                symbolAnalysisCancelled.store(false);
                const QStringList svFiles = project.systemVerilogFiles;
                showAnalysisProgress(svFiles);

                QTimer::singleShot(10, this, [this, svFiles]() {
                    if (progressDialog) {
                        progressDialog->statusLabel->setText("Stage 1/2: Symbol analysis running...");
                        progressDialog->currentFileLabel->setText("Scanning and parsing SystemVerilog file structure...");
                        progressDialog->progressBar->setFormat("Symbol analysis running... Please wait");

                        if (progressDialog->config.showDetails) {
                            progressDialog->logProgress("Starting symbol analysis stage...");
                            progressDialog->logProgress(QString("Found %1 SV files").arg(svFiles.size()));
                        }

                        progressDialog->update();
                        progressDialog->repaint();
                    }
                });
            });

    connect(analysisScheduler.get(), &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this, [this](const ProjectSnapshot& project, int filesAnalyzed, int totalSymbols) {
                if (statusBar()) {
                    statusBar()->showMessage(
                        QString("Symbol analysis complete: %1 files, %2 symbols - relationship analysis running...")
                        .arg(filesAnalyzed).arg(totalSymbols),
                        3000);
                }
                const QStringList svFiles = project.systemVerilogFiles;
                if (progressDialog) {
                    progressDialog->statusLabel->setText("Stage 2/2: Relationship analysis running...");
                    progressDialog->currentFileLabel->setText("Analyzing symbol dependencies between files...");
                    progressDialog->progressBar->setFormat(QString("%v / %1 files (%p%)").arg(svFiles.size()));
                    if (progressDialog->config.showDetails) {
                        progressDialog->logProgress("Starting relationship analysis stage...");
                        progressDialog->logProgress("Analyzing module instantiation relationships...");
                        progressDialog->logProgress("Analyzing variable assignment relationships...");
                        progressDialog->logProgress("Analyzing task/function call relationships...");
                    }
                    progressDialog->update();
                    progressDialog->repaint();
                }
            });

    connect(analysisScheduler.get(), &AnalysisScheduler::workspaceRelationshipAnalysisStarted,
            this, [this](const ProjectSnapshot& project, int totalFiles) {
                Q_UNUSED(project)
                relationshipAnalysisTracker.totalFiles = totalFiles;
                relationshipAnalysisTracker.processedFiles = 0;
                relationshipAnalysisTracker.isActive = totalFiles > 0;
            });

    connect(analysisScheduler.get(), &AnalysisScheduler::workspaceRelationshipAnalysisFinished,
            this, &MainWindow::onWorkspaceRelationshipAnalysisFinished);

    connect(analysisScheduler.get(), &AnalysisScheduler::workspaceRelationshipAnalysisCancelled,
            this, [this]() {
                relationshipAnalysisTracker.isActive = false;
            });

    connect(modeManager.get(), &ModeManager::modeChanged,
            this,[]{});

    connect(modeManager.get(), &ModeManager::navigationToggleRequested,
                this, [this]() {
                    if (navigationDock) {
                        if (navigationDock->isVisible()) {
                            navigationDock->hide();
                        } else {
                            navigationDock->show();
                            navigationDock->raise();
                            navigationDock->activateWindow();
                        }
                    }
                });

    connect(symbolAnalyzer.get(), &SymbolAnalyzer::analysisCompleted,
            this, [this](const QString& fileName, int symbolCount) {
                Q_UNUSED(symbolCount)
                updateProblemsPanel(fileName);
                MyCodeEditor* editor = tabManager->getCurrentEditor();
                if (!editor || editor->getFileName() != fileName) return;
                editor->refreshScopeAndCurrentLineHighlight();
            });

    connect(symbolAnalyzer.get(), &SymbolAnalyzer::batchAnalysisCompleted,
            this, [this](int, int) {
                updateProblemsPanel();
            });

    connect(symbolAnalyzer.get(), &SymbolAnalyzer::batchProgress,
            this, [this](int filesDone, int totalFiles, const QString& currentFileName) {
                if (progressDialog && totalFiles > 0) {
                    progressDialog->progressBar->setValue(filesDone);
                    progressDialog->progressBar->setMaximum(totalFiles);
                    progressDialog->setSymbolAnalysisProgress(filesDone, totalFiles);
                    QString shortName = QFileInfo(currentFileName).fileName();
                    if (shortName.length() > 45)
                        shortName = "..." + shortName.right(42);
                    progressDialog->currentFileLabel->setText(
                        QString("Symbol analysis: %1 / %2 - %3").arg(filesDone).arg(totalFiles).arg(shortName));
                }
            });

    navigationManager->connectToTabManager(tabManager.get());
    navigationManager->connectToWorkspaceManager(workspaceManager.get());
    navigationManager->connectToSymbolAnalyzer(symbolAnalyzer.get());

    if (!relationshipBuilder)
        return;

    connect(relationshipBuilder.get(), &SmartRelationshipBuilder::analysisCompleted,
            this, [this](const QString& fileName, int relationshipsFound) {
                if (progressDialog) {
                    progressDialog->updateProgress(fileName, relationshipsFound);

                    QString shortName = QFileInfo(fileName).fileName();
                    if (progressDialog->config.showDetails) {
                        progressDialog->logProgress(
                            QString("%1: found %2 relationships").arg(shortName).arg(relationshipsFound));
                    }
                }

                if (relationshipAnalysisTracker.isActive) {
                    relationshipAnalysisTracker.processedFiles++;

                    if (progressDialog) {
                        progressDialog->statusLabel->setText(
                            QString("Stage 2/2: Relationship analysis running (%1/%2)")
                            .arg(relationshipAnalysisTracker.processedFiles)
                            .arg(relationshipAnalysisTracker.totalFiles));
                    }

                    if (relationshipAnalysisTracker.processedFiles >= relationshipAnalysisTracker.totalFiles) {
                        relationshipAnalysisTracker.isActive = false;

                        if (progressDialog) {
                            progressDialog->statusLabel->setText("All analysis complete!");
                            if (progressDialog->config.showDetails) {
                                progressDialog->logProgress("Relationship analysis complete!");
                                progressDialog->logProgress(QString("Processed %1 files")
                                    .arg(relationshipAnalysisTracker.totalFiles));
                            }
                        }

                        QTimer::singleShot(200, this, [this]() {
                            if (progressDialog) {
                                progressDialog->finishAnalysis();
                            }

                            if (statusBar()) {
                                statusBar()->showMessage(
                                    QString("Relationship analysis complete: %1 files")
                                    .arg(relationshipAnalysisTracker.totalFiles),
                                    5000);
                            }
                        });
                    }
                }

                QString shortName = QFileInfo(fileName).fileName();
                if (statusBar()) {
                    statusBar()->showMessage(
                        QString("Relationship analysis: %1 (%2 relationships)")
                        .arg(shortName).arg(relationshipsFound),
                        1000);
                }
            });

    connect(relationshipBuilder.get(), &SmartRelationshipBuilder::analysisError,
            this, [this](const QString& fileName, const QString& error) {
                Q_UNUSED(fileName)
                Q_UNUSED(error)
                if (progressDialog && progressDialog->isVisible()) {
                    progressDialog->showError(fileName, error);
                }

                if (relationshipAnalysisTracker.isActive) {
                    relationshipAnalysisTracker.processedFiles++;

                    if (relationshipAnalysisTracker.processedFiles >= relationshipAnalysisTracker.totalFiles) {
                        relationshipAnalysisTracker.isActive = false;

                        QTimer::singleShot(200, this, [this]() {
                            if (progressDialog) {
                                progressDialog->finishAnalysis();
                            }
                        });
                    }
                }
            });

    connect(relationshipBuilder.get(), &SmartRelationshipBuilder::analysisCancelled,
            this, [this]() {
                relationshipAnalysisTracker.isActive = false;

                if (progressDialog) {
                    progressDialog->finishAnalysis();
                }

                if (statusBar()) {
                    statusBar()->showMessage("Relationship analysis cancelled", 3000);
                }
            });

    if (relationshipEngine) {
        connect(relationshipEngine.get(), &SymbolRelationshipEngine::relationshipAdded,
                this, &MainWindow::onRelationshipAdded);

        connect(relationshipEngine.get(), &SymbolRelationshipEngine::relationshipsCleared,
                this, &MainWindow::onRelationshipsCleared);
    }
}


void MainWindow::setupNavigationPane()
{
    navigationWidget = new NavigationWidget(this);

    navigationDock = new QDockWidget("Navigation", this);
    navigationDock->setWidget(navigationWidget);
    navigationDock->setFeatures(QDockWidget::DockWidgetMovable |
                               QDockWidget::DockWidgetFloatable |
                               QDockWidget::DockWidgetClosable);

    navigationDock->setMinimumWidth(200);
    navigationDock->setMaximumWidth(400);
    navigationWidget->setMinimumWidth(180);

    addDockWidget(Qt::LeftDockWidgetArea, navigationDock);

    navigationManager->setNavigationWidget(navigationWidget);
}

void MainWindow::setupProblemsPane()
{
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* filtersLayout = new QHBoxLayout();
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);

    problemsScopeCombo = new QComboBox(panel);
    problemsScopeCombo->setObjectName(QStringLiteral("problemsScopeCombo"));
    problemsScopeCombo->addItem(QStringLiteral("Current File"), 0);
    problemsScopeCombo->addItem(QStringLiteral("All Files"), 1);
    problemsScopeCombo->setToolTip(QStringLiteral("Problem scope"));
    filtersLayout->addWidget(problemsScopeCombo);

    problemsSeverityCombo = new QComboBox(panel);
    problemsSeverityCombo->setObjectName(QStringLiteral("problemsSeverityCombo"));
    problemsSeverityCombo->addItem(QStringLiteral("All Severities"), 0);
    problemsSeverityCombo->addItem(QStringLiteral("Errors"), 1);
    problemsSeverityCombo->addItem(QStringLiteral("Warnings"), 2);
    problemsSeverityCombo->addItem(QStringLiteral("Info"), 3);
    problemsSeverityCombo->setToolTip(QStringLiteral("Severity filter"));
    filtersLayout->addWidget(problemsSeverityCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    problemsTree = new QTreeWidget(this);
    problemsTree->setObjectName(QStringLiteral("problemsTree"));
    problemsTree->setColumnCount(5);
    problemsTree->setHeaderLabels({"Severity", "File", "Line", "Column", "Message"});
    problemsTree->setRootIsDecorated(false);
    problemsTree->setAlternatingRowColors(true);
    problemsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    problemsTree->header()->setStretchLastSection(true);
    problemsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    problemsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(problemsTree);

    problemsDock = new QDockWidget("Problems", this);
    problemsDock->setObjectName(QStringLiteral("problemsDock"));
    problemsDock->setWidget(panel);
    problemsDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetFloatable |
                              QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);

    connect(problemsScopeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateProblemsPanel(); });
    connect(problemsSeverityCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateProblemsPanel(); });

    connect(problemsTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
                if (!item)
                    return;
                const QString fileName = item->data(0, Qt::UserRole).toString();
                const int line = item->data(0, Qt::UserRole + 1).toInt();
                const int column = item->data(0, Qt::UserRole + 2).toInt();
                navigateToFileAndLine(fileName, line, column);
            });
}

static QString diagnosticSeverityText(SemanticDiagnostic::Severity severity)
{
    switch (severity) {
    case SemanticDiagnostic::Error:
        return QStringLiteral("Error");
    case SemanticDiagnostic::Warning:
        return QStringLiteral("Warning");
    case SemanticDiagnostic::Info:
    default:
        return QStringLiteral("Info");
    }
}

static QString relationshipTypeText(SymbolRelationshipEngine::RelationType type)
{
    switch (type) {
    case SymbolRelationshipEngine::CONTAINS:
        return QStringLiteral("Contains");
    case SymbolRelationshipEngine::REFERENCES:
        return QStringLiteral("References");
    case SymbolRelationshipEngine::INSTANTIATES:
        return QStringLiteral("Instantiates");
    case SymbolRelationshipEngine::CALLS:
        return QStringLiteral("Calls");
    case SymbolRelationshipEngine::INHERITS:
        return QStringLiteral("Inherits");
    case SymbolRelationshipEngine::IMPLEMENTS:
        return QStringLiteral("Implements");
    case SymbolRelationshipEngine::ASSIGNS_TO:
        return QStringLiteral("Assigns To");
    case SymbolRelationshipEngine::READS_FROM:
        return QStringLiteral("Reads From");
    case SymbolRelationshipEngine::CLOCKS:
        return QStringLiteral("Clocks");
    case SymbolRelationshipEngine::RESETS:
        return QStringLiteral("Resets");
    case SymbolRelationshipEngine::GENERATES:
        return QStringLiteral("Generates");
    case SymbolRelationshipEngine::CONSTRAINS:
        return QStringLiteral("Constrains");
    }
    return QStringLiteral("Relationship");
}

static QString normalizedUiFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
}

void MainWindow::setupReferencesPane()
{
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* filtersLayout = new QHBoxLayout();
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);

    referenceScopeCombo = new QComboBox(panel);
    referenceScopeCombo->setObjectName(QStringLiteral("referenceScopeCombo"));
    referenceScopeCombo->addItem(QStringLiteral("All Files"), 0);
    referenceScopeCombo->addItem(QStringLiteral("Current File"), 1);
    referenceScopeCombo->setToolTip(QStringLiteral("Reference scope"));
    filtersLayout->addWidget(referenceScopeCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    referencesTree = new QTreeWidget(panel);
    referencesTree->setObjectName(QStringLiteral("referencesTree"));
    referencesTree->setColumnCount(4);
    referencesTree->setHeaderLabels({"Symbol", "File", "Line", "Relationship"});
    referencesTree->setRootIsDecorated(false);
    referencesTree->setAlternatingRowColors(true);
    referencesTree->setSelectionMode(QAbstractItemView::SingleSelection);
    referencesTree->header()->setStretchLastSection(true);
    referencesTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    referencesTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    referencesTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(referencesTree);

    referencesDock = new QDockWidget("References", this);
    referencesDock->setObjectName(QStringLiteral("referencesDock"));
    referencesDock->setWidget(panel);
    referencesDock->setFeatures(QDockWidget::DockWidgetMovable |
                                QDockWidget::DockWidgetFloatable |
                                QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, referencesDock);
    referencesDock->hide();

    connect(referenceScopeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshReferencesPanel(); });

    connect(referencesTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
                if (!item)
                    return;
                const QString fileName = item->data(0, Qt::UserRole).toString();
                const int line = item->data(0, Qt::UserRole + 1).toInt();
                const int column = item->data(0, Qt::UserRole + 2).toInt();
                navigateToFileAndLine(fileName, line, column);
            });
}

void MainWindow::setupRelationshipsPane()
{
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* filtersLayout = new QHBoxLayout();
    filtersLayout->setContentsMargins(0, 0, 0, 0);
    filtersLayout->setSpacing(6);

    relationshipDirectionCombo = new QComboBox(panel);
    relationshipDirectionCombo->setObjectName(QStringLiteral("relationshipDirectionCombo"));
    relationshipDirectionCombo->addItem(QStringLiteral("All Directions"), 0);
    relationshipDirectionCombo->addItem(QStringLiteral("Outgoing"), 1);
    relationshipDirectionCombo->addItem(QStringLiteral("Incoming"), 2);
    relationshipDirectionCombo->setToolTip(QStringLiteral("Relationship direction"));
    filtersLayout->addWidget(relationshipDirectionCombo);

    relationshipTypeCombo = new QComboBox(panel);
    relationshipTypeCombo->setObjectName(QStringLiteral("relationshipTypeCombo"));
    relationshipTypeCombo->addItem(QStringLiteral("All Types"), -1);
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::REFERENCES),
                                   static_cast<int>(SymbolRelationshipEngine::REFERENCES));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::INSTANTIATES),
                                   static_cast<int>(SymbolRelationshipEngine::INSTANTIATES));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::CALLS),
                                   static_cast<int>(SymbolRelationshipEngine::CALLS));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::ASSIGNS_TO),
                                   static_cast<int>(SymbolRelationshipEngine::ASSIGNS_TO));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::READS_FROM),
                                   static_cast<int>(SymbolRelationshipEngine::READS_FROM));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::CLOCKS),
                                   static_cast<int>(SymbolRelationshipEngine::CLOCKS));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::RESETS),
                                   static_cast<int>(SymbolRelationshipEngine::RESETS));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::CONTAINS),
                                   static_cast<int>(SymbolRelationshipEngine::CONTAINS));
    relationshipTypeCombo->addItem(relationshipTypeText(SymbolRelationshipEngine::GENERATES),
                                   static_cast<int>(SymbolRelationshipEngine::GENERATES));
    relationshipTypeCombo->setToolTip(QStringLiteral("Relationship type"));
    filtersLayout->addWidget(relationshipTypeCombo);
    filtersLayout->addStretch(1);
    layout->addLayout(filtersLayout);

    relationshipsTree = new QTreeWidget(panel);
    relationshipsTree->setObjectName(QStringLiteral("relationshipsTree"));
    relationshipsTree->setColumnCount(5);
    relationshipsTree->setHeaderLabels({"Direction", "Symbol", "File", "Line", "Relationship"});
    relationshipsTree->setRootIsDecorated(false);
    relationshipsTree->setAlternatingRowColors(true);
    relationshipsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    relationshipsTree->header()->setStretchLastSection(true);
    relationshipsTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    relationshipsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(relationshipsTree);

    relationshipsDock = new QDockWidget("Relationships", this);
    relationshipsDock->setObjectName(QStringLiteral("relationshipsDock"));
    relationshipsDock->setWidget(panel);
    relationshipsDock->setFeatures(QDockWidget::DockWidgetMovable |
                                   QDockWidget::DockWidgetFloatable |
                                   QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, relationshipsDock);
    relationshipsDock->hide();

    connect(relationshipDirectionCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshRelationshipsPanel(); });
    connect(relationshipTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshRelationshipsPanel(); });

    connect(relationshipsTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
                if (!item)
                    return;
                const QString fileName = item->data(0, Qt::UserRole).toString();
                const int line = item->data(0, Qt::UserRole + 1).toInt();
                const int column = item->data(0, Qt::UserRole + 2).toInt();
                navigateToFileAndLine(fileName, line, column);
            });
}

void MainWindow::updateProblemsPanel(const QString& fileName)
{
    if (!problemsTree)
        return;

    DiagnosticQuery query;
    const bool currentFileOnly =
        !problemsScopeCombo || problemsScopeCombo->currentData().toInt() == 0;
    if (currentFileOnly) {
        query.fileName = fileName;
        if (query.fileName.isEmpty()) {
            MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
            if (editor)
                query.fileName = editor->getFileName();
        }
    }

    const int severityFilter =
        problemsSeverityCombo ? problemsSeverityCombo->currentData().toInt() : 0;
    query.includeErrors = severityFilter == 0 || severityFilter == 1;
    query.includeWarnings = severityFilter == 0 || severityFilter == 2;
    query.includeInfo = severityFilter == 0 || severityFilter == 3;

    const QList<DiagnosticResult> diagnostics =
        DiagnosticService::getInstance()->findDiagnostics(query);

    problemsTree->clear();
    for (const DiagnosticResult& result : diagnostics) {
        const SemanticDiagnostic& diagnostic = result.diagnostic;
        auto* item = new QTreeWidgetItem(problemsTree);
        item->setText(0, diagnosticSeverityText(diagnostic.severity));
        item->setText(1, QFileInfo(diagnostic.fileName).fileName());
        item->setText(2, QString::number(diagnostic.line));
        item->setText(3, QString::number(diagnostic.column));
        item->setText(4, diagnostic.message);
        item->setToolTip(1, diagnostic.fileName);
        item->setToolTip(4, diagnostic.message);
        item->setData(0, Qt::UserRole, diagnostic.fileName);
        item->setData(0, Qt::UserRole + 1, diagnostic.line);
        item->setData(0, Qt::UserRole + 2, diagnostic.column);
    }

    if (problemsDock) {
        problemsDock->setWindowTitle(QStringLiteral("Problems (%1)").arg(diagnostics.size()));
        if (!diagnostics.isEmpty())
            problemsDock->show();
    }
}

static QTreeWidgetItem* createRelationshipItem(QTreeWidget* tree,
                                               const QString& direction,
                                               const sym_list::SymbolInfo& symbol,
                                               SymbolRelationshipEngine::RelationType type)
{
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, direction);
    item->setText(1, symbol.symbolName);
    item->setText(2, QFileInfo(symbol.fileName).fileName());
    item->setText(3, QString::number(symbol.startLine));
    item->setText(4, relationshipTypeText(type));
    item->setToolTip(2, symbol.fileName);
    item->setData(0, Qt::UserRole, symbol.fileName);
    item->setData(0, Qt::UserRole + 1, symbol.startLine);
    item->setData(0, Qt::UserRole + 2, symbol.startColumn);
    return item;
}

void MainWindow::showReferencesForSymbol(const QString& symbolName,
                                         const QString& fileName,
                                         const QString& moduleName)
{
    if (!referencesTree || symbolName.isEmpty())
        return;

    currentReferenceSymbolName = symbolName;
    currentReferenceFileName = fileName;
    currentReferenceModuleName = moduleName;
    refreshReferencesPanel();
}

void MainWindow::refreshReferencesPanel()
{
    if (!referencesTree || currentReferenceSymbolName.isEmpty())
        return;

    ReferenceQuery query;
    query.symbolName = currentReferenceSymbolName;
    query.fileName = currentReferenceFileName;
    query.moduleName = currentReferenceModuleName;

    const QList<ReferenceResult> references =
        ReferenceService::getInstance()->findReferences(query);

    const bool currentFileOnly =
        referenceScopeCombo && referenceScopeCombo->currentData().toInt() == 1;
    const QString normalizedReferenceFile = normalizedUiFileName(currentReferenceFileName);

    int visibleCount = 0;
    referencesTree->clear();
    for (const ReferenceResult& reference : references) {
        const sym_list::SymbolInfo& source = reference.referencingSymbol;
        if (currentFileOnly
            && normalizedUiFileName(source.fileName) != normalizedReferenceFile) {
            continue;
        }
        auto* item = new QTreeWidgetItem(referencesTree);
        item->setText(0, source.symbolName);
        item->setText(1, QFileInfo(source.fileName).fileName());
        item->setText(2, QString::number(source.startLine));
        item->setText(3, relationshipTypeText(reference.relationship.relationship.type));
        item->setToolTip(1, source.fileName);
        item->setData(0, Qt::UserRole, source.fileName);
        item->setData(0, Qt::UserRole + 1, source.startLine);
        item->setData(0, Qt::UserRole + 2, source.startColumn);
        visibleCount++;
    }

    if (referencesDock) {
        referencesDock->setWindowTitle(
            QStringLiteral("References: %1 (%2)")
                .arg(currentReferenceSymbolName)
                .arg(visibleCount));
        referencesDock->show();
        referencesDock->raise();
    }

    if (statusBar()) {
        statusBar()->showMessage(
            QStringLiteral("Found %1 references for %2")
                .arg(visibleCount)
                .arg(currentReferenceSymbolName),
            3000);
    }
}

void MainWindow::showRelationshipsForSymbol(const QString& symbolName,
                                            const QString& fileName,
                                            const QString& moduleName)
{
    if (!relationshipsTree || symbolName.isEmpty())
        return;

    currentRelationshipSymbolName = symbolName;
    currentRelationshipFileName = fileName;
    currentRelationshipModuleName = moduleName;
    refreshRelationshipsPanel();
}

void MainWindow::refreshRelationshipsPanel()
{
    if (!relationshipsTree || currentRelationshipSymbolName.isEmpty())
        return;

    RelationshipQuery query;
    query.symbolName = currentRelationshipSymbolName;
    query.fileName = currentRelationshipFileName;
    query.moduleName = currentRelationshipModuleName;

    const int typeFilter = relationshipTypeCombo
        ? relationshipTypeCombo->currentData().toInt()
        : -1;
    if (typeFilter >= 0) {
        query.types = {
            static_cast<SymbolRelationshipEngine::RelationType>(typeFilter)
        };
    }

    const int directionFilter = relationshipDirectionCombo
        ? relationshipDirectionCombo->currentData().toInt()
        : 0;

    RelationshipService* service = RelationshipService::getInstance();
    QList<RelationshipResult> outgoing;
    QList<RelationshipResult> incoming;
    if (directionFilter == 0 || directionFilter == 1)
        outgoing = service->findOutgoingRelationships(query);
    if (directionFilter == 0 || directionFilter == 2)
        incoming = service->findIncomingRelationships(query);

    relationshipsTree->clear();
    for (const RelationshipResult& relationship : std::as_const(outgoing)) {
        const sym_list::SymbolInfo& target = relationship.toSymbol;
        if (target.symbolId < 0)
            continue;
        createRelationshipItem(relationshipsTree,
                               QStringLiteral("Outgoing"),
                               target,
                               relationship.relationship.type);
    }
    for (const RelationshipResult& relationship : std::as_const(incoming)) {
        const sym_list::SymbolInfo& source = relationship.fromSymbol;
        if (source.symbolId < 0)
            continue;
        createRelationshipItem(relationshipsTree,
                               QStringLiteral("Incoming"),
                               source,
                               relationship.relationship.type);
    }

    const int total = relationshipsTree->topLevelItemCount();
    if (relationshipsDock) {
        relationshipsDock->setWindowTitle(
            QStringLiteral("Relationships: %1 (%2)")
                .arg(currentRelationshipSymbolName)
                .arg(total));
        relationshipsDock->show();
        relationshipsDock->raise();
    }

    if (statusBar()) {
        statusBar()->showMessage(
            QStringLiteral("Found %1 relationships for %2")
                .arg(total)
                .arg(currentRelationshipSymbolName),
            3000);
    }
}

void MainWindow::connectNavigationSignals()
{
    connect(navigationManager.get(), &NavigationManager::navigationRequested,
            this, &MainWindow::onNavigationRequested);

    connect(navigationManager.get(), &NavigationManager::symbolNavigationRequested,
            this, &MainWindow::onSymbolNavigationRequested);

    connect(tabManager.get(), &TabManager::tabCreated,
            this, [this](MyCodeEditor* editor) {
                if (editor) {
                    connect(editor, &MyCodeEditor::definitionJumpRequested,
                            this, [this](const QString&, const QString& file, int line) {
                                navigateToFileAndLine(file, line);
                            });
                    connect(editor, &MyCodeEditor::referenceSearchRequested,
                            this, &MainWindow::showReferencesForSymbol);
                    connect(editor, &MyCodeEditor::relationshipBrowseRequested,
                            this, &MainWindow::showRelationshipsForSymbol);
                }
            });

    connect(tabManager.get(), &TabManager::activeTabChanged,
            this, [this](MyCodeEditor* editor) {
                if (editor && navigationManager) {
                    navigationManager->onTabChanged(editor->getFileName());
                }
                if (problemsScopeCombo && problemsScopeCombo->currentData().toInt() == 0)
                    updateProblemsPanel();
            });
}

void MainWindow::onNavigationRequested(const QString& filePath, int lineNumber)
{
    navigateToFileAndLine(filePath, lineNumber);
}

void MainWindow::onSymbolNavigationRequested(const sym_list::SymbolInfo& symbol)
{
    navigateToFileAndLine(symbol.fileName, symbol.startLine);
}

void MainWindow::navigateToFileAndLine(const QString& filePath, int lineNumber, int columnNumber)
{
    if (filePath.isEmpty()) return;

    bool fileFound = false;
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        MyCodeEditor* editor = tabManager->getEditorAt(i);
        if (editor && editor->getFileName() == filePath) {
            ui->tabWidget->setCurrentIndex(i);
            fileFound = true;
            break;
        }
    }

    if (!fileFound) {
        if (!tabManager->openFileInTab(filePath)) {
            return;
        }
    }

    if (lineNumber > 0) {
        MyCodeEditor* currentEditor = tabManager->getCurrentEditor();
        if (currentEditor) {
            QTextCursor cursor = currentEditor->textCursor();
            cursor.movePosition(QTextCursor::Start);
            for (int i = 1; i < lineNumber; ++i) {
                cursor.movePosition(QTextCursor::Down);
            }
            if (columnNumber > 1) {
                cursor.movePosition(QTextCursor::Right,
                                    QTextCursor::MoveAnchor,
                                    columnNumber - 1);
            }
            currentEditor->setTextCursor(cursor);
            currentEditor->centerCursor();
            currentEditor->setFocus();
            currentEditor->moveMouseToCursor();
        }
    }
}

void MainWindow::on_new_file_triggered()
{
    tabManager->createNewTab();
}

void MainWindow::on_open_file_triggered()
{
    tabManager->openFileInTab(QString()); // Empty string triggers file dialog
}

void MainWindow::on_save_file_triggered()
{
    tabManager->saveCurrentTab();
}

void MainWindow::on_save_as_triggered()
{
    tabManager->saveAsCurrentTab();
}

void MainWindow::on_copy_triggered()
{
    MyCodeEditor *codeEditor = tabManager->getCurrentEditor();
    if (codeEditor) {
        codeEditor->copy();
    }
}

void MainWindow::on_paste_triggered()
{
    MyCodeEditor *codeEditor = tabManager->getCurrentEditor();
    if (codeEditor) {
        codeEditor->paste();
    }
}

void MainWindow::on_cut_triggered()
{
    MyCodeEditor *codeEditor = tabManager->getCurrentEditor();
    if (codeEditor) {
        codeEditor->cut();
    }
}

void MainWindow::on_undo_triggered()
{
    MyCodeEditor *codeEditor = tabManager->getCurrentEditor();
    if (codeEditor) {
        codeEditor->undo();
    }
}

void MainWindow::on_redo_triggered()
{
    MyCodeEditor *codeEditor = tabManager->getCurrentEditor();
    if (codeEditor) {
        codeEditor->redo();
    }
}

void MainWindow::on_open_direction_as_workspace_triggered()
{
    workspaceManager->openWorkspace(QString()); // Empty string triggers folder dialog
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (tabManager->hasUnsavedChanges()) {
        QMessageBox::question(
            this,
            "Warning",
            "There are unsaved changes. Quit?",
            QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes ? event->accept() : event->ignore();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (modeManager->handleKeyPress(event)) {
        return; // Event handled by mode manager
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (modeManager->handleKeyRelease(event)) {
        return; // Event handled by mode manager
    }
    QMainWindow::keyReleaseEvent(event);
}


void MainWindow::setupRelationshipEngine()
{
    if (!relationshipEngine) return;

    sym_list* symbolDatabase = sym_list::getInstance();
    symbolDatabase->setRelationshipEngine(relationshipEngine.get());

    slangManager = std::make_unique<SlangManager>();
    CompletionManager* completionManager = CompletionManager::getInstance();
    completionManager->setSlangManager(slangManager.get());
    completionManager->setRelationshipEngine(relationshipEngine.get());

    relationshipBuilder = std::make_unique<SmartRelationshipBuilder>(
        relationshipEngine.get(), symbolDatabase, slangManager.get(), this);

    relationshipSingleFileWatcher = new QFutureWatcher<SingleFileRelationshipAnalysisResult>(this);
    connect(relationshipSingleFileWatcher, &QFutureWatcher<SingleFileRelationshipAnalysisResult>::finished,
            this, &MainWindow::onSingleFileRelationshipFinished);

    connect(relationshipEngine.get(), &SymbolRelationshipEngine::relationshipAdded,
            this, &MainWindow::onRelationshipAdded);

    connect(relationshipEngine.get(), &SymbolRelationshipEngine::relationshipsCleared,
            this, &MainWindow::onRelationshipsCleared);

    connect(relationshipBuilder.get(), &SmartRelationshipBuilder::analysisCompleted,
            this, &MainWindow::onRelationshipAnalysisCompleted);

    connect(relationshipBuilder.get(), &SmartRelationshipBuilder::analysisError,
            this, &MainWindow::onRelationshipAnalysisError);
}

void MainWindow::onRelationshipAdded(int fromSymbolId, int toSymbolId,
                                    /*SymbolRelationshipEngine::RelationType*/int type)
{
    Q_UNUSED(fromSymbolId)
    Q_UNUSED(toSymbolId)
    Q_UNUSED(type)

    CompletionManager::getInstance()->invalidateRelationshipCaches();

    if (navigationManager) {
        if (!relationshipRefreshDeferTimer) {
            relationshipRefreshDeferTimer = new QTimer(this);
            relationshipRefreshDeferTimer->setSingleShot(true);
            connect(relationshipRefreshDeferTimer, &QTimer::timeout, this, [this]() {
                if (navigationManager)
                    navigationManager->refreshCurrentView();
                relationshipRefreshDeferTimer = nullptr;
            });
        }
        relationshipRefreshDeferTimer->start(400);
    }
}

void MainWindow::onRelationshipsCleared()
{
    CompletionManager::getInstance()->invalidateRelationshipCaches();

    if (navigationManager) {
        navigationManager->refreshCurrentView();
    }
}

void MainWindow::onRelationshipAnalysisCompleted(const QString& fileName, int relationshipsFound)
{
    CompletionManager::getInstance()->refreshRelationshipData();
    if (statusBar()) {
        statusBar()->showMessage(
            QString("Smart analysis completed: %1 relationships in %2")
            .arg(relationshipsFound).arg(QFileInfo(fileName).fileName()),
            2000);
    }
}

void MainWindow::onRelationshipAnalysisError(const QString& fileName, const QString& error)
{
    Q_UNUSED(fileName)
    if (statusBar()) {
        statusBar()->showMessage(
            QString("Analysis error: %1").arg(error), 3000);
    }
}

void MainWindow::requestSingleFileRelationshipAnalysis(const QString& fileName, const QString& content)
{
    if (analysisScheduler)
        analysisScheduler->requestRelationshipAnalysis(fileName, content);
}

void MainWindow::submitSingleFileRelationshipAnalysis(const QString& fileName, const QString& content)
{
    if (fileName.isEmpty() || !relationshipBuilder || !relationshipEngine)
        return;
    if (!relationshipSingleFileWatcher)
        return;
    if (relationshipSingleFileWatcher->isRunning()) {
        QFuture<SingleFileRelationshipAnalysisResult> oldFuture =
            relationshipSingleFileWatcher->future();
        relationshipSingleFileWatcher->cancel();
        oldFuture.waitForFinished();
    }
    pendingRelationshipFileName = fileName;
    relationshipBuilder->resetCancellation();
    const auto currentSnapshot = SemanticIndex::getInstance()->snapshot();
    const QList<SemanticDiagnostic> currentDiagnostics =
        currentSnapshot ? currentSnapshot->diagnostics() : QList<SemanticDiagnostic>();
    const auto baseSnapshot = std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolDatabase(sym_list::getInstance(), currentDiagnostics));
    SemanticIndex::getInstance()->setSnapshot(baseSnapshot);
    QFuture<SingleFileRelationshipAnalysisResult> future =
        QtConcurrent::run([this, fileName, content, baseSnapshot]() {
            SingleFileRelationshipAnalysisResult result;
            result.baseSnapshot = baseSnapshot;
            result.semanticSnapshot = baseSnapshot;
            if (!baseSnapshot)
                return result;

            const QList<sym_list::SymbolInfo> fs = baseSnapshot->getSymbols(fileName);
            result.relationships =
                relationshipBuilder->computeRelationships(fileName, content, fs, baseSnapshot.get());

            QList<SemanticRelationship> snapshotRelationships = baseSnapshot->relationships();
            QSet<QString> seenRelationships;
            for (const SemanticRelationship& relationship : std::as_const(snapshotRelationships)) {
                seenRelationships.insert(QStringLiteral("%1:%2:%3")
                                             .arg(relationship.fromId)
                                             .arg(relationship.toId)
                                             .arg(static_cast<int>(relationship.type)));
            }
            for (const RelationshipToAdd& relationship : std::as_const(result.relationships)) {
                if (relationship.fromId < 0 || relationship.toId < 0)
                    continue;
                const QString key = QStringLiteral("%1:%2:%3")
                                        .arg(relationship.fromId)
                                        .arg(relationship.toId)
                                        .arg(static_cast<int>(relationship.type));
                if (seenRelationships.contains(key))
                    continue;
                seenRelationships.insert(key);
                snapshotRelationships.append({relationship.fromId,
                                              relationship.toId,
                                              relationship.type});
            }
            result.semanticSnapshot = std::make_shared<SemanticIndexSnapshot>(
                baseSnapshot->getSymbols(),
                snapshotRelationships,
                baseSnapshot->diagnostics(),
                baseSnapshot->fileContents());
        return result;
    });
    relationshipSingleFileWatcher->setFuture(future);
}

void MainWindow::scheduleOpenFileAnalysis(const QString& fileName, int delayMs)
{
    if (analysisScheduler)
        analysisScheduler->scheduleOpenFileAnalysis(fileName, delayMs);
}

void MainWindow::cancelScheduledOpenFileAnalysis(const QString& fileName)
{
    if (analysisScheduler)
        analysisScheduler->cancelScheduledOpenFileAnalysis(fileName);
}

void MainWindow::onSingleFileRelationshipFinished()
{
    if (!relationshipSingleFileWatcher || !relationshipEngine || !relationshipBuilder)
        return;
    if (relationshipSingleFileWatcher->isCanceled()) {
        pendingRelationshipFileName.clear();
        return;
    }
    QString fileName = pendingRelationshipFileName;
    pendingRelationshipFileName.clear();
    const SingleFileRelationshipAnalysisResult result = relationshipSingleFileWatcher->result();
    if (result.baseSnapshot && SemanticIndex::getInstance()->snapshot() != result.baseSnapshot)
        return;

    relationshipEngine->beginUpdate();
    for (const RelationshipToAdd& r : result.relationships) {
        if (r.fromId < 0 || r.toId < 0)
            continue;
        relationshipEngine->addRelationship(r.fromId, r.toId, r.type, r.context, r.confidence);
    }
    relationshipEngine->endUpdate();
    if (result.semanticSnapshot) {
        SemanticIndex::getInstance()->setSnapshot(result.semanticSnapshot);
        CompletionManager::getInstance()->refreshRelationshipData();
    }
    onRelationshipAnalysisCompleted(fileName, result.relationships.size());
}

void MainWindow::onWorkspaceRelationshipAnalysisFinished(
    const WorkspaceRelationshipAnalysisResult& result)
{
    if (!relationshipEngine || !relationshipBuilder)
        return;
    if (result.baseSnapshot && SemanticIndex::getInstance()->snapshot() != result.baseSnapshot)
        return;

    relationshipEngine->beginUpdate();
    for (const auto& pair : result.fileRelationships) {
        const QString& fileName = pair.first;
        for (const RelationshipToAdd& r : pair.second) {
            if (r.fromId < 0 || r.toId < 0)
                continue;
            relationshipEngine->addRelationship(r.fromId, r.toId, r.type, r.context, r.confidence);
        }
        if (progressDialog)
            progressDialog->updateProgress(fileName, pair.second.size());
        if (relationshipAnalysisTracker.isActive)
            relationshipAnalysisTracker.processedFiles++;
    }
    relationshipEngine->endUpdate();
    if (result.semanticSnapshot) {
        SemanticIndex::getInstance()->setSnapshot(result.semanticSnapshot);
        CompletionManager::getInstance()->refreshRelationshipData();
    }
    if (relationshipAnalysisTracker.isActive && relationshipAnalysisTracker.processedFiles >= relationshipAnalysisTracker.totalFiles) {
        relationshipAnalysisTracker.isActive = false;
        if (progressDialog) {
            progressDialog->statusLabel->setText("All analysis complete!");
            if (progressDialog->config.showDetails) {
                progressDialog->logProgress("Relationship analysis complete!");
                progressDialog->logProgress(QString("Processed %1 files")
                    .arg(relationshipAnalysisTracker.totalFiles));
            }
        }
        QTimer::singleShot(200, this, [this]() {
            if (progressDialog)
                progressDialog->finishAnalysis();
            if (statusBar())
                statusBar()->showMessage(
                    QString("Relationship analysis complete: %1 files")
                    .arg(relationshipAnalysisTracker.totalFiles),
                    5000);
        });
    }
}

void MainWindow::showAnalysisProgress(const QStringList& files)
{
    Q_UNUSED(files)
    if (progressDialog) {
        progressDialog->disconnect();
        progressDialog->deleteLater();
        progressDialog = nullptr;
    }

    progressDialog = new RelationshipProgressDialog(this);

    progressDialog->setAutoClose(false);
    progressDialog->setMinimumDuration(0);
    progressDialog->setShowDetails(true);

    connect(progressDialog, &RelationshipProgressDialog::cancelled,
            this, [this]() {
                symbolAnalysisCancelled.store(true);
                if (analysisScheduler) {
                    analysisScheduler->cancelWorkspaceRelationshipAnalysis();
                }

                relationshipAnalysisTracker.isActive = false;

                if (statusBar()) {
                    statusBar()->showMessage("Analysis cancelled", 3000);
                }
            });

    connect(progressDialog, &RelationshipProgressDialog::finished,
            this, [this]() {
                if (statusBar()) {
                    statusBar()->showMessage("Symbol relationship analysis complete", 3000);
                }
            });

    progressDialog->startAnalysis(files.size());

    progressDialog->statusLabel->setText("Initializing analysis environment...");
    progressDialog->currentFileLabel->setText(QString("Preparing to analyze %1 SystemVerilog files").arg(files.size()));
    progressDialog->progressBar->setFormat("Initializing...");

    if (progressDialog->config.showDetails) {
        progressDialog->logProgress("System initialization complete");
        progressDialog->logProgress("Loading analysis components...");
    }

    progressDialog->update();
    progressDialog->repaint();
}

void MainWindow::hideAnalysisProgress()
{
    if (progressDialog && progressDialog->isVisible()) {
        progressDialog->hide();
    }
}

