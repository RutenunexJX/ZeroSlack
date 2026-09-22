#include "uidialogs.h"
#include "workspacechrome.h"
#include "mainwindow.h"
#include "uicontrols.h"
#include "workspacehubsession.h"

#include "actionregistry.h"
#include "applicationthememanager.h"
#include "ui_mainwindow.h"

#include "mycodeeditor.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "projectmodel.h"
#include "analysisscheduler.h"
#include "analysiscoordinator.h"
#include "analysisprogresscoordinator.h"
#include "commandlayercoordinator.h"
#include "editorcoordinator.h"
#include "editorsemanticcontextservice.h"
#include "editoractioncontextservice.h"
#include "editorfileidentity.h"
#include "editorinsertpaletteservice.h"
#include "filecommandcoordinator.h"
#include "formatterservice.h"
#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "navigationpanecoordinator.h"
#include "notificationcenter.h"
#include "panellayoutcontroller.h"
#include "contextworkspacecontroller.h"
#include "pinloomcodelinkcoordinator.h"
#include "pinloomcontextprovider.h"
#include "pinloomhostclient.h"
#include "diagnosticnavigationservice.h"
#include "diagnosticservice.h"
#include "editorappearancesettings.h"
#include "ghostannotationservice.h"
#include "semanticdecorationservice.h"
#include "globalcontrolcoordinator.h"
#include "globalcontrolservice.h"
#include "insightvisualstyle.h"
#include "liveinsightscontextprovider.h"
#include "liveinsightsession.h"
#include "liveinsighttoolpage.h"
#include "instancepairconnectionpanel.h"
#include "multisignalpropagationpanel.h"
#include "semanticdockcoordinator.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticpanelrefreshcoordinator.h"
#include "semanticruntimecoordinator.h"
#include "scopedsearchpanel.h"
#include "scopedreplaceworkflow.h"
#include "shareddocument.h"
#include "settingscenterpanel.h"
#include "settingscenterservice.h"
#include "searchservice.h"
#include "temporaryeditorcontextprovider.h"
#include "temporaryeditorsearchprovider.h"
#include "usertemplateservice.h"
#include "activitylogpanelcoordinator.h"
#include "activitylogservice.h"
#include "problemspanelcoordinator.h"
#include "rtlhighriskeditpanel.h"
#include "rtlactioncoordinator.h"
#include "workspaceconfigurationdialog.h"
#include "workspaceeditdocumentmanager.h"
#include "workspacesessioncoordinator.h"
#include "tsdocument.h"
#include "version.h"
#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QVariant>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QPointer>
#include <QPushButton>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSize>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>
#include <optional>
#include <string>
#include <utility>

namespace {
struct SemanticDecorationBuildResult {
    SemanticDecorationReport report;
    qint64 elapsedMs = 0;
};

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

QString crashRecoverySourceStateText(
    CrashRecoverySourceState state)
{
    switch (state) {
    case CrashRecoverySourceState::Untitled:
        return QStringLiteral("untitled");
    case CrashRecoverySourceState::Missing:
        return QStringLiteral("source missing");
    case CrashRecoverySourceState::Unreadable:
        return QStringLiteral("source unreadable");
    case CrashRecoverySourceState::BaselineUnavailable:
        return QStringLiteral("baseline unavailable");
    case CrashRecoverySourceState::UnchangedSinceBaseline:
        return QStringLiteral("unchanged source");
    case CrashRecoverySourceState::ExternallyModified:
        return QStringLiteral("externally modified");
    }
    return QStringLiteral("unknown source state");
}

QString crashRecoveryDocumentLabel(
    const CrashRecoveryCandidate& candidate)
{
    if (!candidate.originalFilePath.isEmpty())
        return QDir::toNativeSeparators(candidate.originalFilePath);
    if (!candidate.untitledDocumentId.isEmpty()) {
        return QStringLiteral("Untitled (%1)")
            .arg(candidate.untitledDocumentId);
    }
    return QStringLiteral("Untitled");
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    tabManager = std::unique_ptr<TabManager>(new TabManager(ui->tabWidget, this));
    workspaceManager = std::unique_ptr<WorkspaceManager>(new WorkspaceManager(this));
    tabManager->setRegisteredTabActionRequestHandler(
        [this](const QString& actionId,
               QString* failureReason) {
        const ActionDescriptor* descriptor =
            findActionById(actionId);
        if (!descriptor) {
            if (failureReason) {
                *failureReason = QStringLiteral(
                    "Editor Tab Action is unavailable");
            }
            return false;
        }
        ActionInvocation invocation;
        invocation.workspaceId = workspaceManager
            ? workspaceManager->getWorkspacePath()
            : QString();
        const ActionExecutionResult result =
            executeAction(
                *descriptor, *this, invocation);
        const QString reason =
            result.failureReason.isEmpty()
            ? result.message
            : result.failureReason;
        if (failureReason)
            *failureReason = reason;
        if (!result.succeeded
            && !reason.isEmpty()) {
            postActivityMessage(reason, 5000);
        }
        return result.succeeded;
    });
    setupEditorCentralArea();
    setupWorkspaceActivity();
    navigationManager = std::unique_ptr<NavigationManager>(new NavigationManager(this));  // NEW
    analysisScheduler = std::unique_ptr<AnalysisScheduler>(new AnalysisScheduler(this));
    analysisProgressCoordinator =
        std::unique_ptr<AnalysisProgressCoordinator>(new AnalysisProgressCoordinator(this, this));
    setupNotificationCenter();

    setupSemanticRuntime();
    setupNavigationPane();
    setupNavigationCommandCoordinator();
    setupSemanticDocks();
    setupSettingsCenter();
    setupFileCommandCoordinator();
    setupPanelLayoutController();
    setupContextWorkspace();
    if (contextWorkspaceController && settingsCenterService)
        contextWorkspaceController->setFloatingOpacity(settingsCenterService->load(workspaceManager->getWorkspacePath())
            .value(QStringLiteral("appearance.floatingContextOpacity")).toInt());
    setupRtlActionCoordinator();
    setupWorkspaceSessionCoordinator();
    connect(tabManager.get(), &TabManager::workspaceActivationRequested, this,
            [this](const QString& root) {
                const auto entries = workspaceManager->workspaceEntries();
                for (int i = 0; i < entries.size(); ++i)
                    if (EditorFileIdentity::same(entries.at(i).path, root)) { activateWorkspace(i); break; }
            });
    setupWorkspaceMenu();
    setupViewMenu();
    setupToolsMenu();

    setupGlobalControl();
    setupCommandLayer();
    setupEditorCoordinator();
    setupManagerConnections();
    applyModernShellStyle();
    applyRegisteredActionShortcuts();

    if (settingsCenterDock)
        settingsCenterDock->hide();
    if (semanticDocks) {
        if (semanticDocks->activityLogPanelCoordinator()
            && semanticDocks->activityLogPanelCoordinator()->dock())
            panelLayoutController->closePanel(
                QStringLiteral("activity"));
        if (semanticDocks->scopedSearchPanelCoordinator()
            && semanticDocks->scopedSearchPanelCoordinator()->dock())
            panelLayoutController->closePanel(
                ScopedSearchPanelCoordinator::panelId());
        if (semanticDocks
                ->rtlHighRiskEditPanelCoordinator()
            && semanticDocks
                   ->rtlHighRiskEditPanelCoordinator()
                   ->dock()) {
            panelLayoutController->closePanel(
                RtlHighRiskEditPanelCoordinator::
                    panelId());
        }
    }

    setWindowTitle(QStringLiteral("ZeroSlack v%1").arg(QLatin1String(APP_VERSION)));
    auto* workspaceSwitcher = new WorkspaceSwitcher(workspaceManager.get(), tabManager.get(),
        workspaceSessionCoordinator.get(), [this] {
            fileCommandCoordinator->openDirectoryAsWorkspace();
        }, this);
    new WorkspaceChrome(this, navigationPane.get(), [this]() {
        if (!tabManager->closeActiveToolPage(QStringLiteral("settingsCenter")))
            showDockWidget(settingsCenterDock);
    }, workspaceSwitcher);
    connect(navigationPane->dock(), &QDockWidget::visibilityChanged,
            this, [this](bool visible) {
                if (visible)
                    navigationHiddenForWelcome = false;
            });
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceListChanged,
            this, &MainWindow::refreshWelcomePage);
    connect(tabManager.get(), &TabManager::tabCreated,
            this, [this]() { refreshWelcomePage(); });
    connect(ui->tabWidget, &QTabWidget::currentChanged,
            this, [this](int) {
                if (centralContentStack->currentWidget() == welcomePage)
                    refreshWelcomePage();
            });
    connect(tabManager.get(), &TabManager::tabClosed,
            this, [this]() {
                QTimer::singleShot(0, this, &MainWindow::refreshWelcomePage);
            });
    connect(tabManager.get(), &TabManager::toolPageClosed,
            this, [this]() {
                QTimer::singleShot(0, this, &MainWindow::refreshWelcomePage);
            });
    refreshWelcomePage();
    for (const auto& shortcut : {QStringLiteral("Ctrl+Shift+F"), QStringLiteral("Ctrl+Shift+H")}) {
        auto* action = new QAction(this);
        action->setShortcut(QKeySequence(shortcut));
        addAction(action);
        connect(action, &QAction::triggered, this, [this, shortcut]() {
            showPanelById(ScopedSearchPanelCoordinator::panelId());
            auto* panel = semanticDocks->scopedSearchPanelCoordinator()->panel();
            panel->setScope(ScopedSearchScope::Workspace);
            if (shortcut.endsWith(QLatin1Char('H'))) panel->replacementEditor()->setFocus();
            else panel->queryEditor()->setFocus();
        });
    }

}

NotificationCenter* MainWindow::notificationCenterForTesting() const
{
    return notificationCenter.get();
}

void MainWindow::setupNotificationCenter()
{
    notificationCenter =
        std::make_unique<NotificationCenter>(this);
    const auto recordNotification =
        [](const NotificationItem& item) {
            ActivityLogService::getInstance()->append(
                item.source.isEmpty() ? QStringLiteral("Notification") : item.source,
                item.severity == NotificationSeverity::Error || item.severity == NotificationSeverity::Critical
                    ? ActivityLogLevel::Error
                    : item.severity == NotificationSeverity::Warning
                        ? ActivityLogLevel::Warning : ActivityLogLevel::Info,
                item.message, -1, item.id, true);
        };
    connect(notificationCenter.get(),
            &NotificationCenter::notificationAdded,
            this,
            recordNotification);
    connect(notificationCenter.get(),
            &NotificationCenter::notificationUpdated,
            this,
            recordNotification);

    if (tabManager) {
        connect(tabManager.get(),
                &TabManager::fileSaveFailed,
                this,
                [this](const QString& fileName,
                       const QString& failureReason) {
                    NotificationDraft draft;
                    draft.key =
                        QStringLiteral("save:%1").arg(fileName);
                    draft.topic = NotificationTopic::Save;
                    draft.severity = NotificationSeverity::Error;
                    draft.source =
                        QStringLiteral("DocumentSave");
                    draft.message = failureReason;
                    notificationCenter->post(draft);
                });
        connect(tabManager.get(),
                &TabManager::fileSaved,
                this,
                [this](const QString& fileName) {
                    notificationCenter->dismissByKey(
                        QStringLiteral("save:%1")
                            .arg(fileName));
                });
        connect(tabManager.get(),
                &TabManager::externalFileConflict,
                this,
                [this](const QString& fileName) {
                    NotificationDraft draft;
                    draft.key =
                        QStringLiteral("external:%1").arg(fileName);
                    draft.topic =
                        NotificationTopic::ExternalModification;
                    draft.severity =
                        NotificationSeverity::Critical;
                    draft.source =
                        QStringLiteral("ExternalDocumentSync");
                    draft.message = QStringLiteral(
                        "Local and external changes conflict: %1")
                                        .arg(fileName);
                    draft.actions = {
                        {QStringLiteral("external.review"),
                         QStringLiteral("Compare...")},
                        {QStringLiteral("external.keep-local"),
                         QStringLiteral("Keep Local")},
                        {QStringLiteral("external.reload"),
                         QStringLiteral("Reload External")},
                        {QStringLiteral("external.save-as"),
                         QStringLiteral("Save Local As...")}};
                    const NotificationPostResult posted =
                        notificationCenter->post(draft);
                    externalConflictNotificationFiles.insert(
                        posted.id,
                        fileName);
                    if (externalConflictReviewBar
                        && externalConflictReviewBar->isVisible()
                        && reviewedExternalConflict
                        && reviewedExternalConflict->fileName
                               == fileName) {
                        openExternalConflictReview(
                            fileName);
                    }
                });
        connect(tabManager.get(),
                &TabManager::externalFileUnavailable,
                this,
                [this](const QString& fileName,
                       const QString& failureReason) {
                    NotificationDraft draft;
                    draft.key =
                        QStringLiteral("external:%1").arg(fileName);
                    draft.topic =
                        NotificationTopic::ExternalModification;
                    draft.severity =
                        NotificationSeverity::Warning;
                    draft.source =
                        QStringLiteral("ExternalDocumentSync");
                    draft.message = QStringLiteral("%1: %2")
                                        .arg(fileName,
                                             failureReason);
                    const ExternalDocumentConflictReview review =
                        tabManager
                        ? tabManager->externalConflictReview(
                              fileName)
                        : ExternalDocumentConflictReview();
                    if (review.valid) {
                        draft.actions = {
                            {QStringLiteral("external.review"),
                             QStringLiteral("Compare...")},
                            {QStringLiteral("external.keep-local"),
                             QStringLiteral("Keep Local")},
                            {QStringLiteral("external.reload"),
                             QStringLiteral("Reload External")},
                            {QStringLiteral("external.save-as"),
                             QStringLiteral("Save Local As...")}};
                    }
                    const NotificationPostResult posted =
                        notificationCenter->post(draft);
                    if (review.valid) {
                        externalConflictNotificationFiles.insert(
                            posted.id,
                            fileName);
                        if (externalConflictReviewBar
                            && externalConflictReviewBar
                                   ->isVisible()
                            && reviewedExternalConflict
                            && reviewedExternalConflict
                                   ->fileName
                                   == fileName) {
                            openExternalConflictReview(
                                fileName);
                        }
                    }
                });
        connect(tabManager.get(),
                &TabManager::externalFileReloaded,
                this,
                [this](const QString& fileName) {
                    notificationCenter->dismissByKey(
                        QStringLiteral("external:%1")
                            .arg(fileName));
                    if (reviewedExternalConflict
                        && reviewedExternalConflict->fileName
                               == fileName) {
                        closeExternalConflictReview();
                    }
                });
        connect(tabManager.get(),
                &TabManager::crashRecoveryCandidatesAvailable,
                this,
                &MainWindow::notifyCrashRecoveryCandidates);
        connect(tabManager.get(),
                &TabManager::crashRecoveryOperationFailed,
                this,
                &MainWindow::postCrashRecoveryFailure);
    }
    connect(notificationCenter.get(),
            &NotificationCenter::actionRequested,
            this,
            [this](const QString& notificationId,
                   const QString& actionId) {
                const QString conflictFile =
                    externalConflictNotificationFiles
                        .value(notificationId);
                if (!conflictFile.isEmpty()) {
                    if (actionId
                        == QStringLiteral(
                            "external.review")) {
                        openExternalConflictReview(
                            conflictFile);
                    } else {
                        ExternalDocumentConflictReview review =
                            tabManager
                            ? tabManager->externalConflictReview(
                                  conflictFile)
                            : ExternalDocumentConflictReview();
                        if (!review.valid) {
                            postExternalConflictActionFailure(
                                conflictFile,
                                review.failureReason);
                            return;
                        }
                        reviewedExternalConflict =
                            std::make_unique<
                                ExternalDocumentConflictReview>(
                                std::move(review));
                        if (actionId
                            == QStringLiteral(
                                "external.keep-local")) {
                            keepReviewedExternalConflict();
                        } else if (actionId
                                   == QStringLiteral(
                                       "external.reload")) {
                            reloadReviewedExternalConflict();
                        } else if (actionId
                                   == QStringLiteral(
                                       "external.save-as")) {
                            saveReviewedExternalConflictAs();
                        }
                    }
                    return;
                }
                if (actionId
                    != QString::fromLatin1(
                        ActionIds::
                            ReviewCrashRecovery)) {
                    return;
                }
                const QString workspaceRoot =
                    crashRecoveryNotificationWorkspaces
                        .value(notificationId);
                if (!workspaceRoot.isEmpty())
                    openCrashRecoveryReview(workspaceRoot);
            });
    connect(notificationCenter.get(),
            &NotificationCenter::notificationRemoved,
            this,
            [this](const NotificationItem& item,
                   NotificationRemovalReason) {
                crashRecoveryNotificationWorkspaces
                    .remove(item.id);
                externalConflictNotificationFiles
                    .remove(item.id);
            });
    if (analysisScheduler) {
        connect(analysisScheduler.get(),
                &AnalysisScheduler::relationshipAnalysisError,
                this,
                [this](const QString& fileName,
                       const QString& error) {
                    NotificationDraft draft;
                    draft.key =
                        QStringLiteral("analysis:%1")
                            .arg(fileName);
                    draft.topic = NotificationTopic::Analysis;
                    draft.severity = NotificationSeverity::Error;
                    draft.source = QStringLiteral("Analysis");
                    draft.message = error;
                    notificationCenter->post(draft);
                });
    }
}

MainWindow::~MainWindow()
{
    ++semanticDecorationGeneration;
    if (semanticDecorationCancellation)
        semanticDecorationCancellation->store(true);
    // The semantic runtime owns the shared Slang relationship builder and is
    // declared after the scheduler, so normal reverse member destruction would
    // otherwise destroy the builder first. Join and detach every background
    // analysis while the runtime-owned QObjects are still alive.
    if (analysisScheduler)
        analysisScheduler->shutdown();

    workspaceSessionCoordinator.reset();

    // TabManager owns SharedDocuments whose teardown rebinds every attached
    // editor to an independent QTextDocument.  That operation legitimately
    // emits editor state signals.  Stop MainWindow routes and destroy the
    // manager while the generated UI and its tool buttons are still alive;
    // deleting the UI first leaves those raw widget pointers dangling.
    if (tabManager) {
        for (MyCodeEditor* editor : tabManager->openEditors()) {
            if (editor)
                QObject::disconnect(editor, nullptr, this, nullptr);
        }
        QObject::disconnect(tabManager.get(), nullptr, this, nullptr);
        tabManager.reset();
    }
    delete ui;
    ui = nullptr;
}

void MainWindow::applyModernShellStyle(bool applyApplicationTheme)
{
    if (applyApplicationTheme)
        ApplicationThemeManager::instance().applyToApplication();

    if (ui && ui->tabWidget) {
        ui->tabWidget->setDocumentMode(true);
        ui->tabWidget->setIconSize(QSize(14, 14));
        QTabBar* bar = ui->tabWidget->tabBar();
        if (bar) {
            if (bar->objectName().isEmpty())
                bar->setObjectName(QStringLiteral("mainEditorTabBar"));
            bar->setStyleSheet(
                InsightVisualStyle::tabBarStyleSheet(bar->objectName()));
        }
    }
}

void MainWindow::refreshThemePresentation()
{
    // ApplicationThemeManager already applied the global palette and QSS
    // before emitting themeChanged. Re-applying it here would re-polish every
    // widget during graph-specific refresh callbacks and shift viewports.
    applyModernShellStyle(false);

    const QList<MyCodeEditor*> editorViews =
        findChildren<MyCodeEditor*>();
    for (MyCodeEditor* editor : editorViews) {
        if (!editor)
            continue;
        editor->refreshSemanticPresentation();
        editor->viewport()->update();
    }

    const QList<QWidget*> widgets = findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        if (widget)
            widget->update();
    }
    update();
}

void MainWindow::refreshTemporaryEditorFileCatalog()
{
    if (!temporaryEditorSearchProvider)
        return;
    const QStringList cachedFiles = workspaceManager
        ? workspaceManager->getAllFiles()
        : QStringList{};
    const QString workspaceRoot = workspaceManager
        ? workspaceManager->getWorkspacePath()
        : QString{};
    temporaryEditorSearchProvider->setWorkspaceFiles(
        cachedFiles, workspaceRoot);
}

void MainWindow::refreshTemporaryEditorSemanticCatalog()
{
    if (!temporaryEditorSearchProvider)
        return;
    temporaryEditorSearchProvider->setSemanticCatalog(
        SearchService::getInstance()->symbolCatalog());
}

