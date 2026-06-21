#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "mycodeeditor.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "projectmodel.h"
#include "modemanager.h"
#include "analysisscheduler.h"
#include "analysiscoordinator.h"
#include "analysisprogresscoordinator.h"
#include "editorcoordinator.h"
#include "filecommandcoordinator.h"
#include "modecommandcoordinator.h"
#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "navigationpanecoordinator.h"
#include "diagnosticservice.h"
#include "editorappearancepanel.h"
#include "editorappearancesettings.h"
#include "semanticdecorationservice.h"
#include "semanticdockcoordinator.h"
#include "semanticpanelrefreshcoordinator.h"
#include "semanticruntimecoordinator.h"
#include "version.h"
#include <QCloseEvent>
#include <QDockWidget>
#include <QDir>
#include <QFileInfo>
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
    navigationManager = std::unique_ptr<NavigationManager>(new NavigationManager(this));  // NEW
    analysisScheduler = std::unique_ptr<AnalysisScheduler>(new AnalysisScheduler(this));
    analysisProgressCoordinator =
        std::unique_ptr<AnalysisProgressCoordinator>(new AnalysisProgressCoordinator(this, this));

    setupSemanticRuntime();
    setupNavigationPane();
    setupNavigationCommandCoordinator();
    setupSemanticDocks();
    setupEditorAppearanceSettings();
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
        semanticRuntime.get(),
        tabManager.get(),
        workspaceManager.get(),
        navigationManager.get(),
        this);
    analysisCoordinator->setFileChangeDebounceMs(kFileChangeDebounceMs);
    analysisCoordinator->setProblemsRefreshHandler(
        [this](const QString& fileName) {
            if (semanticDocks && semanticDocks->refreshCoordinator())
                semanticDocks->refreshCoordinator()->updateProblemsPanel(fileName);
            refreshActiveEditorDiagnosticHighlights(fileName);
            refreshActiveEditorSemanticDecorations(fileName);
        });
    analysisCoordinator->setStatusMessageHandler(
        [this](const QString& message, int timeoutMs) {
            if (statusBar())
                statusBar()->showMessage(message, timeoutMs);
        });
    analysisCoordinator->connectSignals();
    connect(tabManager.get(),
            &TabManager::activeDocumentChanged,
            this,
            [this](const DocumentSnapshot&) {
                refreshActiveEditorDiagnosticHighlights();
                refreshActiveEditorSemanticDecorations();
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::fileSymbolAnalysisFinished,
            this,
            [this](const QString& fileName, int) {
                refreshActiveEditorSemanticDecorations(fileName);
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot&, int, int) {
                refreshActiveEditorSemanticDecorations();
            });

}

void MainWindow::refreshActiveEditorDiagnosticHighlights(
    const QString& changedFileName)
{
    if (!tabManager)
        return;

    MyCodeEditor* editor = tabManager->getCurrentEditor();
    if (!editor)
        return;

    const DocumentSnapshot document = tabManager->getCurrentDocument();
    if (document.fileName.isEmpty()) {
        editor->setDiagnosticHighlights({});
        return;
    }

    if (!changedFileName.isEmpty()) {
        const QString changed =
            QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(changedFileName).absoluteFilePath()));
        const QString current =
            QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(document.fileName).absoluteFilePath()));
        if (changed != current)
            return;
    }

    DiagnosticQuery query;
    query.fileName = document.fileName;
    query.includeInfo = false;
    const QList<DiagnosticResult> results =
        DiagnosticService::getInstance()->findDiagnostics(query);
    QList<SemanticDiagnostic> diagnostics;
    diagnostics.reserve(results.size());
    for (const DiagnosticResult& result : results)
        diagnostics.append(result.diagnostic);
    editor->setDiagnosticHighlights(diagnostics);
}

void MainWindow::refreshActiveEditorSemanticDecorations(
    const QString& changedFileName)
{
    if (!tabManager)
        return;

    MyCodeEditor* editor = tabManager->getCurrentEditor();
    if (!editor)
        return;

    const DocumentSnapshot document = tabManager->getCurrentDocument();
    if (document.fileName.isEmpty()) {
        editor->setSemanticDecorations({});
        return;
    }

    if (!changedFileName.isEmpty()) {
        const QString changed =
            QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(changedFileName).absoluteFilePath()));
        const QString current =
            QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(document.fileName).absoluteFilePath()));
        if (changed != current)
            return;
    }

    SemanticDecorationQuery query;
    query.fileName = document.fileName;
    query.documentText = editor->toPlainText();
    const SemanticDecorationReport report =
        SemanticDecorationService::getInstance()->decorationsForDocument(query);
    editor->setSemanticDecorations(report.decorations);
}


void MainWindow::setupNavigationPane()
{
    navigationPane = std::make_unique<NavigationPaneCoordinator>(this);
    navigationPane->attachNavigationManager(navigationManager.get());
    navigationPane->connectNavigationInputs(
        tabManager.get(),
        workspaceManager.get());
    addDockWidget(Qt::LeftDockWidgetArea, navigationPane->dock());
}

void MainWindow::setupNavigationCommandCoordinator()
{
    navigationCommandCoordinator = std::make_unique<NavigationCommandCoordinator>(
        tabManager.get(), navigationManager.get(), this);
    navigationCommandCoordinator->connectSignals();
}

void MainWindow::setupSemanticDocks()
{
    semanticDocks = std::make_unique<SemanticDockCoordinator>(
        this,
        tabManager.get(),
        workspaceManager.get(),
        navigationManager.get(),
        navigationCommandCoordinator.get());
    semanticDocks->setStatusMessageHandler(
        [this](const QString& message, int timeoutMs) {
            if (statusBar())
                statusBar()->showMessage(message, timeoutMs);
        });
    semanticDocks->setup();
}

void MainWindow::setupFileCommandCoordinator()
{
    fileCommandCoordinator = std::make_unique<FileCommandCoordinator>(
        tabManager.get(), workspaceManager.get(), this);
    fileCommandCoordinator->connectActions(
        ui->new_file,
        ui->open_file,
        ui->save_file,
        ui->save_as,
        ui->copy,
        ui->paste,
        ui->cut,
        ui->undo,
        ui->redo,
        ui->open_direction_as_workspace);
}

void MainWindow::setupModeCommandCoordinator()
{
    modeCommandCoordinator = std::make_unique<ModeCommandCoordinator>(
        modeManager.get(), navigationPane.get(), this);
    modeCommandCoordinator->connectSignals();
}

void MainWindow::setupEditorAppearanceSettings()
{
    editorAppearanceSettings =
        std::make_unique<EditorAppearanceSettings>();

    editorAppearanceDock = new QDockWidget(tr("Editor Appearance"), this);
    editorAppearanceDock->setObjectName(QStringLiteral("editorAppearanceDock"));
    editorAppearanceDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    editorAppearanceDock->setWidget(
        new EditorAppearancePanel(editorAppearanceSettings.get(),
                                  editorAppearanceDock));
    addDockWidget(Qt::RightDockWidgetArea, editorAppearanceDock);
}

void MainWindow::setupEditorCoordinator()
{
    editorCoordinator = std::make_unique<EditorCoordinator>(
        tabManager.get(), modeManager.get(), this);
    editorCoordinator->setWorkflowDependencies(
        workspaceManager.get(),
        fileCommandCoordinator.get(),
        navigationCommandCoordinator.get(),
        semanticDocks ? semanticDocks->refreshCoordinator() : nullptr);
    editorCoordinator->setAppearanceSettings(editorAppearanceSettings.get());
    editorCoordinator->connectSignals();
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
