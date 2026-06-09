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
#include "analysiscommandcoordinator.h"
#include "analysiscoordinator.h"
#include "analysisprogresscoordinator.h"
#include "editorcoordinator.h"
#include "filecommandcoordinator.h"
#include "modecommandcoordinator.h"
#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "navigationpanecoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "semanticruntimecoordinator.h"
#include "version.h"
#include <QCloseEvent>
#include <QLabel>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setCentralWidget(ui->tabWidget);

    tabManager = std::unique_ptr<TabManager>(new TabManager(ui->tabWidget, this));
    workspaceManager = std::unique_ptr<WorkspaceManager>(new WorkspaceManager(this));
    modeManager = std::unique_ptr<ModeManager>(new ModeManager(ui->tabWidget, this));
    symbolAnalyzer = std::unique_ptr<SymbolAnalyzer>(new SymbolAnalyzer(this));
    navigationManager = std::unique_ptr<NavigationManager>(new NavigationManager(this));  // NEW
    analysisScheduler = std::unique_ptr<AnalysisScheduler>(new AnalysisScheduler(this));
    analysisProgressCoordinator =
        std::unique_ptr<AnalysisProgressCoordinator>(new AnalysisProgressCoordinator(this, this));

    setupSemanticRuntime();
    setupAnalysisCommandCoordinator();
    setupNavigationPane();
    setupNavigationCommandCoordinator();
    setupProblemsPane();
    setupReferencesPane();
    setupRelationshipsPane();
    setupSemanticPanelRefreshCoordinator();
    setupFileCommandCoordinator();
    setupModeCommandCoordinator();
    setupEditorCoordinator();
    setupManagerConnections();

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
    delete ui;
}