void MainWindow::setupEditorCentralArea()
{
    QWidget* editorContainer = new QWidget(this);
    editorContainer->setObjectName(
        QStringLiteral("editorCentralPage"));
    auto* layout = new QVBoxLayout(editorContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    editorSplitHost = new QWidget(editorContainer);
    editorSplitHost->setObjectName(
        QStringLiteral("editorSplitHost"));
    auto* splitLayout =
        new QVBoxLayout(editorSplitHost);
    splitLayout->setContentsMargins(0, 0, 0, 0);
    splitLayout->setSpacing(0);
    splitLayout->addWidget(ui->tabWidget);
    layout->addWidget(editorSplitHost, 1);
    setupExternalConflictReviewUi(layout, editorContainer);
    if (tabManager) {
        tabManager->enableSplitLayout(editorSplitHost);
        temporaryEditorSearchProvider =
            std::make_unique<TemporaryEditorSearchProvider>();
        refreshTemporaryEditorFileCatalog();
        refreshTemporaryEditorSemanticCatalog();
        if (workspaceManager) {
            connectTemporaryEditorFileCatalogRefresh(
                workspaceManager.get(),
                this,
                [this]() {
                    refreshTemporaryEditorFileCatalog();
                });
            connect(workspaceManager.get(),
                    &WorkspaceManager::workspaceActivated,
                    this,
                    [this](
                        int, const QString&, const QString&) {
                        refreshTemporaryEditorFileCatalog();
                        if (temporaryEditorSearchProvider) {
                            temporaryEditorSearchProvider
                                ->setSemanticCatalog({});
                        }
                    });
            connect(workspaceManager.get(),
                    &WorkspaceManager::workspaceClosed,
                    this,
                    [this]() {
                        refreshTemporaryEditorFileCatalog();
                        if (temporaryEditorSearchProvider) {
                            temporaryEditorSearchProvider
                                ->setSemanticCatalog({});
                        }
                    });
        }
        connect(tabManager.get(),
                &TabManager::tabGroupCreated,
                this,
                [](QTabWidget* group) {
                    if (!group || !group->tabBar())
                        return;
                    group->tabBar()->setStyleSheet(
                        InsightVisualStyle::tabBarStyleSheet(
                            group->tabBar()->objectName()));
                });
    }
    editorCentralPage = editorContainer;
    centralContentStack = new QStackedWidget(this);
    centralContentStack->setObjectName(
        QStringLiteral("centralContentStack"));
    centralContentStack->addWidget(editorCentralPage);
    setupWelcomePage();
    setCentralWidget(centralContentStack);

    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceListChanged,
            this,
            [this]() {
                refreshWorkspaceScope();
                refreshWorkspaceMenuEntries();
            });
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceActivated,
            this,
            [this](int, const QString&, const QString& path) {
                UserTemplateService::getInstance()->setWorkspaceRoot(path);
                UserTemplateService::getInstance()->reload();
                refreshSettingsCenterWorkspace(path);
                refreshWorkspaceScope();
                refreshWorkspaceMenuEntries();
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
                if (activePath.isEmpty())
                    refreshSettingsCenterWorkspace(QString());
                refreshWorkspaceScope();
    });
}

void MainWindow::setupWelcomePage()
{
    welcomePage = new QWidget(centralContentStack);
    welcomePage->setObjectName(QStringLiteral("welcomePage"));
    auto* pageLayout = new QHBoxLayout(welcomePage);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto* center = new QWidget(welcomePage);
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(24, 32, 24, 32);
    centerLayout->setSpacing(0);
    centerLayout->addStretch(2);
    auto* content = new QWidget(center);
    content->setMinimumWidth(360);
    content->setMaximumWidth(420);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    auto* open = UiControls::toolButton(content);
    open->setObjectName(QStringLiteral("welcomeOpenProjectButton"));
    open->setIcon(RoundedIcons::icon(RoundedIcons::OpenProject));
    open->setIconSize(QSize(68, 68));
    open->setFixedSize(88, 88);
    open->setToolTip(tr("Open a project"));
    open->setAccessibleName(tr("Open a project"));
    connect(open, &QToolButton::clicked, this, [this]() {
        if (fileCommandCoordinator)
            fileCommandCoordinator->openDirectoryAsWorkspace();
    });
    contentLayout->addWidget(open, 0, Qt::AlignHCenter);
    contentLayout->addSpacing(18);
    auto* recentTitle = UiControls::label(tr("Recent projects"), content);
    recentTitle->setObjectName(QStringLiteral("welcomeRecentTitle"));
    UiTypography::apply(recentTitle, UiTypography::Role::PanelTitle);
    contentLayout->addWidget(recentTitle, 0, Qt::AlignHCenter);
    recentProjectsLayout = new QVBoxLayout;
    recentProjectsLayout->setSpacing(8);
    contentLayout->addLayout(recentProjectsLayout);
    centerLayout->addWidget(content, 0, Qt::AlignHCenter);
    centerLayout->addStretch(3);
    pageLayout->addWidget(center, 1);
    centralContentStack->addWidget(welcomePage);
}

void MainWindow::refreshWelcomePage()
{
    if (!welcomePage || !recentProjectsLayout || !centralContentStack)
        return;
    while (QLayoutItem* item = recentProjectsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (workspaceManager) {
        const auto recent = workspaceManager->recentWorkspaceEntries();
        if (auto* title = welcomePage->findChild<QLabel*>(
                QStringLiteral("welcomeRecentTitle")))
            title->setVisible(!recent.isEmpty());
        for (int index = 0; index < qMin(recent.size(), 5); ++index) {
            const auto& entry = recent.at(index);
            auto* row = UiControls::toolButton(welcomePage);
            row->setObjectName(QStringLiteral("welcomeRecentProject"));
            row->setSizePolicy(QSizePolicy::Expanding,
                               QSizePolicy::Preferred);
            auto* rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(14, 9, 14, 9);
            rowLayout->setSpacing(3);
            auto* name = UiControls::label(entry.alias, row);
            name->setAttribute(Qt::WA_TransparentForMouseEvents);
            UiTypography::apply(name, UiTypography::Role::Body);
            QFont nameFont = name->font();
            nameFont.setWeight(QFont::DemiBold);
            name->setFont(nameFont);
            rowLayout->addWidget(name);
            const QString fullPath = QDir::toNativeSeparators(entry.path);
            auto* path = UiControls::label(row);
            path->setObjectName(QStringLiteral("welcomeRecentPath"));
            path->setAttribute(Qt::WA_TransparentForMouseEvents);
            UiTypography::apply(path, UiTypography::Role::Metadata);
            path->setForegroundRole(QPalette::PlaceholderText);
            path->setProperty("fullPath", fullPath);
            path->setText(path->fontMetrics().elidedText(
                fullPath, Qt::ElideMiddle, 328));
            rowLayout->addWidget(path);
            row->setToolTip(QDir::toNativeSeparators(entry.path));
            row->setAccessibleName(entry.alias + QLatin1Char(' ')
                                   + entry.path);
            row->setMinimumHeight(60);
            row->setStyleSheet(QStringLiteral(
                "QToolButton { background: palette(base); "
                "border: 1px solid palette(midlight); border-radius: 9px; }"
                "QToolButton:hover { background: palette(midlight); }"));
            connect(row, &QToolButton::clicked, this,
                    [this, path = entry.path]() {
                        if (workspaceSessionCoordinator)
                            workspaceSessionCoordinator->openWorkspace(path);
                    });
            recentProjectsLayout->addWidget(row);
        }
    }
    const bool showWelcome = workspaceManager
        && !workspaceManager->isWorkspaceOpen()
        && tabManager && tabManager->openEditors().isEmpty()
        && ui->tabWidget->count() == 0;
    centralContentStack->setCurrentWidget(
        showWelcome ? welcomePage : editorCentralPage);
    if (!navigationPane || !navigationPane->dock())
        return;
    if (showWelcome && !navigationPane->dock()->isHidden()) {
        navigationHiddenForWelcome = true;
        navigationPane->dock()->hide();
    } else if (!showWelcome && navigationHiddenForWelcome) {
        navigationHiddenForWelcome = false;
        navigationPane->dock()->show();
    }
}

void MainWindow::setupExternalConflictReviewUi(
    QVBoxLayout* editorLayout,
    QWidget* parent)
{
    if (!editorLayout || externalConflictReviewBar)
        return;

    externalConflictReviewBar =
        new QWidget(parent ? parent : this);
    externalConflictReviewBar->setObjectName(
        QStringLiteral("externalConflictReview"));
    externalConflictReviewBar->setVisible(false);
    externalConflictReviewBar->setMinimumHeight(180);
    externalConflictReviewBar->setMaximumHeight(330);

    auto* outer =
        new QVBoxLayout(externalConflictReviewBar);
    outer->setContentsMargins(8, 6, 8, 8);
    outer->setSpacing(5);

    auto* heading = new QHBoxLayout();
    externalConflictReviewTitle =
        UiControls::label(externalConflictReviewBar);
    externalConflictReviewTitle->setObjectName(
        QStringLiteral("externalConflictReviewTitle"));
    externalConflictReviewTitle->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    heading->addWidget(externalConflictReviewTitle, 1);
    auto* closeButton =
        UiControls::pushButton(
            QStringLiteral("Close"),
            externalConflictReviewBar);
    closeButton->setObjectName(
        QStringLiteral("externalConflictCloseButton"));
    heading->addWidget(closeButton);
    outer->addLayout(heading);

    auto* comparison =
        new QSplitter(
            Qt::Horizontal,
            externalConflictReviewBar);
    comparison->setObjectName(
        QStringLiteral("externalConflictComparison"));
    auto* localGroup =
        new QGroupBox(
            QStringLiteral("Local buffer (unsaved)"),
            comparison);
    auto* localLayout =
        new QVBoxLayout(localGroup);
    localLayout->setContentsMargins(4, 4, 4, 4);
    externalConflictLocalText =
        UiControls::readOnlyText(localGroup);
    externalConflictLocalText->setObjectName(
        QStringLiteral("externalConflictLocalText"));
    externalConflictLocalText->setReadOnly(true);
    externalConflictLocalText->setPlaceholderText(
        QStringLiteral("Local buffer"));
    localLayout->addWidget(
        externalConflictLocalText);
    auto* diskGroup =
        new QGroupBox(
            QStringLiteral("External file (disk)"),
            comparison);
    auto* diskLayout =
        new QVBoxLayout(diskGroup);
    diskLayout->setContentsMargins(4, 4, 4, 4);
    externalConflictDiskText =
        UiControls::readOnlyText(diskGroup);
    externalConflictDiskText->setObjectName(
        QStringLiteral("externalConflictDiskText"));
    externalConflictDiskText->setReadOnly(true);
    externalConflictDiskText->setPlaceholderText(
        QStringLiteral("External file"));
    diskLayout->addWidget(
        externalConflictDiskText);
    comparison->addWidget(localGroup);
    comparison->addWidget(diskGroup);
    comparison->setStretchFactor(0, 1);
    comparison->setStretchFactor(1, 1);
    outer->addWidget(comparison, 1);

    auto* actions = new QHBoxLayout();
    externalConflictReviewStatus =
        UiControls::label(externalConflictReviewBar);
    externalConflictReviewStatus->setObjectName(
        QStringLiteral("externalConflictReviewStatus"));
    externalConflictReviewStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    actions->addWidget(externalConflictReviewStatus, 1);
    externalConflictKeepLocalButton =
        UiControls::pushButton(
            QStringLiteral("Keep Local"),
            externalConflictReviewBar);
    externalConflictKeepLocalButton->setObjectName(
        QStringLiteral("externalConflictKeepLocalButton"));
    externalConflictSaveAsButton =
        UiControls::pushButton(
            QStringLiteral("Save Local As..."),
            externalConflictReviewBar);
    externalConflictSaveAsButton->setObjectName(
        QStringLiteral("externalConflictSaveAsButton"));
    externalConflictReloadButton =
        UiControls::pushButton(
            QStringLiteral("Reload External"),
            externalConflictReviewBar);
    externalConflictReloadButton->setObjectName(
        QStringLiteral("externalConflictReloadButton"));
    actions->addWidget(externalConflictKeepLocalButton);
    actions->addWidget(externalConflictSaveAsButton);
    actions->addWidget(externalConflictReloadButton);
    outer->addLayout(actions);

    connect(closeButton,
            &QPushButton::clicked,
            this,
            &MainWindow::closeExternalConflictReview);
    connect(externalConflictKeepLocalButton,
            &QPushButton::clicked,
            this,
            &MainWindow::keepReviewedExternalConflict);
    connect(externalConflictReloadButton,
            &QPushButton::clicked,
            this,
            &MainWindow::reloadReviewedExternalConflict);
    connect(externalConflictSaveAsButton,
            &QPushButton::clicked,
            this,
            &MainWindow::saveReviewedExternalConflictAs);
    auto* closeShortcut =
        new QShortcut(
            QKeySequence(Qt::Key_Escape),
            externalConflictReviewBar);
    closeShortcut->setContext(
        Qt::WidgetWithChildrenShortcut);
    connect(closeShortcut,
            &QShortcut::activated,
            this,
            &MainWindow::closeExternalConflictReview);

    editorLayout->addWidget(externalConflictReviewBar);
}

void MainWindow::openExternalConflictReview(
    const QString& fileName)
{
    if (!tabManager || !externalConflictReviewBar)
        return;
    ExternalDocumentConflictReview review =
        tabManager->externalConflictReview(fileName);
    if (!review.valid) {
        if (reviewedExternalConflict
            && reviewedExternalConflict->fileName
                   == fileName
            && externalConflictReviewStatus) {
            externalConflictReviewStatus->setText(
                review.failureReason);
            externalConflictKeepLocalButton
                ->setEnabled(false);
            externalConflictReloadButton
                ->setEnabled(false);
            externalConflictSaveAsButton
                ->setEnabled(false);
        }
        postExternalConflictActionFailure(
            fileName,
            review.failureReason.isEmpty()
                ? QStringLiteral(
                      "The conflict comparison is no longer available.")
                : review.failureReason);
        return;
    }

    if (!externalConflictReviewBar->isVisible())
        externalConflictPreviousFocus = QApplication::focusWidget();
    reviewedExternalConflict =
        std::make_unique<ExternalDocumentConflictReview>(
            std::move(review));
    externalConflictReviewTitle->setText(
        QStringLiteral("External conflict: %1")
            .arg(QDir::toNativeSeparators(
                reviewedExternalConflict->fileName)));
    externalConflictReviewStatus->setText(
        reviewedExternalConflict->externalAvailable
        ? QStringLiteral(
              "Compared local revision %1 with the current external generation.")
              .arg(
                  reviewedExternalConflict
                      ->documentRevision)
        : QStringLiteral(
              "The external file is unavailable. Save Local As remains available."));
    externalConflictKeepLocalButton->setEnabled(
        reviewedExternalConflict->externalAvailable);
    externalConflictReloadButton->setEnabled(
        reviewedExternalConflict->externalAvailable);
    externalConflictSaveAsButton->setEnabled(true);
    if (MyCodeEditor* editor =
            tabManager->getCurrentEditor()) {
        externalConflictLocalText->setFont(
            editor->font());
        externalConflictDiskText->setFont(
            editor->font());
    }
    externalConflictLocalText->setPlainText(
        reviewedExternalConflict->localText);
    externalConflictDiskText->setPlainText(
        reviewedExternalConflict->externalAvailable
        ? reviewedExternalConflict->externalText
        : reviewedExternalConflict->failureReason);
    externalConflictLocalText->moveCursor(
        QTextCursor::Start);
    externalConflictDiskText->moveCursor(
        QTextCursor::Start);
    externalConflictReviewBar->setVisible(true);
}

void MainWindow::closeExternalConflictReview()
{
    if (externalConflictReviewBar)
        externalConflictReviewBar->setVisible(false);
    reviewedExternalConflict.reset();
    if (externalConflictPreviousFocus)
        externalConflictPreviousFocus->setFocus(
            Qt::OtherFocusReason);
    externalConflictPreviousFocus.clear();
}

void MainWindow::keepReviewedExternalConflict()
{
    if (!tabManager || !reviewedExternalConflict)
        return;
    const ExternalDocumentConflictReview review =
        *reviewedExternalConflict;
    const QString fileName = review.fileName;
    const ExternalDocumentConflictActionResult result =
        tabManager->keepLocalExternalConflict(
            review);
    if (!result.applied()) {
        postExternalConflictActionFailure(
            fileName,
            result.failureReason);
        if (result.currentReview.valid)
            openExternalConflictReview(fileName);
        return;
    }
    if (notificationCenter) {
        notificationCenter->dismissByKey(
            QStringLiteral("external:%1")
                .arg(fileName));
    }
    closeExternalConflictReview();
    {
        postActivityMessage(
            QStringLiteral(
                "Kept the local version. Saving this path will recheck the external generation."),
            5000);
    }
}

void MainWindow::reloadReviewedExternalConflict()
{
    if (!tabManager || !reviewedExternalConflict)
        return;
    const ExternalDocumentConflictReview review =
        *reviewedExternalConflict;
    const QString fileName = review.fileName;
    const ExternalDocumentConflictActionResult result =
        tabManager->reloadExternalConflict(
            review);
    if (!result.applied()) {
        postExternalConflictActionFailure(
            fileName,
            result.failureReason);
        if (result.currentReview.valid)
            openExternalConflictReview(fileName);
        return;
    }
    closeExternalConflictReview();
}

void MainWindow::saveReviewedExternalConflictAs()
{
    if (!tabManager || !reviewedExternalConflict)
        return;
    const ExternalDocumentConflictReview review =
        *reviewedExternalConflict;
    const QString originalFileName =
        review.fileName;
    QString failureReason;
    const bool comparisonWasVisible =
        externalConflictReviewBar
        && externalConflictReviewBar->isVisible();
    if (!tabManager->saveExternalConflictLocalAs(
            review,
            QString(),
            &failureReason)) {
        if (failureReason != QStringLiteral(
                "Save As was cancelled.")) {
            postExternalConflictActionFailure(
                originalFileName,
                failureReason);
            if (comparisonWasVisible)
                openExternalConflictReview(
                    originalFileName);
        }
        return;
    }
    if (notificationCenter) {
        notificationCenter->dismissByKey(
            QStringLiteral("external:%1")
                .arg(originalFileName));
    }
    closeExternalConflictReview();
}

void MainWindow::postExternalConflictActionFailure(
    const QString& fileName,
    const QString& failureReason)
{
    if (!notificationCenter)
        return;
    NotificationDraft draft;
    draft.key =
        QStringLiteral("external:%1").arg(fileName);
    draft.topic =
        NotificationTopic::ExternalModification;
    draft.severity = NotificationSeverity::Critical;
    draft.source =
        QStringLiteral("ExternalDocumentSync");
    draft.message = failureReason.isEmpty()
        ? QStringLiteral(
              "The external conflict action could not be applied.")
        : failureReason;
    draft.actions = {
        {QStringLiteral("external.review"),
         QStringLiteral("Compare...")},
        {QStringLiteral("external.keep-local"),
         QStringLiteral("Keep Local")},
        {QStringLiteral("external.reload"),
         QStringLiteral("Reload External")},
        {QStringLiteral("external.save-as"),
         QStringLiteral("Save Local As...")}};
    const NotificationPostResult posted =
        notificationCenter->post(draft);
    externalConflictNotificationFiles.insert(
        posted.id,
        fileName);
}

void MainWindow::refreshWorkspaceScope()
{
    if (!workspaceManager)
        return;

    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    QStringList workspaceRoots;
    workspaceRoots.reserve(entries.size());
    for (const WorkspaceManager::WorkspaceEntry& entry : entries)
        workspaceRoots.append(entry.path);
    const int activeIndex = workspaceManager->activeWorkspaceIndex();
    const QString activeWorkspacePath =
        activeIndex >= 0 && activeIndex < entries.size()
            ? entries.at(activeIndex).path
            : QString();
    if (tabManager)
        tabManager->setWorkspaceScope(workspaceRoots, activeWorkspacePath);
}

void MainWindow::setupWorkspaceActivity()
{
    if (!workspaceManager)
        return;
    const auto lastProgressCount = std::make_shared<int>(0);
    connect(workspaceManager.get(), &WorkspaceManager::workspaceScanStarted,
            this, [lastProgressCount](const QString& path) {
                *lastProgressCount = 0;
                ActivityLogService::getInstance()->append(QStringLiteral("Workspace"),
                    ActivityLogLevel::Info, QStringLiteral("Scanning workspace: %1").arg(path));
            });
    connect(workspaceManager.get(), &WorkspaceManager::workspaceScanFinished,
            this, [](const QString& path, int files, int svFiles) {
                ActivityLogService::getInstance()->append(QStringLiteral("Workspace"),
                    ActivityLogLevel::Info,
                    QStringLiteral("Workspace scan complete: %1 files, %2 SystemVerilog (%3)")
                        .arg(files).arg(svFiles).arg(path));
            });
    connect(workspaceManager.get(), &WorkspaceManager::workspaceScanProgress,
            this, [lastProgressCount](const QString&, int files) {
                if (files - *lastProgressCount >= 1000) {
                    *lastProgressCount = files;
                    ActivityLogService::getInstance()->append(QStringLiteral("Workspace"),
                        ActivityLogLevel::Info, QStringLiteral("Scanning workspace: %1 files found").arg(files));
                }
            });
}

void MainWindow::setupManagerConnections()
{
    if (navigationManager
        && contextWorkspaceController) {
        navigationManager->setTemporaryEditorOpenHandler(
            [this](const EditorLocation& location) {
                if (!contextWorkspaceController)
                    return false;
                const ContextResource resource =
                    TemporaryEditorContextProvider::
                        resourceForLocation(
                            location,
                            workspaceManager
                                ? workspaceManager
                                      ->getWorkspacePath()
                                : QString());
                return contextWorkspaceController->openResource(
                    resource, ContextPlacement{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global});
            });
        connect(
            navigationManager.get(),
            &NavigationManager::temporaryEditorOpenFinished,
            this,
            [this](const EditorLocation&,
                   bool succeeded,
                   const QString& failureReason) {
                if (!succeeded
                    && !failureReason.isEmpty()) {
                    postActivityMessage(
                        failureReason, 5000);
                }
            });
    }
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
            scheduleActiveEditorPassiveRefresh(fileName);
        });
    analysisCoordinator->connectSignals();
    connect(analysisScheduler.get(),
            &AnalysisScheduler::semanticAnalysisTelemetry,
            this,
            [this](const SemanticAnalysisTelemetry& telemetry) {
                if (telemetry.stage == SemanticAnalysisStage::Publication) {
                    setProperty(
                        "pendingSemanticPublicationTelemetry",
                        QVariant::fromValue(telemetry));
                }
            });
    connect(this,
            &MainWindow::semanticUiRefreshTelemetry,
            analysisScheduler.get(),
            &AnalysisScheduler::semanticAnalysisTelemetry);
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceOpened,
            this,
            [this](const QString&) {

            });
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceClosed,
            this,
            [this]() {
                if (editorActionContextService)
                    editorActionContextService->clearWorkspaceContext();

            });
    connect(workspaceManager.get(),
            &WorkspaceManager::projectChanged,
            this,
            [this](const ProjectSnapshot& project) {
                if (!editorActionContextService) {
                    editorActionContextService =
                        std::make_unique<EditorActionContextService>();
                }
                editorActionContextService->updateWorkspaceContext(project);

            });
    connect(tabManager.get(),
            &TabManager::activeDocumentChanged,
            this,
            [this](const DocumentSnapshot& snapshot) {
                scheduleActiveEditorPassiveRefresh();

                requestLiveInsightUpdates();
                if (analysisScheduler && !snapshot.fileName.isEmpty()) {
                    refreshDiagnosticsAnalysisState();
                }
            });
    connect(tabManager.get(),
            &TabManager::tabCreated,
            this,
            [this](MyCodeEditor* editor) {
                if (!editor)
                    return;
                connect(editor,
                        &QPlainTextEdit::cursorPositionChanged,
                        this,
                        [this, editor]() {
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor) {

                                requestLiveInsightUpdates();
                            }
                        });
                connect(editor,
                        &QPlainTextEdit::selectionChanged,
                        this,
                        [this, editor]() {
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor) {
                                requestWorkspaceHubUpdate();
                            }
                        });
                connect(editor,
                        &MyCodeEditor::hierarchyInstanceContextChanged,
                        this,
                        [this, editor](const HierarchyInstanceContext&) {
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor) {

                            }
                        });
                connect(editor,
                        &MyCodeEditor::insightScopeChanged,
                        this,
                        [this, editor]() {
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor) {
                                requestLiveInsightUpdates();
                            }
                        });
                connect(editor,
                        &MyCodeEditor::documentChangeApplied,
                        this,
                        [this, editor](const DocumentChange&) {
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor) {

                                requestLiveInsightUpdates();
                            }
                        });
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::fileSymbolAnalysisFinished,
            this,
            [this](const QString&, int) {
                if (editorCoordinator)
                    editorCoordinator->refreshPinloomCodeLinkMarkers();
                if (!analysisScheduler
                    || !analysisScheduler->isSemanticAnalysisActive()) {
                    refreshTemporaryEditorSemanticCatalog();
                }

            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisStarted,
            this,
            [this](const ProjectSnapshot&, int) {
                refreshDiagnosticsAnalysisState();

            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot&, int, int) {
                const QVariant pending =
                    property("pendingSemanticPublicationTelemetry");
                const bool triviaOnly = pending.isValid()
                    && pending.value<SemanticAnalysisTelemetry>().impact
                           == SemanticChangeImpact::TriviaOnly;
                if (!triviaOnly)
                    refreshTemporaryEditorSemanticCatalog();
                refreshDiagnosticsAnalysisState();

                requestLiveInsightUpdates();
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisDeferred,
            this,
            [this](const ProjectSnapshot&, int, qint64, qint64) {
                refreshDiagnosticsAnalysisState();

            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceAnalysisRequestResolved,
            this,
            [this](const WorkspaceAnalysisRequestTelemetry&) {
                refreshDiagnosticsAnalysisState();

            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisCancelled,
            this,
            [this](const WorkspaceAnalysisRequestTelemetry&) {
                refreshDiagnosticsAnalysisState();

            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::documentSemanticStateChanged,
            this,
            [this](const DocumentSemanticStatus& status) {
                if (!tabManager)
                    return;
                MyCodeEditor* currentEditor =
                    tabManager->getCurrentEditor();
                const QString currentFileName = currentEditor
                    ? currentEditor->documentFileName()
                    : QString();
                if (currentFileName.isEmpty()
                    || QFileInfo(currentFileName).absoluteFilePath()
                           .compare(QFileInfo(status.fileName)
                                        .absoluteFilePath(),
                                    Qt::CaseInsensitive)
                           != 0) {
                    return;
                }
                refreshDiagnosticsAnalysisState();

            });

}

void MainWindow::scheduleActiveEditorPassiveRefresh(
    const QString& changedFileName)
{
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

    if (activeEditorPassiveRefreshQueued)
        return;
    activeEditorPassiveRefreshQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            activeEditorPassiveRefreshQueued = false;
            runActiveEditorPassiveRefresh();
        },
        Qt::QueuedConnection);
}

void MainWindow::runActiveEditorPassiveRefresh()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    const QString changedFileName = pendingActiveEditorPassiveRefreshAll
        ? QString()
        : pendingActiveEditorPassiveRefreshFile;
    pendingActiveEditorPassiveRefreshFile.clear();
    pendingActiveEditorPassiveRefreshAll = false;

    QElapsedTimer stageTimer;
    stageTimer.start();
    refreshActiveEditorDiagnosticHighlights(changedFileName);
    const qint64 diagnosticsMs = stageTimer.elapsed();
    stageTimer.restart();
    refreshActiveEditorSemanticDecorations(changedFileName);
    const qint64 decorationScheduleMs = stageTimer.elapsed();
    stageTimer.restart();
    if (semanticDocks) {
        if (ProblemsPanelCoordinator* problemsPanel =
                semanticDocks->problemsPanelCoordinator()) {
            // Re-query after the analysis publication turn. The immediate
            // analysis-state update can precede other queued active-document
            // UI work, leaving Current File on an older diagnostic snapshot.
            if (problemsPanel->isVisibleToUser())
                problemsPanel->update();
        }
    }
    const qint64 panelsMs = stageTimer.elapsed();
    const qint64 totalMs = totalTimer.elapsed();

    const QVariant pending =
        property("pendingSemanticPublicationTelemetry");
    if (pending.isValid()) {
        SemanticAnalysisTelemetry telemetry =
            pending.value<SemanticAnalysisTelemetry>();
        telemetry.stage = SemanticAnalysisStage::Navigation;
        telemetry.uiRefreshMs = totalMs;
        telemetry.detail = QStringLiteral(
            "passiveEditorRefresh=1 hierarchyRebuild=0 diagnosticsMs=%1 decorationScheduleMs=%2 ghostWorker=editor panelsMs=%3")
                               .arg(diagnosticsMs)
                               .arg(decorationScheduleMs)
                               .arg(panelsMs);
        setProperty("pendingSemanticPublicationTelemetry", QVariant());
        ActivityLogService::getInstance()->append(
            QStringLiteral("SemanticUI"),
            ActivityLogLevel::Info,
            QStringLiteral(
                "Refresh gen=%1 ui=%2 ms (%3)")
                .arg(telemetry.generation)
                .arg(totalMs)
                .arg(telemetry.detail));
        emit semanticUiRefreshTelemetry(telemetry);
    }
}

