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
#include "symbolrelationshipengine.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "searchservice.h"
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
#include <QTimer>

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
        [this](const ProjectSnapshot& project) {
            QVector<QPair<QString, QVector<RelationshipToAdd>>> out;
            if (!relationshipBuilder)
                return out;

            relationshipBuilder->resetCancellation();
            const QStringList svFiles = project.systemVerilogFiles;
            out.reserve(svFiles.size());
            for (const QString& filePath : svFiles) {
                if (relationshipBuilder->isCancelled())
                    break;
                QFile file(filePath);
                if (!file.open(QIODevice::ReadOnly | QFile::Text))
                    continue;
                const QString content = QTextStream(&file).readAll();
                SearchQuery query;
                query.fileName = filePath;
                QList<sym_list::SymbolInfo> fs;
                const QList<SearchResult> results = SearchService::getInstance()->findSymbols(query);
                fs.reserve(results.size());
                for (const SearchResult& result : results)
                    fs.append(result.symbol);
                out.append({filePath, relationshipBuilder->computeRelationships(filePath, content, fs)});
            }
            return out;
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
                MyCodeEditor* editor = tabManager->getCurrentEditor();
                if (!editor || editor->getFileName() != fileName) return;
                editor->refreshScopeAndCurrentLineHighlight();
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
                }
            });

    connect(tabManager.get(), &TabManager::activeTabChanged,
            this, [this](MyCodeEditor* editor) {
                if (editor && navigationManager) {
                    navigationManager->onTabChanged(editor->getFileName());
                }
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

void MainWindow::navigateToFileAndLine(const QString& filePath, int lineNumber)
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

    relationshipSingleFileWatcher = new QFutureWatcher<QVector<RelationshipToAdd>>(this);
    connect(relationshipSingleFileWatcher, &QFutureWatcher<QVector<RelationshipToAdd>>::finished,
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
        QFuture<QVector<RelationshipToAdd>> oldFuture = relationshipSingleFileWatcher->future();
        relationshipSingleFileWatcher->cancel();
        oldFuture.waitForFinished();
    }
    pendingRelationshipFileName = fileName;
    relationshipBuilder->resetCancellation();
    QFuture<QVector<RelationshipToAdd>> future = QtConcurrent::run([this, fileName, content]() {
        SearchQuery query;
        query.fileName = fileName;
        QList<sym_list::SymbolInfo> fs;
        const QList<SearchResult> results = SearchService::getInstance()->findSymbols(query);
        fs.reserve(results.size());
        for (const SearchResult& result : results)
            fs.append(result.symbol);
        return relationshipBuilder->computeRelationships(fileName, content, fs);
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
    QVector<RelationshipToAdd> results = relationshipSingleFileWatcher->result();
    relationshipEngine->beginUpdate();
    for (const RelationshipToAdd& r : results) {
        if (r.fromId < 0 || r.toId < 0)
            continue;
        relationshipEngine->addRelationship(r.fromId, r.toId, r.type, r.context, r.confidence);
    }
    relationshipEngine->endUpdate();
    onRelationshipAnalysisCompleted(fileName, results.size());
}

void MainWindow::onWorkspaceRelationshipAnalysisFinished(
    const QVector<QPair<QString, QVector<RelationshipToAdd>>>& allResults)
{
    if (!relationshipEngine || !relationshipBuilder)
        return;

    relationshipEngine->beginUpdate();
    for (const auto& pair : allResults) {
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

