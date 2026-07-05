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
#include "commodecoordinator.h"
#include "editorcoordinator.h"
#include "filecommandcoordinator.h"
#include "modecommandcoordinator.h"
#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "navigationpanecoordinator.h"
#include "diagnosticnavigationservice.h"
#include "diagnosticservice.h"
#include "editorappearancepanel.h"
#include "editorappearancesettings.h"
#include "formattersettings.h"
#include "foldblockshelfmodel.h"
#include "foldblockshelfpanel.h"
#include "foldshelfpersistenceservice.h"
#include "foldshelfrestoreservice.h"
#include "ghostannotationservice.h"
#include "semanticdecorationservice.h"
#include "globalcontrolcoordinator.h"
#include "globalcontrolservice.h"
#include "insightvisualstyle.h"
#include "semanticdockcoordinator.h"
#include "semanticindex.h"
#include "semanticpanelrefreshcoordinator.h"
#include "semanticruntimecoordinator.h"
#include "usertemplateservice.h"
#include "activitylogpanelcoordinator.h"
#include "activitylogservice.h"
#include "problemspanelcoordinator.h"
#include "referencespanelcoordinator.h"
#include "relationshipspanelcoordinator.h"
#include "rtlinsightspanelcoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "wavepreviewpanelcoordinator.h"
#include "workspaceconfigurationdialog.h"
#include "workspacesessionstateservice.h"
#include "version.h"
#include <QAction>
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QTextBlock>
#include <QTextCursor>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSize>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace {
DiagnosticPanelScope diagnosticScopeFromProblemsCombo(QComboBox* combo)
{
    if (!combo)
        return DiagnosticPanelScope::CurrentFile;

    switch (combo->currentData().toInt()) {
    case 1:
        return DiagnosticPanelScope::WorkspaceFiles;
    case 2:
        return DiagnosticPanelScope::AllFiles;
    default:
        return DiagnosticPanelScope::CurrentFile;
    }
}

DiagnosticSeverityFilter diagnosticSeverityFromProblemsCombo(QComboBox* combo)
{
    if (!combo)
        return DiagnosticSeverityFilter::All;

    switch (combo->currentData().toInt()) {
    case 1:
        return DiagnosticSeverityFilter::Errors;
    case 2:
        return DiagnosticSeverityFilter::Warnings;
    case 3:
        return DiagnosticSeverityFilter::Info;
    default:
        return DiagnosticSeverityFilter::All;
    }
}

QString workspaceSessionRootKey(const QString& path)
{
    if (path.isEmpty())
        return QString();
    QString normalized = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    tabManager = std::unique_ptr<TabManager>(new TabManager(ui->tabWidget, this));
    workspaceManager = std::unique_ptr<WorkspaceManager>(new WorkspaceManager(this));
    setupWorkspaceBar();
    setupWorkspaceProgressIndicator();
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
    setupWorkspaceMenu();
    setupViewMenu();
    setupToolsMenu();
    setupEditorModeChip();
    setupGlobalControl();
    setupComMode();
    setupEditorCoordinator();
    setupManagerConnections();
    applyModernShellStyle();

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
        versionLabel->setStyleSheet(
            InsightVisualStyle::labelStyleSheet(versionLabel->objectName()));
        statusBar()->addPermanentWidget(versionLabel);
        statusBar()->showMessage(QStringLiteral("Ready"));
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::applyModernShellStyle()
{
    if (menuBar()) {
        if (!findChild<QMenu*>(QStringLiteral("navigateMenu"))) {
            QAction* beforeTools = toolsMenu ? toolsMenu->menuAction() : nullptr;
            QMenu* navigateMenu = new QMenu(tr("&Navigate"), this);
            navigateMenu->setObjectName(QStringLiteral("navigateMenu"));
            menuBar()->insertMenu(beforeTools, navigateMenu);
        }
        if (!findChild<QMenu*>(QStringLiteral("searchMenu"))) {
            QAction* beforeTools = toolsMenu ? toolsMenu->menuAction() : nullptr;
            QMenu* searchMenu = new QMenu(tr("&Search"), this);
            searchMenu->setObjectName(QStringLiteral("searchMenu"));
            menuBar()->insertMenu(beforeTools, searchMenu);
        }
        if (!findChild<QMenu*>(QStringLiteral("helpMenu"))) {
            QMenu* helpMenu = new QMenu(tr("&Help"), this);
            helpMenu->setObjectName(QStringLiteral("helpMenu"));
            menuBar()->addMenu(helpMenu);
        }
    }

    setStyleSheet(InsightVisualStyle::applicationStyleSheet());

    if (ui && ui->tabWidget) {
        ui->tabWidget->setDocumentMode(true);
        ui->tabWidget->setIconSize(QSize(14, 14));
    }
    if (workspaceTabBar)
        workspaceTabBar->setDrawBase(false);
}

void MainWindow::setupShellNavigationRail()
{
    if (shellNavigationRailDock)
        return;

    auto* rail = new QWidget(this);
    rail->setObjectName(QStringLiteral("shellNavigationRail"));
    auto* layout = new QVBoxLayout(rail);
    layout->setContentsMargins(6, 8, 6, 8);
    layout->setSpacing(6);

    auto addRailButton = [this, layout, rail](const QString& id,
                                              const QString& text,
                                              const QString& targetPanel = {}) {
        auto* button = new QToolButton(rail);
        button->setObjectName(QStringLiteral("shellRail_%1").arg(id));
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setAutoRaise(false);
        button->setCheckable(id == QStringLiteral("insights"));
        button->setChecked(id == QStringLiteral("insights"));
        button->setFixedSize(56, 54);
        button->setStyleSheet(
            InsightVisualStyle::sideRailButtonStyleSheet(
                button->objectName()));
        if (!targetPanel.isEmpty()) {
            connect(button,
                    &QToolButton::clicked,
                    this,
                    [this, targetPanel]() { showPanelById(targetPanel); });
        } else if (id == QStringLiteral("explorer")
                   || id == QStringLiteral("outline")
                   || id == QStringLiteral("design")
                   || id == QStringLiteral("search")) {
            connect(button,
                    &QToolButton::clicked,
                    this,
                    [this]() {
                        if (navigationPane && navigationPane->dock())
                            showDockWidget(navigationPane->dock());
                    });
        }
        layout->addWidget(button);
        return button;
    };

    addRailButton(QStringLiteral("explorer"), QStringLiteral("Explorer"));
    addRailButton(QStringLiteral("outline"), QStringLiteral("Outline"));
    addRailButton(QStringLiteral("design"), QStringLiteral("Design"));
    addRailButton(QStringLiteral("problems"), QStringLiteral("Problems"),
                  QStringLiteral("problems"));
    addRailButton(QStringLiteral("search"), QStringLiteral("Search"));
    addRailButton(QStringLiteral("insights"), QStringLiteral("Insights"),
                  QStringLiteral("rtlInsights"));
    layout->addStretch(1);
    addRailButton(QStringLiteral("settings"), QStringLiteral("Settings"),
                  QStringLiteral("editorAppearance"));

    rail->setStyleSheet(
        InsightVisualStyle::sideRailStyleSheet(rail->objectName()));

    shellNavigationRailDock = new QDockWidget(this);
    shellNavigationRailDock->setObjectName(QStringLiteral("shellNavigationRailDock"));
    shellNavigationRailDock->setWidget(rail);
    shellNavigationRailDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    shellNavigationRailDock->setTitleBarWidget(new QWidget(shellNavigationRailDock));
    shellNavigationRailDock->setMinimumWidth(68);
    shellNavigationRailDock->setMaximumWidth(68);
    rail->setMinimumWidth(66);
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
    workspaceTabBar->setTabsClosable(true);
    workspaceTabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    workspaceTabBar->hide();
    workspaceTabBar->setStyleSheet(
        InsightVisualStyle::workspaceTabBarStyleSheet(
            workspaceTabBar->objectName()));
    layout->addWidget(workspaceTabBar);
    setupPackageTools(layout, editorContainer);
    layout->addWidget(ui->tabWidget, 1);
    setCentralWidget(editorContainer);

    connect(workspaceTabBar,
            &QTabBar::currentChanged,
            this,
            [this](int index) {
                if (workspaceManager
                    && index != workspaceManager->activeWorkspaceIndex()) {
                    saveWorkspaceSession(false);
                    workspaceManager->switchWorkspace(index);
                }
            });
    connect(workspaceTabBar,
            &QTabBar::tabCloseRequested,
            this,
            &MainWindow::closeWorkspaceTab);
    connect(workspaceTabBar,
            &QWidget::customContextMenuRequested,
            this,
            &MainWindow::showWorkspaceTabContextMenu);
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceListChanged,
            this,
            &MainWindow::refreshWorkspaceTabs);
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceActivated,
            this,
            [this](int, const QString&, const QString& path) {
                UserTemplateService::getInstance()->setWorkspaceRoot(path);
                UserTemplateService::getInstance()->reload();
                if (foldShelfModel)
                    foldShelfModel->setWorkspaceRoot(path);
                refreshWorkspaceTabs();
                noteWorkspaceSessionAvailability();
            });
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceClosed,
            this,
            [this]() {
                const QString activePath =
                    workspaceManager ? workspaceManager->getWorkspacePath()
                                     : QString();
                UserTemplateService::getInstance()->setWorkspaceRoot(activePath);
                UserTemplateService::getInstance()->reload();
                if (foldShelfModel)
                    foldShelfModel->setWorkspaceRoot(activePath);
                refreshWorkspaceTabs();
            });
}

