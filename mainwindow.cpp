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
#include "foldblockshelfmodel.h"
#include "foldblockshelfpanel.h"
#include "semanticdecorationservice.h"
#include "globalcontrolcoordinator.h"
#include "globalcontrolservice.h"
#include "semanticdockcoordinator.h"
#include "semanticindex.h"
#include "semanticpanelrefreshcoordinator.h"
#include "semanticruntimecoordinator.h"
#include "activitylogpanelcoordinator.h"
#include "activitylogservice.h"
#include "problemspanelcoordinator.h"
#include "rtlinsightspanelcoordinator.h"
#include "version.h"
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
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
    setupGlobalControl();
    setupFoldBlockShelf();
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

void MainWindow::setupGlobalControl()
{
    globalControlCoordinator =
        std::make_unique<GlobalControlCoordinator>(this, this);
    globalControlCoordinator->setProjectModel(
        workspaceManager ? workspaceManager->getProjectModel() : nullptr);
    globalControlCoordinator->setSemanticIndex(SemanticIndex::getInstance());
    globalControlCoordinator->setActionHandler(
        [this](const GlobalControlItem& item) {
            if (item.kind == GlobalControlItemKind::File
                || item.kind == GlobalControlItemKind::Symbol) {
                if (navigationCommandCoordinator)
                    navigationCommandCoordinator->navigateToFileAndLine(
                        item.filePath,
                        item.line,
                        item.column);
                return;
            }

            if (item.id == QStringLiteral("openWorkspace")) {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->openDirectoryAsWorkspace();
            } else if (item.id == QStringLiteral("openFile")) {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->openFile();
            } else if (item.id == QStringLiteral("saveFile")) {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->saveFile();
            } else if (item.id == QStringLiteral("saveAs")) {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->saveFileAs();
            } else if (item.id == QStringLiteral("find")) {
                if (MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr) {
                    QKeyEvent press(QEvent::KeyPress,
                                    Qt::Key_F,
                                    Qt::ControlModifier,
                                    QStringLiteral("f"));
                    QCoreApplication::sendEvent(editor, &press);
                }
            } else if (item.id == QStringLiteral("toggleNavigation")) {
                if (navigationPane)
                    navigationPane->toggleVisible();
            } else if (item.id == QStringLiteral("toggleProblems")) {
                if (semanticDocks && semanticDocks->problemsPanelCoordinator())
                    semanticDocks->problemsPanelCoordinator()->dock()->setVisible(
                        !semanticDocks->problemsPanelCoordinator()->dock()->isVisible());
            } else if (item.id == QStringLiteral("toggleActivity")) {
                if (semanticDocks && semanticDocks->activityLogPanelCoordinator())
                    semanticDocks->activityLogPanelCoordinator()->dock()->setVisible(
                        !semanticDocks->activityLogPanelCoordinator()->dock()->isVisible());
            } else if (item.id == QStringLiteral("toggleRtlInsights")
                       || item.kind == GlobalControlItemKind::RtlInsight) {
                if (semanticDocks && semanticDocks->rtlInsightsPanelCoordinator())
                    semanticDocks->rtlInsightsPanelCoordinator()->dock()->setVisible(
                        !semanticDocks->rtlInsightsPanelCoordinator()->dock()->isVisible());
            } else if (item.id == QStringLiteral("editorAppearance")) {
                if (editorAppearanceDock)
                    editorAppearanceDock->setVisible(!editorAppearanceDock->isVisible());
            }
        });
    globalControlCoordinator->install();
}

void MainWindow::setupFoldBlockShelf()
{
    foldShelfModel = std::make_unique<FoldBlockShelfModel>(this);
    foldShelfDock = new QDockWidget(QStringLiteral("Fold Shelf"), this);
    foldShelfDock->setObjectName(QStringLiteral("FoldShelfDock"));
    foldShelfPanel = new FoldBlockShelfPanel(foldShelfDock);
    foldShelfPanel->setModel(foldShelfModel.get());
    connect(foldShelfPanel,
            &FoldBlockShelfPanel::restoreItemRequested,
            this,
            &MainWindow::restoreFoldShelfItem);
    foldShelfDock->setWidget(foldShelfPanel);
    addDockWidget(Qt::BottomDockWidgetArea, foldShelfDock);
    foldShelfDock->hide();
}

void MainWindow::showFoldBlockShelf()
{
    if (!foldShelfDock)
        return;
    foldShelfDock->show();
    foldShelfDock->raise();
    if (statusBar())
        statusBar()->showMessage(QStringLiteral("Fold Shelf ready"), 3000);
}

void MainWindow::restoreFoldShelfItem(const QString& id)
{
    if (!foldShelfModel || !tabManager)
        return;

    const FoldShelfItem item = foldShelfModel->item(id);
    if (item.id.isEmpty() || item.text.isEmpty()) {
        ActivityLogService::getInstance()->append(
            QStringLiteral("Fold Shelf"),
            ActivityLogLevel::Warning,
            QStringLiteral("Restore failed: item unavailable"));
        if (statusBar())
            statusBar()->showMessage(QStringLiteral("Fold Shelf restore failed: item unavailable"), 5000);
        return;
    }
    if (item.sourceFile.isEmpty()) {
        ActivityLogService::getInstance()->append(
            QStringLiteral("Fold Shelf"),
            ActivityLogLevel::Warning,
            QStringLiteral("Restore failed for \"%1\": source file unavailable").arg(item.alias));
        if (statusBar())
            statusBar()->showMessage(QStringLiteral("Fold Shelf restore failed: source file unavailable"), 5000);
        return;
    }

    if (!tabManager->activateOpenFile(item.sourceFile)
        && !tabManager->openFileInTab(item.sourceFile)) {
        ActivityLogService::getInstance()->append(
            QStringLiteral("Fold Shelf"),
            ActivityLogLevel::Warning,
            QStringLiteral("Restore failed for \"%1\": source file could not be opened").arg(item.alias));
        if (statusBar())
            statusBar()->showMessage(QStringLiteral("Fold Shelf restore failed: source file could not be opened"), 5000);
        return;
    }

    MyCodeEditor* editor = tabManager->getCurrentEditor();
    if (!editor || !editor->insertFoldShelfItemAtLineForTest(item, item.sourceStartLine)) {
        ActivityLogService::getInstance()->append(
            QStringLiteral("Fold Shelf"),
            ActivityLogLevel::Warning,
            QStringLiteral("Restore failed for \"%1\": source location unavailable").arg(item.alias));
        if (statusBar())
            statusBar()->showMessage(QStringLiteral("Fold Shelf restore failed: source location unavailable"), 5000);
        return;
    }

    foldShelfModel->removeItem(id);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Fold Shelf"),
        ActivityLogLevel::Info,
        QStringLiteral("Restored shelf item \"%1\"").arg(item.alias));
    if (statusBar())
        statusBar()->showMessage(QStringLiteral("Fold Shelf item restored"), 3000);
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
    editorCoordinator->setStatusMessageHandler(
        [this](const QString& message, int timeoutMs) {
            if (statusBar()) {
                if (message.isEmpty())
                    statusBar()->clearMessage();
                else
                    statusBar()->showMessage(message, timeoutMs);
            }
        });
    editorCoordinator->setFoldShelfRequestedHandler([this]() {
        showFoldBlockShelf();
    });
    editorCoordinator->setFoldShelfItemConsumedHandler([this](const QString& id) {
        if (foldShelfModel)
            foldShelfModel->consumeItem(id);
    });
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
