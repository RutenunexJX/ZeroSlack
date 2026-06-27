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
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QTextBlock>
#include <QTextCursor>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
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
    workspaceTabBar->setTabsClosable(true);
    workspaceTabBar->setContextMenuPolicy(Qt::CustomContextMenu);
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
            [this](int, const QString&, const QString&) {
                refreshWorkspaceTabs();
            });
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceClosed,
            this,
            &MainWindow::refreshWorkspaceTabs);
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
                connect(editor,
                        &MyCodeEditor::cursorPositionChanged,
                        this,
                        [this, editor]() {
                            const bool hasSelection =
                                editor && editor->textCursor().hasSelection();
                            const bool hadSelection =
                                editor
                                && editor->property("wavePreviewHadSelection")
                                       .toBool();
                            if (editor)
                                editor->setProperty("wavePreviewHadSelection",
                                                    hasSelection);
                            QDockWidget* waveDock =
                                dockForPanelId(QStringLiteral("wavePreview"));
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor
                                && waveDock
                                && waveDock->isVisible()
                                && (hasSelection || hadSelection)) {
                                refreshActiveEditorWavePreview();
                            }
                        });
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::fileSymbolAnalysisFinished,
            this,
            [this](const QString& fileName, int) {
                scheduleActiveEditorPassiveRefresh(fileName);
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot&, int, int) {
                scheduleActiveEditorPassiveRefresh();
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
    int scopeStartPosition = -1;
    int scopeEndPosition = -1;
    QString scopeLabel;
    const QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection()) {
        scopeStartPosition = cursor.selectionStart();
        scopeEndPosition = cursor.selectionEnd();
        const int startLine =
            editor->document()->findBlock(scopeStartPosition).blockNumber() + 1;
        const int endLine =
            editor->document()->findBlock(qMax(scopeStartPosition,
                                               scopeEndPosition - 1))
                .blockNumber()
            + 1;
        scopeLabel = QStringLiteral("selected lines %1-%2")
                         .arg(startLine)
                         .arg(endLine);
    }
    semanticDocks->wavePreviewPanelCoordinator()->refreshFromDocument(
        document.fileName,
        editor->toPlainText(),
        document.dirty,
        scopeStartPosition,
        scopeEndPosition,
        scopeLabel);
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

            if (item.id == QStringLiteral("ow")) {
                if (fileCommandCoordinator)
                    fileCommandCoordinator->openDirectoryAsWorkspace();
            } else if (item.id == QStringLiteral("ow r")) {
                showRecentWorkspacesDialog();
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