void MainWindow::setupPackageTools(QVBoxLayout* editorLayout, QWidget* parent)
{
    if (!editorLayout)
        return;

    packageToolsBar = new QWidget(parent ? parent : this);
    packageToolsBar->setObjectName(QStringLiteral("packageToolsBar"));
    auto* layout = new QHBoxLayout(packageToolsBar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    QLabel* title =
        new QLabel(QStringLiteral("Package Tools"), packageToolsBar);
    title->setObjectName(QStringLiteral("packageToolsTitle"));
    title->setStyleSheet(
        InsightVisualStyle::labelStyleSheet(title->objectName(), true));
    layout->addWidget(title);

    packageToolsPackageLabel = new QLabel(packageToolsBar);
    packageToolsPackageLabel->setObjectName(
        QStringLiteral("packageToolsPackageLabel"));
    packageToolsPackageLabel->setStyleSheet(
        InsightVisualStyle::labelStyleSheet(
            packageToolsPackageLabel->objectName()));
    layout->addWidget(packageToolsPackageLabel);

    const PackageToolService service;
    for (PackageToolKind kind : PackageToolService::toolOrder()) {
        auto* button = new QToolButton(packageToolsBar);
        button->setObjectName(
            QStringLiteral("packageToolButton_%1")
                .arg(PackageToolService::idForKind(kind)));
        button->setText(PackageToolService::labelForKind(kind));
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setToolTip(
            QStringLiteral("Insert %1").arg(service.labelForKind(kind)));
        connect(button,
                &QToolButton::clicked,
                this,
                [this, kind]() { insertPackageTool(kind); });
        packageToolButtons.append(button);
        layout->addWidget(button);
    }

    layout->addStretch(1);
    packageToolsBar->setStyleSheet(
        InsightVisualStyle::packageToolsBarStyleSheet(
            packageToolsBar->objectName()));
    packageToolsBar->hide();
    editorLayout->addWidget(packageToolsBar);
}

void MainWindow::updatePackageTools()
{
    if (!packageToolsBar)
        return;

    MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor()
                                      : nullptr;
    const EditorPackageToolAvailability availability =
        editor ? editor->currentPackageToolAvailability()
               : EditorPackageToolAvailability();
    packageToolsBar->setVisible(availability.available);

    const QString packageText =
        availability.packageName.isEmpty()
            ? QStringLiteral("package")
            : QStringLiteral("package %1").arg(availability.packageName);
    if (packageToolsPackageLabel)
        packageToolsPackageLabel->setText(packageText);

    const QString tooltip =
        availability.available
            ? QStringLiteral("Insert definition in %1").arg(packageText)
            : availability.failureMessage;
    for (QToolButton* button : std::as_const(packageToolButtons)) {
        if (!button)
            continue;
        button->setEnabled(availability.available);
        if (!tooltip.isEmpty())
            button->setToolTip(tooltip);
    }
}

void MainWindow::insertPackageTool(PackageToolKind kind)
{
    MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor()
                                      : nullptr;
    if (!editor) {
        if (statusBar())
            statusBar()->showMessage(QStringLiteral("No active editor"), 3000);
        return;
    }

    QString message;
    const bool inserted = editor->executePackageToolInsert(kind, &message);
    if (statusBar() && !message.isEmpty())
        statusBar()->showMessage(message, inserted ? 3000 : 5000);
    updatePackageTools();
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
    QStringList workspaceRoots;
    workspaceRoots.reserve(entries.size());
    for (const WorkspaceManager::WorkspaceEntry& entry : entries) {
        const int tab = workspaceTabBar->addTab(entry.alias);
        workspaceTabBar->setTabToolTip(tab, QDir::toNativeSeparators(entry.path));
        workspaceRoots.append(entry.path);
    }
    workspaceTabBar->setVisible(!entries.isEmpty());
    const int activeIndex = workspaceManager->activeWorkspaceIndex();
    if (activeIndex >= 0 && activeIndex < workspaceTabBar->count())
        workspaceTabBar->setCurrentIndex(activeIndex);

    const QString activeWorkspacePath =
        activeIndex >= 0 && activeIndex < entries.size()
            ? entries.at(activeIndex).path
            : QString();
    if (tabManager)
        tabManager->setWorkspaceScope(workspaceRoots, activeWorkspacePath);
}

void MainWindow::closeWorkspaceTab(int index)
{
    if (!workspaceManager || !tabManager)
        return;

    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    if (index < 0 || index >= entries.size())
        return;

    const bool closingActive =
        index == workspaceManager->activeWorkspaceIndex();
    if (closingActive)
        saveWorkspaceSession(false);

    if (!tabManager->closeTabsInWorkspace(entries.at(index).path))
        return;

    workspaceManager->closeWorkspace(index);
}

void MainWindow::showWorkspaceTabContextMenu(const QPoint& position)
{
    if (!workspaceTabBar)
        return;

    const int index = workspaceTabBar->tabAt(position);
    if (index < 0)
        return;

    QMenu menu(this);
    QAction* renameAction = menu.addAction(QStringLiteral("Rename"));
    QAction* chosen = menu.exec(workspaceTabBar->mapToGlobal(position));
    if (chosen == renameAction)
        renameWorkspaceTab(index);
}

void MainWindow::renameWorkspaceTab(int index)
{
    if (!workspaceManager)
        return;

    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    if (index < 0 || index >= entries.size())
        return;

    bool accepted = false;
    const QString alias = QInputDialog::getText(
        this,
        QStringLiteral("Rename Workspace"),
        QStringLiteral("Alias"),
        QLineEdit::Normal,
        entries.at(index).alias,
        &accepted).trimmed();
    if (!accepted)
        return;

    QString errorMessage;
    if (!workspaceManager->renameWorkspaceAlias(index, alias, &errorMessage)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Rename Workspace"),
            errorMessage.isEmpty()
                ? QStringLiteral("Unable to rename workspace.")
                : errorMessage);
    }
}