void MainWindow::refreshActiveEditorDiagnosticHighlights(
    const QString& changedFileName)
{
    if (!tabManager)
        return;

    MyCodeEditor* editor = tabManager->getCurrentEditor();
    if (!editor)
        return;

    const DocumentSnapshot document = tabManager->getCurrentDocumentMetadata();
    if (document.fileName.isEmpty()) {
        editor->setDiagnosticHighlights({});
        return;
    }

    if (!changedFileName.isEmpty()) {
        if (!EditorFileIdentity::same(changedFileName, document.fileName))
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
    if (!editor) {
        ++semanticDecorationGeneration;
        if (semanticDecorationCancellation)
            semanticDecorationCancellation->store(true);
        return;
    }

    const DocumentSnapshot document = tabManager->getCurrentDocumentMetadata();
    if (document.fileName.isEmpty()) {
        ++semanticDecorationGeneration;
        if (semanticDecorationCancellation)
            semanticDecorationCancellation->store(true);
        editor->setSemanticDecorations({});
        return;
    }

    if (!changedFileName.isEmpty()) {
        if (!EditorFileIdentity::same(changedFileName, document.fileName))
            return;
    }

    SemanticDecorationQuery query;
    query.fileName = document.fileName;
    query.documentText = editor->cachedDocumentText();
    const std::uint64_t documentRevision = editor->semanticDocumentRevision();
    const std::uint64_t generation = ++semanticDecorationGeneration;
    if (semanticDecorationCancellation)
        semanticDecorationCancellation->store(true);
    const std::shared_ptr<std::atomic_bool> cancellation =
        std::make_shared<std::atomic_bool>(false);
    semanticDecorationCancellation = cancellation;
    query.isCancelled = [cancellation]() {
        return cancellation->load();
    };

    const std::shared_ptr<const SemanticIndexSnapshot> snapshot =
        SemanticIndex::getInstance()->snapshot();
    const QPointer<MyCodeEditor> guardedEditor(editor);
    auto* watcher =
        new QFutureWatcher<SemanticDecorationBuildResult>(this);
    connect(
        watcher,
        &QFutureWatcher<SemanticDecorationBuildResult>::finished,
        this,
        [this,
         watcher,
         guardedEditor,
         generation,
         cancellation,
         documentRevision,
         fileName = query.fileName]() {
            const SemanticDecorationBuildResult result = watcher->result();
            watcher->deleteLater();
            if (cancellation->load()
                || generation != semanticDecorationGeneration
                || !guardedEditor
                || !tabManager
                || tabManager->getCurrentEditor() != guardedEditor
                || !EditorFileIdentity::same(
                    guardedEditor->documentFileName(), fileName)
                || guardedEditor->semanticDocumentRevision()
                    != documentRevision) {
                return;
            }
            guardedEditor->setSemanticDecorations(
                result.report.decorations);
            ActivityLogService::getInstance()->append(
                QStringLiteral("SemanticUI"),
                ActivityLogLevel::Info,
                QStringLiteral(
                    "Decoration worker gen=%1 build=%2 ms items=%3")
                    .arg(generation)
                    .arg(result.elapsedMs)
                    .arg(result.report.decorations.size()));
        });
    watcher->setFuture(QtConcurrent::run(
        [query, snapshot, cancellation]() {
            SemanticDecorationBuildResult result;
            if (cancellation->load() || !snapshot)
                return result;
            QElapsedTimer timer;
            timer.start();
            SemanticIndex localIndex;
            localIndex.setSnapshot(snapshot);
            SemanticDecorationService service(&localIndex);
            result.report = service.decorationsForDocument(query);
            result.elapsedMs = timer.elapsed();
            if (cancellation->load())
                result.report.decorations.clear();
            return result;
        }));
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
                postActivityMessage(message, timeoutMs);
        });
    semanticDocks->setup();
    if (semanticDocks->scopedSearchPanelCoordinator()) {
        scopedReplaceDocuments =
            std::make_unique<WorkspaceEditDocumentManager>(
                tabManager.get());
        scopedReplaceWorkflow =
            std::make_unique<ScopedReplaceWorkflow>(
                scopedReplaceDocuments.get(),
                nullptr,
                notificationCenter.get());
        semanticDocks->scopedSearchPanelCoordinator()
            ->setReplaceWorkflow(
                scopedReplaceWorkflow.get());
        semanticDocks->scopedSearchPanelCoordinator()
            ->setContextProvider(
                [this]() { return scopedSearchContext(); });
        semanticDocks->scopedSearchPanelCoordinator()
            ->setNavigationHandler(
                [this](const QString& fileName,
                       int line,
                       int column) {
                    if (navigationCommandCoordinator) {
                        navigationCommandCoordinator
                            ->navigateToFileAndLine(
                                fileName, line, column);
                    }
                });
        semanticDocks->scopedSearchPanelCoordinator()
            ->setRegisteredActionRequestHandler(
                [this](const QString& actionId,
                       const QVariantMap& parameters) {
                    return executeRegisteredUiAction(
                        actionId, parameters);
                });
    }
}

ScopedSearchPanelContext MainWindow::scopedSearchContext() const
{
    ScopedSearchPanelContext context;
    if (!tabManager)
        return context;

    MyCodeEditor* const activeEditor =
        tabManager->getCurrentEditor();
    SharedDocument* const activeDocument =
        tabManager->sharedDocumentForEditor(activeEditor);
    if (activeEditor) {
        context.activeFileName =
            activeDocument && !activeDocument->fileName().isEmpty()
            ? activeDocument->fileName()
            : (activeDocument
                   ? activeDocument->documentId()
                   : activeEditor->documentFileName());
        context.cursorChar =
            activeEditor->textCursor().position();
    }

    const QStringList workspaceFiles =
        workspaceManager
            && workspaceManager->isWorkspaceOpen()
        ? workspaceManager->getSystemVerilogFiles()
        : QStringList{};
    QSet<QString> workspaceFileIdentities;
    for (const QString& fileName : workspaceFiles) {
        const QString identity =
            EditorFileIdentity::lookupKey(fileName);
        if (!identity.isEmpty())
            workspaceFileIdentities.insert(identity);
    }

    QList<MyCodeEditor*> editors =
        tabManager->openEditors();
    if (activeEditor) {
        editors.removeAll(activeEditor);
        editors.prepend(activeEditor);
    }

    QSet<SharedDocument*> capturedDocuments;
    QSet<QString> capturedWorkspaceFiles;
    SearchDocumentSnapshot activeSnapshot;
    bool hasActiveSnapshot = false;
    for (MyCodeEditor* editor : std::as_const(editors)) {
        SharedDocument* const document =
            tabManager->sharedDocumentForEditor(editor);
        if (!editor || !document
            || capturedDocuments.contains(document)) {
            continue;
        }

        const QString fileName =
            !document->fileName().isEmpty()
            ? document->fileName()
            : document->documentId();
        const QString fileIdentity =
            EditorFileIdentity::lookupKey(fileName);
        const bool isWorkspaceDocument =
            !fileIdentity.isEmpty()
            && workspaceFileIdentities.contains(
                fileIdentity);
        if (!isWorkspaceDocument
            && document != activeDocument) {
            continue;
        }

        QTextDocument* const textDocument =
            document->textDocument();
        if (fileName.isEmpty() || !textDocument)
            continue;

        const QString text = textDocument->toPlainText();
        QList<MyCodeEditor*> candidateViews =
            document->views();
        if (activeEditor
            && activeDocument == document) {
            candidateViews.removeAll(activeEditor);
            candidateViews.prepend(activeEditor);
        }

        const TSDocument* syntax = nullptr;
        for (MyCodeEditor* candidate :
             std::as_const(candidateViews)) {
            const TSDocument* const candidateSyntax =
                candidate ? candidate->syntaxDocument()
                          : nullptr;
            if (candidateSyntax
                && candidateSyntax->text() == text) {
                syntax = candidateSyntax;
                break;
            }
        }

        const SearchDocumentSnapshot snapshot{
            fileName,
            text,
            syntax,
            static_cast<quint64>(
                document->textRevision())};
        if (document == activeDocument) {
            activeSnapshot = snapshot;
            hasActiveSnapshot = true;
        }
        if (isWorkspaceDocument
            && !capturedWorkspaceFiles.contains(
                fileIdentity)) {
            context.workspaceDocuments.append(snapshot);
            capturedWorkspaceFiles.insert(fileIdentity);
        }
        capturedDocuments.insert(document);
    }

    for (const QString& fileName : workspaceFiles) {
        const QString fileIdentity =
            EditorFileIdentity::lookupKey(fileName);
        if (fileIdentity.isEmpty()
            || capturedWorkspaceFiles.contains(
                fileIdentity)) {
            continue;
        }

        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString text =
            QString::fromUtf8(file.readAll());
        const qint64 modifiedMilliseconds =
            QFileInfo(fileName)
                .lastModified()
                .toMSecsSinceEpoch();
        context.workspaceDocuments.append(
            SearchDocumentSnapshot{
                fileName,
                text,
                nullptr,
                modifiedMilliseconds > 0
                    ? static_cast<quint64>(
                          modifiedMilliseconds)
                    : 0});
        capturedWorkspaceFiles.insert(fileIdentity);
    }

    context.workspaceDocumentsSpecified = true;
    context.documents = context.workspaceDocuments;
    if (hasActiveSnapshot) {
        const QString activeIdentity =
            EditorFileIdentity::lookupKey(
                activeSnapshot.fileName);
        if (activeIdentity.isEmpty()
            || !capturedWorkspaceFiles.contains(
                activeIdentity)) {
            context.documents.prepend(activeSnapshot);
        }
    }
    return context;
}

void MainWindow::setupFileCommandCoordinator()
{
    fileCommandCoordinator = std::make_unique<FileCommandCoordinator>(
        tabManager.get(), workspaceManager.get(), this);
    const QStringList actionIds = {
        QString::fromLatin1(ActionIds::FileNew),
        QString::fromLatin1(ActionIds::FileOpen),
        QString::fromLatin1(ActionIds::FileSave),
        QString::fromLatin1(ActionIds::FileSaveAs),
        QString::fromLatin1(ActionIds::WorkspaceOpen),
    };
    for (const QString& actionId : actionIds) {
        const ActionDescriptor* descriptor =
            findActionById(actionId);
        if (!descriptor)
            continue;
        const ActionAliasDescriptor shortcutAlias =
            descriptor->aliasForSurface(
                ActionSurface::Shortcut);
        auto* action = new QAction(
            descriptor->canonicalName, this);
        action->setObjectName(shortcutAlias.adapterKey);
        action->setProperty("actionId", descriptor->id);
        action->setProperty(
            "executionRoute",
            descriptor->executionRoute);
        action->setToolTip(descriptor->description);
        action->setStatusTip(descriptor->description);
        action->setShortcutContext(
            Qt::ApplicationShortcut);
        action->setShortcut(
            QKeySequence::fromString(
                effectiveActionShortcut(descriptor->id),
                QKeySequence::PortableText));
        addAction(action);
        connect(
            action,
            &QAction::triggered,
            this,
            [this, descriptor]() {
                ActionInvocation invocation;
                invocation.workspaceId =
                    workspaceManager
                    ? workspaceManager->getWorkspacePath()
                    : QString();
                const ActionExecutionResult result =
                    executeAction(
                        *descriptor,
                        *this,
                        invocation);
                if (!result.succeeded) {
                    postActivityMessage(
                        result.failureReason.isEmpty()
                            ? result.message
                            : result.failureReason,
                        5000);
                }
            });
    }
}

