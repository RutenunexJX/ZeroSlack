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
#include "smartrelationshipbuilder.h"
#include "syminfo.h"
#include <QtConcurrent/QtConcurrent>

#include <QMessageBox>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextStream>
#include <QFileInfo>
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
    // TabManager connections
    // 优化：打开文件时只分析新打开的文件，且延后到下一事件循环，避免卡顿
    connect(tabManager.get(), &TabManager::tabCreated,
            this, [this](MyCodeEditor* editor) {
                if (!editor) return;
                connect(editor, &MyCodeEditor::debugScopeInfo, this, &MainWindow::onDebugScopeInfo);
                if (tabManager->getCurrentEditor() == editor) editor->refreshDebugScopeInfo();
                QString fileName = editor->getFileName();
                QString content = editor->toPlainText();
                // 延后执行，先让标签页显示出来，再在后台做符号/关系分析
                QTimer::singleShot(0, this, [this, fileName, content]() {
                    // 只分析新打开的这一个文件，不再重分析所有已打开标签
                    symbolAnalyzer->analyzeFileContent(fileName, content);
                    if (!fileName.isEmpty())
                        requestSingleFileRelationshipAnalysis(fileName, content);
                });
            });

    connect(tabManager.get(), &TabManager::activeTabChanged,
            this, [this](MyCodeEditor* editor) {
                if (editor) editor->refreshDebugScopeInfo();
            });

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
                if (!content.isEmpty() && !sym_list::getInstance()->contentAffectsSymbols(fileName, content))
                    return;
                symbolAnalyzer->analyzeOpenTabs(tabManager.get());

                if (editor && editor->getFileName() == fileName)
                    requestSingleFileRelationshipAnalysis(fileName, editor->toPlainText());
            });

    // WorkspaceManager connections
    connect(workspaceManager.get(), &WorkspaceManager::workspaceOpened,
            this, [this](const QString& workspacePath) {
                Q_UNUSED(workspacePath)
                QStringList svFiles = workspaceManager->getSystemVerilogFiles();

                // 显示进度对话框
                showAnalysisProgress(svFiles);

                // 🔧 关键修复：立即更新进度对话框内容，不等待
                QTimer::singleShot(10, this, [this, svFiles]() {
                    if (progressDialog) {
                        // 立即显示符号分析阶段
                        progressDialog->statusLabel->setText("阶段 1/2: 符号分析进行中...");
                        progressDialog->currentFileLabel->setText("正在扫描和解析SystemVerilog文件结构...");
                        progressDialog->progressBar->setFormat("符号分析中... 请稍候");

                        if (progressDialog->config.showDetails) {
                            progressDialog->logProgress("📊 开始符号分析阶段...");
                            progressDialog->logProgress(QString("📁 扫描到 %1 个SV文件").arg(svFiles.size()));
                        }

                        // 强制刷新UI（阶段 A：不再在此处调用 processEvents，避免阻塞）
                        progressDialog->update();
                        progressDialog->repaint();
                    }
                });

                // 符号分析由 filesScanned 统一触发（openWorkspace 会先发 workspaceOpened 再发 filesScanned，避免重复启动两次）
            });
    connect(workspaceManager.get(), &WorkspaceManager::fileChanged,
            this, [this](const QString& filePath) {
                // 防抖：保存时 QFileSystemWatcher 常会触发两次，只对最后一次做一次分析
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

    // ModeManager connections
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
                Q_UNUSED(fileName)
                Q_UNUSED(symbolCount)
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
                // 阶段 A：符号分析在后台完成后，于主线程启动关系分析（依赖符号表）
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

    // NEW: Connect managers to navigation manager
    navigationManager->connectToTabManager(tabManager.get());
    navigationManager->connectToWorkspaceManager(workspaceManager.get());
    navigationManager->connectToSymbolAnalyzer(symbolAnalyzer.get());

    // 🔧 FIX: 确保relationshipBuilder存在再连接
    if (!relationshipBuilder)
        return;

    // 🚀 连接SmartRelationshipBuilder信号（基于实际存在的信号）
    connect(relationshipBuilder.get(), &SmartRelationshipBuilder::analysisCompleted,
            this, [this](const QString& fileName, int relationshipsFound) {
                if (progressDialog) {
                    progressDialog->updateProgress(fileName, relationshipsFound);

                    // 【新增】在进度对话框中显示当前处理的文件信息
                    QString shortName = QFileInfo(fileName).fileName();
                    if (progressDialog->config.showDetails) {
                        progressDialog->logProgress(
                            QString("✅ %1: 发现 %2 个关系").arg(shortName).arg(relationshipsFound));
                    }
                }

                // 手动跟踪批量分析进度
                if (relationshipAnalysisTracker.isActive) {
                    relationshipAnalysisTracker.processedFiles++;

                    // 【新增】更新进度对话框的状态显示
                    if (progressDialog) {
                        progressDialog->statusLabel->setText(
                            QString("阶段 2/2: 关系分析进行中 (%1/%2)")
                            .arg(relationshipAnalysisTracker.processedFiles)
                            .arg(relationshipAnalysisTracker.totalFiles));
                    }

                    // 检查是否所有文件都分析完成
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

                        // 延迟一点确保最后的updateProgress调用完成
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

                // 状态栏更新
                QString shortName = QFileInfo(fileName).fileName();
                if (statusBar()) {
                    statusBar()->showMessage(
                        QString("关系分析: %1 (%2个关系)")
                        .arg(shortName).arg(relationshipsFound),
                        1000);
                }
            });

    // 🚀 连接分析错误信号
    connect(relationshipBuilder.get(), &SmartRelationshipBuilder::analysisError,
            this, [this](const QString& fileName, const QString& error) {
                Q_UNUSED(fileName)
                Q_UNUSED(error)
                if (progressDialog && progressDialog->isVisible()) {
                    progressDialog->showError(fileName, error);
                }

                // 🔧 FIX: 错误也算作处理完成，避免进度卡住
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

    // 🚀 连接取消信号
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

    // 🔧 FIX: 添加关系引擎信号连接
    if (relationshipEngine) {
        connect(relationshipEngine.get(), &SymbolRelationshipEngine::relationshipAdded,
                this, &MainWindow::onRelationshipAdded);

        connect(relationshipEngine.get(), &SymbolRelationshipEngine::relationshipsCleared,
                this, &MainWindow::onRelationshipsCleared);
    }
}


void MainWindow::setupNavigationPane()
{
    // 创建导航widget
    navigationWidget = new NavigationWidget(this);

    // 创建dock widget
    navigationDock = new QDockWidget("导航", this);
    navigationDock->setWidget(navigationWidget);
    navigationDock->setFeatures(QDockWidget::DockWidgetMovable |
                               QDockWidget::DockWidgetFloatable |
                               QDockWidget::DockWidgetClosable);

    // 设置dock widget的大小策略
    navigationDock->setMinimumWidth(200);
    navigationDock->setMaximumWidth(400);
    navigationWidget->setMinimumWidth(180);

    // 将dock添加到左侧
    addDockWidget(Qt::LeftDockWidgetArea, navigationDock);

    // 将navigation widget连接到navigation manager
    navigationManager->setNavigationWidget(navigationWidget);
}

void MainWindow::connectNavigationSignals()
{
    // 连接导航请求信号
    connect(navigationManager.get(), &NavigationManager::navigationRequested,
            this, &MainWindow::onNavigationRequested);

    connect(navigationManager.get(), &NavigationManager::symbolNavigationRequested,
            this, &MainWindow::onSymbolNavigationRequested);

    // Ctrl+Click 跳转到定义（跨文件）：编辑器发出 definitionJumpRequested 后由主窗口打开文件并跳转
    connect(tabManager.get(), &TabManager::tabCreated,
            this, [this](MyCodeEditor* editor) {
                if (editor) {
                    connect(editor, &MyCodeEditor::definitionJumpRequested,
                            this, [this](const QString&, const QString& file, int line) {
                                navigateToFileAndLine(file, line);
                            });
                }
            });

    // 连接标签页变化到导航管理器
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

    // 首先尝试在已打开的标签页中找到文件
    bool fileFound = false;
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        MyCodeEditor* editor = tabManager->getEditorAt(i);
        if (editor && editor->getFileName() == filePath) {
            ui->tabWidget->setCurrentIndex(i);
            fileFound = true;
            break;
        }
    }

    // 如果文件没有打开，则打开它
    if (!fileFound) {
        if (!tabManager->openFileInTab(filePath)) {
            return; // 无法打开文件
        }
    }

    // 导航到指定行
    if (lineNumber > 0) {
        MyCodeEditor* currentEditor = tabManager->getCurrentEditor();
        if (currentEditor) {
            // 将光标移动到指定行
            QTextCursor cursor = currentEditor->textCursor();
            cursor.movePosition(QTextCursor::Start);
            for (int i = 1; i < lineNumber; ++i) {
                cursor.movePosition(QTextCursor::Down);
            }
            currentEditor->setTextCursor(cursor);
            currentEditor->centerCursor();
            currentEditor->setFocus();
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

    // 🚀 将关系引擎连接到符号数据库
    sym_list* symbolDatabase = sym_list::getInstance();
    symbolDatabase->setRelationshipEngine(relationshipEngine.get());

    // 🚀 将关系引擎连接到补全管理器
    CompletionManager* completionManager = CompletionManager::getInstance();
    completionManager->setRelationshipEngine(relationshipEngine.get());

    // 🚀 创建智能关系构建器
    relationshipBuilder = std::make_unique<SmartRelationshipBuilder>(
        relationshipEngine.get(), symbolDatabase, this);

    // 🚀 异步单文件关系分析
    relationshipSingleFileWatcher = new QFutureWatcher<QVector<RelationshipToAdd>>(this);
    connect(relationshipSingleFileWatcher, &QFutureWatcher<QVector<RelationshipToAdd>>::finished,
            this, &MainWindow::onSingleFileRelationshipFinished);

    relationshipBatchWatcher = new QFutureWatcher<QVector<QPair<QString, QVector<RelationshipToAdd>>>>(this);
    connect(relationshipBatchWatcher, &QFutureWatcher<QVector<QPair<QString, QVector<RelationshipToAdd>>>>::finished,
            this, &MainWindow::onBatchRelationshipFinished);

    // 🚀 连接关系引擎的信号
    connect(relationshipEngine.get(), &SymbolRelationshipEngine::relationshipAdded,
            this, &MainWindow::onRelationshipAdded);

    connect(relationshipEngine.get(), &SymbolRelationshipEngine::relationshipsCleared,
            this, &MainWindow::onRelationshipsCleared);

    // 🚀 连接关系构建器的信号
    connect(relationshipBuilder.get(), &SmartRelationshipBuilder::analysisCompleted,
            this, &MainWindow::onRelationshipAnalysisCompleted);

    connect(relationshipBuilder.get(), &SmartRelationshipBuilder::analysisError,
            this, &MainWindow::onRelationshipAnalysisError);
}

// 🚀 NEW: 关系引擎信号处理
void MainWindow::onRelationshipAdded(int fromSymbolId, int toSymbolId,
                                    /*SymbolRelationshipEngine::RelationType*/int type)
{
    Q_UNUSED(fromSymbolId)
    Q_UNUSED(toSymbolId)
    Q_UNUSED(type)

    // 🚀 关系添加后的处理
    CompletionManager::getInstance()->invalidateRelationshipCaches();

    // 🚀 推迟刷新导航视图：符号分析后台持写锁时，主线程若立即 refreshCurrentView() 会读 sym_list 阻塞，导致界面卡在 2/30
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
    // 🚀 关系清除后的处理
    CompletionManager::getInstance()->invalidateRelationshipCaches();

    if (navigationManager) {
        navigationManager->refreshCurrentView();
    }
}

void MainWindow::onRelationshipAnalysisCompleted(const QString& fileName, int relationshipsFound)
{
    // 🚀 刷新相关缓存
    CompletionManager::getInstance()->refreshRelationshipData();
    // 🚀 更新状态栏信息
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
    debugButton = new QPushButton("调试: 打印Symbol IDs", this);

    // 添加到工具栏或菜单栏
    ui->toolBar->addWidget(debugButton);

    connect(debugButton, &QPushButton::clicked, this, &MainWindow::onDebug0);
}

void MainWindow::onDebug0(){
    relationshipEngine->getModuleInstances(1);
}

void MainWindow::onDebugScopeInfo(const QString& currentModule, int logicCount, int structVarCount, int structTypeCount)
{
    if (sender() != tabManager->getCurrentEditor()) return;
    QString moduleDisplay = currentModule.isEmpty() ? QStringLiteral("(无模块)") : currentModule;
    QString msg = QStringLiteral("模块: %1 | logic: %2 | struct 变量: %3 | struct 类型: %4")
                      .arg(moduleDisplay).arg(logicCount).arg(structVarCount).arg(structTypeCount);
    if (statusBar()) statusBar()->showMessage(msg, 0);
}