void MainWindow::setupWorkspaceProgressIndicator()
{
    if (!statusBar() || !workspaceManager)
        return;

    workspaceProgressBar = new QProgressBar(this);
    workspaceProgressBar->setObjectName(QStringLiteral("workspaceProgressBar"));
    workspaceProgressBar->setRange(0, 0);
    workspaceProgressBar->setTextVisible(true);
    workspaceProgressBar->setFixedWidth(180);
    workspaceProgressBar->hide();
    statusBar()->addPermanentWidget(workspaceProgressBar);

    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceScanStarted,
            this,
            [this](const QString&) {
                if (workspaceProgressBar) {
                    workspaceProgressBar->setRange(0, 0);
                    workspaceProgressBar->setFormat(QStringLiteral("Scanning workspace"));
                    workspaceProgressBar->show();
                }
                if (statusBar())
                    statusBar()->showMessage(QStringLiteral("Scanning workspace..."));
            });
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceScanProgress,
            this,
            [this](const QString&, int filesFound) {
                if (workspaceProgressBar)
                    workspaceProgressBar->setFormat(
                        QStringLiteral("Scanning %1 files").arg(filesFound));
                if (statusBar()) {
                    statusBar()->showMessage(
                        QStringLiteral("Scanning workspace: %1 files found")
                            .arg(filesFound),
                        1000);
                }
            });
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceScanFinished,
            this,
            [this](const QString&, int totalFiles, int systemVerilogFiles) {
                if (workspaceProgressBar)
                    workspaceProgressBar->hide();
                if (statusBar()) {
                    statusBar()->showMessage(
                        QStringLiteral("Workspace scan complete: %1 files, %2 SystemVerilog")
                            .arg(totalFiles)
                            .arg(systemVerilogFiles),
                        3000);
                }
            });
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
                scheduleActiveEditorPassiveRefresh();
                updatePackageTools();
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
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor) {
                                updatePackageTools();
                            }
                            QDockWidget* waveDock =
                                dockForPanelId(QStringLiteral("wavePreview"));
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor
                                && waveDock
                                && waveDock->isVisible()) {
                                refreshActiveEditorWavePreview();
                            }
                        });
                connect(editor,
                        &MyCodeEditor::cursorPositionChanged,
                        this,
                        [this, editor]() {
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor) {
                                updatePackageTools();
                            }
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
                setDiagnosticsAnalysisState(QStringLiteral("current"));
                scheduleActiveEditorPassiveRefresh(fileName);
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::fileSymbolAnalysisStarted,
            this,
            [this](const QString&) {
                setDiagnosticsAnalysisState(QStringLiteral("analyzing"));
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisStarted,
            this,
            [this](const ProjectSnapshot&, int) {
                setDiagnosticsAnalysisState(QStringLiteral("analyzing"));
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot&, int, int) {
                setDiagnosticsAnalysisState(QStringLiteral("current"));
                scheduleActiveEditorPassiveRefresh();
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisDeferred,
            this,
            [this](const ProjectSnapshot&, int, qint64, qint64) {
                setDiagnosticsAnalysisState(QStringLiteral("background"));
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisCancelled,
            this,
            [this](const WorkspaceAnalysisRequestTelemetry&) {
                setDiagnosticsAnalysisState(QStringLiteral("stale"));
            });

}

void MainWindow::scheduleActiveEditorPassiveRefresh(
    const QString& changedFileName)
{
    if (!activeEditorPassiveRefreshTimer) {
        activeEditorPassiveRefreshTimer = new QTimer(this);
        activeEditorPassiveRefreshTimer->setSingleShot(true);
        connect(activeEditorPassiveRefreshTimer,
                &QTimer::timeout,
                this,
                &MainWindow::runActiveEditorPassiveRefresh);
    }

    if (changedFileName.isEmpty()) {
        pendingActiveEditorPassiveRefreshAll = true;
        pendingActiveEditorPassiveRefreshFile.clear();
    } else if (!pendingActiveEditorPassiveRefreshAll
               && pendingActiveEditorPassiveRefreshFile.isEmpty()) {
        pendingActiveEditorPassiveRefreshFile = changedFileName;
    } else if (!pendingActiveEditorPassiveRefreshAll
               && pendingActiveEditorPassiveRefreshFile != changedFileName) {
        pendingActiveEditorPassiveRefreshAll = true;
        pendingActiveEditorPassiveRefreshFile.clear();
    }

    activeEditorPassiveRefreshTimer->start(50);
}

void MainWindow::runActiveEditorPassiveRefresh()
{
    const QString changedFileName = pendingActiveEditorPassiveRefreshAll
        ? QString()
        : pendingActiveEditorPassiveRefreshFile;
    pendingActiveEditorPassiveRefreshFile.clear();
    pendingActiveEditorPassiveRefreshAll = false;
    refreshActiveEditorDiagnosticHighlights(changedFileName);
    refreshActiveEditorSemanticDecorations(changedFileName);
    refreshActiveEditorGhostAnnotations(changedFileName);
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

    constexpr int kMaxPassiveSemanticDecorationCharacters = 2 * 1024 * 1024;
    if (editor->document()->characterCount()
        > kMaxPassiveSemanticDecorationCharacters) {
        editor->setSemanticDecorations({});
        return;
    }

    SemanticDecorationQuery query;
    query.fileName = document.fileName;
    query.documentText = editor->toPlainText();
    if (workspaceManager)
        query.configuredDefines = workspaceManager->workspaceConfiguration().defines;
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

    constexpr int kMaxPassiveGhostAnnotationCharacters = 2 * 1024 * 1024;
    if (editor->document()->characterCount()
        > kMaxPassiveGhostAnnotationCharacters) {
        editor->setGhostAnnotations({});
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
    const EditorAlwaysScopeTarget alwaysScope =
        editor->currentAlwaysScopeTarget();
    if (alwaysScope.ok()) {
        semanticDocks->wavePreviewPanelCoordinator()->refreshFromDocument(
            document.fileName,
            editor->toPlainText(),
            document.dirty,
            alwaysScope.startPosition,
            alwaysScope.endPosition,
            alwaysScope.label);
        return;
    }

    const EditorModuleScopeTarget moduleScope =
        editor->currentModuleScopeTarget();
    if (!moduleScope.ok()) {
        semanticDocks->wavePreviewPanelCoordinator()->renderUnavailable(
            moduleScope.failureMessage.isEmpty()
                ? QStringLiteral("Place the cursor in a module or always block to preview.")
                : moduleScope.failureMessage);
        return;
    }

    semanticDocks->wavePreviewPanelCoordinator()->refreshFromDocument(
        document.fileName,
        editor->toPlainText(),
        document.dirty,
        moduleScope.startPosition,
        moduleScope.endPosition,
        moduleScope.label);
}


void MainWindow::setupNavigationPane()
{
    navigationPane = std::make_unique<NavigationPaneCoordinator>(this);
    navigationPane->attachNavigationManager(navigationManager.get());
    navigationPane->connectNavigationInputs(
        tabManager.get(),
        workspaceManager.get());
    setupShellNavigationRail();
    addDockWidget(Qt::LeftDockWidgetArea, shellNavigationRailDock);
    addDockWidget(Qt::LeftDockWidgetArea, navigationPane->dock());
    splitDockWidget(shellNavigationRailDock,
                    navigationPane->dock(),
                    Qt::Horizontal);
}

void MainWindow::setupNavigationCommandCoordinator()
{
    navigationCommandCoordinator = std::make_unique<NavigationCommandCoordinator>(
        tabManager.get(),
        navigationManager.get(),
        workspaceManager.get(),
        this);
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
    if (ui->save_file) {
        ui->save_file->setShortcutContext(Qt::ApplicationShortcut);
        addAction(ui->save_file);
    }
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

            if (item.id == QStringLiteral("ow r")) {
                showRecentWorkspacesDialog();
            } else if (item.id == QStringLiteral("ow s save")) {
                saveWorkspaceSession(true);
            } else if (item.id == QStringLiteral("ow s restore")) {
                restoreWorkspaceSession();
            } else if (item.id == QStringLiteral("ow s clean")) {
                cleanWorkspaceSession();
            } else if (item.id == QStringLiteral("ow s")) {
                if (statusBar())
                    statusBar()->showMessage(
                        QStringLiteral("Use ow s save, ow s restore, or ow s clean"),
                        4000);
            } else if (item.id.startsWith(QStringLiteral("ow "))) {
                bool ok = false;
                const int count = item.id.mid(3).trimmed().toInt(&ok);
                if (ok && count > 0 && fileCommandCoordinator) {
                    for (int i = 0; i < count; ++i)
                        fileCommandCoordinator->openDirectoryAsWorkspace();
                }
            } else if (item.id == QStringLiteral("ow")) {
                if (statusBar())
                    statusBar()->showMessage(
                        QStringLiteral("Use ow <num>, for example ow 1"),
                        3000);
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
            } else if (item.id == QStringLiteral("fd r")
                       || item.id == QStringLiteral("fd")) {
                if (MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr)
                    editor->startFoldRegionMarkMode();
            } else if (item.id == QStringLiteral("fd s")
                       || item.id == QStringLiteral("fds")) {
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

void MainWindow::setupComMode()
{
    comModeCoordinator = std::make_unique<ComModeCoordinator>(
        statusBar(),
        this,
        tabManager.get(),
        workspaceManager ? workspaceManager->getProjectModel() : nullptr,
        SemanticIndex::getInstance(),
        navigationCommandCoordinator.get(),
        this);
    comModeCoordinator->connectSignals();
}

void MainWindow::setupFoldBlockShelf()
{
    foldShelfModel = std::make_unique<FoldBlockShelfModel>(this);
    foldShelfModel->setPersistenceService(
        FoldShelfPersistenceService::getInstance());
    if (workspaceManager)
        foldShelfModel->setWorkspaceRoot(workspaceManager->getWorkspacePath());
    foldShelfDock = new QDockWidget(QStringLiteral("Fold Shelf"), this);
    foldShelfDock->setObjectName(QStringLiteral("FoldShelfDock"));
    foldShelfPanel = new FoldBlockShelfPanel(foldShelfDock);
    foldShelfPanel->setModel(foldShelfModel.get());
    connect(foldShelfPanel,
            &FoldBlockShelfPanel::restoreItemRequested,
            this,
            &MainWindow::restoreFoldShelfItem);
    connect(foldShelfPanel,
            &FoldBlockShelfPanel::restoreToActiveEditorRequested,
            this,
            &MainWindow::restoreFoldShelfItemToActiveEditor);
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

void MainWindow::setupWorkspaceMenu()
{
    if (!menuBar() || workspaceMenu)
        return;

    workspaceMenu = menuBar()->addMenu(tr("&Workspace"));
    workspaceMenu->setObjectName(QStringLiteral("workspaceMenu"));

    QAction* configureAction =
        workspaceMenu->addAction(tr("Workspace Configuration..."));
    configureAction->setObjectName(
        QStringLiteral("workspaceConfigurationAction"));
    connect(configureAction,
            &QAction::triggered,
            this,
            &MainWindow::showWorkspaceConfigurationDialog);

    workspaceMenu->addSeparator();
    QAction* nextDiagnosticAction =
        workspaceMenu->addAction(tr("Next Diagnostic"));
    nextDiagnosticAction->setObjectName(
        QStringLiteral("nextDiagnosticAction"));
    nextDiagnosticAction->setShortcut(QKeySequence(Qt::Key_F8));
    nextDiagnosticAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(nextDiagnosticAction);
    connect(nextDiagnosticAction,
            &QAction::triggered,
            this,
            [this]() { navigateDiagnostic(false); });

    QAction* previousDiagnosticAction =
        workspaceMenu->addAction(tr("Previous Diagnostic"));
    previousDiagnosticAction->setObjectName(
        QStringLiteral("previousDiagnosticAction"));
    previousDiagnosticAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8));
    previousDiagnosticAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(previousDiagnosticAction);
    connect(previousDiagnosticAction,
            &QAction::triggered,
            this,
            [this]() { navigateDiagnostic(true); });
}

void MainWindow::setupToolsMenu()
{
    if (!menuBar() || toolsMenu)
        return;

    toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->setObjectName(QStringLiteral("toolsMenu"));

    userTemplatesMenu = toolsMenu->addMenu(tr("User Templates"));
    userTemplatesMenu->setObjectName(QStringLiteral("userTemplatesMenu"));

    QAction* openGlobalAction =
        userTemplatesMenu->addAction(tr("Open Global User Templates"));
    openGlobalAction->setObjectName(
        QStringLiteral("openGlobalUserTemplatesAction"));
    connect(openGlobalAction,
            &QAction::triggered,
            this,
            &MainWindow::openGlobalUserTemplates);

    QAction* openWorkspaceAction =
        userTemplatesMenu->addAction(tr("Open Workspace User Templates"));
    openWorkspaceAction->setObjectName(
        QStringLiteral("openWorkspaceUserTemplatesAction"));
    connect(openWorkspaceAction,
            &QAction::triggered,
            this,
            &MainWindow::openWorkspaceUserTemplates);

    userTemplatesMenu->addSeparator();
    QAction* reloadAction =
        userTemplatesMenu->addAction(tr("Reload User Templates"));
    reloadAction->setObjectName(QStringLiteral("reloadUserTemplatesAction"));
    connect(reloadAction,
            &QAction::triggered,
            this,
            &MainWindow::reloadUserTemplates);
}

void MainWindow::openGlobalUserTemplates()
{
    openUserTemplateFile(
        UserTemplateService::getInstance()->globalTemplateLocation(),
        QStringLiteral("global user templates"));
}

void MainWindow::openWorkspaceUserTemplates()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) {
        if (statusBar()) {
            statusBar()->showMessage(
                QStringLiteral("Open a workspace before opening workspace user templates"),
                5000);
        }
        return;
    }

    UserTemplateService* service = UserTemplateService::getInstance();
    service->setWorkspaceRoot(workspaceManager->getWorkspacePath());
    openUserTemplateFile(service->workspaceTemplateLocation(),
                         QStringLiteral("workspace user templates"));
}

bool MainWindow::openUserTemplateFile(const QString& filePath,
                                      const QString& label)
{
    QString errorMessage;
    if (!ensureUserTemplateJsonFile(filePath, &errorMessage)) {
        if (errorMessage.isEmpty())
            errorMessage = QStringLiteral("Failed to prepare user template file.");
        if (statusBar())
            statusBar()->showMessage(errorMessage, 5000);
        QMessageBox::warning(this, tr("User Templates"), errorMessage);
        return false;
    }

    if (!tabManager || !tabManager->openFileInTab(filePath)) {
        const QString message =
            QStringLiteral("Failed to open %1.").arg(label);
        if (statusBar())
            statusBar()->showMessage(message, 5000);
        QMessageBox::warning(this, tr("User Templates"), message);
        return false;
    }

    if (statusBar()) {
        statusBar()->showMessage(
            QStringLiteral("Opened %1").arg(label),
            3000);
    }
    return true;
}

bool MainWindow::ensureUserTemplateJsonFile(const QString& filePath,
                                            QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();
    if (filePath.trimmed().isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("No user template file path is configured.");
        return false;
    }

    const QFileInfo fileInfo(filePath);
    if (fileInfo.exists()) {
        if (fileInfo.isFile())
            return true;
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("User template path exists but is not a file: %1")
                    .arg(filePath);
        }
        return false;
    }

    QDir parentDir = fileInfo.dir();
    if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Failed to create user template directory: %1")
                    .arg(parentDir.absolutePath());
        }
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Failed to create user template file: %1")
                    .arg(filePath);
        }
        return false;
    }
    file.write("{\n  \"templates\": []\n}\n");
    return true;
}