void MainWindow::setupGlobalControl()
{
    globalControlCoordinator =
        std::make_unique<GlobalControlCoordinator>(this, this);
    globalControlCoordinator->setAnchorPositionProvider([this]() {
        MyCodeEditor* editor =
            tabManager ? tabManager->getCurrentEditor() : nullptr;
        if (!editor || !editor->viewport())
            return QPoint();
        return editor->viewport()->mapToGlobal(
            editor->cursorRect().bottomLeft());
    });
    globalControlCoordinator->setOpenRequestHandler(
        [this]() {
            const ActionDescriptor* descriptor =
                findActionById(
                    QString::fromLatin1(
                        ActionIds::ViewGlobalControl));
            if (!descriptor)
                return false;
            ActionInvocation invocation;
            invocation.workspaceId = workspaceManager
                ? workspaceManager->getWorkspacePath()
                : QString();
            const ActionExecutionResult result =
                executeAction(
                    *descriptor,
                    *this,
                    invocation);
            if (!result.succeeded) {
                postActivityMessage(
                    result.failureReason.isEmpty()
                        ? result.message
                        : result.failureReason,
                    5000);
            }
            return result.handled;
        });
    globalControlCoordinator->setOpeningHandler([this]() {
        if (MyCodeEditor* editor =
                tabManager ? tabManager->getCurrentEditor() : nullptr) {
            editor->exitInteractionModes(
                EditorModeExitReason::ExternalControl);
        }
    });
    globalControlCoordinator->setContextProvider([this]() {
        GlobalControlQueryContext context;
        MyCodeEditor* editor =
            tabManager ? tabManager->getCurrentEditor() : nullptr;
        QWidget* focus = QApplication::focusWidget();
        if (!editor
            || (focus != editor && !editor->isAncestorOf(focus)))
            return context;
        const EditorSemanticContext editorContext =
            editor->editorSemanticContextForPosition(-1, true);
        context.editorAvailable = true;
        context.fileName = editorContext.fileName;
        context.moduleName = editorContext.moduleName;
        context.packageName = editorContext.packageName;
        context.documentText = editorContext.documentText;
        context.includeFiles =
            editor->includeFileCompletionCandidates();
        context.cursorLine = editorContext.cursorLine;
        context.cursorPosition = editorContext.cursorPosition;
        const EditorSymbolPaletteContext paletteContext =
            editor->symbolPaletteContext();
        context.initialQuery = paletteContext.initialQuery;
        context.memberPath = paletteContext.memberPath;
        context.expectedTypeIdentifier =
            paletteContext.expectedTypeIdentifier;
        context.replacementStart = paletteContext.replacementStart;
        context.replacementLength = paletteContext.replacementLength;
        context.documentRevision = paletteContext.documentRevision;
        context.memberAccess = paletteContext.memberAccess;
        return context;
    });
    globalControlCoordinator->setItemProvider(
        [](GlobalControlCategory category,
           const QString& text,
           const GlobalControlQueryContext& context) {
            return EditorInsertPaletteService().query(
                category, text, context);
        });
    globalControlCoordinator->setActionHandler(
        [this](const GlobalControlItem& item) {
            if (item.kind == GlobalControlItemKind::Symbol
                || item.kind == GlobalControlItemKind::Template) {
                MyCodeEditor* editor =
                    tabManager ? tabManager->getCurrentEditor() : nullptr;
                QString failureReason;
                bool inserted = false;
                if (editor) {
                    switch (item.operation) {
                    case GlobalControlItemOperation::InsertText:
                        if (item.replacementStart >= 0) {
                            if (editor->document()->revision()
                                != item.sourceDocumentRevision) {
                                failureReason = QStringLiteral(
                                    "The document changed while completion was open.");
                                break;
                            }
                            QTextCursor replacement(editor->document());
                            replacement.setPosition(item.replacementStart);
                            replacement.setPosition(
                                item.replacementStart
                                    + item.replacementLength,
                                QTextCursor::KeepAnchor);
                            editor->setTextCursor(replacement);
                        }
                        inserted = editor->insertCompletionText(
                            item.insertionText,
                            item.selectionStart,
                            item.selectionLength,
                            item.templateSlots,
                            &failureReason);
                        break;
                    case GlobalControlItemOperation::InsertPackageImport:
                        inserted = editor->insertPackageImport(
                            item.parameters.value(
                                QStringLiteral("packageName")).toString(),
                            &failureReason);
                        break;
                    case GlobalControlItemOperation::InsertHeaderInclude:
                        inserted = editor->insertHeaderInclude(
                            item.parameters.value(
                                QStringLiteral("includePath")).toString(),
                            &failureReason);
                        break;
                    case GlobalControlItemOperation::CreateHeader:
                        inserted = editor->createAndInsertHeader(
                            item.parameters.value(
                                QStringLiteral("fileName")).toString(),
                            &failureReason);
                        break;
                    }
                }
                if (!inserted) {
                    {
                        postActivityMessage(
                            failureReason.isEmpty()
                                ? QStringLiteral(
                                      "No editable editor tab is available.")
                                : failureReason,
                            5000);
                    }
                }
                return;
            }
            if (!item.actionId.isEmpty()
                && commandLayerCoordinator
                && commandLayerCoordinator->executePaletteCommand(
                    item.actionId,
                    item.parameters)) {
                return;
            }
            QString executionRoute = item.executionRoute;
            if (executionRoute.isEmpty()) {
                const ActionDescriptor* descriptor =
                    findActionByAlias(
                        ActionSurface::GlobalControl,
                        item.id);
                if (descriptor)
                    executionRoute = descriptor->executionRoute;
            }
            if (executionRoute
                == QStringLiteral(
                    "globalControl.recentWorkspaces")) {
                showRecentWorkspacesDialog();
            } else if (executionRoute
                       == QStringLiteral(
                           "globalControl.workspaceSession.save")) {
                if (workspaceSessionCoordinator)
                    workspaceSessionCoordinator->saveSession(true);
            } else if (executionRoute
                       == QStringLiteral(
                           "globalControl.workspaceSession.restore")) {
                if (workspaceSessionCoordinator)
                    workspaceSessionCoordinator->restoreSession();
            } else if (executionRoute
                       == QStringLiteral(
                           "globalControl.workspaceSession.clean")) {
                if (workspaceSessionCoordinator)
                    workspaceSessionCoordinator->clearSession();
            } else if (item.id == QStringLiteral("ow s")) {
                    postActivityMessage(
                        QStringLiteral(
                            "ow s manages local UI/session state only; "
                            "portable project settings use "
                            ".zeroslack/project.json"),
                        4000);
            } else if (executionRoute
                       == QStringLiteral(
                           "globalControl.openWorkspaces")) {
                bool ok = false;
                const int count = item.id.mid(3).trimmed().toInt(&ok);
                if (ok && count > 0 && fileCommandCoordinator) {
                    for (int i = 0; i < count; ++i)
                        fileCommandCoordinator->openDirectoryAsWorkspace();
                }
            }
        });
    globalControlCoordinator->install();
}

void MainWindow::setupCommandLayer()
{
    commandLayerCoordinator = std::make_unique<CommandLayerCoordinator>(
        this,
        tabManager.get(),
        workspaceManager ? workspaceManager->getProjectModel() : nullptr,
        SemanticIndex::getInstance(),
        navigationCommandCoordinator.get(),
        this,
        static_cast<ActionExecutionHost*>(this));
    commandLayerCoordinator->connectSignals();
}

void MainWindow::setupPanelLayoutController()
{
    panelLayoutController =
        std::make_unique<PanelLayoutController>(this, this);
    panelLayoutController->setMainAreaRequestHandler([this](QWidget* content, QWidget* home) {
        const QString id = QStringLiteral("connections");
        if (tabManager->toolPage(id)) tabManager->activateToolPage(id);
        else tabManager->openToolPage(new BorrowedPanelPage(content, home), id, tr("Connections"));
    });
    panelLayoutController
        ->setRegisteredPanelActionRequestHandler(
            [this](const QString& actionId,
                   const QString& panelId,
                   QString* failureReason) {
                const ActionDescriptor* descriptor =
                    findActionById(actionId);
                if (!descriptor) {
                    if (failureReason) {
                        *failureReason = QStringLiteral(
                            "Bottom-panel Action is unavailable");
                    }
                    return false;
                }
                ActionInvocation invocation;
                invocation.workspaceId =
                    workspaceManager
                    ? workspaceManager
                          ->getWorkspacePath()
                    : QString();
                invocation.parameters.insert(
                    QStringLiteral("panelId"),
                    panelId);
                const ActionExecutionResult result =
                    executeAction(
                        *descriptor,
                        *this,
                        invocation);
                const QString reason =
                    result.failureReason.isEmpty()
                    ? result.message
                    : result.failureReason;
                if (failureReason)
                    *failureReason = reason;
                if (!result.succeeded
                    && !reason.isEmpty()) {
                    postActivityMessage(
                        reason, 5000);
                }
                return result.succeeded;
            });
    panelLayoutController->setNavigationDock(
        navigationPane ? navigationPane->dock() : nullptr);

    const auto registerPanel =
        [this](const QString& id, QDockWidget* dock) {
            if (panelLayoutController)
                panelLayoutController->registerBottomPanel(id, dock);
        };
    registerPanel(
        QStringLiteral("problems"),
        semanticDocks && semanticDocks->problemsPanelCoordinator()
            ? semanticDocks->problemsPanelCoordinator()->dock()
            : nullptr);
    registerPanel(
        ScopedSearchPanelCoordinator::panelId(),
        semanticDocks
                && semanticDocks->scopedSearchPanelCoordinator()
            ? semanticDocks->scopedSearchPanelCoordinator()->dock()
            : nullptr);
    registerPanel(
        QStringLiteral("activity"),
        semanticDocks && semanticDocks->activityLogPanelCoordinator()
            ? semanticDocks->activityLogPanelCoordinator()->dock()
            : nullptr);
    registerPanel(
        RtlHighRiskEditPanelCoordinator::panelId(),
        semanticDocks
                && semanticDocks
                       ->rtlHighRiskEditPanelCoordinator()
            ? semanticDocks
                  ->rtlHighRiskEditPanelCoordinator()
                  ->dock()
            : nullptr);
    registerPanel(
        QStringLiteral("connections"),
        semanticDocks
            ? semanticDocks->connectionsDock()
            : nullptr);
    panelLayoutController->registerBottomPanelAlias(
        InstancePairConnectionCoordinator::panelId(),
        QStringLiteral("connections"));
    panelLayoutController->registerBottomPanelAlias(
        MultiSignalPropagationPanel::panelId(),
        QStringLiteral("connections"));
    panelLayoutController->finalize();
    const bool drawerAnimationsEnabled =
        panelLayoutController->animationsEnabled();
    panelLayoutController->setAnimationsEnabled(false);
    panelLayoutController->setBottomCollapsed(true);
    panelLayoutController->setAnimationsEnabled(
        drawerAnimationsEnabled);

    if (semanticDocks && semanticDocks->scopedSearchPanelCoordinator()) {
        connect(semanticDocks->scopedSearchPanelCoordinator(),
                &ScopedSearchPanelCoordinator::resultCountChanged, this,
                [this](int count) {
                    panelLayoutController->setPanelBadge(
                        ScopedSearchPanelCoordinator::panelId(),
                        count > 0 ? QString::number(count) : QString(),
                        count > 0 ? QStringLiteral("info") : QString());
                });
    }
    if (semanticDocks
        && semanticDocks->rtlHighRiskEditPanelCoordinator()) {
        connect(
            semanticDocks->rtlHighRiskEditPanelCoordinator(),
            &RtlHighRiskEditPanelCoordinator::stateChanged,
            this,
            [this](const RtlHighRiskEditPanelOutcome& outcome) {
                if (outcome.panelState == RtlHighRiskEditPanelState::Applied
                    || outcome.panelState == RtlHighRiskEditPanelState::Cancelled
                    || outcome.panelState == RtlHighRiskEditPanelState::Undone
                    || outcome.panelState == RtlHighRiskEditPanelState::NoChanges
                    || outcome.panelState == RtlHighRiskEditPanelState::DryRunComplete)
                    panelLayoutController->closePanel(RtlHighRiskEditPanelCoordinator::panelId());
                const bool failed =
                    outcome.panelState == RtlHighRiskEditPanelState::Rejected
                    || outcome.panelState == RtlHighRiskEditPanelState::Conflict;
                const bool confirmationRequired = outcome.canConfirm;
                panelLayoutController->setPanelBadge(
                    RtlHighRiskEditPanelCoordinator::panelId(),
                    failed
                        ? QStringLiteral("Failed")
                        : confirmationRequired
                            ? QStringLiteral("Confirm")
                            : QString(),
                    failed
                        ? QStringLiteral("error")
                        : confirmationRequired
                            ? QStringLiteral("warning")
                            : QString());
            });
    }
    const auto updateActivityBadge = [this]() {
        const int unread = ActivityLogService::getInstance()->unreadCount();
        panelLayoutController->setPanelBadge(QStringLiteral("activity"),
            unread ? QString::number(unread) : QString(),
            ActivityLogService::getInstance()->unreadLevel() == ActivityLogLevel::Error
                ? QStringLiteral("error") : QStringLiteral("info"));
    };
    connect(ActivityLogService::getInstance(), &ActivityLogService::unreadChanged,
            this, updateActivityBadge);
    updateActivityBadge();

}

void MainWindow::setupRtlActionCoordinator()
{
    RtlActionCoordinatorCallbacks callbacks;
    callbacks.resolveContext =
        [this](const EditorSemanticContext& context) {
            return resolveEditorActionContext(context);
        };
    callbacks.showPanel =
        [this](const QString& panelId) {
            showPanelById(panelId);
        };
    callbacks.refreshSemanticDocuments =
        [this](const QStringList& fileNames) {
            if (analysisScheduler)
                analysisScheduler->requestDocumentSemanticRefresh(fileNames);
        };

    rtlActionCoordinator =
        std::make_unique<RtlActionCoordinator>(
            tabManager.get(),
            workspaceManager.get(),
            semanticDocks.get(),
            panelLayoutController.get(),
            this,
            std::move(callbacks),
            notificationCenter.get(),
            this);
}

void MainWindow::setupViewMenu()
{
    if (!menuBar() || viewMenu)
        return;

    viewMenu = UiControls::addMenu(menuBar(), tr("&View"));
    viewMenu->setObjectName(QStringLiteral("viewMenu"));

    const auto addRegistryAction =
        [this](QMenu* menu, const char* id) {
            return addRegistryMenuAction(
                menu, QString::fromLatin1(id));
        };

    QAction* navigationAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewNavigation);
    if (navigationAction)
        navigationAction->setCheckable(true);
    QAction* contextSidebarAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewContextSidebar);
    if (contextSidebarAction)
        contextSidebarAction->setCheckable(true);
    QAction* scopedSearchAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewScopedSearch);

    QMenu* editorLayoutMenu =
        UiControls::addMenu(viewMenu, tr("Editor Layout"));
    editorLayoutMenu->setObjectName(
        QStringLiteral("editorLayoutMenu"));
    QAction* splitLeftAction =
        addRegistryAction(
            editorLayoutMenu,
            ActionIds::ViewEditorSplitLeft);
    QAction* splitRightAction =
        addRegistryAction(
            editorLayoutMenu,
            ActionIds::ViewEditorSplitRight);
    QAction* splitAboveAction =
        addRegistryAction(
            editorLayoutMenu,
            ActionIds::ViewEditorSplitAbove);
    QAction* splitBelowAction =
        addRegistryAction(
            editorLayoutMenu,
            ActionIds::ViewEditorSplitBelow);
    editorLayoutMenu->addSeparator();
    QAction* maximizeSplitAction =
        addRegistryAction(
            editorLayoutMenu,
            ActionIds::ViewEditorSplitMaximize);
    QAction* equalizeSplitsAction =
        addRegistryAction(
            editorLayoutMenu,
            ActionIds::ViewEditorSplitsEqualize);
    QAction* mergeSplitAction =
        addRegistryAction(
            editorLayoutMenu,
            ActionIds::ViewEditorSplitMerge);
    QAction* reopenTabAction =
        addRegistryAction(
            editorLayoutMenu,
            ActionIds::ViewReopenClosedTab);
    QMenu* groupingMenu =
        UiControls::addMenu(editorLayoutMenu,
            tr("Group Tabs"));
    groupingMenu->setObjectName(
        QStringLiteral("tabGroupingMenu"));
    auto* groupingActions =
        new QActionGroup(groupingMenu);
    groupingActions->setExclusive(true);
    QAction* groupNoneAction =
        addRegistryAction(
            groupingMenu, ActionIds::ViewGroupTabsNone);
    QAction* groupModuleAction =
        addRegistryAction(
            groupingMenu, ActionIds::ViewGroupTabsModule);
    QAction* groupWorkspaceAction =
        addRegistryAction(
            groupingMenu, ActionIds::ViewGroupTabsWorkspace);
    for (QAction* action :
         {groupNoneAction,
          groupModuleAction,
          groupWorkspaceAction}) {
        if (!action)
            continue;
        action->setCheckable(true);
        groupingActions->addAction(action);
    }

    viewMenu->addSeparator();
    QAction* problemsAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewProblems);
    QAction* activityAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewActivity);
    for (QAction* action :
         {problemsAction,
          activityAction}) {
        if (action)
            action->setCheckable(true);
    }
    QAction* collapseBottomAction =
        addRegistryAction(
            viewMenu,
            ActionIds::ViewBottomPanelCollapsed);
    if (collapseBottomAction)
        collapseBottomAction->setCheckable(true);
    viewMenu->addSeparator();
    QAction* settingsAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewSettingsCenter);
    viewMenu->addSeparator();
    QAction* resetLayoutAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewResetPanelLayout);

    const auto setPanelChecked =
        [this](QAction* action,
               const QString& panelId) {
            if (!action)
                return;
            const QDockWidget* dock =
                dockForPanelId(panelId);
            if (dock && panelLayoutController
                && panelLayoutController->isBottomPanel(dock)) {
                action->setChecked(
                    panelLayoutController->isPanelOpen(panelId));
                return;
            }
            action->setChecked(dock && dock->isVisible());
        };
    connect(
        viewMenu,
        &QMenu::aboutToShow,
        this,
        [this,
         navigationAction,
         contextSidebarAction,
         scopedSearchAction,
         splitLeftAction,
         splitRightAction,
         splitAboveAction,
         splitBelowAction,
         maximizeSplitAction,
         equalizeSplitsAction,
         mergeSplitAction,
         reopenTabAction,
         groupNoneAction,
         groupModuleAction,
         groupWorkspaceAction,
         problemsAction,
         activityAction,
         collapseBottomAction,
         settingsAction,
         resetLayoutAction,
         setPanelChecked]() {
            ActionAvailabilityContext context;
            context.editorAvailable =
                tabManager
                && tabManager->getCurrentEditor();
            context.workspaceAvailable =
                workspaceManager
                && workspaceManager->isWorkspaceOpen();
            const auto refreshAvailability =
                [&context](QAction* action) {
                    if (!action)
                        return;
                    const ActionDescriptor* descriptor =
                        findActionById(
                            action->property(
                                "actionId").toString());
                    if (descriptor) {
                        action->setEnabled(
                            evaluateActionAvailability(
                                *descriptor,
                                context)
                                .executable);
                    }
                };
            for (QAction* action :
                 {navigationAction,
                  contextSidebarAction,
                  scopedSearchAction,
                  splitLeftAction,
                  splitRightAction,
                  splitAboveAction,
                  splitBelowAction,
                  maximizeSplitAction,
                  equalizeSplitsAction,
                  mergeSplitAction,
                  reopenTabAction,
                  groupNoneAction,
                  groupModuleAction,
                  groupWorkspaceAction,
                  problemsAction,
                  activityAction,
                  collapseBottomAction,
                  settingsAction,
                  resetLayoutAction}) {
                refreshAvailability(action);
            }

            if (contextSidebarAction) {
                const bool contextVisible =
                    contextWorkspaceController
                    && contextWorkspaceController->dockVisible();
                contextSidebarAction->setChecked(contextVisible);
                contextSidebarAction->setEnabled(
                    contextVisible
                    || (contextWorkspaceController
                        && contextWorkspaceController
                               ->canShowDock()));
            }
            if (collapseBottomAction) {
                collapseBottomAction->setChecked(
                    panelLayoutController
                    && panelLayoutController
                           ->isBottomCollapsed());
            }
            if (equalizeSplitsAction) {
                equalizeSplitsAction->setEnabled(
                    context.editorAvailable
                    && tabManager->splitCount() > 1);
            }
            if (mergeSplitAction) {
                mergeSplitAction->setEnabled(
                    context.editorAvailable
                    && tabManager->splitCount() > 1);
            }

            const TabGroupingMode grouping =
                tabManager
                ? tabManager->tabGroupingMode()
                : TabGroupingMode::None;
            if (groupNoneAction) {
                groupNoneAction->setChecked(
                    grouping == TabGroupingMode::None);
            }
            if (groupModuleAction) {
                groupModuleAction->setChecked(
                    grouping == TabGroupingMode::Module);
            }
            if (groupWorkspaceAction) {
                groupWorkspaceAction->setChecked(
                    grouping == TabGroupingMode::Workspace);
            }
            setPanelChecked(
                navigationAction,
                QStringLiteral("navigation"));
            setPanelChecked(
                problemsAction,
                QStringLiteral("problems"));
            setPanelChecked(
                activityAction,
                QStringLiteral("activity"));
        });


}

void MainWindow::setupWorkspaceMenu()
{
    if (!menuBar() || workspaceMenu)
        return;

    workspaceMenu = UiControls::addMenu(menuBar(), tr("&Workspace"));
    workspaceMenu->setObjectName(QStringLiteral("workspaceMenu"));

    openWorkspacesMenu = UiControls::addMenu(workspaceMenu, tr("Open Workspaces"));
    openWorkspacesMenu->setObjectName(
        QStringLiteral("openWorkspacesMenu"));
    closeActiveWorkspaceAction =
        addRegistryMenuAction(
            workspaceMenu,
            QString::fromLatin1(
                ActionIds::WorkspaceCloseActive));
    refreshWorkspaceMenuEntries();

    workspaceMenu->addSeparator();
    QAction* configureAction =
        addRegistryMenuAction(
            workspaceMenu,
            QString::fromLatin1(
                ActionIds::WorkspaceConfigure));

    workspaceMenu->addSeparator();
    QAction* nextDiagnosticAction =
        addRegistryMenuAction(
            workspaceMenu,
            QString::fromLatin1(
                ActionIds::WorkspaceNextDiagnostic));
    QAction* previousDiagnosticAction =
        addRegistryMenuAction(
            workspaceMenu,
            QString::fromLatin1(
                ActionIds::WorkspacePreviousDiagnostic));
    if (nextDiagnosticAction)
        nextDiagnosticAction->setShortcutContext(
            Qt::ApplicationShortcut);
    if (previousDiagnosticAction) {
        previousDiagnosticAction->setShortcutContext(
            Qt::ApplicationShortcut);
    }
    connect(
        workspaceMenu,
        &QMenu::aboutToShow,
        this,
        [this,
         configureAction,
         nextDiagnosticAction,
         previousDiagnosticAction]() {
            const bool workspaceAvailable =
                workspaceManager
                && workspaceManager->isWorkspaceOpen();
            if (closeActiveWorkspaceAction) {
                closeActiveWorkspaceAction->setEnabled(
                    workspaceAvailable);
            }
            if (configureAction)
                configureAction->setEnabled(workspaceAvailable);
            if (nextDiagnosticAction) {
                nextDiagnosticAction->setEnabled(
                    workspaceAvailable);
            }
            if (previousDiagnosticAction) {
                previousDiagnosticAction->setEnabled(
                    workspaceAvailable);
            }
        });
}

