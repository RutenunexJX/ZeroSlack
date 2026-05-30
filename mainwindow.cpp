#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "completionmanager.h"

#include "tabmanager.h"
#include "workspacemanager.h"
#include "modemanager.h"
#include "symbolanalyzer.h"
#include "navigationmanager.h"
#include "navigationwidget.h"
#include "symbolrelationshipengine.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "syminfo.h"
#include "sv_treesitter_parser.h"
#include "version.h"
#include <QtConcurrent/QtConcurrent>
#include <QLabel>
#include <QStatusBar>

#include <slang/syntax/SyntaxTree.h>
#include <slang/syntax/SyntaxNode.h>
#include <slang/diagnostics/Diagnostics.h>
#include <slang/parsing/Token.h>

#include <functional>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>
#include <QDebug>

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

    setupRelationshipEngine();
    setupNavigationPane();
    setupManagerConnections();
    connectNavigationSignals();

    setupDebugButton();

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
    // 优化：打开文件时只分析新打开的文件，且延后到下一事件循环，避免卡顿
    connect(tabManager.get(), &TabManager::tabCreated,
            this, [this](MyCodeEditor* editor) {
                if (!editor) return;
                QString fileName = editor->getFileName();
                QString content = editor->toPlainText();
                // 延后执行，先让标签页显示出来，再在后台做符号/关系分析
                QTimer::singleShot(0, this, [this, fileName, content]() {
                    symbolAnalyzer->analyzeFileContent(fileName, content);
                    MyCodeEditor* ed = tabManager->getCurrentEditor();
                    if (ed && ed->getFileName() == fileName)
                        ed->refreshScopeAndCurrentLineHighlight();
                    if (!fileName.isEmpty())
                        requestSingleFileRelationshipAnalysis(fileName, content);
                });
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

    connect(tabManager.get(), &TabManager::fileSaved,
            this, [this](const QString& fileName) {
                MyCodeEditor* editor = tabManager->getCurrentEditor();
                QString content = (editor && editor->getFileName() == fileName)
                    ? editor->toPlainText() : QString();
                if (!content.isEmpty() && !sym_list::getInstance()->contentAffectsSymbols(fileName, content)) {
                    if (editor && editor->getFileName() == fileName)
                        editor->refreshScopeAndCurrentLineHighlight();
                    return;
                }
                // 仅重分析当前保存的文件，避免 analyzeOpenTabs 重分析所有标签导致卡顿
                symbolAnalyzer->analyzeFileContent(fileName, content);
                if (editor && editor->getFileName() == fileName) {
                    editor->refreshScopeAndCurrentLineHighlight();
                    QTimer::singleShot(0, editor, [editor]() { editor->refreshScopeAndCurrentLineHighlight(); });
                }
                if (editor && editor->getFileName() == fileName)
                    requestSingleFileRelationshipAnalysis(fileName, editor->toPlainText());
            });

    connect(workspaceManager.get(), &WorkspaceManager::workspaceOpened,
            this, [this](const QString& workspacePath) {
                Q_UNUSED(workspacePath)
                QStringList svFiles = workspaceManager->getSystemVerilogFiles();

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
                if (fileChangeDebounceTimers.contains(filePath)) {
                    QTimer* oldTimer = fileChangeDebounceTimers.take(filePath);
                    oldTimer->stop();
                    oldTimer->deleteLater();
                }
                QTimer* timer = new QTimer(this);
                timer->setSingleShot(true);
                connect(timer, &QTimer::timeout, this, [this, timer, filePath]() {
                    fileChangeDebounceTimers.remove(filePath);
                    timer->deleteLater();
                    symbolAnalyzer->analyzeFile(filePath);
                    QFile file(filePath);
                    if (file.open(QIODevice::ReadOnly | QFile::Text)) {
                        QString content = QTextStream(&file).readAll();
                        file.close();
                        requestSingleFileRelationshipAnalysis(filePath, content);
                    }
                });
                fileChangeDebounceTimers[filePath] = timer;
                timer->start(kFileChangeDebounceMs);
            });

    connect(workspaceManager.get(), &WorkspaceManager::filesScanned,
            this, [this](const QStringList& svFiles) {
                Q_UNUSED(svFiles)
                symbolAnalysisCancelled.store(false);
                symbolAnalyzer->startAnalyzeWorkspaceAsync(workspaceManager.get(),
                    [this]() { return symbolAnalysisCancelled.load(); });
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
                QStringList svFiles = workspaceManager->getSystemVerilogFiles();
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
                if (relationshipBuilder && !svFiles.isEmpty()) {
                    relationshipAnalysisTracker.totalFiles = svFiles.size();
                    relationshipAnalysisTracker.processedFiles = 0;
                    relationshipAnalysisTracker.isActive = true;
                    if (relationshipBatchWatcher && relationshipBatchWatcher->isRunning())
                        relationshipBatchWatcher->cancel();
                    QFuture<QVector<QPair<QString, QVector<RelationshipToAdd>>>> batchFuture =
                        QtConcurrent::run([this, svFiles]() {
                            QVector<QPair<QString, QVector<RelationshipToAdd>>> out;
                            out.reserve(svFiles.size());
                            sym_list* db = sym_list::getInstance();
                            for (const QString& filePath : svFiles) {
                                if (relationshipBuilder->isCancelled()) break;
                                QFile file(filePath);
                                if (!file.open(QIODevice::ReadOnly | QFile::Text)) continue;
                                QString content = QTextStream(&file).readAll();
                                file.close();
                                QList<sym_list::SymbolInfo> fs = db->findSymbolsByFileName(filePath);
                                out.append({filePath, relationshipBuilder->computeRelationships(filePath, content, fs)});
                            }
                                return out;
                        });
                    relationshipBatchWatcher->setFuture(batchFuture);
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
    navigateToFileAndLine(symbol.fileName, symbol.startLine + 1); // +1 because lines are 0-based
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

    relationshipBatchWatcher = new QFutureWatcher<QVector<QPair<QString, QVector<RelationshipToAdd>>>>(this);
    connect(relationshipBatchWatcher, &QFutureWatcher<QVector<QPair<QString, QVector<RelationshipToAdd>>>>::finished,
            this, &MainWindow::onBatchRelationshipFinished);

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
    if (fileName.isEmpty() || !relationshipBuilder || !relationshipEngine)
        return;
    if (!relationshipSingleFileWatcher)
        return;
    // 阶段 C：仅当结构/定义有显著变更时才触发关系重构，跳过仅注释/空白变更
    if (symbolAnalyzer) {
        QString lastContent = lastRelationshipAnalysisContent.value(fileName);
        if (!lastContent.isNull() && !symbolAnalyzer->hasSignificantChanges(lastContent, content))
            return;
    }
    lastRelationshipAnalysisContent.insert(fileName, content);
    // 避免快速连续 setFuture 导致崩溃：先等待当前任务结束再提交新任务（fileSaved + fileChanged + 去抖定时器可能同时触发）
    if (relationshipSingleFileWatcher->isRunning()) {
        QFuture<QVector<RelationshipToAdd>> oldFuture = relationshipSingleFileWatcher->future();
        relationshipSingleFileWatcher->cancel();
        oldFuture.waitForFinished();
    }
    pendingRelationshipFileName = fileName;
    QFuture<QVector<RelationshipToAdd>> future = QtConcurrent::run([this, fileName, content]() {
        sym_list* db = sym_list::getInstance();
        QList<sym_list::SymbolInfo> fs = db->findSymbolsByFileName(fileName);
        return relationshipBuilder->computeRelationships(fileName, content, fs);
    });
    relationshipSingleFileWatcher->setFuture(future);
}

void MainWindow::scheduleOpenFileAnalysis(const QString& fileName, int delayMs)
{
    if (fileName.isEmpty() || !symbolAnalyzer || !tabManager)
        return;
    cancelScheduledOpenFileAnalysis(fileName);
    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(delayMs);
    connect(timer, &QTimer::timeout, this, [this, fileName, timer]() {
        QString content = tabManager->getPlainTextFromOpenFile(fileName);
        if (!content.isNull())
            symbolAnalyzer->analyzeFileContent(fileName, content);
        if (openFileAnalysisTimers.value(fileName) == timer)
            openFileAnalysisTimers.remove(fileName);
        timer->deleteLater();
    });
    openFileAnalysisTimers[fileName] = timer;
    timer->start();
}

void MainWindow::cancelScheduledOpenFileAnalysis(const QString& fileName)
{
    auto it = openFileAnalysisTimers.find(fileName);
    if (it != openFileAnalysisTimers.end()) {
        if (it.value()) {
            it.value()->stop();
            it.value()->deleteLater();
        }
        openFileAnalysisTimers.erase(it);
    }
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

void MainWindow::onBatchRelationshipFinished()
{
    if (!relationshipBatchWatcher || !relationshipEngine || !relationshipBuilder)
        return;
    if (relationshipBatchWatcher->isCanceled())
        return;
    QVector<QPair<QString, QVector<RelationshipToAdd>>> allResults = relationshipBatchWatcher->result();
    relationshipEngine->beginUpdate();
    for (const auto& pair : allResults) {
        const QString& fileName = pair.first;
        for (const RelationshipToAdd& r : pair.second)
            relationshipEngine->addRelationship(r.fromId, r.toId, r.type, r.context, r.confidence);
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
                if (relationshipBuilder) {
                    relationshipBuilder->cancelAnalysis();
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

void MainWindow::setupDebugButton()
{
    debugButton = new QPushButton("Tree-sitter 验证", this);

    // 添加到工具栏或菜单栏
    ui->toolBar->addWidget(debugButton);

    connect(debugButton, &QPushButton::clicked, this, &MainWindow::onDebug0);
}

void MainWindow::onDebug0()
{
    // 1. Get current editor and content
    MyCodeEditor *editor = tabManager->getCurrentEditor();
    if (!editor) {
        qDebug() << "[Verification] Error: No active editor found.";
        return;
    }

    QString content = editor->toPlainText();
    if (content.isEmpty()) {
        qDebug() << "[Verification] Warning: Editor is empty.";
        return;
    }

    QString fileName = editor->getFileName();
    qDebug() << "========================================";
    qDebug() << "   Starting Verification: " << fileName;
    qDebug() << "========================================";

    // --- Slang Analysis ---
    qDebug() << "[Slang] Parsing content...";
    QElapsedTimer timer;
    timer.start();

    qint64 slangTime = 0;
    QString slangRootName;
    QString slangSummary;  // 解析到的内容摘要，便于和 Tree-sitter 对比
    size_t slangDiagnosticsCount = 0;

    try {
        std::string src = content.toStdString();
        std::string nameStr = fileName.toStdString();
        // 显式 string_view 以消除与 fromText(text, Bag&, name, path) 重载的歧义
        auto tree = slang::syntax::SyntaxTree::fromText(
            std::string_view(src),
            std::string_view(nameStr),
            std::string_view{});

        slangTime = timer.nsecsElapsed() / 1000; // microseconds

        const auto& root = tree->root();
        auto rootKind = root.kind;
        slangRootName = QString::fromStdString(std::string(slang::syntax::toString(rootKind)));

        // 递归收集语法树摘要：节点类型 + 可选名称（首 token 文本），便于看出具体识别到了什么
        QStringList detailLines;
        const int maxDepth = 4;
        const int maxLines = 35;
        std::function<void(const slang::syntax::SyntaxNode&, int)> collectDetail;
        collectDetail = [&](const slang::syntax::SyntaxNode& node, int depth) {
            if (detailLines.size() >= maxLines) return;
            QString kindStr = QString::fromStdString(std::string(slang::syntax::toString(node.kind)));
            QString indent(depth * 2, QChar(' '));
            QString nameHint;
            try {
                auto tok = node.getFirstToken();
                if (tok.valid()) {
                    std::string_view raw = tok.rawText();
                    if (!raw.empty() && raw.size() <= 64) {
                        QString t = QString::fromUtf8(raw.data(), int(raw.size()));
                        if (t.contains(QRegularExpression("^[a-zA-Z_][a-zA-Z0-9_]*$"))) {
                            nameHint = " \"" + t + "\"";
                        }
                    }
                }
            } catch (...) {}
            if (depth == 0) {
                detailLines.append(kindStr + nameHint);
            } else {
                detailLines.append(indent + kindStr + nameHint);
            }
            if (depth >= maxDepth) return;
            const size_t maxChildren = 12;
            size_t n = node.getChildCount();
            for (size_t i = 0; i < n && detailLines.size() < maxLines; ++i) {
                if (i >= maxChildren) {
                    detailLines.append(indent + "  ... (+" + QString::number(n - maxChildren) + " 项)");
                    break;
                }
                const slang::syntax::SyntaxNode* child = node.childNode(i);
                if (child)
                    collectDetail(*child, depth + 1);
            }
        };
        collectDetail(root, 0);
        slangSummary = detailLines.join("\n");

        qDebug() << "[Slang] Parse completed in" << slangTime << "us";
        qDebug() << "[Slang] Root Node:" << slangRootName;
        qDebug() << "[Slang] Parsed:" << slangSummary;

        auto& diagnostics = tree->diagnostics();
        slangDiagnosticsCount = diagnostics.size();

        if (diagnostics.empty()) {
            qDebug() << "[Slang] Diagnostics: OK (No errors)";
        } else {
            qDebug() << "[Slang] Diagnostics found:" << slangDiagnosticsCount;
            for (const auto& diag : diagnostics) {
                qDebug() << "[Slang] Error Code:" << QString::fromStdString(std::string(slang::toString(diag.code)));
            }
        }

    } catch (const std::exception& e) {
        qDebug() << "[Slang] EXCEPTION:" << e.what();
    }

    // --- Tree-sitter Analysis ---
    SVTreeSitterParser parser;
    timer.restart();
    parser.parse(content);
    qint64 tsTime = timer.nsecsElapsed() / 1000; // microseconds
    QList<sym_list::SymbolInfo> symbols = parser.getSymbols();

    // --- Summary ---
    QString summary = QString("Verification Complete\n\n"
                              "File: %1\n\n"
                              "--- Slang ---\n"
                              "Time: %2 us\n"
                              "Parsed: %3\n"
                              "Diagnostics: %4\n\n"
                              "--- Tree-sitter ---\n"
                              "Time: %5 us\n"
                              "Symbols: %6\n\n"
                              "Check Application Output for details.")
                      .arg(QFileInfo(fileName).fileName())
                      .arg(slangTime)
                      .arg(slangSummary)
                      .arg(slangDiagnosticsCount)
                      .arg(tsTime)
                      .arg(symbols.size());

    QMessageBox::information(this, "Parser Verification", summary);
}