void MainWindow::reloadUserTemplates()
{
    const UserTemplateLoadReport report =
        UserTemplateService::getInstance()->reload();
    const QString summary = userTemplateReloadSummary(report);
    if (statusBar())
        statusBar()->showMessage(summary, report.issues.isEmpty() ? 3000 : 7000);

    if (!report.issues.isEmpty()) {
        QMessageBox::warning(this,
                             tr("User Templates"),
                             userTemplateIssueReportText(report));
    }
}

QString MainWindow::userTemplateReloadSummary(
    const UserTemplateLoadReport& report) const
{
    return QStringLiteral("User templates loaded: %1, ignored: %2")
        .arg(report.records.size())
        .arg(report.issues.size());
}

QString MainWindow::userTemplateIssueReportText(
    const UserTemplateLoadReport& report) const
{
    QStringList lines;
    lines.append(userTemplateReloadSummary(report));
    lines.append(QString());

    constexpr int kMaxVisibleIssues = 20;
    for (int i = 0; i < report.issues.size() && i < kMaxVisibleIssues; ++i) {
        const UserTemplateIssue& issue = report.issues.at(i);
        const QString source = issue.source.trimmed().isEmpty()
            ? QStringLiteral("<unknown file>")
            : issue.source;
        const QString command = issue.id.trimmed().isEmpty()
            ? QStringLiteral("<unknown command>")
            : issue.id;
        const QString field = issue.field.trimmed().isEmpty()
            ? QStringLiteral("<unknown field>")
            : issue.field;
        lines.append(QStringLiteral("%1 | command %2 | field %3 | %4")
                         .arg(source, command, field, issue.reason));
    }
    if (report.issues.size() > kMaxVisibleIssues) {
        lines.append(QStringLiteral("... %1 more issue(s)")
                         .arg(report.issues.size() - kMaxVisibleIssues));
    }
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::showWorkspaceConfigurationDialog()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) {
        if (statusBar())
            statusBar()->showMessage(
                QStringLiteral("Open a workspace before configuring it"),
                3000);
        return;
    }

    WorkspaceConfigurationDialog dialog(this);
    dialog.setConfiguration(workspaceManager->workspaceConfiguration());
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString errorMessage;
    if (!workspaceManager->setWorkspaceConfiguration(dialog.configuration(),
                                                     &errorMessage)) {
        if (errorMessage.isEmpty())
            errorMessage = QStringLiteral("Failed to apply workspace configuration.");
        QMessageBox::warning(this,
                             tr("Workspace Configuration"),
                             errorMessage);
        return;
    }

    if (statusBar()) {
        statusBar()->showMessage(
            QStringLiteral("Workspace configuration saved; analysis queued"),
            4000);
    }
    scheduleWorkspaceSessionSave();
    if (semanticDocks && semanticDocks->refreshCoordinator())
        semanticDocks->refreshCoordinator()->updateProblemsPanel();
}