void MainWindow::refreshWorkspaceMenuEntries()
{
    if (!openWorkspacesMenu || !workspaceManager)
        return;

    openWorkspacesMenu->clear();
    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    const int activeIndex = workspaceManager->activeWorkspaceIndex();
    for (int index = 0; index < entries.size(); ++index) {
        const WorkspaceManager::WorkspaceEntry& entry = entries.at(index);
        QAction* action = openWorkspacesMenu->addAction(entry.alias);
        action->setObjectName(
            QStringLiteral("activateWorkspaceAction_%1").arg(index));
        action->setCheckable(true);
        action->setChecked(index == activeIndex);
        action->setToolTip(QDir::toNativeSeparators(entry.path));
        connect(action,
                &QAction::triggered,
                this,
                [this, index]() { activateWorkspace(index); });
    }
    if (entries.isEmpty()) {
        QAction* emptyAction =
            openWorkspacesMenu->addAction(tr("No Open Workspaces"));
        emptyAction->setEnabled(false);
    }
    if (closeActiveWorkspaceAction)
        closeActiveWorkspaceAction->setEnabled(activeIndex >= 0);
}

void MainWindow::activateWorkspace(int index)
{
    if (!workspaceManager
        || !workspaceSessionCoordinator
        || index < 0
        || index >= workspaceManager->workspaceEntries().size()
        || index == workspaceManager->activeWorkspaceIndex()) {
        return;
    }

    if (workspaceSessionCoordinator->switchWorkspace(index)) {
        refreshSettingsCenterWorkspace(
            workspaceManager->getWorkspacePath());
    }
}

void MainWindow::closeActiveWorkspace()
{
    if (!workspaceManager || !workspaceSessionCoordinator)
        return;

    const int index = workspaceManager->activeWorkspaceIndex();
    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    if (index < 0 || index >= entries.size())
        return;

    if (workspaceSessionCoordinator->closeWorkspace(index)) {
        refreshSettingsCenterWorkspace(
            workspaceManager->getWorkspacePath());
    }
}

void MainWindow::setupToolsMenu()
{
    if (!menuBar() || toolsMenu)
        return;

    toolsMenu = UiControls::addMenu(menuBar(), tr("&Tools"));
    toolsMenu->setObjectName(QStringLiteral("toolsMenu"));

    userTemplatesMenu = UiControls::addMenu(toolsMenu, tr("User Templates"));
    userTemplatesMenu->setObjectName(QStringLiteral("userTemplatesMenu"));

    QAction* openGlobalAction =
        addRegistryMenuAction(
            userTemplatesMenu,
            QString::fromLatin1(
                ActionIds::UserTemplatesOpenGlobal));
    QAction* openWorkspaceAction =
        addRegistryMenuAction(
            userTemplatesMenu,
            QString::fromLatin1(
                ActionIds::UserTemplatesOpenWorkspace));
    userTemplatesMenu->addSeparator();
    QAction* reloadAction =
        addRegistryMenuAction(
            userTemplatesMenu,
            QString::fromLatin1(
                ActionIds::UserTemplatesReload));

    QMenu* rtlActionsMenu =
        UiControls::addMenu(toolsMenu,
            tr("RTL Actions"));
    rtlActionsMenu->setObjectName(
        QStringLiteral("rtlActionsMenu"));
    QAction* rtlRenameAction =
        addRegistryMenuAction(
            rtlActionsMenu,
            QString::fromLatin1(
                ActionIds::RtlRename));
    if (rtlRenameAction) {
        rtlRenameAction->setShortcutContext(
            Qt::ApplicationShortcut);
    }
    QAction* rtlConnectionTransformAction =
        addRegistryMenuAction(
            rtlActionsMenu,
            QString::fromLatin1(
                ActionIds::
                    RtlConnectionTransform));
    QAction* connectInstancePairAction =
        addRegistryMenuAction(
            rtlActionsMenu,
            QString::fromLatin1(
                ActionIds::RtlConnectInstancePair));
    QAction* propagateMultipleSignalsAction =
        addRegistryMenuAction(
            rtlActionsMenu,
            QString::fromLatin1(
                ActionIds::RtlPropagateMultipleSignals));
    if (rtlActionCoordinator) {
        rtlActionCoordinator->connectActionAvailability(
            toolsMenu,
            rtlRenameAction,
            rtlConnectionTransformAction,
            connectInstancePairAction,
            propagateMultipleSignalsAction);
    }

    toolsMenu->addSeparator();
    QAction* crashRecoveryAction =
        addRegistryMenuAction(
        toolsMenu,
        QString::fromLatin1(
            ActionIds::ReviewCrashRecovery));
    connect(
        toolsMenu,
        &QMenu::aboutToShow,
        this,
        [this,
         openGlobalAction,
         openWorkspaceAction,
         reloadAction,
         crashRecoveryAction]() {
            const bool workspaceAvailable =
                workspaceManager
                && workspaceManager->isWorkspaceOpen();
            if (openGlobalAction)
                openGlobalAction->setEnabled(true);
            if (openWorkspaceAction) {
                openWorkspaceAction->setEnabled(
                    workspaceAvailable);
            }
            if (reloadAction)
                reloadAction->setEnabled(true);
            if (crashRecoveryAction)
                crashRecoveryAction->setEnabled(true);
        });
}

QAction* MainWindow::addRegistryMenuAction(
    QMenu* menu,
    const QString& actionId)
{
    if (!menu)
        return nullptr;
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor
        || !descriptor->hasSurface(
            ActionSurface::Menu)) {
        return nullptr;
    }
    const ActionAliasDescriptor menuAlias =
        descriptor->aliasForSurface(
            ActionSurface::Menu);
    QAction* action = menu->addAction(
        menuAlias.label.isEmpty()
            ? descriptor->canonicalName
            : menuAlias.label);
    action->setObjectName(
        menuAlias.adapterKey);
    action->setProperty(
        "actionId", descriptor->id);
    action->setProperty(
        "executionRoute",
        descriptor->executionRoute);
    action->setToolTip(
        descriptor->description);
    const QString shortcut =
        effectiveActionShortcut(descriptor->id);
    if (!shortcut.isEmpty()) {
        action->setShortcut(
            QKeySequence::fromString(
                shortcut,
                QKeySequence::PortableText));
    }
    connect(
        action,
        &QAction::triggered,
        this,
        [this, descriptor]() {
            ActionInvocation invocation;
            invocation.workspaceId =
                workspaceManager
                ? workspaceManager
                      ->getWorkspacePath()
                : QString();
            const ActionExecutionResult result =
                executeAction(
                    *descriptor,
                    *this,
                    invocation);
            if (!result.succeeded) {
                postActivityMessage(
                    result.failureReason.isEmpty()
                        ? result.message
                        : result.failureReason,
                    5000);
            }
        });
    return action;
}

ActionExecutionResult MainWindow::executeRegisteredUiAction(
    const QString& actionId,
    const QVariantMap& parameters)
{
    ActionExecutionResult result;
    result.handled = true;
    const ActionDescriptor* descriptor =
        findActionById(actionId);
    if (!descriptor) {
        result.failureReason = QStringLiteral(
            "The requested Action is not registered.");
    } else {
        ActionInvocation invocation;
        invocation.workspaceId = workspaceManager
            ? workspaceManager->getWorkspacePath()
            : QString();
        invocation.parameters = parameters;
        result = executeAction(
            *descriptor, *this, invocation);
    }
    const QString statusMessage =
        result.succeeded
        ? result.message
        : (result.failureReason.isEmpty()
               ? result.message
               : result.failureReason);
    if (!statusMessage.isEmpty()) {
        postActivityMessage(
            statusMessage,
            result.succeeded ? 3000 : 5000);
    }
    return result;
}