void MainWindow::setupManagerConnections()
{
    analysisCoordinator = std::make_unique<AnalysisCoordinator>(
        analysisScheduler.get(),
        analysisProgressCoordinator.get(),
        symbolAnalyzer.get(),
        tabManager.get(),
        workspaceManager.get(),
        navigationManager.get(),
        semanticRuntime ? semanticRuntime->relationshipEngine() : nullptr,
        semanticRuntime ? semanticRuntime->relationshipBuilder() : nullptr,
        this);
    analysisCoordinator->setFileChangeDebounceMs(kFileChangeDebounceMs);
    analysisCoordinator->setProblemsRefreshHandler(
        [this](const QString& fileName) {
            if (semanticPanelRefresh)
                semanticPanelRefresh->updateProblemsPanel(fileName);
        });
    analysisCoordinator->setStatusMessageHandler(
        [this](const QString& message, int timeoutMs) {
            if (statusBar())
                statusBar()->showMessage(message, timeoutMs);
        });
    analysisCoordinator->connectSignals();

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

void MainWindow::setupNavigationCommandCoordinator()
{
    navigationCommandCoordinator = std::make_unique<NavigationCommandCoordinator>(
        tabManager.get(), navigationManager.get(), this);
    navigationCommandCoordinator->connectSignals();
}

void MainWindow::setupProblemsPane()
{
    problemsPanel = std::make_unique<ProblemsPanelCoordinator>(this);
    addDockWidget(Qt::BottomDockWidgetArea, problemsPanel->dock());
}

void MainWindow::setupReferencesPane()
{
    referencesPanel = std::make_unique<ReferencesPanelCoordinator>(this);
    addDockWidget(Qt::BottomDockWidgetArea, referencesPanel->dock());
}

void MainWindow::setupRelationshipsPane()
{
    relationshipsPanel = std::make_unique<RelationshipsPanelCoordinator>(this);
    addDockWidget(Qt::BottomDockWidgetArea, relationshipsPanel->dock());
}

void MainWindow::setupSemanticPanelRefreshCoordinator()
{
    semanticPanelRefresh = std::make_unique<SemanticPanelRefreshCoordinator>(
        tabManager.get(),
        workspaceManager.get(),
        navigationManager.get(),
        navigationCommandCoordinator.get(),
        problemsPanel.get(),
        referencesPanel.get(),
        relationshipsPanel.get());
    semanticPanelRefresh->setStatusMessageHandler(
        [this](const QString& message, int timeoutMs) {
            if (statusBar())
                statusBar()->showMessage(message, timeoutMs);
        });
    semanticPanelRefresh->configurePanels();
}

void MainWindow::setupAnalysisCommandCoordinator()
{
    analysisCommandCoordinator =
        std::make_unique<AnalysisCommandCoordinator>(analysisScheduler.get(), this);
}

void MainWindow::setupFileCommandCoordinator()
{
    fileCommandCoordinator = std::make_unique<FileCommandCoordinator>(
        tabManager.get(), workspaceManager.get(), this);
}

void MainWindow::setupModeCommandCoordinator()
{
    modeCommandCoordinator = std::make_unique<ModeCommandCoordinator>(
        modeManager.get(), navigationPane.get(), this);
    modeCommandCoordinator->connectSignals();
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
            if (navigationCommandCoordinator)
                navigationCommandCoordinator->navigateToFileAndLine(fileName, line);
        });
    editorCoordinator->setRelationshipAnalysisHandler(
        [this](const QString& fileName, const QString& content) {
            if (analysisCommandCoordinator)
                analysisCommandCoordinator->requestSingleFileRelationshipAnalysis(fileName, content);
        });
    editorCoordinator->setSaveFileHandler([this]() {
        if (fileCommandCoordinator)
            fileCommandCoordinator->saveFile();
    });
    editorCoordinator->setSaveFileAsHandler([this]() {
        if (fileCommandCoordinator)
            fileCommandCoordinator->saveFileAs();
    });
    editorCoordinator->setOpenFileHandler([this]() {
        if (fileCommandCoordinator)
            fileCommandCoordinator->openFile();
    });
    editorCoordinator->setNewFileHandler([this]() {
        if (fileCommandCoordinator)
            fileCommandCoordinator->newFile();
    });
    editorCoordinator->setReferenceSearchHandler(
        [this](const QString& symbolName,
               const QString& fileName,
               const QString& moduleName) {
            if (semanticPanelRefresh)
                semanticPanelRefresh->showReferencesForSymbol(symbolName, fileName, moduleName);
        });
    editorCoordinator->setRelationshipBrowseHandler(
        [this](const QString& symbolName,
               const QString& fileName,
               const QString& moduleName) {
            if (semanticPanelRefresh)
                semanticPanelRefresh->showRelationshipsForSymbol(symbolName, fileName, moduleName);
        });
    editorCoordinator->setActiveEditorChangedHandler([this](MyCodeEditor* editor) {
        if (semanticPanelRefresh)
            semanticPanelRefresh->handleActiveEditorChanged(editor);
    });
    editorCoordinator->connectSignals();
}

void MainWindow::on_new_file_triggered()
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->newFile();
}

void MainWindow::on_open_file_triggered()
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->openFile();
}

void MainWindow::on_save_file_triggered()
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->saveFile();
}

void MainWindow::on_save_as_triggered()
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->saveFileAs();
}

void MainWindow::on_copy_triggered()
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->copy();
}

void MainWindow::on_paste_triggered()
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->paste();
}

void MainWindow::on_cut_triggered()
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->cut();
}

void MainWindow::on_undo_triggered()
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->undo();
}

void MainWindow::on_redo_triggered()
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->redo();
}

void MainWindow::on_open_direction_as_workspace_triggered()
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->openDirectoryAsWorkspace();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->handleCloseEvent(event, this);
    else
        event->accept();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (modeCommandCoordinator && modeCommandCoordinator->handleKeyPress(event))
        return;
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (modeCommandCoordinator && modeCommandCoordinator->handleKeyRelease(event))
        return;
    QMainWindow::keyReleaseEvent(event);
}


void MainWindow::setupSemanticRuntime()
{
    semanticRuntime = std::make_unique<SemanticRuntimeCoordinator>(this);
}