WorkspaceSessionState MainWindow::captureWorkspaceSessionState() const
{
    WorkspaceSessionState state;
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return state;

    const QString workspaceRoot = workspaceManager->getWorkspacePath();
    state.workspaceRoot = workspaceRoot;
    state.configuration = workspaceManager->workspaceConfiguration();
    if (tabManager)
        state.tabs = tabManager->workspaceSessionTabs(workspaceRoot);
    state.ui.mainWindowGeometry = saveGeometry();
    state.ui.mainWindowState = saveState();

    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    const int activeIndex = workspaceManager->activeWorkspaceIndex();
    if (activeIndex >= 0 && activeIndex < entries.size()) {
        const WorkspaceManager::WorkspaceEntry& entry =
            entries.at(activeIndex);
        if (entry.path == workspaceRoot) {
            state.scannedFiles = entry.scannedFiles;
            state.scanComplete = entry.scanComplete;
        }
    }
    if (state.scannedFiles.isEmpty()) {
        const ProjectSnapshot snapshot = workspaceManager->projectSnapshot();
        state.scannedFiles = snapshot.allFiles;
    }
    return state;
}

bool MainWindow::saveWorkspaceSession(bool showStatus)
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) {
        if (showStatus && statusBar())
            statusBar()->showMessage(
                QStringLiteral("Open a workspace before saving a session"),
                3000);
        return false;
    }

    WorkspaceSessionStateService service;
    const WorkspaceSessionSaveResult result =
        service.save(captureWorkspaceSessionState());
    if (showStatus && statusBar()) {
        statusBar()->showMessage(
            result.saved
                ? QStringLiteral("Workspace session saved to %1")
                      .arg(QDir::toNativeSeparators(result.sessionFilePath))
                : result.message,
            result.saved ? 3000 : 5000);
    }
    return result.saved;
}