ActionExecutionResult MainWindow::executeActionRoute(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation)
{
    ActionExecutionResult result;
    result.handled = true;
    const QString& route = descriptor.executionRoute;
    const auto fail =
        [&result, &descriptor](
            const QString& reason = QString()) {
            result.failureReason =
                reason.isEmpty()
                ? descriptor.unavailableReason
                : reason;
            return result;
        };
    const auto succeeded =
        [&result]() {
            result.succeeded = true;
            return result;
        };
    const auto togglePanel =
        [this](const QString& panelId) {
            QDockWidget* dock =
                dockForPanelId(panelId);
            if (!dock)
                return false;

            if (panelLayoutController
                && panelLayoutController->isBottomPanel(dock)) {
                if (panelLayoutController->isPanelOpen(panelId))
                    return panelLayoutController->closePanel(panelId);
                return panelLayoutController->restorePanel(panelId);
            }

            const bool visible = dock->isVisible();
            if (visible) {
                dock->hide();
                return true;
            }

            showPanelById(panelId);
            return dock->isVisible();
        };

    if (RtlActionCoordinator::handlesRoute(route)) {
        if (!rtlActionCoordinator) {
            return fail(QStringLiteral(
                "RTL action orchestration is unavailable."));
        }
        return rtlActionCoordinator->execute(
            descriptor, invocation);
    }
    if (route
        == QStringLiteral(
            "rtledit.signal.exposeToTop")) {
        if (!editorCoordinator) {
            return fail(QStringLiteral(
                "Expose Signal to Top is unavailable."));
        }
        return editorCoordinator
            ->executeRegisteredExposeSignalAction(
                descriptor, invocation);
    }
    if (route == QStringLiteral(
                     "ui.pinloom.toggleBindingMarkers")) {
        if (!editorCoordinator) {
            return fail(QStringLiteral(
                "Pinloom binding markers are unavailable."));
        }
        editorCoordinator->togglePinloomCodeLinkMarkers();
        result.message = editorCoordinator
                ->pinloomCodeLinkMarkersVisible()
            ? QStringLiteral("Pinloom binding markers shown.")
            : QStringLiteral("Pinloom binding markers hidden.");
        return succeeded();
    }
    if (PinloomCodeLinkCoordinator::handlesRoute(route)) {
        if (!pinloomCodeLinkCoordinator)
            return fail(QStringLiteral(
                "Pinloom code linking is unavailable."));
        return pinloomCodeLinkCoordinator->execute(
            descriptor, invocation);
    }
    if (route.startsWith(QStringLiteral("ui.file."))
        || route == QStringLiteral("ui.workspace.open")) {
        if (!fileCommandCoordinator)
            return fail();
        if (route == QStringLiteral("ui.file.new")) {
            fileCommandCoordinator->newFile();
        } else if (route
                   == QStringLiteral("ui.file.open")) {
            fileCommandCoordinator->openFile();
        } else if (route
                   == QStringLiteral("ui.file.save")) {
            const QString preferredViewId =
                invocation.parameters
                    .value(QStringLiteral("editorViewId"))
                    .toString();
            if (!tabManager
                || !tabManager->editorActionTarget(
                    preferredViewId)) {
                return fail();
            }
            fileCommandCoordinator->saveEditor(
                preferredViewId, false);
        } else if (route
                   == QStringLiteral("ui.file.saveAs")) {
            const QString preferredViewId =
                invocation.parameters
                    .value(QStringLiteral("editorViewId"))
                    .toString();
            if (!tabManager
                || !tabManager->editorActionTarget(
                    preferredViewId)) {
                return fail();
            }
            fileCommandCoordinator->saveEditor(
                preferredViewId, true);
        } else if (route
                   == QStringLiteral("ui.workspace.open")) {
            fileCommandCoordinator
                ->openDirectoryAsWorkspace();
        } else {
            return fail();
        }
        return succeeded();
    }

    if (route == QStringLiteral(
                     "editor.source.goToDefinition")
        || route == QStringLiteral(
                        "insight.signalKernel.showSymbol")
        || route == QStringLiteral(
                        "insight.signalUsageHotspot.showSymbol")
        || route == QStringLiteral(
                        "insight.stateTransition.showSymbol")
        || route == QStringLiteral(
                        "insight.moduleBlock.showSymbol")) {
        if (!editorCoordinator) {
            return fail(QStringLiteral(
                "Editor source Actions are unavailable."));
        }
        return editorCoordinator
            ->executeRegisteredSourceAction(
                descriptor, invocation);
    }

    if (route == QStringLiteral(
                     "editor.multicursor.addNextOccurrence")
        || route == QStringLiteral(
                        "editor.multicursor.selectScopeOccurrences")) {
        const QString preferredViewId =
            invocation.parameters
                .value(QStringLiteral("editorViewId"))
                .toString();
        MyCodeEditor* editor = tabManager
            ? tabManager->editorActionTarget(
                  preferredViewId)
            : nullptr;
        if (!editor) {
            return fail(preferredViewId.isEmpty()
                ? QStringLiteral(
                      "No editor tab is available.")
                : QStringLiteral(
                      "The requested editor view is no longer available."));
        }
        QString message;
        const bool executed = route == QStringLiteral(
                                  "editor.multicursor.addNextOccurrence")
            ? editor->addNextSymbolOccurrence(&message)
            : editor->selectAllSymbolOccurrences(&message);
        if (!executed)
            return fail(message);
        return succeeded();
    }

    if (route == QStringLiteral(
                     "editor.selection.expandSmart")
        || route == QStringLiteral(
                        "editor.navigation.nextSelectedSymbolOccurrence")
        || route == QStringLiteral(
                        "editor.navigation.previousSelectedSymbolOccurrence")) {
        MyCodeEditor* editor = tabManager
            ? tabManager->getCurrentEditor()
            : nullptr;
        if (!editor) {
            return fail(QStringLiteral(
                "No editor tab is available."));
        }
        QString message;
        bool executed = false;
        if (route == QStringLiteral(
                         "editor.selection.expandSmart")) {
            executed = editor->expandSmartSelection(
                &message);
        } else if (route == QStringLiteral(
                                "editor.navigation.nextSelectedSymbolOccurrence")) {
            executed =
                editor->goToNextSelectedSymbolOccurrence(
                    &message);
        } else {
            executed =
                editor->goToPreviousSelectedSymbolOccurrence(
                    &message);
        }
        if (!executed)
            return fail(message);
        result.message = message;
        return succeeded();
    }

    if (route == QStringLiteral("editor.standard.undo")
        || route == QStringLiteral("editor.standard.redo")
        || route == QStringLiteral("editor.standard.cut")
        || route == QStringLiteral("editor.standard.copy")
        || route == QStringLiteral("editor.standard.paste")
        || route == QStringLiteral("editor.standard.selectAll")) {
        const QString preferredViewId =
            invocation.parameters
                .value(QStringLiteral("editorViewId"))
                .toString();
        MyCodeEditor* editor = tabManager
            ? tabManager->editorActionTarget(
                  preferredViewId)
            : nullptr;
        if (!editor) {
            return fail(QStringLiteral(
                "No editor tab is available."));
        }
        if (route == QStringLiteral("editor.standard.undo")) {
            if (!editor->document()->isUndoAvailable())
                return fail(QStringLiteral("Nothing to undo."));
            editor->undo();
        } else if (route == QStringLiteral("editor.standard.redo")) {
            if (!editor->document()->isRedoAvailable())
                return fail(QStringLiteral("Nothing to redo."));
            editor->redo();
        } else if (route == QStringLiteral("editor.standard.copy")) {
            editor->copy();
        } else if (route == QStringLiteral("editor.standard.selectAll")) {
            if (editor->document()->characterCount() <= 1) {
                return fail(QStringLiteral(
                    "The document is empty."));
            }
            editor->selectAll();
        } else {
            if (editor->isReadOnly()) {
                return fail(QStringLiteral(
                    "The editor is read-only."));
            }
            if (route == QStringLiteral("editor.standard.cut")) {
                editor->cut();
            } else {
                QClipboard* clipboard =
                    QApplication::clipboard();
                if (!clipboard
                    || clipboard->text().isEmpty()) {
                    return fail(QStringLiteral(
                        "The clipboard has no text that can be pasted."));
                }
                editor->paste();
            }
        }
        return succeeded();
    }

    if (route == QStringLiteral("editor.navigation.goLine")) {
        MyCodeEditor* editor = tabManager
            ? tabManager->getCurrentEditor()
            : nullptr;
        if (!editor) {
            return fail(QStringLiteral(
                "No editor tab is available."));
        }
        if (!invocation.parameters.contains(
                QStringLiteral("line"))) {
            const int line = editor->showGotoLineDialog();
            if (line < 1) {
                return fail(QStringLiteral(
                    "Go to Line was canceled."));
            }
            result.hasResolvedParameters = true;
            result.resolvedParameters.insert(
                QStringLiteral("line"), line);
            result.message =
                QStringLiteral("Line %1").arg(line);
            return succeeded();
        }
        const int line = invocation.parameters
            .value(QStringLiteral("line"))
            .toInt();
        if (!editor->goToLineNumber(line)) {
            return fail(QStringLiteral(
                "The requested line is outside the document."));
        }
        result.message = QStringLiteral("Line %1").arg(line);
        return succeeded();
    }

    if (route == QStringLiteral("editor.lines.duplicate")
        || route == QStringLiteral("editor.lines.delete")
        || route == QStringLiteral("editor.lines.join")
        || route == QStringLiteral("editor.lines.moveUp")
        || route == QStringLiteral("editor.lines.moveDown")) {
        const QString preferredViewId =
            invocation.parameters
                .value(QStringLiteral("editorViewId"))
                .toString();
        MyCodeEditor* editor = tabManager
            ? tabManager->editorActionTarget(
                  preferredViewId)
            : nullptr;
        if (!editor) {
            return fail(preferredViewId.isEmpty()
                ? QStringLiteral(
                      "No editor tab is available.")
                : QStringLiteral(
                      "The requested editor view is no longer available."));
        }
        if (editor->isReadOnly()) {
            return fail(QStringLiteral(
                "The editor is read-only."));
        }
        QString message;
        bool executed = false;
        if (route == QStringLiteral(
                         "editor.lines.duplicate")) {
            executed = editor->duplicateLines(&message);
        } else if (route == QStringLiteral(
                         "editor.lines.delete")) {
            executed = editor->deleteLines(&message);
        } else if (route == QStringLiteral(
                                "editor.lines.join")) {
            executed = editor->joinLines(&message);
        } else if (route == QStringLiteral(
                                "editor.lines.moveUp")) {
            executed = editor->moveLinesUp(&message);
        } else {
            executed = editor->moveLinesDown(&message);
        }
        if (!executed)
            return fail(message);
        return succeeded();
    }

    if (route == QStringLiteral("editor.edit.find")
        || route == QStringLiteral("editor.edit.replace")
        || route == QStringLiteral("editor.edit.toggleSelectionCase")
        || route == QStringLiteral(
                        "editor.edit.replaceSelectionWithSpaces")
        || route == QStringLiteral(
                        "editor.structure.organizeSignalDeclarations")
        || route == QStringLiteral(
                        "editor.structure.createSignalDefinition")
        || route == QStringLiteral(
                        "editor.structure.editInstanceSlots")
        || route == QStringLiteral(
                        "editor.structure.createAssignmentQueue")
        || route.startsWith(
            QStringLiteral("editor.format."))) {
        const QString preferredViewId =
            invocation.parameters
                .value(QStringLiteral("editorViewId"))
                .toString();
        MyCodeEditor* editor = tabManager
            ? tabManager->editorActionTarget(
                  preferredViewId)
            : nullptr;
        if (!editor) {
            return fail(preferredViewId.isEmpty()
                ? QStringLiteral(
                      "No editor tab is available.")
                : QStringLiteral(
                      "The requested editor view is no longer available."));
        }

        const bool editsDocument =
            route == QStringLiteral(
                         "editor.edit.replace")
            || route == QStringLiteral(
                            "editor.edit.toggleSelectionCase")
            || route == QStringLiteral(
                            "editor.edit.replaceSelectionWithSpaces")
            || route == QStringLiteral(
                            "editor.structure.organizeSignalDeclarations")
            || route == QStringLiteral(
                            "editor.format.commentLines")
            || route == QStringLiteral(
                            "editor.format.uncommentLines")
            || route == QStringLiteral(
                            "editor.format.indentLines")
            || route == QStringLiteral(
                            "editor.format.unindentLines")
            || route == QStringLiteral(
                            "editor.format.document");
        if (editsDocument && editor->isReadOnly()) {
            return fail(QStringLiteral(
                "The editor is read-only."));
        }

        if (route == QStringLiteral(
                         "editor.edit.find")) {
            editor->showFindDialog();
        } else if (route == QStringLiteral(
                         "editor.edit.replace")) {
            editor->showReplaceDialog();
        } else if (route == QStringLiteral(
                                "editor.edit.toggleSelectionCase")) {
            QString message;
            if (!editor->toggleSelectionCase(&message))
                return fail(message);
        } else if (route == QStringLiteral(
                                "editor.edit.replaceSelectionWithSpaces")) {
            QString message;
            if (!editor->replaceSelectionWithSpaces(&message))
                return fail(message);
        } else if (route == QStringLiteral(
                                "editor.structure.organizeSignalDeclarations")) {
            const int cursorPosition =
                invocation.parameters
                    .value(
                        QStringLiteral("cursorPosition"),
                        editor->textCursor().position())
                    .toInt();
            QString message;
            if (!editor->organizeSignalDeclarationsAt(
                    cursorPosition, &message)) {
                return fail(message);
            }
        } else if (route == QStringLiteral(
                                "editor.structure.createSignalDefinition")) {
            const int cursorPosition =
                invocation.parameters
                    .value(
                        QStringLiteral("cursorPosition"),
                        editor->textCursor().position())
                    .toInt();
            QString message;
            if (!editor->beginSignalDefinitionEditorAt(
                    cursorPosition, &message)) {
                return fail(message);
            }
            result.hasResolvedParameters = true;
            result.resolvedParameters.clear();
        } else if (route == QStringLiteral(
                                "editor.structure.editInstanceSlots")) {
            const int cursorPosition =
                invocation.parameters
                    .value(
                        QStringLiteral("cursorPosition"),
                        editor->textCursor().position())
                    .toInt();
            QString message;
            if (!editor->editInstanceSlotsAt(
                    cursorPosition, &message)) {
                return fail(message);
            }
            result.hasResolvedParameters = true;
            result.resolvedParameters.clear();
        } else if (route == QStringLiteral(
                                "editor.structure.createAssignmentQueue")) {
            const int cursorPosition =
                invocation.parameters
                    .value(
                        QStringLiteral("cursorPosition"),
                        editor->textCursor().position())
                    .toInt();
            QString message;
            if (!editor->createAssignmentQueueAt(
                    cursorPosition, &message)) {
                return fail(message);
            }
            result.hasResolvedParameters = true;
            result.resolvedParameters.clear();
        } else if (route == QStringLiteral(
                                "editor.format.commentLines")) {
            editor->commentSelectionOrLine();
        } else if (route == QStringLiteral(
                                "editor.format.uncommentLines")) {
            editor->uncommentSelectionOrLine();
        } else if (route == QStringLiteral(
                                "editor.format.indentLines")) {
            editor->indentSelectionOrLine();
        } else if (route == QStringLiteral(
                                "editor.format.unindentLines")) {
            editor->unindentSelectionOrLine();
        } else if (route == QStringLiteral(
                                "editor.format.document")) {
            const FormatterReport report =
                editor->formatDocument();
            if (!report.accepted()) {
                return fail(report.diagnostic.isEmpty()
                    ? QStringLiteral(
                          "Document formatting was rejected.")
                    : report.diagnostic);
            }
            result.output.insert(
                QStringLiteral("changed"), report.changed);
            result.output.insert(
                QStringLiteral("outcome"),
                report.outcome
                        == FormatterOutcome::ConservativeFallback
                    ? QStringLiteral("conservativeFallback")
                    : (report.outcome
                               == FormatterOutcome::Applied
                           ? QStringLiteral("applied")
                           : QStringLiteral("unchanged")));
            result.message = report.diagnostic;
            if (result.message.isEmpty()
                && !report.changed) {
                result.message = QStringLiteral(
                    "Document already formatted");
            }
        } else {
            return fail();
        }
        return succeeded();
    }

    if (route
        == QStringLiteral(
            "ui.panel.navigation.toggle")) {
        if (!navigationPane
            || !navigationPane->dock()) {
            return fail();
        }
        navigationPane->toggleVisible();
        return succeeded();
    }

    if (route
        == QStringLiteral(
            "ui.panel.contextSidebar.toggle")) {
        if (!contextWorkspaceController)
            return fail();
        QString failureReason;
        if (!contextWorkspaceController->setDockVisible(
                !contextWorkspaceController->dockVisible(),
                &failureReason)) {
            return fail(failureReason);
        }
        return succeeded();
    }

    if (route == QStringLiteral(
                     "ui.globalControl.show")) {
        if (!globalControlCoordinator)
            return fail();
        globalControlCoordinator->open();
        return succeeded();
    }

    if (route
        == QStringLiteral("ui.temporaryEditor.open")) {
        if (!tabManager
            || !contextWorkspaceController) {
            return fail(QStringLiteral(
                "The temporary editor is unavailable."));
        }

        const QString preferredViewId =
            invocation.parameters
                .value(QStringLiteral("editorViewId"))
                .toString();
        MyCodeEditor* sourceEditor =
            tabManager->editorActionTarget(
                preferredViewId);
        const QString requestedDocumentId =
            invocation.parameters
                .value(QStringLiteral("documentId"))
                .toString();
        const QString requestedPath =
            invocation.parameters
                .value(QStringLiteral("path"))
                .toString();
        const auto matchesRequest =
            [&requestedDocumentId,
             &requestedPath](MyCodeEditor* editor) {
                if (!editor)
                    return false;
                if (!requestedDocumentId.isEmpty()
                    && editor->property("sharedDocumentId")
                           .toString()
                           == requestedDocumentId) {
                    return true;
                }
                return !requestedPath.isEmpty()
                    && EditorFileIdentity::same(
                        editor->documentFileName(), requestedPath);
            };
        if ((!requestedDocumentId.isEmpty()
             || !requestedPath.isEmpty())
            && !matchesRequest(sourceEditor)) {
            const QList<MyCodeEditor*> candidates =
                tabManager->openEditors()
                + tabManager->auxiliaryViews();
            const auto found = std::find_if(
                candidates.cbegin(),
                candidates.cend(),
                matchesRequest);
            sourceEditor = found == candidates.cend()
                ? nullptr
                : *found;
        }

        EditorLocation location =
            editorLocationFromActionParameters(
                invocation.parameters);
        if (sourceEditor) {
            if (SharedDocument* shared =
                    tabManager->sharedDocumentForEditor(
                        sourceEditor)) {
                location.documentId = shared->documentId();
                location.filePath = shared->fileName();
            }
            QTextDocument* document =
                sourceEditor->document();
            const int maximumPosition = document
                ? qMax(0, document->characterCount() - 1)
                : 0;
            int cursorPosition =
                sourceEditor->textCursor().position();
            if (invocation.parameters.contains(
                    QStringLiteral("cursorPosition"))) {
                cursorPosition = invocation.parameters
                                     .value(QStringLiteral(
                                         "cursorPosition"))
                                     .toInt();
            } else if (document
                       && invocation.parameters.contains(
                           QStringLiteral("line"))) {
                const int blockNumber = qBound(
                    0,
                    location.line - 1,
                    qMax(0, document->blockCount() - 1));
                const QTextBlock targetBlock =
                    document->findBlockByNumber(blockNumber);
                if (targetBlock.isValid()) {
                    cursorPosition = targetBlock.position()
                        + qBound(
                            0,
                            location.column - 1,
                            qMax(0, targetBlock.length() - 1));
                }
            }
            cursorPosition = qBound(
                0, cursorPosition, maximumPosition);
            const QTextBlock cursorBlock =
                document
                ? document->findBlock(cursorPosition)
                : QTextBlock();
            if (cursorBlock.isValid()) {
                location.line =
                    cursorBlock.blockNumber() + 1;
                location.column = cursorPosition
                    - cursorBlock.position() + 1;
            }

            if (invocation.parameters.contains(
                    QStringLiteral("selectionStart"))
                && invocation.parameters.contains(
                    QStringLiteral("selectionEnd"))
                && document) {
                const int start = qBound(
                    0,
                    invocation.parameters
                        .value(QStringLiteral("selectionStart"))
                        .toInt(),
                    maximumPosition);
                const int end = qBound(
                    start,
                    invocation.parameters
                        .value(QStringLiteral("selectionEnd"))
                        .toInt(),
                    maximumPosition);
                const QTextBlock startBlock =
                    document->findBlock(start);
                const QTextBlock endBlock =
                    document->findBlock(end);
                if (startBlock.isValid()
                    && endBlock.isValid()) {
                    EditorSelectionRange range;
                    range.startLine =
                        startBlock.blockNumber() + 1;
                    range.startColumn =
                        start - startBlock.position() + 1;
                    range.endLine =
                        endBlock.blockNumber() + 1;
                    range.endColumn =
                        end - endBlock.position() + 1;
                    location.selection = range;
                }
            }
        }
        if (!location.isValid()) {
            return fail(QStringLiteral(
                "The temporary-editor target has no document identity."));
        }
        const ContextResource contextResource =
            TemporaryEditorContextProvider::resourceForLocation(
                location,
                workspaceManager
                    ? workspaceManager->getWorkspacePath()
                    : QString());
        if (!contextWorkspaceController->openResource(
                contextResource, ContextPlacement{ContextSurface::Floating, ContextPersistence::Transient, ContextBinding::Global})) {
            return fail(QStringLiteral(
                "The temporary editor could not open the target."));
        }
        result.output.insert(
            QStringLiteral("documentId"),
            location.documentId);
        result.output.insert(
            QStringLiteral("path"),
            location.filePath);
        return succeeded();
    }

    if (route == QStringLiteral("ui.editorTabs.close")
        || route == QStringLiteral(
            "ui.editorTabs.closeOthers")
        || route == QStringLiteral(
            "ui.editorTabs.closeRight")
        || route == QStringLiteral(
            "ui.editorTabs.closeAll")
        || route == QStringLiteral(
            "ui.editorTabs.duplicateView")
        || route == QStringLiteral(
            "ui.editorTabs.toggleLocked")) {
        if (!tabManager)
            return fail();
        QString failureReason;
        if (!tabManager->executeRegisteredTabAction(
                descriptor.id,
                &failureReason)) {
            return fail(failureReason);
        }
        return succeeded();
    }

    const auto splitEditor =
        [this, &fail, &succeeded](
            EditorSplitDirection direction) {
            if (!tabManager
                || !tabManager->getCurrentEditor()
                || !tabManager
                        ->splitCurrentView(direction)) {
                return fail();
            }
            return succeeded();
        };
    if (route
        == QStringLiteral(
            "ui.editorLayout.split.left")) {
        return splitEditor(
            EditorSplitDirection::Left);
    }
    if (route
        == QStringLiteral(
            "ui.editorLayout.split.right")) {
        return splitEditor(
            EditorSplitDirection::Right);
    }
    if (route
        == QStringLiteral(
            "ui.editorLayout.split.above")) {
        return splitEditor(
            EditorSplitDirection::Above);
    }
    if (route
        == QStringLiteral(
            "ui.editorLayout.split.below")) {
        return splitEditor(
            EditorSplitDirection::Below);
    }
    if (route
        == QStringLiteral(
            "ui.editorLayout.split.toggleMaximized")) {
        if (!tabManager
            || !tabManager->getCurrentEditor()) {
            return fail();
        }
        tabManager->toggleCurrentSplitMaximized();
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.editorLayout.split.equalize")) {
        if (!tabManager
            || tabManager->splitCount() < 2) {
            return fail();
        }
        tabManager->equalizeSplitSizes();
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.editorLayout.split.merge")) {
        if (!tabManager
            || !tabManager->mergeCurrentSplit()) {
            return fail();
        }
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.editorTabs.reopenClosed")) {
        if (!tabManager
            || !tabManager->reopenClosedTab()) {
            return fail();
        }
        return succeeded();
    }
    if (route.startsWith(
            QStringLiteral(
                "ui.editorTabs.group."))) {
        if (!tabManager
            || !tabManager->getCurrentEditor()) {
            return fail();
        }
        if (route.endsWith(
                QStringLiteral(".module"))) {
            tabManager->setTabGroupingMode(
                TabGroupingMode::Module);
        } else if (route.endsWith(
                       QStringLiteral(
                           ".workspace"))) {
            tabManager->setTabGroupingMode(
                TabGroupingMode::Workspace);
        } else {
            tabManager->setTabGroupingMode(
                TabGroupingMode::None);
        }
        return succeeded();
    }

    const QHash<QString, QString> panelIdsByRoute = {
        {QStringLiteral("ui.panel.problems.toggle"),
         QStringLiteral("problems")},
        {QStringLiteral("ui.panel.activity.toggle"),
         QStringLiteral("activity")},
    };
    const auto panelRoute =
        panelIdsByRoute.constFind(route);
    if (panelRoute != panelIdsByRoute.constEnd()) {
        if (!togglePanel(panelRoute.value()))
            return fail();
        return succeeded();
    }

    if (route
        == QStringLiteral(
            "ui.bottomPanel.collapsed.toggle")) {
        if (!panelLayoutController)
            return fail();
        panelLayoutController->setBottomCollapsed(
            !panelLayoutController
                 ->isBottomCollapsed());
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.bottomPanel.pinned.toggle")) {
        if (!panelLayoutController)
            return fail();
        QString panelId =
            invocation.parameters
                .value(QStringLiteral("panelId"))
                .toString()
                .trimmed();
        if (panelId.isEmpty()) {
            panelId = panelLayoutController
                          ->activeBottomPanelId();
        }
        if (panelId.isEmpty())
            return fail();
        if (!panelLayoutController->setPanelPinned(
                panelId,
                !panelLayoutController
                     ->isPanelPinned(panelId))) {
            return fail(QStringLiteral(
                "The selected bottom page could not change its pin state."));
        }
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.bottomPanel.closeActive")) {
        if (!panelLayoutController)
            return fail();
        QString panelId =
            invocation.parameters
                .value(QStringLiteral("panelId"))
                .toString()
                .trimmed();
        if (panelId.isEmpty()) {
            panelId = panelLayoutController
                          ->activeBottomPanelId();
        }
        if (panelId.isEmpty()
            || panelLayoutController
                   ->isPanelPinned(panelId)
            || !panelLayoutController
                    ->closePanel(panelId)) {
            return fail(QStringLiteral(
                "The selected bottom page is pinned or unavailable."));
        }
        return succeeded();
    }

    if (route
        == QStringLiteral(
            "ui.panelLayout.reset")) {
        if (!panelLayoutController)
            return fail();
        resetPanelLayout();
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.workspace.closeActive")) {
        if (!workspaceManager
            || !workspaceManager->isWorkspaceOpen()) {
            return fail();
        }
        closeActiveWorkspace();
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.workspace.configure")) {
        if (!workspaceManager
            || !workspaceManager->isWorkspaceOpen()) {
            return fail();
        }
        showWorkspaceConfigurationDialog();
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.diagnostics.next")) {
        if (!workspaceManager
            || !workspaceManager->isWorkspaceOpen()) {
            return fail();
        }
        navigateDiagnostic(false);
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.diagnostics.previous")) {
        if (!workspaceManager
            || !workspaceManager->isWorkspaceOpen()) {
            return fail();
        }
        navigateDiagnostic(true);
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.userTemplates.openGlobal")) {
        openGlobalUserTemplates();
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.userTemplates.openWorkspace")) {
        if (!workspaceManager
            || !workspaceManager->isWorkspaceOpen()) {
            return fail();
        }
        openWorkspaceUserTemplates();
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.userTemplates.reload")) {
        reloadUserTemplates();
        return succeeded();
    }

    if (route
        == QStringLiteral(
            "ui.settingsCenter.show")) {
        if (!settingsCenterDock) {
            result.failureReason =
                QStringLiteral(
                    "Settings Center is unavailable.");
            return result;
        }
        showDockWidget(settingsCenterDock);
        result.succeeded = true;
        return result;
    }

    if (route
        == QStringLiteral(
            "ui.crashRecovery.review")) {
        const QString workspaceRoot =
            tabManager && (tabManager->isTemporaryEditor(tabManager->getCurrentEditor())
                           || !workspaceManager->isWorkspaceOpen())
            ? tabManager->temporaryRecoveryWorkspace()
            : workspaceManager
            ? workspaceManager
                  ->getWorkspacePath()
            : QString();
        openCrashRecoveryReview(
            !workspaceRoot.isEmpty()
                ? workspaceRoot
                : crashRecoveryReviewWorkspace);
        result.succeeded = true;
        return result;
    }

    if (route
        == QStringLiteral(
            "ui.panel.scopedSearch.show")) {
        if (!workspaceManager
            || !workspaceManager->isWorkspaceOpen()) {
            result.failureReason =
                QStringLiteral(
                    "Open a workspace before searching.");
            return result;
        }
        const QString panelId =
            ScopedSearchPanelCoordinator::panelId();
        if (!panelLayoutController) {
            result.failureReason =
                QStringLiteral(
                    "Search and Replace is unavailable.");
            return result;
        }
        panelLayoutController->setBottomCollapsed(false);
        if (!panelLayoutController
                 ->restorePanel(panelId)) {
            result.failureReason =
                QStringLiteral(
                    "Search and Replace is unavailable.");
            return result;
        }
        result.succeeded = true;
        return result;
    }

    result.failureReason =
        QStringLiteral(
            "No main-window route is registered for %1.")
            .arg(descriptor.executionRoute);
    return result;
}


QString MainWindow::crashRecoveryNotificationKey(
    const QString& workspaceRoot) const
{
    return QStringLiteral("crash-recovery:%1")
        .arg(workspaceSessionRootKey(workspaceRoot));
}

QString MainWindow::crashRecoveryHandledKey(
    const QString& workspaceRoot,
    const QString& recoveryId) const
{
    return QStringLiteral("%1|%2")
        .arg(workspaceSessionRootKey(workspaceRoot),
             recoveryId);
}

void MainWindow::notifyCrashRecoveryCandidates(
    const QString& workspaceRoot,
    int candidateCount,
    int isolatedRecordCount)
{
    if (!notificationCenter || workspaceRoot.isEmpty())
        return;

    const QString workspaceKey =
        workspaceSessionRootKey(workspaceRoot);
    crashRecoveryIsolatedRecordCounts.insert(
        workspaceKey,
        qMax(crashRecoveryIsolatedRecordCounts
                 .value(workspaceKey),
             isolatedRecordCount));

    NotificationDraft draft;
    draft.key =
        crashRecoveryNotificationKey(workspaceRoot);
    draft.topic = NotificationTopic::General;
    draft.severity =
        candidateCount > 0
        ? NotificationSeverity::Warning
        : NotificationSeverity::Error;
    draft.source = QStringLiteral("CrashRecovery");
    if (candidateCount > 0) {
        draft.message = QStringLiteral(
            "%1 crash recovery snapshot(s) are available for review.")
                            .arg(candidateCount);
    } else {
        draft.message = QStringLiteral(
            "%1 invalid crash recovery record(s) were quarantined.")
                            .arg(isolatedRecordCount);
    }
    if (isolatedRecordCount > 0 && candidateCount > 0) {
        draft.message += QStringLiteral(
            " %1 invalid record(s) were quarantined.")
                             .arg(isolatedRecordCount);
    }
    if (const ActionDescriptor* action =
            findActionById(
                QString::fromLatin1(
                    ActionIds::
                        ReviewCrashRecovery))) {
        draft.actions.append(
            {action->id,
             action->canonicalName});
    }
    const NotificationPostResult posted =
        notificationCenter->post(draft);
    if (!posted.id.isEmpty()) {
        crashRecoveryNotificationWorkspaces.insert(
            posted.id,
            workspaceRoot);
    }
}

void MainWindow::postCrashRecoveryFailure(
    const QString& documentId,
    const QString& failureReason)
{
    if (!notificationCenter)
        return;

    NotificationDraft draft;
    draft.key =
        QStringLiteral("crash-recovery-error:%1")
            .arg(documentId.isEmpty()
                 ? QStringLiteral("general")
                 : documentId);
    draft.topic = NotificationTopic::General;
    draft.severity = NotificationSeverity::Error;
    draft.source = QStringLiteral("CrashRecovery");
    draft.message =
        failureReason.isEmpty()
        ? QStringLiteral("Crash recovery operation failed.")
        : failureReason;
    notificationCenter->post(draft);
}

