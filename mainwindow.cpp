#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "completionmanager.h"

#include "mycodeeditor.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "projectmodel.h"
#include "modemanager.h"
#include "symbolanalyzer.h"
#include "analysisscheduler.h"
#include "analysisprogresscoordinator.h"
#include "editorcoordinator.h"
#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "navigationmanager.h"
#include "navigationpanecoordinator.h"
#include "symbolrelationshipengine.h"
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "semanticindex.h"
#include "syminfo.h"
#include "version.h"
#include <QLabel>
#include <QStatusBar>

#include <QMessageBox>
#include <QTextCursor>
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
    analysisProgressCoordinator =
        std::unique_ptr<AnalysisProgressCoordinator>(new AnalysisProgressCoordinator(this, this));

    setupRelationshipEngine();
    setupNavigationPane();
    setupProblemsPane();
    setupReferencesPane();
    setupRelationshipsPane();
    setupEditorCoordinator();
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
        return analysisProgressCoordinator
            && analysisProgressCoordinator->isSymbolAnalysisCancelled();
    });
    analysisScheduler->setRelationshipEngine(relationshipEngine.get());
    analysisScheduler->setRelationshipBuilder(relationshipBuilder.get());
    connect(analysisScheduler.get(), &AnalysisScheduler::relationshipDataInvalidated,
            this, []() {
                CompletionManager::getInstance()->invalidateRelationshipCaches();
            });
    connect(analysisScheduler.get(), &AnalysisScheduler::relationshipDataRefreshRequested,
            this, [this]() {
                CompletionManager::getInstance()->refreshRelationshipData();
                if (navigationManager)
                    navigationManager->refreshCurrentView();
            });
    connect(analysisScheduler.get(), &AnalysisScheduler::relationshipAnalysisFinished,
            this, &MainWindow::onSingleFileRelationshipFinished);
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
    connect(analysisScheduler.get(), &AnalysisScheduler::diagnosticsRefreshRequested,
            this, [this](const QString& fileName) {
                updateProblemsPanel(fileName);
            });

    connect(workspaceManager.get(), &WorkspaceManager::fileChanged,
            this, [this](const QString& filePath) {
                if (analysisScheduler)
                    analysisScheduler->handleExternalFileChanged(filePath, kFileChangeDebounceMs);
            });

    analysisProgressCoordinator->connectToScheduler(analysisScheduler.get());
    analysisProgressCoordinator->connectToSymbolAnalyzer(symbolAnalyzer.get());
    connect(analysisProgressCoordinator.get(),
            &AnalysisProgressCoordinator::statusMessageRequested,
            this,
            [this](const QString& message, int timeoutMs) {
                if (statusBar())
                    statusBar()->showMessage(message, timeoutMs);
            });
    connect(analysisProgressCoordinator.get(),
            &AnalysisProgressCoordinator::relationshipAnalysisErrorReported,
            this,
            &MainWindow::onRelationshipAnalysisError);

    connect(analysisScheduler.get(), &AnalysisScheduler::workspaceRelationshipAnalysisCancelled,
            this, []() {});

    connect(modeManager.get(), &ModeManager::navigationToggleRequested,
                this, [this]() {
                    if (navigationPane)
                        navigationPane->toggleVisible();
                });

    connect(symbolAnalyzer.get(), &SymbolAnalyzer::analysisCompleted,
            this, [this](const QString& fileName, int symbolCount) {
                Q_UNUSED(symbolCount)
                MyCodeEditor* editor = tabManager->getCurrentEditor();
                if (!editor || editor->getFileName() != fileName) return;
                editor->refreshScopeAndCurrentLineHighlight();
            });

    navigationManager->connectToTabManager(tabManager.get());
    navigationManager->connectToWorkspaceManager(workspaceManager.get());
    navigationManager->connectToSymbolAnalyzer(symbolAnalyzer.get());

}


void MainWindow::setupNavigationPane()
{
    navigationPane = std::make_unique<NavigationPaneCoordinator>(this);
    navigationPane->attachNavigationManager(navigationManager.get());
    addDockWidget(Qt::LeftDockWidgetArea, navigationPane->dock());
}

void MainWindow::setupProblemsPane()
{
    problemsPanel = std::make_unique<ProblemsPanelCoordinator>(this);
    problemsPanel->setCurrentFileProvider([this]() {
        MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
        return editor ? editor->getFileName() : QString();
    });
    problemsPanel->setWorkspaceFilesProvider([this]() {
        return workspaceManager ? workspaceManager->getSystemVerilogFiles() : QStringList();
    });
    problemsPanel->setNavigationHandler(
        [this](const QString& fileName, int line, int column) {
            navigateToFileAndLine(fileName, line, column);
        });
    addDockWidget(Qt::BottomDockWidgetArea, problemsPanel->dock());
}

