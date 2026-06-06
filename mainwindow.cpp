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

    // 版本号显示：窗口标题 + 状态栏右下角常驻标签，便于确认当前运行的是哪一版构建。
    setWindowTitle(QStringLiteral("ZeroSlack  %1").arg(QLatin1String(APP_VERSION)));
    if (statusBar()) {
        QLabel* versionLabel = new QLabel(
            QStringLiteral("v%1").arg(QLatin1String(APP_VERSION)), this);
        versionLabel->setToolTip(
            QStringLiteral("ZeroSlack %1\n构建于 %2")
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
    analysisScheduler->setSymbolAnalyzer(symbolAnalyzer.get());
    analysisScheduler->setOpenFileContentProvider([this](const QString& fileName) {
        return tabManager ? tabManager->getPlainTextFromOpenFile(fileName) : QString();
    });
    analysisScheduler->setWorkspaceOpenProvider([this]() {
        return workspaceManager && workspaceManager->isWorkspaceOpen();
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
            sym_list* db = sym_list::getInstance();
            for (const QString& filePath : svFiles) {
                if (relationshipBuilder->isCancelled())
                    break;
                QFile file(filePath);
                if (!file.open(QIODevice::ReadOnly | QFile::Text))
                    continue;
                const QString content = QTextStream(&file).readAll();
                QList<sym_list::SymbolInfo> fs = db->findSymbolsByFileName(filePath);
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

    connect(workspaceManager.get(), &WorkspaceManager::workspaceOpened,
            this, [this](const QString& workspacePath) {
                Q_UNUSED(workspacePath)
                const ProjectSnapshot project = workspaceManager->projectSnapshot();
                QStringList svFiles = project.systemVerilogFiles;

                showAnalysisProgress(svFiles);

                QTimer::singleShot(10, this, [this, svFiles]() {
                    if (progressDialog) {
                        progressDialog->statusLabel->setText("阶段 1/2: 符号分析进行中...");
                        progressDialog->currentFileLabel->setText("正在扫描和解析SystemVerilog文件结构...");
                        progressDialog->progressBar->setFormat("符号分析中... 请稍候");

                        if (progressDialog->config.showDetails) {
                            progressDialog->logProgress("📊 开始符号分析阶段...");
                            progressDialog->logProgress(QString("📁 扫描到 %1 个SV文件").arg(svFiles.size()));
                        }

                        progressDialog->update();
                        progressDialog->repaint();
                    }
                });
            });
    connect(workspaceManager.get(), &WorkspaceManager::fileChanged,
            this, [this](const QString& filePath) {
                if (analysisScheduler)
                    analysisScheduler->handleExternalFileChanged(filePath, kFileChangeDebounceMs);
            });

    connect(workspaceManager.get(), &WorkspaceManager::filesScanned,
            this, [this](const QStringList& svFiles) {
                Q_UNUSED(svFiles)
                symbolAnalysisCancelled.store(false);
                symbolAnalyzer->startAnalyzeProjectAsync(workspaceManager->projectSnapshot(),
                    [this]() { return symbolAnalysisCancelled.load(); });
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
                        QString("符号分析: %1 / %2 — %3").arg(filesDone).arg(totalFiles).arg(shortName));
                }
            });

    connect(symbolAnalyzer.get(), &SymbolAnalyzer::batchAnalysisCompleted,
            this, [this](int filesAnalyzed, int totalSymbols) {
                if (statusBar()) {
                    statusBar()->showMessage(
                        QString("符号分析完成: %1个文件, %2个符号 - 关系分析进行中...")
                        .arg(filesAnalyzed).arg(totalSymbols),
                        3000);
                }
                const ProjectSnapshot project = workspaceManager->projectSnapshot();
                QStringList svFiles = project.systemVerilogFiles;
                if (progressDialog) {
                    progressDialog->statusLabel->setText("阶段 2/2: 关系分析进行中...");
                    progressDialog->currentFileLabel->setText("正在分析文件间的符号依赖关系...");
                    progressDialog->progressBar->setFormat(QString("%v / %1 文件 (%p%)").arg(svFiles.size()));
                    if (progressDialog->config.showDetails) {
                        progressDialog->logProgress("🔗 开始关系分析阶段...");
                        progressDialog->logProgress("🔍 分析模块实例化关系...");
                        progressDialog->logProgress("🔍 分析变量赋值关系...");
                        progressDialog->logProgress("🔍 分析任务/函数调用关系...");
                    }
                    progressDialog->update();
                    progressDialog->repaint();
                }
                if (analysisScheduler && relationshipBuilder && !svFiles.isEmpty())
                    analysisScheduler->requestWorkspaceRelationshipAnalysis(project);
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
                            QString("✅ %1: 发现 %2 个关系").arg(shortName).arg(relationshipsFound));
                    }
                }

                if (relationshipAnalysisTracker.isActive) {
                    relationshipAnalysisTracker.processedFiles++;

                    if (progressDialog) {
                        progressDialog->statusLabel->setText(
                            QString("阶段 2/2: 关系分析进行中 (%1/%2)")
                            .arg(relationshipAnalysisTracker.processedFiles)
                            .arg(relationshipAnalysisTracker.totalFiles));
                    }

                    if (relationshipAnalysisTracker.processedFiles >= relationshipAnalysisTracker.totalFiles) {
                        relationshipAnalysisTracker.isActive = false;

                        if (progressDialog) {
                            progressDialog->statusLabel->setText("🎉 所有分析完成！");
                            if (progressDialog->config.showDetails) {
                                progressDialog->logProgress("🎉 关系分析全部完成！");
                                progressDialog->logProgress(QString("📊 总计处理 %1 个文件")
                                    .arg(relationshipAnalysisTracker.totalFiles));
                            }
                        }

                        QTimer::singleShot(200, this, [this]() {
                            if (progressDialog) {
                                progressDialog->finishAnalysis();
                            }

                            if (statusBar()) {
                                statusBar()->showMessage(
                                    QString("关系分析完成: %1个文件")
                                    .arg(relationshipAnalysisTracker.totalFiles),
                                    5000);
                            }
                        });
                    }
                }

                QString shortName = QFileInfo(fileName).fileName();
                if (statusBar()) {
                    statusBar()->showMessage(
                        QString("关系分析: %1 (%2个关系)")
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
                    statusBar()->showMessage("关系分析已取消", 3000);
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

    navigationDock = new QDockWidget("导航", this);
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
    navigateToFileAndLine(symbol.fileName, symbol.startLine); // startLine 为 1-based，navigateToFileAndLine 也按 1-based
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
            return; // 无法打开文件
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
            "warning",
            "file do not save, quit?",
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
    // 避免快速连续 setFuture 导致崩溃：先等待当前任务结束再提交新任务（fileSaved + fileChanged + 去抖定时器可能同时触发）
    if (relationshipSingleFileWatcher->isRunning()) {
        QFuture<QVector<RelationshipToAdd>> oldFuture = relationshipSingleFileWatcher->future();
        relationshipSingleFileWatcher->cancel();
        oldFuture.waitForFinished();
    }
    pendingRelationshipFileName = fileName;
    relationshipBuilder->resetCancellation();
    QFuture<QVector<RelationshipToAdd>> future = QtConcurrent::run([this, fileName, content]() {
        sym_list* db = sym_list::getInstance();
        QList<sym_list::SymbolInfo> fs = db->findSymbolsByFileName(fileName);
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
            progressDialog->statusLabel->setText("🎉 所有分析完成！");
            if (progressDialog->config.showDetails) {
                progressDialog->logProgress("🎉 关系分析全部完成！");
                progressDialog->logProgress(QString("📊 总计处理 %1 个文件")
                    .arg(relationshipAnalysisTracker.totalFiles));
            }
        }
        QTimer::singleShot(200, this, [this]() {
            if (progressDialog)
                progressDialog->finishAnalysis();
            if (statusBar())
                statusBar()->showMessage(
                    QString("关系分析完成: %1个文件")
                    .arg(relationshipAnalysisTracker.totalFiles),
                    5000);
        });
    }
}

void MainWindow::showAnalysisProgress(const QStringList& files)
{
    Q_UNUSED(files)
    // 如果已有对话框，先清理
    if (progressDialog) {
        progressDialog->disconnect();
        progressDialog->deleteLater();
        progressDialog = nullptr;
    }

    progressDialog = new RelationshipProgressDialog(this);

    // 配置对话框
    progressDialog->setAutoClose(false);
    progressDialog->setMinimumDuration(0);
    progressDialog->setShowDetails(true);

    // 连接信号
    connect(progressDialog, &RelationshipProgressDialog::cancelled,
            this, [this]() {
                symbolAnalysisCancelled.store(true);
                if (analysisScheduler) {
                    analysisScheduler->cancelWorkspaceRelationshipAnalysis();
                }

                relationshipAnalysisTracker.isActive = false;

                if (statusBar()) {
                    statusBar()->showMessage("分析已取消", 3000);
                }
            });

    connect(progressDialog, &RelationshipProgressDialog::finished,
            this, [this]() {
                if (statusBar()) {
                    statusBar()->showMessage("符号关系分析完成", 3000);
                }
            });

    progressDialog->startAnalysis(files.size());

    // 立即更新UI内容，不使用定时器
    progressDialog->statusLabel->setText("正在初始化分析环境...");
    progressDialog->currentFileLabel->setText(QString("准备分析 %1 个SystemVerilog文件").arg(files.size()));
    progressDialog->progressBar->setFormat("初始化中...");

    if (progressDialog->config.showDetails) {
        progressDialog->logProgress("🚀 系统初始化完成");
        progressDialog->logProgress("⏳ 正在加载分析组件...");
    }

    // 强制刷新显示（阶段 A：不再在此处调用 processEvents）
    progressDialog->update();
    progressDialog->repaint();
}

void MainWindow::hideAnalysisProgress()
{
    if (progressDialog && progressDialog->isVisible()) {
        progressDialog->hide();
    }
}

