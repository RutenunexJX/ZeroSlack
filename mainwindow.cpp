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
#include "formattersettings.h"
#include "foldblockshelfmodel.h"
#include "foldblockshelfpanel.h"
#include "ghostannotationservice.h"
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
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "rtlinsightspanelcoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "wavepreviewpanelcoordinator.h"
#include "version.h"
#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    tabManager = std::unique_ptr<TabManager>(new TabManager(ui->tabWidget, this));
    workspaceManager = std::unique_ptr<WorkspaceManager>(new WorkspaceManager(this));
    setupWorkspaceBar();
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
    setupFoldBlockShelf();
    setupViewMenu();
    setupEditorModeChip();
    setupGlobalControl();
    setupEditorCoordinator();
    setupManagerConnections();

    if (editorAppearanceDock)
        editorAppearanceDock->hide();
    if (semanticDocks) {
        if (semanticDocks->activityLogPanelCoordinator()
            && semanticDocks->activityLogPanelCoordinator()->dock())
            semanticDocks->activityLogPanelCoordinator()->dock()->hide();
        if (semanticDocks->rtlInsightsPanelCoordinator()
            && semanticDocks->rtlInsightsPanelCoordinator()->dock())
            semanticDocks->rtlInsightsPanelCoordinator()->dock()->hide();
        if (semanticDocks->signalKernelGraphPanelCoordinator()
            && semanticDocks->signalKernelGraphPanelCoordinator()->dock())
            semanticDocks->signalKernelGraphPanelCoordinator()->dock()->hide();
        if (semanticDocks->wavePreviewPanelCoordinator()
            && semanticDocks->wavePreviewPanelCoordinator()->dock())
            semanticDocks->wavePreviewPanelCoordinator()->dock()->hide();
    }

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

void MainWindow::setupWorkspaceBar()
{
    QWidget* editorContainer = new QWidget(this);
    auto* layout = new QVBoxLayout(editorContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    workspaceTabBar = new QTabBar(editorContainer);
    workspaceTabBar->setObjectName(QStringLiteral("workspaceTabBar"));
    workspaceTabBar->setExpanding(false);
    workspaceTabBar->setMovable(false);
    workspaceTabBar->hide();
    workspaceTabBar->setStyleSheet(QStringLiteral(
        "QTabBar#workspaceTabBar { background: #eef2f7; }"
        "QTabBar#workspaceTabBar::tab {"
        "  background: #e5e7eb;"
        "  color: #374151;"
        "  padding: 5px 12px;"
        "  border: 1px solid #cbd5e1;"
        "  border-bottom: none;"
        "}"
        "QTabBar#workspaceTabBar::tab:selected {"
        "  background: #334155;"
        "  color: #ffffff;"
        "}"
        "QTabBar#workspaceTabBar::tab:hover {"
        "  background: #cbd5e1;"
        "  color: #111827;"
        "}"));
    layout->addWidget(workspaceTabBar);
    layout->addWidget(ui->tabWidget, 1);
    setCentralWidget(editorContainer);

    connect(workspaceTabBar,
            &QTabBar::currentChanged,
            this,
            [this](int index) {
                if (workspaceManager)
                    workspaceManager->switchWorkspace(index);
            });
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceListChanged,
            this,
            &MainWindow::refreshWorkspaceTabs);
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceActivated,
            this,
            [this](int, const QString&, const QString&) {
                refreshWorkspaceTabs();
            });
}