void MainWindow::setupReferencesPane()
{
    referencesPanel = std::make_unique<ReferencesPanelCoordinator>(this);
    referencesPanel->setWorkspaceFilesProvider([this]() {
        return workspaceManager ? workspaceManager->getSystemVerilogFiles() : QStringList();
    });
    referencesPanel->setNavigationHandler(
        [this](const QString& fileName, int line, int column) {
            navigateToFileAndLine(fileName, line, column);
        });
    referencesPanel->setStatusMessageHandler([this](const QString& message, int timeoutMs) {
        if (statusBar())
            statusBar()->showMessage(message, timeoutMs);
    });
    addDockWidget(Qt::BottomDockWidgetArea, referencesPanel->dock());
}

void MainWindow::setupRelationshipsPane()
{
    relationshipsPanel = std::make_unique<RelationshipsPanelCoordinator>(this);
    relationshipsPanel->setNavigationHandler(
        [this](const QString& fileName, int line, int column) {
            navigateToFileAndLine(fileName, line, column);
        });
    relationshipsPanel->setStatusMessageHandler([this](const QString& message, int timeoutMs) {
        if (statusBar())
            statusBar()->showMessage(message, timeoutMs);
    });
    addDockWidget(Qt::BottomDockWidgetArea, relationshipsPanel->dock());
}

void MainWindow::setupEditorCoordinator()
{
    editorCoordinator = std::make_unique<EditorCoordinator>(
        tabManager.get(), modeManager.get(), this);
    editorCoordinator->setIncludePathResolver(
        [this](const QString& includePath, const QString& currentFile) {
            return workspaceManager
                ? workspaceManager->resolveIncludePath(includePath, currentFile)
                : QString();
        });
    editorCoordinator->setFileOpenHandler([this](const QString& filePath) {
        return tabManager && tabManager->openFileInTab(filePath);
    });
    editorCoordinator->setDefinitionNavigationHandler(
        [this](const QString& fileName, int line) {
            navigateToFileAndLine(fileName, line);
        });
    editorCoordinator->setRelationshipAnalysisHandler(
        [this](const QString& fileName, const QString& content) {
            requestSingleFileRelationshipAnalysis(fileName, content);
        });
    editorCoordinator->setSaveFileHandler([this]() {
        on_save_file_triggered();
    });
    editorCoordinator->setSaveFileAsHandler([this]() {
        on_save_as_triggered();
    });
    editorCoordinator->setOpenFileHandler([this]() {
        on_open_file_triggered();
    });
    editorCoordinator->setNewFileHandler([this]() {
        on_new_file_triggered();
    });
    editorCoordinator->setReferenceSearchHandler(
        [this](const QString& symbolName,
               const QString& fileName,
               const QString& moduleName) {
            showReferencesForSymbol(symbolName, fileName, moduleName);
        });
    editorCoordinator->setRelationshipBrowseHandler(
        [this](const QString& symbolName,
               const QString& fileName,
               const QString& moduleName) {
            showRelationshipsForSymbol(symbolName, fileName, moduleName);
        });
    editorCoordinator->setActiveEditorChangedHandler([this](MyCodeEditor* editor) {
        if (editor && navigationManager)
            navigationManager->onTabChanged(editor->getFileName());
        if (problemsPanel && problemsPanel->scopeCombo()
            && problemsPanel->scopeCombo()->currentData().toInt() == 0) {
            updateProblemsPanel();
        }
    });
    editorCoordinator->connectSignals();
}

void MainWindow::updateProblemsPanel(const QString& fileName)
{
    if (problemsPanel)
        problemsPanel->update(fileName);
}

void MainWindow::showReferencesForSymbol(const QString& symbolName,
                                         const QString& fileName,
                                         const QString& moduleName)
{
    if (referencesPanel)
        referencesPanel->showReferencesForSymbol(symbolName, fileName, moduleName);
}

void MainWindow::refreshReferencesPanel()
{
    if (referencesPanel)
        referencesPanel->refresh();
}

void MainWindow::showRelationshipsForSymbol(const QString& symbolName,
                                            const QString& fileName,
                                            const QString& moduleName)
{
    if (relationshipsPanel)
        relationshipsPanel->showRelationshipsForSymbol(symbolName, fileName, moduleName);
}

void MainWindow::refreshRelationshipsPanel()
{
    if (relationshipsPanel)
        relationshipsPanel->refresh();
}

void MainWindow::connectNavigationSignals()
{
    connect(navigationManager.get(), &NavigationManager::navigationRequested,
            this, &MainWindow::onNavigationRequested);

    connect(navigationManager.get(), &NavigationManager::symbolNavigationRequested,
            this, &MainWindow::onSymbolNavigationRequested);

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

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->attachRelationshipEngine(relationshipEngine.get());

    slangManager = std::make_unique<SlangManager>();
    CompletionManager* completionManager = CompletionManager::getInstance();
    completionManager->setSlangManager(slangManager.get());
    completionManager->setRelationshipEngine(relationshipEngine.get());

    relationshipBuilder = semanticIndex->createRelationshipBuilder(
        relationshipEngine.get(), slangManager.get(), this);

}

void MainWindow::onRelationshipAnalysisCompleted(const QString& fileName, int relationshipsFound)
{
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

void MainWindow::onSingleFileRelationshipFinished(
    const SingleFileRelationshipAnalysisResult& result)
{
    onRelationshipAnalysisCompleted(result.fileName, result.relationships.size());
}