bool MainWindow::restoreWorkspaceSession()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) {
        if (statusBar())
            statusBar()->showMessage(
                QStringLiteral("Open a workspace before restoring a session"),
                3000);
        return false;
    }

    const QString workspaceRoot = workspaceManager->getWorkspacePath();
    const QString rootKey = workspaceSessionRootKey(workspaceRoot);
    if (workspaceSessionCleanRoots.contains(rootKey)) {
        if (statusBar())
            statusBar()->showMessage(
                QStringLiteral("Workspace session ignored for this activation"),
                3000);
        return false;
    }

    WorkspaceSessionStateService service;
    const WorkspaceSessionRestoreResult result =
        service.load(workspaceRoot);
    if (!result.loaded) {
        if (statusBar())
            statusBar()->showMessage(result.message, 5000);
        return false;
    }

    QString errorMessage;
    WorkspaceConfiguration configuration = result.state.configuration;
    configuration.workspaceRoot = workspaceRoot;
    const bool configurationApplied =
        workspaceManager->setWorkspaceConfiguration(configuration,
                                                   &errorMessage);

    const bool scanRestored =
        workspaceManager->restoreSessionScanState(
            result.state.scannedFiles,
            result.state.scanComplete);

    QStringList skippedTabs = result.skippedTabs;
    QStringList tabRestoreSkips;
    const QStringList restoredTabs =
        tabManager
            ? tabManager->restoreWorkspaceSessionTabs(workspaceRoot,
                                                      result.state.tabs,
                                                      &tabRestoreSkips)
            : QStringList();
    skippedTabs.append(tabRestoreSkips);
    skippedTabs.removeDuplicates();

    bool geometryRestored = true;
    bool dockStateRestored = true;
    if (!result.state.ui.mainWindowGeometry.isEmpty())
        geometryRestored = restoreGeometry(result.state.ui.mainWindowGeometry);
    if (!result.state.ui.mainWindowState.isEmpty())
        dockStateRestored = restoreState(result.state.ui.mainWindowState);
    if (!dockStateRestored)
        resetPanelLayout();

    QStringList notes;
    if (!configurationApplied) {
        notes.append(errorMessage.isEmpty()
                         ? QStringLiteral("configuration skipped")
                         : errorMessage);
    }
    if (!scanRestored)
        notes.append(QStringLiteral("scan list skipped"));
    if (!geometryRestored || !dockStateRestored)
        notes.append(QStringLiteral("layout fallback used"));
    if (!skippedTabs.isEmpty())
        notes.append(QStringLiteral("%1 tab(s) skipped").arg(skippedTabs.size()));
    if (!result.skippedScannedFiles.isEmpty()) {
        notes.append(QStringLiteral("%1 scanned file(s) skipped")
                         .arg(result.skippedScannedFiles.size()));
    }
    if (!result.externalPaths.isEmpty()) {
        notes.append(QStringLiteral("%1 external path(s)")
                         .arg(result.externalPaths.size()));
    }

    QString message =
        QStringLiteral("Workspace session restored: %1 tab(s), %2 scanned file(s)")
            .arg(restoredTabs.size())
            .arg(result.state.scannedFiles.size());
    if (!notes.isEmpty())
        message += QStringLiteral(" (%1)").arg(notes.join(QStringLiteral("; ")));
    if (statusBar())
        statusBar()->showMessage(message, notes.isEmpty() ? 4000 : 7000);
    scheduleWorkspaceSessionSave();
    return configurationApplied && scanRestored && dockStateRestored;
}