void MainWindow::refreshWorkspaceTabs()
{
    if (!workspaceTabBar || !workspaceManager)
        return;

    const QSignalBlocker blocker(workspaceTabBar);
    while (workspaceTabBar->count() > 0)
        workspaceTabBar->removeTab(0);
    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    for (const WorkspaceManager::WorkspaceEntry& entry : entries) {
        const int tab = workspaceTabBar->addTab(entry.alias);
        workspaceTabBar->setTabToolTip(tab, QDir::toNativeSeparators(entry.path));
    }
    workspaceTabBar->setVisible(!entries.isEmpty());
    const int activeIndex = workspaceManager->activeWorkspaceIndex();
    if (activeIndex >= 0 && activeIndex < workspaceTabBar->count())
        workspaceTabBar->setCurrentIndex(activeIndex);
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
            refreshActiveEditorGhostAnnotations(fileName);
        });
    analysisCoordinator->setStatusMessageHandler(
        [this](const QString& message, int timeoutMs) {
            if (statusBar())
                statusBar()->showMessage(message, timeoutMs);
        });
    analysisCoordinator->connectSignals();
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceOpened,
            this,
            [this](const QString&) {
                showPanelById(QStringLiteral("activity"));
            });
    connect(tabManager.get(),
            &TabManager::activeDocumentChanged,
            this,
            [this](const DocumentSnapshot&) {
                refreshActiveEditorDiagnosticHighlights();
                refreshActiveEditorSemanticDecorations();
                refreshActiveEditorGhostAnnotations();
                QDockWidget* waveDock = dockForPanelId(QStringLiteral("wavePreview"));
                if (waveDock && waveDock->isVisible())
                    refreshActiveEditorWavePreview();
            });
    connect(tabManager.get(),
            &TabManager::tabCreated,
            this,
            [this](MyCodeEditor* editor) {
                if (!editor)
                    return;
                connect(editor,
                        &MyCodeEditor::textChanged,
                        this,
                        [this, editor]() {
                            QDockWidget* waveDock =
                                dockForPanelId(QStringLiteral("wavePreview"));
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor
                                && waveDock
                                && waveDock->isVisible()) {
                                refreshActiveEditorWavePreview();
                            }
                        });
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::fileSymbolAnalysisFinished,
            this,
            [this](const QString& fileName, int) {
                refreshActiveEditorSemanticDecorations(fileName);
                refreshActiveEditorGhostAnnotations(fileName);
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot&, int, int) {
                refreshActiveEditorSemanticDecorations();
                refreshActiveEditorGhostAnnotations();
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

void MainWindow::refreshActiveEditorGhostAnnotations(
    const QString& changedFileName)
{
    if (!tabManager)
        return;

    MyCodeEditor* editor = tabManager->getCurrentEditor();
    if (!editor)
        return;

    const DocumentSnapshot document = tabManager->getCurrentDocument();
    if (document.fileName.isEmpty()) {
        editor->setGhostAnnotations({});
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

    GhostAnnotationQuery query;
    query.fileName = document.fileName;
    query.documentText = editor->toPlainText();
    const GhostAnnotationReport report =
        GhostAnnotationService::getInstance()->annotationsForDocument(query);
    editor->setGhostAnnotations(report.annotations);
}

void MainWindow::refreshActiveEditorWavePreview()
{
    if (!tabManager || !semanticDocks
        || !semanticDocks->wavePreviewPanelCoordinator()) {
        return;
    }

    MyCodeEditor* editor = tabManager->getCurrentEditor();
    if (!editor) {
        semanticDocks->wavePreviewPanelCoordinator()->renderUnavailable(
            QStringLiteral("No document selected."));
        return;
    }

    const DocumentSnapshot document = tabManager->getCurrentDocument();
    semanticDocks->wavePreviewPanelCoordinator()->refreshFromDocument(
        document.fileName,
        editor->toPlainText(),
        document.dirty);
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
    if (semanticDocks->wavePreviewPanelCoordinator()) {
        semanticDocks->wavePreviewPanelCoordinator()->setNavigationHandler(
            [this](const QString& fileName, int line, int column) {
                if (navigationCommandCoordinator)
                    navigationCommandCoordinator->navigateToFileAndLine(
                        fileName, line, column);
            });
        if (semanticDocks->wavePreviewPanelCoordinator()->dock()) {
            connect(semanticDocks->wavePreviewPanelCoordinator()->dock(),
                    &QDockWidget::visibilityChanged,
                    this,
                    [this](bool visible) {
                        if (visible)
                            refreshActiveEditorWavePreview();
                    });
        }
    }
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

            if (item.id == QStringLiteral("ow")) {
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
            } else if (item.id == QStringLiteral("fd")) {
                if (MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr)
                    editor->startFoldRegionMarkMode();
            } else if (item.id == QStringLiteral("fds")) {
                showFoldBlockShelf();
                if (MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr)
                    editor->startFoldShelfMode();
            } else if (item.id == QStringLiteral("showNavigation")) {
                showPanelById(QStringLiteral("navigation"));
            } else if (item.id == QStringLiteral("showProblems")) {
                showPanelById(QStringLiteral("problems"));
            } else if (item.id == QStringLiteral("showActivity")) {
                showPanelById(QStringLiteral("activity"));
            } else if (item.id == QStringLiteral("showReferences")) {
                showPanelById(QStringLiteral("references"));
            } else if (item.id == QStringLiteral("showRelationships")) {
                showPanelById(QStringLiteral("relationships"));
            } else if (item.id == QStringLiteral("showRtlInsights")
                       || item.kind == GlobalControlItemKind::RtlInsight) {
                showPanelById(QStringLiteral("rtlInsights"));
            } else if (item.id == QStringLiteral("showFoldShelf")) {
                showPanelById(QStringLiteral("foldShelf"));
            } else if (item.id == QStringLiteral("showEditorAppearance")
                       || item.id == QStringLiteral("editorAppearance")) {
                showPanelById(QStringLiteral("editorAppearance"));
            } else if (item.id == QStringLiteral("resetPanelLayout")) {
                resetPanelLayout();
            } else if (item.id == QStringLiteral("toggleNavigation")) {
                togglePanelById(QStringLiteral("navigation"));
            } else if (item.id == QStringLiteral("toggleProblems")) {
                togglePanelById(QStringLiteral("problems"));
            } else if (item.id == QStringLiteral("toggleActivity")) {
                togglePanelById(QStringLiteral("activity"));
            } else if (item.id == QStringLiteral("toggleRtlInsights")) {
                togglePanelById(QStringLiteral("rtlInsights"));
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

void MainWindow::setupViewMenu()
{
    if (!menuBar() || viewMenu)
        return;

    viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->setObjectName(QStringLiteral("viewMenu"));

    addPanelViewAction(navigationPane ? navigationPane->dock() : nullptr,
                       tr("Navigation"),
                       QStringLiteral("viewNavigationAction"));
    viewMenu->addSeparator();
    addPanelViewAction(semanticDocks && semanticDocks->problemsPanelCoordinator()
                           ? semanticDocks->problemsPanelCoordinator()->dock()
                           : nullptr,
                       tr("Problems"),
                       QStringLiteral("viewProblemsAction"));
    addPanelViewAction(semanticDocks && semanticDocks->activityLogPanelCoordinator()
                           ? semanticDocks->activityLogPanelCoordinator()->dock()
                           : nullptr,
                       tr("Activity / Output"),
                       QStringLiteral("viewActivityAction"));
    addPanelViewAction(semanticDocks && semanticDocks->referencesPanelCoordinator()
                           ? semanticDocks->referencesPanelCoordinator()->dock()
                           : nullptr,
                       tr("References"),
                       QStringLiteral("viewReferencesAction"));
    addPanelViewAction(semanticDocks && semanticDocks->relationshipsPanelCoordinator()
                           ? semanticDocks->relationshipsPanelCoordinator()->dock()
                           : nullptr,
                       tr("Relationships"),
                       QStringLiteral("viewRelationshipsAction"));
    addPanelViewAction(semanticDocks && semanticDocks->rtlInsightsPanelCoordinator()
                           ? semanticDocks->rtlInsightsPanelCoordinator()->dock()
                           : nullptr,
                       tr("RTL Insights"),
                       QStringLiteral("viewRtlInsightsAction"));
    addPanelViewAction(semanticDocks && semanticDocks->signalKernelGraphPanelCoordinator()
                           ? semanticDocks->signalKernelGraphPanelCoordinator()->dock()
                           : nullptr,
                       tr("Signal Kernel Graph"),
                       QStringLiteral("viewSignalKernelGraphAction"));
    addPanelViewAction(semanticDocks && semanticDocks->wavePreviewPanelCoordinator()
                           ? semanticDocks->wavePreviewPanelCoordinator()->dock()
                           : nullptr,
                       tr("Wave Preview"),
                       QStringLiteral("viewWavePreviewAction"));
    addPanelViewAction(foldShelfDock,
                       tr("Fold Shelf"),
                       QStringLiteral("viewFoldShelfAction"));
    viewMenu->addSeparator();
    addPanelViewAction(editorAppearanceDock,
                       tr("Editor Appearance"),
                       QStringLiteral("viewEditorAppearanceAction"));

    viewMenu->addSeparator();
    QAction* resetLayoutAction = viewMenu->addAction(tr("Reset Panel Layout"));
    resetLayoutAction->setObjectName(QStringLiteral("resetPanelLayoutAction"));
    connect(resetLayoutAction, &QAction::triggered,
            this, &MainWindow::resetPanelLayout);

    if (statusBar()) {
        panelsStatusButton = new QToolButton(this);
        panelsStatusButton->setObjectName(QStringLiteral("panelsStatusButton"));
        panelsStatusButton->setText(tr("Panels"));
        panelsStatusButton->setToolTip(tr("Open or restore ZeroSlack panels"));
        panelsStatusButton->setPopupMode(QToolButton::InstantPopup);
        panelsStatusButton->setMenu(viewMenu);
        statusBar()->addPermanentWidget(panelsStatusButton);
    }
}

void MainWindow::setupEditorModeChip()
{
    if (!statusBar() || editorModeChip)
        return;

    editorModeChip = new QLabel(this);
    editorModeChip->setObjectName(QStringLiteral("editorModeChip"));
    editorModeChip->setVisible(false);
    editorModeChip->setTextInteractionFlags(Qt::NoTextInteraction);
    editorModeChip->setContentsMargins(8, 2, 8, 2);
    statusBar()->addPermanentWidget(editorModeChip);
}

void MainWindow::updateEditorModeChip(const QString& message)
{
    if (!editorModeChip)
        return;

    if (message.startsWith(QStringLiteral("Fold region: click start line"))) {
        editorModeChip->setText(tr("Fold Region - click start line - Esc cancel"));
        editorModeChip->setStyleSheet(QStringLiteral(
            "QLabel#editorModeChip {"
            "  color: #064E3B;"
            "  background: rgba(16, 185, 129, 0.22);"
            "  border: 1px solid rgba(16, 185, 129, 0.65);"
            "  border-radius: 6px;"
            "  padding: 2px 8px;"
            "}"));
        setFoldShelfModeVisualActive(false);
        editorModeChip->setVisible(true);
        return;
    }

    if (message.startsWith(QStringLiteral("Fold region: click end line"))) {
        editorModeChip->setText(tr("Fold Region - click end line - Esc cancel"));
        editorModeChip->setStyleSheet(QStringLiteral(
            "QLabel#editorModeChip {"
            "  color: #064E3B;"
            "  background: rgba(16, 185, 129, 0.22);"
            "  border: 1px solid rgba(16, 185, 129, 0.65);"
            "  border-radius: 6px;"
            "  padding: 2px 8px;"
            "}"));
        setFoldShelfModeVisualActive(false);
        editorModeChip->setVisible(true);
        return;
    }

    if (message.startsWith(QStringLiteral("Fold Shelf"))) {
        editorModeChip->setText(tr("Fold Shelf - drag custom fold blocks - Esc cancel"));
        editorModeChip->setStyleSheet(QStringLiteral(
            "QLabel#editorModeChip {"
            "  color: #78350F;"
            "  background: rgba(245, 158, 11, 0.24);"
            "  border: 1px solid rgba(245, 158, 11, 0.70);"
            "  border-radius: 6px;"
            "  padding: 2px 8px;"
            "}"));
        setFoldShelfModeVisualActive(true);
        editorModeChip->setVisible(true);
        return;
    }

    if (message.isEmpty()) {
        editorModeChip->clear();
        editorModeChip->setVisible(false);
        setFoldShelfModeVisualActive(false);
    }
}

void MainWindow::setFoldShelfModeVisualActive(bool active)
{
    if (foldShelfPanel)
        foldShelfPanel->setShelfModeActive(active);
    if (!foldShelfDock)
        return;

    foldShelfDock->setProperty("foldShelfModeActive", active);
    foldShelfDock->setStyleSheet(active
        ? QStringLiteral(
              "QDockWidget#FoldShelfDock::title {"
              "  background: rgba(245, 158, 11, 0.35);"
              "  padding-left: 6px;"
              "}"
              "QDockWidget#FoldShelfDock {"
              "  border: 1px solid rgba(245, 158, 11, 0.70);"
              "}")
        : QString());
}

void MainWindow::addPanelViewAction(QDockWidget* dock,
                                    const QString& text,
                                    const QString& objectName)
{
    if (!viewMenu || !dock)
        return;

    QAction* action = dock->toggleViewAction();
    action->setText(text);
    action->setObjectName(objectName);
    viewMenu->addAction(action);
}

QDockWidget* MainWindow::dockForPanelId(const QString& panelId) const
{
    if (panelId == QStringLiteral("navigation"))
        return navigationPane ? navigationPane->dock() : nullptr;
    if (panelId == QStringLiteral("problems"))
        return semanticDocks && semanticDocks->problemsPanelCoordinator()
            ? semanticDocks->problemsPanelCoordinator()->dock()
            : nullptr;
    if (panelId == QStringLiteral("activity"))
        return semanticDocks && semanticDocks->activityLogPanelCoordinator()
            ? semanticDocks->activityLogPanelCoordinator()->dock()
            : nullptr;
    if (panelId == QStringLiteral("references"))
        return semanticDocks && semanticDocks->referencesPanelCoordinator()
            ? semanticDocks->referencesPanelCoordinator()->dock()
            : nullptr;
    if (panelId == QStringLiteral("relationships"))
        return semanticDocks && semanticDocks->relationshipsPanelCoordinator()
            ? semanticDocks->relationshipsPanelCoordinator()->dock()
            : nullptr;
    if (panelId == QStringLiteral("rtlInsights"))
        return semanticDocks && semanticDocks->rtlInsightsPanelCoordinator()
            ? semanticDocks->rtlInsightsPanelCoordinator()->dock()
            : nullptr;
    if (panelId == QStringLiteral("signalKernelGraph"))
        return semanticDocks && semanticDocks->signalKernelGraphPanelCoordinator()
            ? semanticDocks->signalKernelGraphPanelCoordinator()->dock()
            : nullptr;
    if (panelId == QStringLiteral("wavePreview"))
        return semanticDocks && semanticDocks->wavePreviewPanelCoordinator()
            ? semanticDocks->wavePreviewPanelCoordinator()->dock()
            : nullptr;
    if (panelId == QStringLiteral("foldShelf"))
        return foldShelfDock;
    if (panelId == QStringLiteral("editorAppearance"))
        return editorAppearanceDock;
    return nullptr;
}

void MainWindow::showDockWidget(QDockWidget* dock,
                                const QString& statusMessage)
{
    if (!dock)
        return;

    dock->show();
    dock->raise();
    dock->activateWindow();
    if (!statusMessage.isEmpty() && statusBar())
        statusBar()->showMessage(statusMessage, 3000);
}

void MainWindow::showPanelById(const QString& panelId)
{
    if (panelId == QStringLiteral("foldShelf")) {
        showFoldBlockShelf();
        return;
    }
    if (panelId == QStringLiteral("wavePreview"))
        refreshActiveEditorWavePreview();

    QDockWidget* dock = dockForPanelId(panelId);
    showDockWidget(dock);
}

void MainWindow::togglePanelById(const QString& panelId)
{
    QDockWidget* dock = dockForPanelId(panelId);
    if (!dock)
        return;

    if (dock->isVisible())
        dock->hide();
    else
        showPanelById(panelId);
}

void MainWindow::resetPanelLayout()
{
    QDockWidget* navigationDock = dockForPanelId(QStringLiteral("navigation"));
    QDockWidget* problemsDock = dockForPanelId(QStringLiteral("problems"));
    QDockWidget* activityDock = dockForPanelId(QStringLiteral("activity"));
    QDockWidget* referencesDock = dockForPanelId(QStringLiteral("references"));
    QDockWidget* relationshipsDock = dockForPanelId(QStringLiteral("relationships"));
    QDockWidget* rtlInsightsDock = dockForPanelId(QStringLiteral("rtlInsights"));
    QDockWidget* signalKernelGraphDock =
        dockForPanelId(QStringLiteral("signalKernelGraph"));
    QDockWidget* wavePreviewDock = dockForPanelId(QStringLiteral("wavePreview"));
    QDockWidget* editorAppearanceDockWidget =
        dockForPanelId(QStringLiteral("editorAppearance"));
    QDockWidget* foldShelfDockWidget = dockForPanelId(QStringLiteral("foldShelf"));

    if (navigationDock)
        addDockWidget(Qt::LeftDockWidgetArea, navigationDock);
    if (editorAppearanceDockWidget)
        addDockWidget(Qt::RightDockWidgetArea, editorAppearanceDockWidget);

    QDockWidget* bottomDocks[] = {
        problemsDock,
        activityDock,
        referencesDock,
        relationshipsDock,
        rtlInsightsDock,
        signalKernelGraphDock,
        wavePreviewDock,
        foldShelfDockWidget,
    };
    for (QDockWidget* dock : bottomDocks) {
        if (dock)
            addDockWidget(Qt::BottomDockWidgetArea, dock);
    }
    if (problemsDock) {
        for (QDockWidget* dock : bottomDocks) {
            if (dock && dock != problemsDock)
                tabifyDockWidget(problemsDock, dock);
        }
    }

    showDockWidget(navigationDock);
    showDockWidget(editorAppearanceDockWidget);
    for (QDockWidget* dock : bottomDocks)
        showDockWidget(dock);
    if (problemsDock)
        problemsDock->raise();

    if (statusBar())
        statusBar()->showMessage(tr("Panel layout reset"), 3000);
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
    formatterSettings =
        std::make_unique<FormatterSettings>();

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
    editorCoordinator->setFormatterSettings(formatterSettings.get());
    editorCoordinator->setStatusMessageHandler(
        [this](const QString& message, int timeoutMs) {
            updateEditorModeChip(message);
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