void MainWindow::setupCrashRecoveryReviewUi()
{
    if (crashRecoveryReviewDialog)
        return;

    crashRecoveryReviewDialog = new QDialog(this);
    crashRecoveryReviewDialog->setObjectName(
        QStringLiteral("crashRecoveryReviewDialog"));
    crashRecoveryReviewDialog->setWindowTitle(
        tr("Crash Recovery Review"));
    crashRecoveryReviewDialog->setModal(false);
    crashRecoveryReviewDialog->setWindowModality(
        Qt::NonModal);
    crashRecoveryReviewDialog->setAttribute(
        Qt::WA_ShowWithoutActivating,
        true);
    crashRecoveryReviewDialog->setWindowFlag(
        Qt::WindowStaysOnTopHint,
        false);
    crashRecoveryReviewDialog->resize(1040, 720);

    auto* rootLayout =
        new QVBoxLayout(crashRecoveryReviewDialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(8);

    auto* introduction = UiControls::label(
        tr("Review each recovery snapshot against the current source. "
           "Restoring changes the in-memory document only; saving remains "
           "an explicit action."),
        crashRecoveryReviewDialog);
    introduction->setObjectName(
        QStringLiteral("crashRecoveryIntroduction"));
    introduction->setWordWrap(true);
    rootLayout->addWidget(introduction);

    crashRecoveryCandidateList =
        UiControls::treeWidget(crashRecoveryReviewDialog);
    crashRecoveryCandidateList->setObjectName(
        QStringLiteral("crashRecoveryCandidateList"));
    crashRecoveryCandidateList->setColumnCount(3);
    crashRecoveryCandidateList->setHeaderLabels(
        {tr("Document"), tr("Snapshot"), tr("Source State")});
    crashRecoveryCandidateList->setRootIsDecorated(false);
    crashRecoveryCandidateList->setUniformRowHeights(true);
    crashRecoveryCandidateList->setSelectionMode(
        QAbstractItemView::SingleSelection);
    crashRecoveryCandidateList->header()
        ->setSectionResizeMode(0, QHeaderView::Stretch);
    crashRecoveryCandidateList->header()
        ->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    crashRecoveryCandidateList->header()
        ->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    rootLayout->addWidget(crashRecoveryCandidateList, 1);

    auto* comparisonSplitter =
        new QSplitter(Qt::Horizontal,
                      crashRecoveryReviewDialog);
    comparisonSplitter->setObjectName(
        QStringLiteral("crashRecoveryComparisonSplitter"));
    auto* sourceGroup =
        new QGroupBox(tr("Current Source"),
                      comparisonSplitter);
    auto* sourceLayout = new QVBoxLayout(sourceGroup);
    crashRecoverySourceText =
        UiControls::readOnlyText(sourceGroup);
    crashRecoverySourceText->setObjectName(
        QStringLiteral("crashRecoverySourceText"));
    crashRecoverySourceText->setReadOnly(true);
    crashRecoverySourceText->setLineWrapMode(
        QPlainTextEdit::NoWrap);
    sourceLayout->addWidget(crashRecoverySourceText);

    auto* recoveredGroup =
        new QGroupBox(tr("Recovered Snapshot"),
                      comparisonSplitter);
    auto* recoveredLayout =
        new QVBoxLayout(recoveredGroup);
    crashRecoveryRecoveredText =
        UiControls::readOnlyText(recoveredGroup);
    crashRecoveryRecoveredText->setObjectName(
        QStringLiteral("crashRecoveryRecoveredText"));
    crashRecoveryRecoveredText->setReadOnly(true);
    crashRecoveryRecoveredText->setLineWrapMode(
        QPlainTextEdit::NoWrap);
    recoveredLayout->addWidget(
        crashRecoveryRecoveredText);
    comparisonSplitter->addWidget(sourceGroup);
    comparisonSplitter->addWidget(recoveredGroup);
    comparisonSplitter->setSizes({500, 500});
    rootLayout->addWidget(comparisonSplitter, 3);

    crashRecoveryReviewStatus =
        UiControls::label(crashRecoveryReviewDialog);
    crashRecoveryReviewStatus->setObjectName(
        QStringLiteral("crashRecoveryReviewStatus"));
    crashRecoveryReviewStatus->setWordWrap(true);
    crashRecoveryReviewStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    rootLayout->addWidget(crashRecoveryReviewStatus);

    auto* buttons = UiDialogs::buttonBox(
        QDialogButtonBox::Close,
        crashRecoveryReviewDialog);
    buttons->setObjectName(
        QStringLiteral("crashRecoveryReviewButtons"));
    crashRecoveryRestoreButton = UiDialogs::addButton(buttons,
        tr("Restore Selected"),
        QDialogButtonBox::AcceptRole);
    crashRecoveryRestoreButton->setObjectName(
        QStringLiteral("crashRecoveryRestoreButton"));
    crashRecoveryDiscardButton = UiDialogs::addButton(buttons,
        tr("Discard Selected"),
        QDialogButtonBox::DestructiveRole);
    crashRecoveryDiscardButton->setObjectName(
        QStringLiteral("crashRecoveryDiscardButton"));
    crashRecoveryRestoreButton->setEnabled(false);
    crashRecoveryDiscardButton->setEnabled(false);
    rootLayout->addWidget(buttons);

    connect(crashRecoveryCandidateList,
            &QTreeWidget::currentItemChanged,
            this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) {
                reviewCrashRecoverySelection();
            });
    connect(crashRecoveryRestoreButton,
            &QPushButton::clicked,
            this,
            &MainWindow::applyReviewedCrashRecovery);
    connect(crashRecoveryDiscardButton,
            &QPushButton::clicked,
            this,
            &MainWindow::discardReviewedCrashRecovery);
    connect(buttons,
            &QDialogButtonBox::rejected,
            crashRecoveryReviewDialog,
            &QDialog::hide);
}

void MainWindow::openCrashRecoveryReview(
    const QString& workspaceRoot)
{
    const QPointer<QWidget> preservedFocus =
        QApplication::focusWidget();
    setupCrashRecoveryReviewUi();
    if (!workspaceRoot.isEmpty())
        crashRecoveryReviewWorkspace = workspaceRoot;
    if (crashRecoveryReviewWorkspace.isEmpty()) {
        crashRecoveryReviewStatus->setText(
            tr("Open a workspace before reviewing crash recovery data."));
        crashRecoveryCandidateList->clear();
        crashRecoverySourceText->clear();
        crashRecoveryRecoveredText->clear();
        crashRecoveryRestoreButton->setEnabled(false);
        crashRecoveryDiscardButton->setEnabled(false);
    } else {
        reloadCrashRecoveryReview();
    }
    crashRecoveryReviewDialog->show();
    if (preservedFocus
        && preservedFocus->isVisible()
        && preservedFocus->isEnabled()) {
        if (QWidget* const focusWindow =
                preservedFocus->window()) {
            focusWindow->activateWindow();
        }
        preservedFocus->setFocus(
            Qt::OtherFocusReason);
    }
    const QPointer<QWidget> reviewDialog(
        crashRecoveryReviewDialog);
    QMetaObject::invokeMethod(
        crashRecoveryReviewDialog,
        [preservedFocus, reviewDialog]() {
            if (!preservedFocus
                || !preservedFocus->isVisible()
                || !preservedFocus->isEnabled()) {
                return;
            }
            QWidget* const currentFocus =
                QApplication::focusWidget();
            if (!currentFocus
                || (reviewDialog
                    && (currentFocus == reviewDialog
                        || reviewDialog->isAncestorOf(
                            currentFocus)))) {
                if (QWidget* const focusWindow =
                        preservedFocus->window()) {
                    focusWindow->activateWindow();
                }
                preservedFocus->setFocus(
                    Qt::OtherFocusReason);
            }
        },
        Qt::QueuedConnection);
}

void MainWindow::reloadCrashRecoveryReview(
    const QString& preferredRecoveryId)
{
    if (!crashRecoveryReviewDialog
        || !crashRecoveryCandidateList
        || !tabManager) {
        return;
    }

    reviewedCrashRecoveryCandidate.reset();
    crashRecoveryRestoreButton->setEnabled(false);
    crashRecoveryDiscardButton->setEnabled(false);
    crashRecoverySourceText->clear();
    crashRecoveryRecoveredText->clear();

    const CrashRecoveryListResult result =
        tabManager->listCrashRecoveryCandidates(
            crashRecoveryReviewWorkspace);
    if (!result.succeeded()) {
        crashRecoveryCandidateList->clear();
        crashRecoveryReviewStatus->setText(result.reason);
        postCrashRecoveryFailure(
            crashRecoveryReviewWorkspace,
            result.reason);
        return;
    }

    const QString workspaceKey =
        workspaceSessionRootKey(
            crashRecoveryReviewWorkspace);
    crashRecoveryIsolatedRecordCounts.insert(
        workspaceKey,
        qMax(crashRecoveryIsolatedRecordCounts
                 .value(workspaceKey),
             static_cast<int>(
                 result.isolatedRecords.size())));

    QTreeWidgetItem* preferredItem = nullptr;
    {
        const QSignalBlocker blocker(
            crashRecoveryCandidateList);
        crashRecoveryCandidateList->clear();
        for (const CrashRecoveryCandidate& candidate :
             result.candidates) {
            const QString candidateWorkspace =
                candidate.workspacePath.isEmpty()
                ? crashRecoveryReviewWorkspace
                : candidate.workspacePath;
            if (handledCrashRecoveryCandidates.contains(
                    crashRecoveryHandledKey(
                        candidateWorkspace,
                        candidate.recoveryId))) {
                continue;
            }

            auto* item = new QTreeWidgetItem(
                crashRecoveryCandidateList);
            item->setText(
                0,
                crashRecoveryDocumentLabel(candidate));
            item->setText(
                1,
                candidate.snapshotCreatedUtc
                    .toLocalTime()
                    .toString(Qt::ISODate));
            item->setText(
                2,
                crashRecoverySourceStateText(
                    candidate.sourceState));
            item->setData(
                0,
                Qt::UserRole,
                candidate.recoveryId);
            item->setData(
                0,
                Qt::UserRole + 1,
                candidateWorkspace);
            item->setToolTip(
                0,
                QStringLiteral(
                    "%1\nRecovery id: %2\nRevision: %3")
                    .arg(crashRecoveryDocumentLabel(candidate),
                         candidate.recoveryId)
                    .arg(candidate.documentRevision));
            if (candidate.recoveryId
                == preferredRecoveryId) {
                preferredItem = item;
            }
        }
    }

    const int isolatedCount =
        crashRecoveryIsolatedRecordCounts
            .value(workspaceKey);
    const int visibleCount =
        crashRecoveryCandidateList
            ->topLevelItemCount();
    if (visibleCount == 0) {
        crashRecoveryReviewStatus->setText(
            isolatedCount > 0
            ? tr("No recoverable snapshots remain. "
                 "%1 invalid record(s) were quarantined.")
                  .arg(isolatedCount)
            : tr("No crash recovery snapshots are pending."));
        return;
    }

    crashRecoveryReviewStatus->setText(
        isolatedCount > 0
        ? tr("%1 snapshot(s) await review; "
             "%2 invalid record(s) were quarantined.")
              .arg(visibleCount)
              .arg(isolatedCount)
        : tr("%1 snapshot(s) await review.")
              .arg(visibleCount));
    if (!preferredItem) {
        preferredItem =
            crashRecoveryCandidateList
                ->topLevelItem(0);
    }
    crashRecoveryCandidateList->setCurrentItem(
        preferredItem);
}

void MainWindow::reviewCrashRecoverySelection()
{
    reviewedCrashRecoveryCandidate.reset();
    if (crashRecoveryRestoreButton)
        crashRecoveryRestoreButton->setEnabled(false);
    if (crashRecoveryDiscardButton)
        crashRecoveryDiscardButton->setEnabled(false);
    if (!crashRecoveryCandidateList
        || !tabManager) {
        return;
    }

    QTreeWidgetItem* item =
        crashRecoveryCandidateList->currentItem();
    if (!item) {
        crashRecoverySourceText->clear();
        crashRecoveryRecoveredText->clear();
        return;
    }
    const QString recoveryId =
        item->data(0, Qt::UserRole).toString();
    const QString workspaceRoot =
        item->data(0, Qt::UserRole + 1)
            .toString();
    const CrashRecoveryReadResult comparison =
        tabManager->compareCrashRecoveryCandidate(
            recoveryId,
            workspaceRoot);
    if (!comparison.succeeded()) {
        crashRecoverySourceText->clear();
        crashRecoveryRecoveredText->clear();
        crashRecoveryReviewStatus->setText(
            comparison.reason);
        postCrashRecoveryFailure(
            recoveryId,
            comparison.reason);
        return;
    }

    reviewedCrashRecoveryCandidate =
        std::make_unique<CrashRecoveryCandidate>(
            comparison.candidate);
    if (comparison.candidate.sourceReadable) {
        crashRecoverySourceText->setPlainText(
            QString::fromUtf8(
                comparison.currentSourceBytes));
    } else {
        crashRecoverySourceText->setPlainText(
            QStringLiteral("<%1>")
                .arg(crashRecoverySourceStateText(
                    comparison.candidate.sourceState)));
    }
    crashRecoveryRecoveredText->setPlainText(
        comparison.recoveredText);
    crashRecoveryReviewStatus->setText(
        tr("%1 | recovery revision %2 | source %3")
            .arg(crashRecoveryDocumentLabel(
                     comparison.candidate))
            .arg(comparison.candidate
                     .documentRevision)
            .arg(crashRecoverySourceStateText(
                comparison.candidate.sourceState)));
    crashRecoveryRestoreButton->setEnabled(true);
    crashRecoveryDiscardButton->setEnabled(true);
}

void MainWindow::applyReviewedCrashRecovery()
{
    if (!reviewedCrashRecoveryCandidate
        || !tabManager) {
        return;
    }

    const CrashRecoveryCandidate reviewed =
        *reviewedCrashRecoveryCandidate;
    const CrashRecoveryApplyResult result =
        tabManager->applyCrashRecoveryCandidate(
            reviewed);
    if (!result.succeeded()) {
        crashRecoveryReviewStatus->setText(
            result.reason);
        postCrashRecoveryFailure(
            reviewed.recoveryId,
            result.reason);
        reviewCrashRecoverySelection();
        return;
    }

    handledCrashRecoveryCandidates.insert(
        crashRecoveryHandledKey(
            reviewed.workspacePath,
            reviewed.recoveryId));
    if (notificationCenter) {
        notificationCenter->dismissByKey(
            QStringLiteral(
                "crash-recovery-error:%1")
                .arg(reviewed.recoveryId));
    }
    reloadCrashRecoveryReview();
    refreshCrashRecoveryAvailability(
        reviewed.workspacePath);
    {
        postActivityMessage(
            tr("Recovered text was applied in memory; "
               "save explicitly after review."),
            5000);
    }
}

void MainWindow::discardReviewedCrashRecovery()
{
    if (!reviewedCrashRecoveryCandidate
        || !tabManager) {
        return;
    }

    const CrashRecoveryCandidate reviewed =
        *reviewedCrashRecoveryCandidate;
    const CrashRecoveryOperationResult result =
        tabManager->discardCrashRecoveryCandidate(
            reviewed.recoveryId,
            reviewed.workspacePath);
    if (!result.succeeded()) {
        crashRecoveryReviewStatus->setText(
            result.reason);
        postCrashRecoveryFailure(
            reviewed.recoveryId,
            result.reason);
        reviewCrashRecoverySelection();
        return;
    }

    handledCrashRecoveryCandidates.insert(
        crashRecoveryHandledKey(
            reviewed.workspacePath,
            reviewed.recoveryId));
    if (notificationCenter) {
        notificationCenter->dismissByKey(
            QStringLiteral(
                "crash-recovery-error:%1")
                .arg(reviewed.recoveryId));
    }
    reloadCrashRecoveryReview();
    refreshCrashRecoveryAvailability(
        reviewed.workspacePath);
    {
        postActivityMessage(
            tr("The selected recovery snapshot was discarded."),
            5000);
    }
}