void MainWindow::cleanWorkspaceSession()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen()) {
        if (statusBar())
            statusBar()->showMessage(
                QStringLiteral("Open a workspace before ignoring a session"),
                3000);
        return;
    }

    workspaceSessionCleanRoots.insert(
        workspaceSessionRootKey(workspaceManager->getWorkspacePath()));
    if (statusBar())
        statusBar()->showMessage(
            QStringLiteral("Workspace session ignored for this activation"),
            3000);
}

void MainWindow::scheduleWorkspaceSessionSave()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return;

    if (!workspaceSessionSaveTimer) {
        workspaceSessionSaveTimer = new QTimer(this);
        workspaceSessionSaveTimer->setSingleShot(true);
        connect(workspaceSessionSaveTimer,
                &QTimer::timeout,
                this,
                [this]() { saveWorkspaceSession(false); });
    }
    workspaceSessionSaveTimer->start(900);
}

void MainWindow::noteWorkspaceSessionAvailability()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return;

    const QString workspaceRoot = workspaceManager->getWorkspacePath();
    if (workspaceSessionCleanRoots.contains(workspaceSessionRootKey(workspaceRoot)))
        return;
    if (WorkspaceSessionStateService::sessionFileExists(workspaceRoot)
        && statusBar()) {
        statusBar()->showMessage(
            QStringLiteral("Workspace session available: use ow s restore"),
            4000);
    }
}

void MainWindow::navigateDiagnostic(bool previous)
{
    ProblemsPanelCoordinator* problemsPanel =
        semanticDocks ? semanticDocks->problemsPanelCoordinator() : nullptr;
    MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr;
    const DocumentSnapshot document =
        tabManager ? tabManager->getCurrentDocument() : DocumentSnapshot();

    DiagnosticNavigationQuery query;
    query.currentFileName = document.fileName;
    if (editor) {
        const QTextCursor cursor = editor->textCursor();
        query.currentLine = cursor.blockNumber() + 1;
        query.currentColumn = cursor.positionInBlock() + 1;
    }
    query.previous = previous;
    query.workspaceFiles =
        workspaceManager ? workspaceManager->getSystemVerilogFiles()
                         : QStringList();
    query.scope = diagnosticScopeFromProblemsCombo(
        problemsPanel ? problemsPanel->scopeCombo() : nullptr);
    query.severity = diagnosticSeverityFromProblemsCombo(
        problemsPanel ? problemsPanel->severityCombo() : nullptr);

    DiagnosticNavigationService service;
    const DiagnosticNavigationResult result = service.navigate(query);
    if (!result.found) {
        if (statusBar()) {
            statusBar()->showMessage(
                result.failureReason.isEmpty()
                    ? QStringLiteral("No diagnostics")
                    : result.failureReason,
                3000);
        }
        return;
    }

    if (!navigationCommandCoordinator
        || !navigationCommandCoordinator->navigateToFileAndLineAndFlash(
            result.diagnostic.diagnostic.fileName,
            result.diagnostic.diagnostic.line,
            result.diagnostic.diagnostic.column)) {
        if (statusBar())
            statusBar()->showMessage(
                QStringLiteral("Failed to open diagnostic location"),
                4000);
        return;
    }

    if (statusBar()) {
        statusBar()->showMessage(
            QStringLiteral("%1 diagnostic: %2")
                .arg(previous ? QStringLiteral("Previous")
                              : QStringLiteral("Next"),
                     result.diagnostic.messageDisplayName),
            3000);
    }
}