void MainWindow::refreshCrashRecoveryAvailability(
    const QString& workspaceRoot)
{
    if (!tabManager || !notificationCenter
        || workspaceRoot.isEmpty()) {
        return;
    }

    const CrashRecoveryListResult result =
        tabManager->listCrashRecoveryCandidates(
            workspaceRoot);
    if (!result.succeeded()) {
        postCrashRecoveryFailure(
            workspaceRoot,
            result.reason);
        return;
    }

    int pendingCount = 0;
    for (const CrashRecoveryCandidate& candidate :
         result.candidates) {
        const QString candidateWorkspace =
            candidate.workspacePath.isEmpty()
            ? workspaceRoot
            : candidate.workspacePath;
        if (!handledCrashRecoveryCandidates.contains(
                crashRecoveryHandledKey(
                    candidateWorkspace,
                    candidate.recoveryId))) {
            ++pendingCount;
        }
    }
    const QString workspaceKey =
        workspaceSessionRootKey(workspaceRoot);
    const int isolatedCount =
        qMax(crashRecoveryIsolatedRecordCounts
                 .value(workspaceKey),
             static_cast<int>(
                 result.isolatedRecords.size()));
    crashRecoveryIsolatedRecordCounts.insert(
        workspaceKey,
        isolatedCount);
    if (pendingCount == 0 && isolatedCount == 0) {
        notificationCenter->dismissByKey(
            crashRecoveryNotificationKey(
                workspaceRoot));
        return;
    }
    notifyCrashRecoveryCandidates(
        workspaceRoot,
        pendingCount,
        isolatedCount);
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
        {
            postActivityMessage(
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
            postActivityMessage(errorMessage, 5000);
        UiDialogs::warning(this, tr("User Templates"), errorMessage);
        return false;
    }

    if (!tabManager || !tabManager->openFileInTab(filePath)) {
        const QString message =
            QStringLiteral("Failed to open %1.").arg(label);
            postActivityMessage(message, 5000);
        UiDialogs::warning(this, tr("User Templates"), message);
        return false;
    }

    {
        postActivityMessage(
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
        postActivityMessage(summary, report.issues.isEmpty() ? 3000 : 7000);

    if (!report.issues.isEmpty()) {
        UiDialogs::warning(this,
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
            postActivityMessage(
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
        UiDialogs::warning(this,
                             tr("Workspace Configuration"),
                             errorMessage);
        return;
    }

    {
        postActivityMessage(
            QStringLiteral(
                "Portable project configuration saved to %1; "
                "analysis queued")
                .arg(QDir::toNativeSeparators(
                    WorkspaceConfigurationService::
                        projectFilePath(
                            workspaceManager
                                ->getWorkspacePath()))),
            4000);
    }
    if (workspaceSessionCoordinator)
        workspaceSessionCoordinator->scheduleSessionSave();
    if (semanticDocks && semanticDocks->refreshCoordinator())
        semanticDocks->refreshCoordinator()->updateProblemsPanel();
}

void MainWindow::setupWorkspaceSessionCoordinator()
{
    WorkspaceSessionUiBridge bridge;
    bridge.captureUiState =
        [this](bool rememberPanelState) {
            WorkspaceSessionUiState state;
            if (rememberPanelState) {
                state.mainWindowGeometry = saveGeometry();
                state.mainWindowState = saveState();
            }
            if (navigationPane) {
                state.navigationFilesQuery =
                    navigationPane->filesSearchQuery();
                state.navigationDesignQuery =
                    navigationPane->designSearchQuery();
            }
            if (panelLayoutController && rememberPanelState) {
                state.panelLayout =
                    panelLayoutController->layoutState();
            }
            if (contextWorkspaceController) {
                state.contextWorkspace =
                    contextWorkspaceController->captureState();
            }
            return state;
        };
    bridge.restoreUiState =
        [this](const WorkspaceSessionUiState& state,
               bool rememberPanelState) {
            WorkspaceSessionUiRestoreResult result;
            if (rememberPanelState
                && !state.mainWindowGeometry.isEmpty()) {
                result.geometryRestored =
                    restoreGeometry(state.mainWindowGeometry);
            }
            if (rememberPanelState
                && !state.mainWindowState.isEmpty()) {
                result.dockStateRestored =
                    restoreState(state.mainWindowState);
            }
            if (navigationPane) {
                navigationPane->setSearchQueries(
                    state.navigationFilesQuery,
                    state.navigationDesignQuery);
            }
            if (rememberPanelState
                && !result.dockStateRestored) {
                resetPanelLayout();
            }
            if (panelLayoutController && rememberPanelState) {
                if (state.panelLayout.valid) {
                    panelLayoutController->restoreLayoutState(
                        state.panelLayout);
                } else {
                    panelLayoutController->bindManagedTabBars();
                }
            }
            if (contextWorkspaceController) {
                contextWorkspaceController->setWorkspaceRoot(
                    workspaceManager
                        ? workspaceManager->getWorkspacePath()
                        : QString());
                contextWorkspaceController->restoreState(
                    state.contextWorkspace,
                    rememberPanelState
                        && result.dockStateRestored);
            }
            return result;
        };
    bridge.showStatus =
        [this](const QString& message, int timeoutMs) {
                postActivityMessage(message, timeoutMs);
        };

    workspaceSessionCoordinator =
        std::make_unique<WorkspaceSessionCoordinator>(
            workspaceManager.get(),
            tabManager.get(),
            panelLayoutController.get(),
            std::move(bridge),
            QString(),
            WorkspaceSessionCoordinator::kDefaultSaveDelayMs,
            this);
    if (contextWorkspaceController) {
        connect(contextWorkspaceController.get(),
                &ContextWorkspaceController::workspaceStateChanged,
                workspaceSessionCoordinator.get(),
                &WorkspaceSessionCoordinator::scheduleSessionSave);
    }

    if (fileCommandCoordinator) {
        QPointer<WorkspaceSessionCoordinator> sessionCoordinator(
            workspaceSessionCoordinator.get());
        fileCommandCoordinator->setWorkspaceOpenHandler(
            [sessionCoordinator](const QString& folderPath) {
                return sessionCoordinator
                    && sessionCoordinator
                           ->openWorkspaceFromUserSelection(folderPath);
            });
    }

    if (settingsCenterService) {
        const SettingsCenterSnapshot snapshot =
            settingsCenterService->load(workspaceManager->getWorkspacePath());
        workspaceSessionCoordinator->setRestoreOnActivation(
            snapshot.value(
                QStringLiteral(
                    "layout.restoreWorkspaceSession")).toBool());
        workspaceSessionCoordinator->setRememberPanelState(
            snapshot.value(
                QStringLiteral(
                    "layout.rememberPanelState")).toBool());
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
        {
            postActivityMessage(
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
            postActivityMessage(
                QStringLiteral("Failed to open diagnostic location"),
                4000);
        return;
    }

    {
        postActivityMessage(
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

    const QString normalizedState = state.trimmed();
    if (diagnosticsAnalysisState == normalizedState)
        return;
    diagnosticsAnalysisState = normalizedState;
    ActivityLogService::getInstance()->append(QStringLiteral("Analysis"),
        ActivityLogLevel::Info,
        QStringLiteral("Semantic analysis: %1").arg(normalizedState));

    problemsPanel->setAnalysisState(normalizedState);
    problemsPanel->update();
}

void MainWindow::refreshDiagnosticsAnalysisState()
{
    const bool workspaceActive = analysisScheduler
        && analysisScheduler->isSemanticAnalysisActive();
    QString visibleState = workspaceActive
        ? QStringLiteral("analyzing")
        : QStringLiteral("current");
    if (analysisScheduler && tabManager) {
        MyCodeEditor* currentEditor = tabManager->getCurrentEditor();
        const QString currentFileName = currentEditor
            ? currentEditor->documentFileName()
            : QString();
        if (!currentFileName.isEmpty()) {
            const DocumentSemanticStatus status =
                analysisScheduler->semanticStatus(currentFileName);
            visibleState = documentSemanticStateName(status.state).toLower();
            if (status.state == DocumentSemanticState::Current
                && workspaceActive) {
                visibleState = QStringLiteral("analyzing");
            }
        }
    }
    setDiagnosticsAnalysisState(visibleState);
}





EditorActionContextQuery MainWindow::editorActionContextQuery(
    const EditorSemanticContext& editorContext)
{
    EditorActionContextQuery query;
    query.editorContext = editorContext;
    MyCodeEditor* editor = tabManager
        ? tabManager->getCurrentEditor() : nullptr;
    if (query.editorContext.fileName.isEmpty() && editor) {
        query.editorContext =
            editor->editorSemanticContextForPosition(-1, false);
    }
    if (analysisScheduler
        && !query.editorContext.fileName.isEmpty()) {
        query.semanticStatus = analysisScheduler->semanticStatus(
            query.editorContext.fileName);
        query.semanticAnalysisActive =
            analysisScheduler->isSemanticAnalysisActive();
    }

    return query;
}

EditorActionContext MainWindow::resolveEditorActionContext(
    const EditorSemanticContext& editorContext)
{
    if (!editorActionContextService) {
        editorActionContextService =
            std::make_unique<EditorActionContextService>();
    }

    const EditorActionContextQuery query =
        editorActionContextQuery(editorContext);
    return editorActionContextService->resolve(query);
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
    if (panelId
        == ScopedSearchPanelCoordinator::panelId()) {
        return semanticDocks
                && semanticDocks
                       ->scopedSearchPanelCoordinator()
            ? semanticDocks
                  ->scopedSearchPanelCoordinator()
                  ->dock()
            : nullptr;
    }
    if (panelId
        == RtlHighRiskEditPanelCoordinator::
            panelId()) {
        return semanticDocks
                && semanticDocks
                       ->rtlHighRiskEditPanelCoordinator()
            ? semanticDocks
                  ->rtlHighRiskEditPanelCoordinator()
                  ->dock()
            : nullptr;
    }
    if (panelId
        == InstancePairConnectionCoordinator::panelId()) {
        return semanticDocks
            ? semanticDocks->connectionsDock()
            : nullptr;
    }
    if (panelId
        == MultiSignalPropagationPanel::panelId()) {
        return semanticDocks
            ? semanticDocks->connectionsDock()
            : nullptr;
    }
    if (panelId == QStringLiteral("connections"))
        return semanticDocks ? semanticDocks->connectionsDock() : nullptr;
    if (panelId == QStringLiteral("settingsCenter")
        || panelId == QStringLiteral("editorAppearance")) {
        return settingsCenterDock;
    }
    return nullptr;
}

void MainWindow::showDockWidget(QDockWidget* dock,
                                const QString& statusMessage)
{
    if (!dock)
        return;

    if (dock == settingsCenterDock) {
        ensureSettingsCenterPanel();
        const QString id = QStringLiteral("settingsCenter");
        if (tabManager->toolPage(id)) tabManager->activateToolPage(id);
        else tabManager->openToolPage(new BorrowedPanelPage(settingsCenterPanel, dock), id, tr("Settings"));
        dock->hide();
        return;
    }

    if (panelLayoutController
        && panelLayoutController->isBottomPanel(dock)) {
        panelLayoutController->restorePanel(
            panelLayoutController->panelIdForDock(dock));
        if (!statusMessage.isEmpty())
            postActivityMessage(statusMessage, 3000);
        return;
    }

    dock->show();
    dock->raise();
    dock->activateWindow();
    if (!statusMessage.isEmpty())
        postActivityMessage(statusMessage, 3000);
}

void MainWindow::showPanelById(const QString& panelId)
{
    if (panelId == QStringLiteral("rtlInsights")
        || panelId == QStringLiteral("signalKernelGraph")) {
        const LiveInsightKind kind =
            panelId == QStringLiteral("signalKernelGraph")
            ? LiveInsightKind::Kernel
            : LiveInsightKind::Module;
        const LiveInsightToolContext context =
            activeLiveInsightToolContext();
        openLiveInsightFromSourceAction(
            kind,
            kind == LiveInsightKind::Module
                ? context.moduleName
                : context.signalName,
            context.fileName,
            context.moduleName,
            context.signalAccessPath);
        return;
    }
    QString resolvedPanelId = panelId;
    if (panelId == InstancePairConnectionCoordinator::panelId()
        || panelId == MultiSignalPropagationPanel::panelId()) {
        if (semanticDocks)
            semanticDocks->showConnectionPage(panelId);
        resolvedPanelId = QStringLiteral("connections");
    }
    QDockWidget* dock = dockForPanelId(resolvedPanelId);
    showDockWidget(dock);
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

    auto* tree = UiControls::treeWidget(dialog);
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

    auto* buttons = UiDialogs::buttonBox(QDialogButtonBox::NoButton, dialog);
    QPushButton* openButton =
        UiDialogs::addButton(buttons, tr("Open"), QDialogButtonBox::AcceptRole);
    openButton->setObjectName(QStringLiteral("recentWorkspacesOpenButton"));
    QPushButton* removeButton = UiDialogs::addButton(buttons,
        tr("Remove from Recent"),
        QDialogButtonBox::DestructiveRole);
    removeButton->setObjectName(
        QStringLiteral("recentWorkspacesRemoveButton"));
    removeButton->setToolTip(
        tr("Remove the selected entry from Recent Workspaces. "
           "Workspace files are not deleted."));
    UiDialogs::addButton(buttons, QDialogButtonBox::Close);
    openButton->setEnabled(!entries.isEmpty());
    removeButton->setEnabled(!entries.isEmpty());
    layout->addWidget(buttons);

    const auto updateButtonState =
        [tree, openButton, removeButton]() {
            QTreeWidgetItem* item = tree ? tree->currentItem() : nullptr;
            const bool hasWorkspace = item
                && !item->data(0, Qt::UserRole).toString().isEmpty();
            openButton->setEnabled(hasWorkspace);
            removeButton->setEnabled(hasWorkspace);
        };

    auto openSelected = [this, dialog, tree]() {
        QTreeWidgetItem* item = tree ? tree->currentItem() : nullptr;
        const QString path =
            item ? item->data(0, Qt::UserRole).toString() : QString();
        if (path.isEmpty() || !workspaceManager)
            return;
        if (workspaceSessionCoordinator
            && workspaceSessionCoordinator->openWorkspace(path)) {
            dialog->close();
        }
    };

    const auto removeSelected =
        [this, tree, updateButtonState]() {
            QTreeWidgetItem* item = tree ? tree->currentItem() : nullptr;
            const QString path =
                item ? item->data(0, Qt::UserRole).toString() : QString();
            if (path.isEmpty() || !workspaceManager
                || !workspaceManager->removeRecentWorkspace(path)) {
                return;
            }

            const int row = tree->indexOfTopLevelItem(item);
            delete tree->takeTopLevelItem(row);
            if (tree->topLevelItemCount() == 0) {
                auto* emptyItem = new QTreeWidgetItem;
                emptyItem->setText(0, tr("No recent workspaces"));
                emptyItem->setText(1, tr("Open a workspace first"));
                emptyItem->setDisabled(true);
                tree->addTopLevelItem(emptyItem);
            } else {
                tree->setCurrentItem(
                    tree->topLevelItem(
                        qMin(row, tree->topLevelItemCount() - 1)));
            }
            updateButtonState();
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
    connect(removeButton,
            &QPushButton::clicked,
            dialog,
            removeSelected);
    connect(tree,
            &QTreeWidget::currentItemChanged,
            dialog,
            [updateButtonState](QTreeWidgetItem*, QTreeWidgetItem*) {
                updateButtonState();
            });

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::resetPanelLayout()
{
    QDockWidget* navigationDock = dockForPanelId(QStringLiteral("navigation"));
    QDockWidget* problemsDock = dockForPanelId(QStringLiteral("problems"));
    QDockWidget* searchDock = dockForPanelId(
        ScopedSearchPanelCoordinator::panelId());
    QDockWidget* activityDock = dockForPanelId(QStringLiteral("activity"));
    QDockWidget* highDiffDock = dockForPanelId(
        RtlHighRiskEditPanelCoordinator::panelId());
    QDockWidget* connectionsDock = dockForPanelId(
        QStringLiteral("connections"));
    QDockWidget* settingsCenterDockWidget =
        dockForPanelId(QStringLiteral("settingsCenter"));

    if (navigationDock)
        addDockWidget(Qt::LeftDockWidgetArea, navigationDock);


    if (panelLayoutController) {
        panelLayoutController->resetLayout();
    } else {
        QDockWidget* bottomDocks[] = {
            problemsDock,
            searchDock,
            activityDock,
            highDiffDock,
            connectionsDock,
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
        for (QDockWidget* dock : bottomDocks)
            showDockWidget(dock);
        if (problemsDock)
            problemsDock->raise();
    }

    if (settingsCenterDockWidget) settingsCenterDockWidget->hide();

    if (workspaceSessionCoordinator)
        workspaceSessionCoordinator->scheduleSessionSave();
        postActivityMessage(tr("Panel layout reset"), 3000);
}

void MainWindow::setupSettingsCenter()
{
    settingsCenterService =
        std::make_unique<SettingsCenterService>();

    // SettingsCenterService is the only persistence owner. The appearance
    // object remains the runtime adapter consumed by EditorCoordinator, with
    // a null QSettings backend to prevent effective workspace values from
    // being written into the global layer.
    editorAppearanceSettings =
        std::make_unique<EditorAppearanceSettings>(
            std::unique_ptr<QSettings>());

    settingsCenterDock = new QDockWidget(tr("Settings"), this);
    // Keep the dock object name so QMainWindow::restoreState continues to
    // restore layouts saved before the Settings Center migration.
    settingsCenterDock->setObjectName(
        QStringLiteral("settingsContentHost"));
    settingsCenterDock->setProperty(
        "settingsCenterPanelId", QStringLiteral("settingsCenter"));
    settingsCenterDock->setProperty(
        "legacyPanelId", QStringLiteral("editorAppearance"));
    settingsCenterDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    settingsCenterDock->hide();


    connect(&ApplicationThemeManager::instance(),
            &ApplicationThemeManager::themeChanged,
            this,
            [this](ThemeMode) {
                refreshThemePresentation();
            });

    applySettingsCenterSnapshot(settingsCenterService->load(
        workspaceManager ? workspaceManager->getWorkspacePath() : QString()));
}

void MainWindow::ensureSettingsCenterPanel()
{
    if (settingsCenterPanel)
        return;
    settingsCenterPanel = new SettingsCenterPanel(
        settingsCenterService.get(),
        workspaceManager ? workspaceManager->getWorkspacePath()
                         : QString(),
        settingsCenterDock);
    settingsCenterDock->setWidget(settingsCenterPanel);
    connect(settingsCenterPanel, &SettingsCenterPanel::settingsApplied, this,
            [this](SettingsCenterScope) {
                applySettingsCenterSnapshot(settingsCenterPanel->snapshot());
            });
}

void MainWindow::applySettingsCenterSnapshot(
    const SettingsCenterSnapshot& snapshot)
{
    if (contextWorkspaceController)
        contextWorkspaceController->setFloatingOpacity(
            snapshot.value(QStringLiteral("appearance.floatingContextOpacity")).toInt());
    if (pinloomHostClient) {
        pinloomHostClient->setExecutablePath(
            snapshot.value(QStringLiteral(
                "integration.pinloomExecutablePath")).toString());
    }
    const QString themeName =
        snapshot.value(
            QStringLiteral("appearance.theme"))
            .toString();
    ThemeMode themeMode = ThemeMode::Light;
    if (themeName.compare(QStringLiteral("Dark"), Qt::CaseInsensitive) == 0) themeMode = ThemeMode::Dark;
    else if (themeName == QStringLiteral("Catppuccin Latte")) themeMode = ThemeMode::CatppuccinLatte;
    else if (themeName == QStringLiteral("Catppuccin Frappe")) themeMode = ThemeMode::CatppuccinFrappe;
    else if (themeName == QStringLiteral("Catppuccin Macchiato")) themeMode = ThemeMode::CatppuccinMacchiato;
    else if (themeName == QStringLiteral("Catppuccin Mocha")) themeMode = ThemeMode::CatppuccinMocha;
    ApplicationThemeManager::instance().setMode(themeMode);

    if (editorAppearanceSettings) {
        EditorAppearanceOptions options;
        options.fontFamily =
            snapshot.value(QStringLiteral("font.family")).toString();
        options.fontSizePt =
            snapshot.value(QStringLiteral("font.sizePt")).toInt();
        options.lineHeight =
            snapshot.value(QStringLiteral("font.lineHeight")).toDouble();
        options.ligaturesEnabled =
            snapshot.value(
                QStringLiteral("font.ligaturesEnabled")).toBool();
        editorAppearanceSettings->setOptions(options);
    }

    if (workspaceSessionCoordinator) {
        workspaceSessionCoordinator->setRestoreOnActivation(
            snapshot.value(
                QStringLiteral(
                    "layout.restoreWorkspaceSession")).toBool());
        workspaceSessionCoordinator->setRememberPanelState(
            snapshot.value(
                QStringLiteral(
                    "layout.rememberPanelState")).toBool());
    }

    editorAnnotationDisplayOptions.enabled =
        snapshot.value(
            QStringLiteral("annotation.enabled")).toBool();
    editorAnnotationDisplayOptions.maxAnnotationsPerLine =
        snapshot.value(
            QStringLiteral(
                "annotation.maxPerLine")).toInt();
    editorAnnotationDisplayOptions.maxLanes =
        snapshot.value(
            QStringLiteral(
                "annotation.maxLanes")).toInt();
    editorAnnotationDisplayOptions =
        editorAnnotationDisplayOptions.normalized();
    if (editorCoordinator) {
        editorCoordinator->setAnnotationDisplayOptions(
            editorAnnotationDisplayOptions);
    }

    if (analysisScheduler) {
        SemanticAnalysisRuntimePolicy policy;
        policy.enabled =
            snapshot.value(
                QStringLiteral(
                    "analysis.enabled")).toBool();
        policy.planningMode =
            snapshot.value(
                QStringLiteral(
                    "analysis.incremental")).toBool()
            ? SemanticAnalysisPlanningMode::
                  DependencyAwareIncremental
            : SemanticAnalysisPlanningMode::
                  FullWorkspace;
        policy.maxDiagnostics =
            snapshot.value(
                QStringLiteral(
                    "analysis.maxDiagnostics")).toInt();
        analysisScheduler
            ->setSemanticAnalysisRuntimePolicy(
                policy);
    }

    QStringList shortcutIssues;
    const bool shortcutsApplied =
        configureActionShortcutOverrides(
            snapshot.value(
                QStringLiteral(
                    "shortcut.overrides")).toMap(),
            &shortcutIssues);
    if (shortcutsApplied)
        applyRegisteredActionShortcuts();
    else if (!shortcutIssues.isEmpty()) {
        postActivityMessage(
            shortcutIssues.constFirst(),
            5000);
    }
}

void MainWindow::applyRegisteredActionShortcuts()
{
    const QList<QAction*> actions =
        findChildren<QAction*>();
    for (QAction* action : actions) {
        if (!action)
            continue;
        const QString actionId =
            action->property("actionId")
                .toString();
        if (actionId.isEmpty()
            || !findActionById(actionId)) {
            continue;
        }
        action->setShortcut(
            QKeySequence::fromString(
                effectiveActionShortcut(actionId),
                QKeySequence::PortableText));
    }
}

void MainWindow::refreshSettingsCenterWorkspace(
    const QString& workspaceRoot)
{
    if (!settingsCenterPanel) {
        if (settingsCenterService)
            applySettingsCenterSnapshot(settingsCenterService->load(workspaceRoot));
        return;
    }
    if (settingsCenterPanel->workspaceRoot() == workspaceRoot)
        settingsCenterPanel->reload();
    else
        settingsCenterPanel->setWorkspaceRoot(workspaceRoot);
    applySettingsCenterSnapshot(settingsCenterPanel->snapshot());
}

void MainWindow::setupEditorCoordinator()
{
    editorCoordinator = std::make_unique<EditorCoordinator>(
        tabManager.get(), this);
    editorCoordinator->setWorkflowDependencies(
        workspaceManager.get(),
        fileCommandCoordinator.get(),
        navigationCommandCoordinator.get(),
        semanticDocks ? semanticDocks->refreshCoordinator() : nullptr);
    editorCoordinator->setAppearanceSettings(editorAppearanceSettings.get());
    editorCoordinator->setAnnotationDisplayOptions(
        editorAnnotationDisplayOptions);
    editorCoordinator->setPinloomCodeLinkStore(
        pinloomCodeLinkCoordinator
            ? pinloomCodeLinkCoordinator->store()
            : nullptr);
    editorCoordinator->setStatusMessageHandler(
        [this](const QString& message, int timeoutMs) {
            {
                if (!message.isEmpty())
                    postActivityMessage(message, timeoutMs);
            }
        });
    editorCoordinator->setActionContextService(
        editorActionContextService.get());
    editorCoordinator->setActionContextQueryProvider(
        [this](const EditorSemanticContext& context) {
            return editorActionContextQuery(context);
        });
    editorCoordinator
        ->setRegisteredActionRequestHandler(
            [this](const QString& actionId,
                   const QVariantMap& parameters) {
                const ActionDescriptor* descriptor =
                    findActionById(actionId);
                if (!descriptor) {
                    {
                        postActivityMessage(
                            QStringLiteral(
                                "The requested editor action "
                                "is not registered."),
                            5000);
                    }
                    return;
                }
                ActionInvocation invocation;
                invocation.workspaceId =
                    workspaceManager
                    ? workspaceManager
                          ->getWorkspacePath()
                    : QString();
                invocation.parameters = parameters;
                const ActionExecutionResult result =
                    executeAction(
                        *descriptor,
                        *this,
                        invocation);
                {
                    const QString message =
                        result.succeeded
                        ? result.message
                        : result
                                  .failureReason
                                  .isEmpty()
                            ? result.message
                            : result
                                  .failureReason;
                    if (!message.isEmpty()) {
                        postActivityMessage(
                            message, 5000);
                    }
                }
            });
    editorCoordinator->connectSignals();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (fileCommandCoordinator)
        fileCommandCoordinator->handleCloseEvent(event, this);
    else
        event->accept();
    if (event && event->isAccepted()
        && workspaceSessionCoordinator) {
        workspaceSessionCoordinator->saveBeforeWorkspaceTransition();
    }
}

void MainWindow::setupSemanticRuntime()
{
    semanticRuntime = std::make_unique<SemanticRuntimeCoordinator>(this);
}

void MainWindow::postActivityMessage(const QString& message, int timeoutMs)
{
    ActivityLogService::getInstance()->append(QStringLiteral("Action"),
        ActivityLogLevel::Info, message, -1, QString(), timeoutMs > 0);
}