void MainWindow::setDiagnosticsAnalysisState(const QString& state)
{
    ProblemsPanelCoordinator* problemsPanel =
        semanticDocks ? semanticDocks->problemsPanelCoordinator() : nullptr;
    if (!problemsPanel)
        return;

    problemsPanel->setAnalysisState(state);
    problemsPanel->update();
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
        editorModeChip->setStyleSheet(
            InsightVisualStyle::statusChipStyleSheet(
                InsightStatusTone::Success,
                editorModeChip->objectName()));
        setFoldShelfModeVisualActive(false);
        editorModeChip->setVisible(true);
        return;
    }

    if (message.startsWith(QStringLiteral("Fold region: click end line"))) {
        editorModeChip->setText(tr("Fold Region - click end line - Esc cancel"));
        editorModeChip->setStyleSheet(
            InsightVisualStyle::statusChipStyleSheet(
                InsightStatusTone::Success,
                editorModeChip->objectName()));
        setFoldShelfModeVisualActive(false);
        editorModeChip->setVisible(true);
        return;
    }

    if (message.startsWith(QStringLiteral("Fold Shelf"))) {
        editorModeChip->setText(tr("Fold Shelf - drag custom fold blocks - Esc cancel"));
        editorModeChip->setStyleSheet(
            InsightVisualStyle::statusChipStyleSheet(
                InsightStatusTone::Warning,
                editorModeChip->objectName()));
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
    foldShelfDock->setStyleSheet(
        active ? InsightVisualStyle::dockAttentionStyleSheet(
                     foldShelfDock->objectName(),
                     InsightStatusTone::Warning)
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

void MainWindow::showRecentWorkspacesDialog()
{
    auto* dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("recentWorkspacesDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Recent Workspaces"));
    dialog->resize(720, 360);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* tree = new QTreeWidget(dialog);
    tree->setObjectName(QStringLiteral("recentWorkspacesTree"));
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("Alias"), tr("Path")});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->header()->setStretchLastSection(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layout->addWidget(tree, 1);

    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager ? workspaceManager->recentWorkspaceEntries()
                         : QList<WorkspaceManager::WorkspaceEntry>();
    for (const WorkspaceManager::WorkspaceEntry& entry : entries) {
        auto* item = new QTreeWidgetItem;
        item->setText(0, entry.alias);
        item->setText(1, QDir::toNativeSeparators(entry.path));
        item->setData(0, Qt::UserRole, entry.path);
        tree->addTopLevelItem(item);
    }

    if (entries.isEmpty()) {
        auto* emptyItem = new QTreeWidgetItem;
        emptyItem->setText(0, tr("No recent workspaces"));
        emptyItem->setText(1, tr("Open a workspace first"));
        emptyItem->setDisabled(true);
        tree->addTopLevelItem(emptyItem);
    } else {
        tree->setCurrentItem(tree->topLevelItem(0));
    }

    auto* buttons = new QDialogButtonBox(dialog);
    QPushButton* openButton =
        buttons->addButton(tr("Open"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    openButton->setEnabled(!entries.isEmpty());
    layout->addWidget(buttons);

    auto openSelected = [this, dialog, tree]() {
        QTreeWidgetItem* item = tree ? tree->currentItem() : nullptr;
        const QString path =
            item ? item->data(0, Qt::UserRole).toString() : QString();
        if (path.isEmpty() || !workspaceManager)
            return;
        if (workspaceManager->openWorkspace(path))
            dialog->close();
    };

    connect(tree,
            &QTreeWidget::itemDoubleClicked,
            dialog,
            [openSelected](QTreeWidgetItem*, int) {
                openSelected();
            });
    connect(buttons,
            &QDialogButtonBox::accepted,
            dialog,
            openSelected);
    connect(buttons,
            &QDialogButtonBox::rejected,
            dialog,
            &QDialog::close);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
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

    if (shellNavigationRailDock)
        addDockWidget(Qt::LeftDockWidgetArea, shellNavigationRailDock);
    if (navigationDock)
        addDockWidget(Qt::LeftDockWidgetArea, navigationDock);
    if (shellNavigationRailDock && navigationDock)
        splitDockWidget(shellNavigationRailDock, navigationDock, Qt::Horizontal);
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

    showDockWidget(shellNavigationRailDock);
    showDockWidget(navigationDock);
    showDockWidget(editorAppearanceDockWidget);
    for (QDockWidget* dock : bottomDocks)
        showDockWidget(dock);
    if (problemsDock)
        problemsDock->raise();

    scheduleWorkspaceSessionSave();
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
        foldShelfModel->markItemStale(id);
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
        foldShelfModel->markItemStale(id);
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
        foldShelfModel->markItemStale(id);
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

void MainWindow::restoreFoldShelfItemToActiveEditor(const QString& id)
{
    if (!foldShelfModel || !tabManager)
        return;

    MyCodeEditor* editor = tabManager->getCurrentEditor();
    const int targetLine = editor ? editor->textCursor().blockNumber() : -1;
    const FoldShelfRestoreReport report =
        FoldShelfRestoreService::restoreIntoEditor(
            foldShelfModel.get(),
            editor,
            id,
            targetLine,
            FoldShelfRestoreCompletion::ConsumeItem);

    if (!report.success) {
        ActivityLogService::getInstance()->append(
            QStringLiteral("Fold Shelf"),
            ActivityLogLevel::Warning,
            QStringLiteral("Cross-file restore failed for \"%1\": %2")
                .arg(report.item.alias.isEmpty() ? id : report.item.alias,
                     report.failureReason));
        if (statusBar()) {
            statusBar()->showMessage(
                QStringLiteral("Fold Shelf restore failed: %1")
                    .arg(report.failureReason),
                5000);
        }
        return;
    }

    ActivityLogService::getInstance()->append(
        QStringLiteral("Fold Shelf"),
        ActivityLogLevel::Info,
        QStringLiteral("Restored shelf item \"%1\" to active editor")
            .arg(report.item.alias));
    if (statusBar()) {
        statusBar()->showMessage(
            QStringLiteral("Fold Shelf item restored to active editor"),
            3000);
    }
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
    if (event && event->isAccepted())
        saveWorkspaceSession(false);
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
