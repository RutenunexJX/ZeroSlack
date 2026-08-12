#include "mainwindow.h"

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
#include "definitionservice.h"
#include "editorcoordinator.h"
#include "editoractioncontextservice.h"
#include "editorfileidentity.h"
#include "filecommandcoordinator.h"
#include "navigationcommandcoordinator.h"
#include "navigationmanager.h"
#include "navigationpanecoordinator.h"
#include "notificationcenter.h"
#include "panellayoutcontroller.h"
#include "diagnosticnavigationservice.h"
#include "diagnosticservice.h"
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
#include "insightfocuscontroller.h"
#include "insightvisualstyle.h"
#include "instancepairconnectionpanel.h"
#include "instancepairconnectionworkflow.h"
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
#include "temporaryeditordrawer.h"
#include "temporaryeditordrawercontroller.h"
#include "temporaryeditorsearchprovider.h"
#include "usertemplateservice.h"
#include "activitylogpanelcoordinator.h"
#include "activitylogservice.h"
#include "problemspanelcoordinator.h"
#include "rtlhighriskeditpanel.h"
#include "rtlinsightspanelcoordinator.h"
#include "signalkernelgraphpanelcoordinator.h"
#include "wavepreviewpanelcoordinator.h"
#include "workspaceconfigurationdialog.h"
#include "workspaceeditdocumentmanager.h"
#include "workspacesessionstateservice.h"
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
#include <QFutureWatcher>
#include <QFormLayout>
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
#include <QProgressBar>
#include <QPointer>
#include <QPushButton>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
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

QString normalizedRtlActionFileName(
    const QString& fileName)
{
    return EditorFileIdentity::normalized(fileName);
}

std::string rtlActionUtf8String(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(
        bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
}

QString rtlActionFromUtf8(const std::string& text)
{
    return QString::fromUtf8(
        text.data(), static_cast<qsizetype>(text.size()));
}

template <typename CapturedDocument>
bool captureRtlActionDocuments(
    const QSet<QString>& workspaceFiles,
    const SemanticSnapshotToken& semanticToken,
    rtledit::WorkspaceDocumentManager& documents,
    QHash<QString, CapturedDocument>* captured,
    QString* failureReason)
{
    if (!captured || !semanticToken.isValid()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "A current Slang semantic snapshot is required.");
        }
        return false;
    }

    QHash<QString, QString> semanticContents;
    for (auto it =
             semanticToken.snapshot
                 ->fileContentsView().constBegin();
         it != semanticToken.snapshot
                   ->fileContentsView().constEnd();
         ++it) {
        const QString fileName =
            normalizedRtlActionFileName(it.key());
        if (!fileName.isEmpty())
            semanticContents.insert(fileName, it.value());
    }

    captured->clear();
    for (const QString& requestedFile : workspaceFiles) {
        const QString fileName =
            normalizedRtlActionFileName(requestedFile);
        if (fileName.isEmpty()
            || captured->contains(fileName)) {
            continue;
        }
        const auto live =
            documents.snapshot(
                rtlActionUtf8String(fileName));
        if (!live) {
            if (failureReason) {
                *failureReason = QStringLiteral(
                    "The workspace document is unavailable: %1")
                    .arg(fileName);
            }
            captured->clear();
            return false;
        }
        const QString text =
            rtlActionFromUtf8(live->text);
        auto syntax = std::make_shared<TSDocument>();
        syntax->setText(text);

        CapturedDocument document;
        document.fileName = fileName;
        document.revision = live->version.value;
        document.text = text;
        document.syntax = std::move(syntax);
        const auto semantic =
            semanticContents.constFind(fileName);
        document.unsaved =
            semantic == semanticContents.constEnd()
            || semantic.value() != text;
        captured->insert(fileName, std::move(document));
    }
    if (captured->isEmpty()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "No SystemVerilog workspace documents were captured.");
        }
        return false;
    }
    return true;
}

bool isSupportedRtlRenameSubject(
    const SemanticSymbolRecord& record)
{
    const bool supportedKind =
        record.declarationKind
            == SymbolTaxonomy::DeclarationKind::Port
        || record.declarationKind
            == SymbolTaxonomy::DeclarationKind::Parameter
        || record.declarationKind
            == SymbolTaxonomy::DeclarationKind::Localparam;
    const bool supportedOwner =
        record.owner.kind
            == SymbolTaxonomy::SymbolOwnerScope::Module
        || record.owner.kind
            == SymbolTaxonomy::SymbolOwnerScope::Interface;
    return supportedKind
        && supportedOwner
        && record.stableKey.isValid();
}

DefinitionResult resolveRtlRenameSubject(
    const EditorSemanticContext& context,
    const TSIdentifierTarget& identifier)
{
    DefinitionQuery query;
    query.symbolName = identifier.text;
    query.fileName = context.fileName;
    query.moduleName = context.moduleName;
    query.linePrefixBeforeCursor =
        context.lineUpToCursor;
    query.cursorLine = context.cursorLine;
    query.cursorColumn = context.column;
    return DefinitionService(
               SemanticIndex::getInstance())
        .resolveDefinition(query);
}

std::optional<SemanticSymbolRecord>
resolveRtlInstanceSubject(
    const SemanticSnapshotToken& token,
    const EditorSemanticContext& context,
    const TSIdentifierTarget& identifier,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!token.isValid() || !identifier.ok()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "A current semantic snapshot and one selected "
                "instance identifier are required.");
        }
        return std::nullopt;
    }

    QList<SemanticSymbolRecord> exact;
    QList<SemanticSymbolRecord> containing;
    for (const SemanticSymbolRecord& record :
         token.snapshot->getSymbolRecords(
             context.fileName)) {
        if (record.name != identifier.text
            || record.declarationKind
                != SymbolTaxonomy::DeclarationKind::Instance
            || !record.stableKey.isValid()
            || !EditorFileIdentity::same(
                record.location.fileName,
                context.fileName)
            || (!context.moduleName.isEmpty()
                && record.owner.name
                    != context.moduleName)) {
            continue;
        }

        const int recordStart =
            record.location.position;
        const int recordLength =
            qMax(record.location.length,
                 static_cast<int>(
                     record.name.size()));
        if (recordStart == identifier.startChar
            || record.stableKey.sourcePosition
                == identifier.startChar) {
            exact.append(record);
        } else if (
            recordStart >= 0
            && identifier.startChar >= recordStart
            && identifier.startChar
                < recordStart + recordLength) {
            containing.append(record);
        }
    }

    const QList<SemanticSymbolRecord>& matches =
        !exact.isEmpty() ? exact : containing;
    if (matches.size() != 1) {
        if (failureReason) {
            *failureReason =
                matches.isEmpty()
                ? QStringLiteral(
                      "Place the cursor on one exact Slang module "
                      "instance declaration.")
                : QStringLiteral(
                      "The selected instance declaration is "
                      "semantically ambiguous.");
        }
        return std::nullopt;
    }
    return matches.constFirst();
}

bool rtlTransactionFailureNeedsNotification(
    const RtlHighRiskEditPanelOutcome& outcome)
{
    if (outcome.panelState
        == RtlHighRiskEditPanelState::Conflict) {
        return true;
    }
    switch (outcome.failure) {
    case RtlHighRiskEditWorkflowFailure::ApplyFailed:
    case RtlHighRiskEditWorkflowFailure::AtomicRollbackFailed:
    case RtlHighRiskEditWorkflowFailure::UndoFailed:
    case RtlHighRiskEditWorkflowFailure::UndoConflict:
    case RtlHighRiskEditWorkflowFailure::
        TransactionGenerationConflict:
    case RtlHighRiskEditWorkflowFailure::ExternalModification:
        return true;
    case RtlHighRiskEditWorkflowFailure::None:
    case RtlHighRiskEditWorkflowFailure::MissingDependency:
    case RtlHighRiskEditWorkflowFailure::InvalidState:
    case RtlHighRiskEditWorkflowFailure::PlanningRejected:
    case RtlHighRiskEditWorkflowFailure::InvalidPlan:
    case RtlHighRiskEditWorkflowFailure::PreviewConflict:
    case RtlHighRiskEditWorkflowFailure::
        ConfirmationTokenMismatch:
    case RtlHighRiskEditWorkflowFailure::
        StaleSemanticGeneration:
    case RtlHighRiskEditWorkflowFailure::
        StaleDocumentRevision:
    case RtlHighRiskEditWorkflowFailure::NothingToUndo:
        break;
    }
    return false;
}

struct InstancePairUserSelection {
    QString leftInstancePath;
    QString rightInstancePath;
    QString connectionName;
};

std::optional<InstancePairUserSelection>
selectInstancePair(
    QWidget* parent,
    const QList<DesignHierarchyNode>& nodes,
    const QString& sourceModule,
    const InstancePairUserSelection& defaults)
{
    QHash<QString, DesignHierarchyNode> nodesByPath;
    QList<DesignHierarchyNode> leftChoices;
    for (const DesignHierarchyNode& node : nodes) {
        if (node.isTop || node.unresolved
            || !node.inSelectedTop
            || node.instancePath.isEmpty()) {
            continue;
        }
        nodesByPath.insert(node.instancePath, node);
        if (node.moduleType == sourceModule)
            leftChoices.append(node);
    }
    if (leftChoices.isEmpty()
        || nodesByPath.size() < 2) {
        return std::nullopt;
    }

    QDialog dialog(parent);
    dialog.setObjectName(
        QStringLiteral("instancePairSelectionDialog"));
    dialog.setWindowTitle(
        QStringLiteral("Connect Instance Pair"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* leftCombo = new QComboBox(&dialog);
    leftCombo->setObjectName(
        QStringLiteral("instancePairLeftSelection"));
    auto* rightCombo = new QComboBox(&dialog);
    rightCombo->setObjectName(
        QStringLiteral("instancePairRightSelection"));
    auto* connectionEdit = new QLineEdit(
        defaults.connectionName, &dialog);
    connectionEdit->setObjectName(
        QStringLiteral("instancePairConnectionName"));

    const auto nodeLabel =
        [](const DesignHierarchyNode& node) {
            return QStringLiteral("%1  (%2)")
                .arg(node.instancePath, node.moduleType);
        };
    for (const DesignHierarchyNode& node : leftChoices) {
        leftCombo->addItem(
            nodeLabel(node), node.instancePath);
    }
    const int preferredLeft =
        leftCombo->findData(defaults.leftInstancePath);
    if (preferredLeft >= 0)
        leftCombo->setCurrentIndex(preferredLeft);

    const auto rebuildRight =
        [rightCombo,
         leftCombo,
         nodesByPath,
         nodeLabel,
         preferred = defaults.rightInstancePath]() {
            const QString leftPath =
                leftCombo->currentData().toString();
            const DesignHierarchyNode left =
                nodesByPath.value(leftPath);
            const QSignalBlocker blocker(rightCombo);
            rightCombo->clear();
            for (const DesignHierarchyNode& node :
                 nodesByPath) {
                if (node.instancePath == leftPath
                    || node.rootId != left.rootId) {
                    continue;
                }
                rightCombo->addItem(
                    nodeLabel(node), node.instancePath);
            }
            const int preferredIndex =
                rightCombo->findData(preferred);
            if (preferredIndex >= 0)
                rightCombo->setCurrentIndex(
                    preferredIndex);
        };
    QObject::connect(
        leftCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        &dialog,
        [rebuildRight](int) { rebuildRight(); });
    rebuildRight();

    form->addRow(
        QStringLiteral("Source instance"),
        leftCombo);
    form->addRow(
        QStringLiteral("Destination instance"),
        rightCombo);
    form->addRow(
        QStringLiteral("Connection identifier"),
        connectionEdit);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok
            | QDialogButtonBox::Cancel,
        &dialog);
    buttons->setObjectName(
        QStringLiteral("instancePairSelectionButtons"));
    QObject::connect(
        buttons, &QDialogButtonBox::accepted,
        &dialog, &QDialog::accept);
    QObject::connect(
        buttons, &QDialogButtonBox::rejected,
        &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (rightCombo->count() == 0
        || dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    InstancePairUserSelection result;
    result.leftInstancePath =
        leftCombo->currentData().toString();
    result.rightInstancePath =
        rightCombo->currentData().toString();
    result.connectionName =
        connectionEdit->text().trimmed();
    if (result.leftInstancePath.isEmpty()
        || result.rightInstancePath.isEmpty()
        || result.leftInstancePath
               == result.rightInstancePath
        || result.connectionName.isEmpty()) {
        return std::nullopt;
    }
    return result;
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
            && statusBar()
            && !reason.isEmpty()) {
            statusBar()->showMessage(reason, 5000);
        }
        return result.succeeded;
    });
    setupEditorCentralArea();
    setupWorkspaceProgressIndicator();
    navigationManager = std::unique_ptr<NavigationManager>(new NavigationManager(this));  // NEW
    analysisScheduler = std::unique_ptr<AnalysisScheduler>(new AnalysisScheduler(this));
    analysisProgressCoordinator =
        std::unique_ptr<AnalysisProgressCoordinator>(new AnalysisProgressCoordinator(this, this));
    setupNotificationCenter();

    setupSemanticRuntime();
    setupNavigationPane();
    setupNavigationCommandCoordinator();
    setupSemanticDocks();
    setupInsightFocusView();
    setupSettingsCenter();
    setupFileCommandCoordinator();
    setupFoldBlockShelf();
    setupPanelLayoutController();
    setupWorkspaceMenu();
    setupViewMenu();
    setupToolsMenu();
    setupEditorModeChip();
    setupEditorActionContextChip();
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
        if (semanticDocks->instancePairConnectionDock())
            panelLayoutController->closePanel(
                InstancePairConnectionCoordinator::panelId());
        if (semanticDocks->multiSignalPropagationDock())
            panelLayoutController->closePanel(
                MultiSignalPropagationPanel::panelId());
        if (semanticDocks->rtlInsightsPanelCoordinator()
            && semanticDocks->rtlInsightsPanelCoordinator()->dock())
            panelLayoutController->closePanel(
                QStringLiteral("rtlInsights"));
        if (semanticDocks->signalKernelGraphPanelCoordinator()
            && semanticDocks->signalKernelGraphPanelCoordinator()->dock())
            panelLayoutController->closePanel(
                QStringLiteral("signalKernelGraph"));
        if (semanticDocks->wavePreviewPanelCoordinator()
            && semanticDocks->wavePreviewPanelCoordinator()->dock())
            panelLayoutController->closePanel(
                QStringLiteral("wavePreview"));
    }

    setWindowTitle(QStringLiteral("ZeroSlack v%1").arg(QLatin1String(APP_VERSION)));
    if (statusBar()) {
        QLabel* versionLabel = new QLabel(
            QStringLiteral("v%1").arg(QLatin1String(APP_VERSION)), this);
        versionLabel->setToolTip(
            QStringLiteral("ZeroSlack v%1\nBuilt at %2")
                .arg(QLatin1String(APP_VERSION), QLatin1String(APP_BUILD_TIME)));
        InsightVisualStyle::applyLabel(versionLabel);
        statusBar()->addPermanentWidget(versionLabel);
        statusBar()->showMessage(QStringLiteral("Ready"));
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
    const auto showNonBlockingStatus =
        [this](const NotificationItem& item) {
            if (statusBar())
                statusBar()->showMessage(item.message, 5000);
        };
    connect(notificationCenter.get(),
            &NotificationCenter::notificationAdded,
            this,
            showNonBlockingStatus);
    connect(notificationCenter.get(),
            &NotificationCenter::notificationUpdated,
            this,
            showNonBlockingStatus);

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

    if (packageToolsBar) {
        packageToolsBar->setStyleSheet(
            InsightVisualStyle::packageToolsBarStyleSheet(
                packageToolsBar->objectName()));
    }
    if (packageToolsPackageLabel) {
        packageToolsPackageLabel->setStyleSheet(
            InsightVisualStyle::labelStyleSheet(
                packageToolsPackageLabel->objectName()));
    }
    refreshEditorActionContextChip();
    if (tabManager && tabManager->getCurrentEditor()) {
        updateEditorModeChip(
            tabManager->getCurrentEditor()->editorModeSnapshot());
    } else {
        updateEditorModeChip(EditorModeSnapshot());
    }

    const QList<MyCodeEditor*> editorViews =
        findChildren<MyCodeEditor*>();
    for (MyCodeEditor* editor : editorViews) {
        if (!editor)
            continue;
        editor->refreshSemanticPresentation();
        editor->viewport()->update();
    }

    if (semanticDocks) {
        if (RtlInsightsPanelCoordinator* insights =
                semanticDocks->rtlInsightsPanelCoordinator()) {
            insights->refreshThemePresentation();
        }
        if (SignalKernelGraphPanelCoordinator* kernel =
                semanticDocks->signalKernelGraphPanelCoordinator()) {
            kernel->refreshThemePresentation();
        }
        if (WavePreviewPanelCoordinator* wave =
                semanticDocks->wavePreviewPanelCoordinator()) {
            if (wave->canvas())
                wave->canvas()->update();
        }
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

    setupPackageTools(layout, editorContainer);
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
        temporaryEditorDrawerController =
            std::make_unique<TemporaryEditorDrawerController>(
                tabManager.get(), editorSplitHost, this);
        temporaryEditorSearchProvider =
            std::make_unique<TemporaryEditorSearchProvider>();
        refreshTemporaryEditorFileCatalog();
        refreshTemporaryEditorSemanticCatalog();
        temporaryEditorDrawerController->setSearchProvider(
            [this](const QString& rawQuery)
                -> EditorSearchCandidates {
                return temporaryEditorSearchProvider
                    ? temporaryEditorSearchProvider->query(rawQuery)
                    : EditorSearchCandidates{};
            });
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
                &TabManager::workspaceSessionStateChanged,
                this,
                [this]() { scheduleWorkspaceSessionSave(); });
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
    setCentralWidget(centralContentStack);
    insightFocusController =
        std::make_unique<InsightFocusController>(
            centralContentStack,
            editorCentralPage,
            this);

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
                if (foldShelfModel)
                    foldShelfModel->setWorkspaceRoot(path);
                refreshSettingsCenterWorkspace(path);
                refreshWorkspaceScope();
                refreshWorkspaceMenuEntries();
                if (restoreWorkspaceSessionOnActivation)
                    restoreWorkspaceSession();
                else
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
                if (activePath.isEmpty())
                    refreshSettingsCenterWorkspace(QString());
                refreshWorkspaceScope();
    });
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
        new QLabel(externalConflictReviewBar);
    externalConflictReviewTitle->setObjectName(
        QStringLiteral("externalConflictReviewTitle"));
    externalConflictReviewTitle->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    heading->addWidget(externalConflictReviewTitle, 1);
    auto* closeButton =
        new QPushButton(
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
        new QPlainTextEdit(localGroup);
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
        new QPlainTextEdit(diskGroup);
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
        new QLabel(externalConflictReviewBar);
    externalConflictReviewStatus->setObjectName(
        QStringLiteral("externalConflictReviewStatus"));
    externalConflictReviewStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    actions->addWidget(externalConflictReviewStatus, 1);
    externalConflictKeepLocalButton =
        new QPushButton(
            QStringLiteral("Keep Local"),
            externalConflictReviewBar);
    externalConflictKeepLocalButton->setObjectName(
        QStringLiteral("externalConflictKeepLocalButton"));
    externalConflictSaveAsButton =
        new QPushButton(
            QStringLiteral("Save Local As..."),
            externalConflictReviewBar);
    externalConflictSaveAsButton->setObjectName(
        QStringLiteral("externalConflictSaveAsButton"));
    externalConflictReloadButton =
        new QPushButton(
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
    if (statusBar()) {
        statusBar()->showMessage(
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
    InsightVisualStyle::applyLabel(title, true);
    layout->addWidget(title);

    packageToolsPackageLabel = new QLabel(packageToolsBar);
    packageToolsPackageLabel->setObjectName(
        QStringLiteral("packageToolsPackageLabel"));
    InsightVisualStyle::applyLabel(packageToolsPackageLabel);
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
    updatePackageToolsForEditor(editor, availability);
}

void MainWindow::updatePackageToolsForEditor(
    MyCodeEditor* editor,
    const EditorPackageToolAvailability& availability)
{
    if (!packageToolsBar)
        return;
    if (packageToolsStateValid && packageToolsStateEditor == editor
        && packageToolsState.available == availability.available
        && packageToolsState.packageName == availability.packageName
        && packageToolsState.failureMessage == availability.failureMessage) {
        return;
    }
    packageToolsStateEditor = editor;
    packageToolsState = availability;
    packageToolsStateValid = true;
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
    if (navigationManager
        && temporaryEditorDrawerController) {
        navigationManager->setTemporaryEditorOpenHandler(
            [this](const EditorLocation& location) {
                return temporaryEditorDrawerController
                    && temporaryEditorDrawerController
                           ->openLocation(location);
            });
        connect(
            navigationManager.get(),
            &NavigationManager::temporaryEditorOpenFinished,
            this,
            [this](const EditorLocation&,
                   bool succeeded,
                   const QString& failureReason) {
                if (!succeeded
                    && statusBar()
                    && !failureReason.isEmpty()) {
                    statusBar()->showMessage(
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
    analysisCoordinator->setStatusMessageHandler(
        [this](const QString& message, int timeoutMs) {
            if (statusBar())
                statusBar()->showMessage(message, timeoutMs);
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
                refreshEditorActionContextChip();
            });
    connect(workspaceManager.get(),
            &WorkspaceManager::workspaceClosed,
            this,
            [this]() {
                if (editorActionContextService)
                    editorActionContextService->clearWorkspaceContext();
                refreshEditorActionContextChip();
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
                refreshEditorActionContextChip();
            });
    connect(tabManager.get(),
            &TabManager::activeDocumentChanged,
            this,
            [this](const DocumentSnapshot& snapshot) {
                scheduleActiveEditorPassiveRefresh();
                updatePackageTools();
                refreshEditorActionContextChip();
                if (analysisScheduler && !snapshot.fileName.isEmpty()) {
                    refreshDiagnosticsAnalysisState();
                }
                if (insightPanelVisibleOrFocused(
                        QStringLiteral("wavePreview"))) {
                    refreshActiveEditorWavePreview();
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
                                refreshEditorActionContextChip();
                            }
                        });
                connect(editor,
                        &MyCodeEditor::hierarchyInstanceContextChanged,
                        this,
                        [this, editor](const HierarchyInstanceContext&) {
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor) {
                                refreshEditorActionContextChip();
                            }
                        });
                connect(editor,
                        &MyCodeEditor::packageToolAvailabilityChanged,
                        this,
                        [this, editor](
                            const EditorPackageToolAvailability& availability) {
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor) {
                                updatePackageToolsForEditor(editor,
                                                            availability);
                            }
                        });
                connect(editor,
                        &MyCodeEditor::wavePreviewScopeChanged,
                        this,
                        [this, editor]() {
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor
                                && insightPanelVisibleOrFocused(
                                    QStringLiteral("wavePreview"))) {
                                refreshActiveEditorWavePreview();
                            }
                        });
                connect(editor,
                        &MyCodeEditor::documentChangeApplied,
                        this,
                        [this, editor](const DocumentChange& change) {
                            applyActiveEditorWavePreviewChange(editor, change);
                            if (tabManager
                                && tabManager->getCurrentEditor() == editor) {
                                refreshEditorActionContextChip();
                            }
                        });
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::fileSymbolAnalysisFinished,
            this,
            [this](const QString& fileName, int) {
                if (!analysisScheduler
                    || !analysisScheduler->isSemanticAnalysisActive()) {
                    refreshTemporaryEditorSemanticCatalog();
                }
                scheduleActiveEditorPassiveRefresh(fileName);
                refreshEditorActionContextChip();
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisStarted,
            this,
            [this](const ProjectSnapshot&, int) {
                refreshDiagnosticsAnalysisState();
                refreshEditorActionContextChip();
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisFinished,
            this,
            [this](const ProjectSnapshot&, int, int) {
                refreshTemporaryEditorSemanticCatalog();
                refreshDiagnosticsAnalysisState();
                scheduleActiveEditorPassiveRefresh();
                refreshEditorActionContextChip();
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisDeferred,
            this,
            [this](const ProjectSnapshot&, int, qint64, qint64) {
                refreshDiagnosticsAnalysisState();
                refreshEditorActionContextChip();
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceAnalysisRequestResolved,
            this,
            [this](const WorkspaceAnalysisRequestTelemetry&) {
                refreshDiagnosticsAnalysisState();
                refreshEditorActionContextChip();
            });
    connect(analysisScheduler.get(),
            &AnalysisScheduler::workspaceSymbolAnalysisCancelled,
            this,
            [this](const WorkspaceAnalysisRequestTelemetry&) {
                refreshDiagnosticsAnalysisState();
                refreshEditorActionContextChip();
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
                refreshEditorActionContextChip();
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

    const DocumentSnapshot document = tabManager->getCurrentDocumentMetadata();
    const EditorAlwaysScopeTarget alwaysScope =
        editor->currentAlwaysScopeTarget();
    if (alwaysScope.ok()) {
        semanticDocks->wavePreviewPanelCoordinator()->refreshFromDocument(
            document.fileName,
            editor->cachedDocumentText(),
            document.dirty,
            alwaysScope.startPosition,
            alwaysScope.endPosition,
            alwaysScope.label,
            alwaysScope.startLine);
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
        editor->cachedDocumentText(),
        document.dirty,
        moduleScope.startPosition,
        moduleScope.endPosition,
        moduleScope.label,
        moduleScope.startLine);
}

void MainWindow::applyActiveEditorWavePreviewChange(
    MyCodeEditor* editor,
    const DocumentChange& change)
{
    if (!editor || !tabManager || !semanticDocks
        || tabManager->getCurrentEditor() != editor
        || !semanticDocks->wavePreviewPanelCoordinator()) {
        return;
    }

    if (!insightPanelVisibleOrFocused(
            QStringLiteral("wavePreview"))) {
        return;
    }

    const DocumentSnapshot document = tabManager->getCurrentDocumentMetadata();
    const int latestDocumentLength = editor->cachedDocumentLength();
    const auto latestDocumentSlice =
        [editor](int position, int length) {
            return editor->cachedDocumentSlice(position, length);
        };
    const EditorAlwaysScopeTarget alwaysScope =
        editor->currentAlwaysScopeTarget();
    if (alwaysScope.ok()) {
        semanticDocks->wavePreviewPanelCoordinator()->applyDocumentChange(
            document.fileName,
            change,
            latestDocumentLength,
            latestDocumentSlice,
            document.dirty,
            alwaysScope.startPosition,
            alwaysScope.endPosition,
            alwaysScope.label,
            alwaysScope.startLine);
        return;
    }

    const EditorModuleScopeTarget moduleScope =
        editor->currentModuleScopeTarget();
    if (moduleScope.ok()) {
        semanticDocks->wavePreviewPanelCoordinator()->applyDocumentChange(
            document.fileName,
            change,
            latestDocumentLength,
            latestDocumentSlice,
            document.dirty,
            moduleScope.startPosition,
            moduleScope.endPosition,
            moduleScope.label,
            moduleScope.startLine);
        return;
    }

    semanticDocks->wavePreviewPanelCoordinator()->renderUnavailable(
        moduleScope.failureMessage.isEmpty()
            ? QStringLiteral("Place the cursor in a module or always block to preview.")
            : moduleScope.failureMessage);
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
            if (statusBar())
                statusBar()->showMessage(message, timeoutMs);
        });
    semanticDocks->setup();
    if (RtlInsightsPanelCoordinator* rtlInsights =
            semanticDocks->rtlInsightsPanelCoordinator()) {
        rtlInsights->setRegisteredActionRequestHandler(
            [this](const QString& actionId,
                   const QVariantMap& parameters) {
                return executeRegisteredUiAction(
                    actionId, parameters);
            });
    }
    if (RtlHighRiskEditPanelCoordinator* rtlEdit =
            semanticDocks
                ->rtlHighRiskEditPanelCoordinator()) {
        connect(
            rtlEdit,
            &RtlHighRiskEditPanelCoordinator::
                acceptedParameters,
            this,
            [this](const QString& workflowActionId,
                   const QVariantMap& parameters,
                   bool dryRun) {
                const QString actionId =
                    workflowActionId.startsWith(
                        QStringLiteral("rtl.rename"))
                    ? RtlRenameWorkflow::
                          actionFamilyId()
                    : workflowActionId;
                const ActionDescriptor* descriptor =
                    findActionById(actionId);
                if (!descriptor)
                    return;
                ActionInvocation invocation;
                invocation.workspaceId =
                    workspaceManager
                    ? workspaceManager
                          ->getWorkspacePath()
                    : QString();
                invocation.parameters = parameters;
                invocation.mode =
                    dryRun
                    ? ActionExecutionMode::DryRun
                    : ActionExecutionMode::Execute;
                ActionExecutionResult result;
                result.handled = true;
                result.succeeded = true;
                result.dryRun = dryRun;
                result.hasResolvedParameters = true;
                result.resolvedParameters =
                    parameters;
                applicationActionExecutionHistory()
                    .recordSuccessful(
                        *descriptor,
                        invocation,
                        result);
            });
        connect(
            rtlEdit,
            &RtlHighRiskEditPanelCoordinator::
                stateChanged,
            this,
            [this](
                const RtlHighRiskEditPanelOutcome&
                    outcome) {
                if (!notificationCenter)
                    return;
                const QString actionId =
                    outcome.actionId.startsWith(
                        QStringLiteral("rtl.rename"))
                    ? RtlRenameWorkflow::
                          actionFamilyId()
                    : outcome.actionId;
                const QString workspaceId =
                    workspaceManager
                    ? workspaceManager
                          ->getWorkspacePath()
                    : QString();
                const QString key =
                    QStringLiteral(
                        "rtl-edit:%1:%2")
                        .arg(
                            actionId.isEmpty()
                                ? QStringLiteral(
                                      "unknown")
                                : actionId,
                            workspaceId);
                if (!rtlTransactionFailureNeedsNotification(
                        outcome)) {
                    if (outcome.panelState
                            == RtlHighRiskEditPanelState::
                                Applied
                        || outcome.panelState
                            == RtlHighRiskEditPanelState::
                                Undone
                        || outcome.panelState
                            == RtlHighRiskEditPanelState::
                                Cancelled) {
                        notificationCenter
                            ->dismissByKey(key);
                    }
                    return;
                }
                NotificationDraft draft;
                draft.key = key;
                draft.topic =
                    NotificationTopic::
                        TransactionConflict;
                draft.severity =
                    outcome.panelState
                        == RtlHighRiskEditPanelState::
                            Conflict
                    ? NotificationSeverity::Critical
                    : NotificationSeverity::Error;
                draft.source =
                    QStringLiteral(
                        "RtlHighRiskEdit");
                draft.message =
                    outcome.message.isEmpty()
                    ? QStringLiteral(
                          "The RTL edit transaction "
                          "failed.")
                    : outcome.message;
                notificationCenter->post(draft);
            });
    }
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

void MainWindow::setupInsightFocusView()
{
    if (!insightFocusController || !semanticDocks)
        return;

    if (RtlInsightsPanelCoordinator* panel =
            semanticDocks->rtlInsightsPanelCoordinator()) {
        InsightFocusPanelRegistration registration;
        registration.id = QStringLiteral("rtlInsights");
        registration.title = QStringLiteral("RTL Insights");
        registration.dock = panel->dock();
        registration.fit = [panel]() { panel->focusFit(); };
        registration.zoomIn =
            [panel]() { panel->focusZoomIn(); };
        registration.zoomOut =
            [panel]() { panel->focusZoomOut(); };
        registration.setSearchText =
            [panel](const QString& text) {
                panel->setFocusSearchText(text);
            };
        registration.searchText =
            [panel]() { return panel->focusSearchText(); };
        registration.showInspector =
            [panel]() { panel->focusInspector(); };
        insightFocusController->registerPanel(registration);
    }

    if (SignalKernelGraphPanelCoordinator* panel =
            semanticDocks
                ->signalKernelGraphPanelCoordinator()) {
        InsightFocusPanelRegistration registration;
        registration.id =
            QStringLiteral("signalKernelGraph");
        registration.title =
            QStringLiteral("Signal Kernel Graph");
        registration.dock = panel->dock();
        registration.fit = [panel]() { panel->focusFit(); };
        registration.zoomIn =
            [panel]() { panel->focusZoomIn(); };
        registration.zoomOut =
            [panel]() { panel->focusZoomOut(); };
        registration.setSearchText =
            [panel](const QString& text) {
                panel->setFocusSearchText(text);
            };
        registration.searchText =
            [panel]() { return panel->focusSearchText(); };
        registration.showInspector =
            [panel]() { panel->focusInspector(); };
        insightFocusController->registerPanel(registration);
    }

    if (WavePreviewPanelCoordinator* panel =
            semanticDocks->wavePreviewPanelCoordinator()) {
        InsightFocusPanelRegistration registration;
        registration.id = QStringLiteral("wavePreview");
        registration.title = QStringLiteral("Wave Preview");
        registration.dock = panel->dock();
        registration.fit = [panel]() { panel->focusFit(); };
        registration.zoomIn =
            [panel]() { panel->focusZoomIn(); };
        registration.zoomOut =
            [panel]() { panel->focusZoomOut(); };
        registration.setSearchText =
            [panel](const QString& text) {
                panel->setFocusSearchText(text);
            };
        registration.searchText =
            [panel]() { return panel->focusSearchText(); };
        registration.showInspector =
            [panel]() { panel->focusInspector(); };
        insightFocusController->registerPanel(registration);
    }
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
                if (!result.succeeded
                    && statusBar()) {
                    statusBar()->showMessage(
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
            if (!result.succeeded && statusBar()) {
                statusBar()->showMessage(
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
    globalControlCoordinator->setActionHandler(
        [this](const GlobalControlItem& item) {
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
                saveWorkspaceSession(true);
            } else if (executionRoute
                       == QStringLiteral(
                           "globalControl.workspaceSession.restore")) {
                restoreWorkspaceSession();
            } else if (executionRoute
                       == QStringLiteral(
                           "globalControl.workspaceSession.clean")) {
                cleanWorkspaceSession();
            } else if (item.id == QStringLiteral("ow s")) {
                if (statusBar())
                    statusBar()->showMessage(
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
            } else if (executionRoute
                       == QStringLiteral(
                           "globalControl.foldRegion")) {
                if (MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr)
                    editor->startFoldRegionMarkMode();
            } else if (executionRoute
                       == QStringLiteral(
                           "globalControl.foldShelf")) {
                showFoldBlockShelf();
                if (MyCodeEditor* editor = tabManager ? tabManager->getCurrentEditor() : nullptr)
                    editor->startFoldShelfMode();
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
    foldShelfPanel->setActionRequestHandler(
        [this](const QString& actionId,
               QString* failureReason) {
        const ActionDescriptor* descriptor =
            findActionById(actionId);
        if (!descriptor) {
            if (failureReason) {
                *failureReason = QStringLiteral(
                    "Fold Shelf Action is unavailable");
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
            && statusBar()
            && !reason.isEmpty()) {
            statusBar()->showMessage(reason, 5000);
        }
        return result.succeeded;
    });
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

void MainWindow::setupPanelLayoutController()
{
    panelLayoutController =
        std::make_unique<PanelLayoutController>(this, this);
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
                    && statusBar()
                    && !reason.isEmpty()) {
                    statusBar()->showMessage(
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
        QStringLiteral("activity"),
        semanticDocks && semanticDocks->activityLogPanelCoordinator()
            ? semanticDocks->activityLogPanelCoordinator()->dock()
            : nullptr);
    registerPanel(
        ScopedSearchPanelCoordinator::panelId(),
        semanticDocks
                && semanticDocks->scopedSearchPanelCoordinator()
            ? semanticDocks->scopedSearchPanelCoordinator()->dock()
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
        InstancePairConnectionCoordinator::panelId(),
        semanticDocks
            ? semanticDocks->instancePairConnectionDock()
            : nullptr);
    registerPanel(
        MultiSignalPropagationPanel::panelId(),
        semanticDocks
            ? semanticDocks->multiSignalPropagationDock()
            : nullptr);
    registerPanel(
        QStringLiteral("rtlInsights"),
        semanticDocks && semanticDocks->rtlInsightsPanelCoordinator()
            ? semanticDocks->rtlInsightsPanelCoordinator()->dock()
            : nullptr);
    registerPanel(
        QStringLiteral("signalKernelGraph"),
        semanticDocks
                && semanticDocks->signalKernelGraphPanelCoordinator()
            ? semanticDocks->signalKernelGraphPanelCoordinator()->dock()
            : nullptr);
    registerPanel(
        QStringLiteral("wavePreview"),
        semanticDocks && semanticDocks->wavePreviewPanelCoordinator()
            ? semanticDocks->wavePreviewPanelCoordinator()->dock()
            : nullptr);
    registerPanel(QStringLiteral("foldShelf"), foldShelfDock);
    panelLayoutController->setStateChangedHandler(
        [this]() { scheduleWorkspaceSessionSave(); });
    panelLayoutController->finalize();

    if (insightFocusController) {
        insightFocusController->setBeforeEnterHandler(
            [this]() {
                if (!panelLayoutController)
                    return;
                if (panelLayoutController->isFocusModeActive())
                    panelLayoutController->setFocusModeActive(false);
            });
    }
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
    QAction* scopedSearchAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewScopedSearch);
    QAction* focusModeAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewFocusMode);
    if (focusModeAction)
        focusModeAction->setCheckable(true);

    QMenu* editorLayoutMenu =
        viewMenu->addMenu(tr("Editor Layout"));
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
        editorLayoutMenu->addMenu(
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
    QAction* rtlInsightsAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewRtlInsights);
    QAction* signalKernelAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewSignalKernelGraph);
    QAction* wavePreviewAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewWavePreview);
    for (QAction* action :
         {problemsAction,
          activityAction,
          rtlInsightsAction,
          signalKernelAction,
          wavePreviewAction}) {
        if (action)
            action->setCheckable(true);
    }
    QAction* collapseBottomAction =
        addRegistryAction(
            viewMenu,
            ActionIds::ViewBottomPanelCollapsed);
    if (collapseBottomAction)
        collapseBottomAction->setCheckable(true);
    QAction* pinBottomAction =
        addRegistryAction(
            viewMenu,
            ActionIds::ViewBottomPanelPinned);
    QAction* closeBottomAction =
        addRegistryAction(
            viewMenu,
            ActionIds::ViewBottomPanelClose);

    QAction* focusRtlInsightsAction = nullptr;
    QAction* focusSignalKernelAction = nullptr;
    QAction* focusWavePreviewAction = nullptr;
    QAction* leaveInsightFocusAction = nullptr;
    if (insightFocusController) {
        QMenu* focusMenu =
            viewMenu->addMenu(tr("Focus View"));
        focusMenu->setObjectName(
            QStringLiteral("insightFocusMenu"));
        focusRtlInsightsAction =
            addRegistryAction(
                focusMenu,
                ActionIds::ViewFocusRtlInsights);
        focusSignalKernelAction =
            addRegistryAction(
                focusMenu,
                ActionIds::ViewFocusSignalKernelGraph);
        focusWavePreviewAction =
            addRegistryAction(
                focusMenu,
                ActionIds::ViewFocusWavePreview);
        focusMenu->addSeparator();
        leaveInsightFocusAction =
            addRegistryAction(
                focusMenu,
                ActionIds::ViewLeaveInsightFocus);
    }
    QAction* foldShelfAction =
        addRegistryAction(
            viewMenu, ActionIds::ViewFoldShelf);
    if (foldShelfAction)
        foldShelfAction->setCheckable(true);
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
            action->setChecked(
                (dock && dock->isVisible())
                || (insightFocusController
                    && insightFocusController->isFocused()
                    && insightFocusController
                           ->focusedPanelId()
                           == panelId));
        };
    connect(
        viewMenu,
        &QMenu::aboutToShow,
        this,
        [this,
         navigationAction,
         scopedSearchAction,
         focusModeAction,
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
         rtlInsightsAction,
         signalKernelAction,
         wavePreviewAction,
         collapseBottomAction,
         pinBottomAction,
         closeBottomAction,
         focusRtlInsightsAction,
         focusSignalKernelAction,
         focusWavePreviewAction,
         leaveInsightFocusAction,
         foldShelfAction,
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
                  scopedSearchAction,
                  focusModeAction,
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
                  rtlInsightsAction,
                  signalKernelAction,
                  wavePreviewAction,
                  collapseBottomAction,
                  pinBottomAction,
                  closeBottomAction,
                  focusRtlInsightsAction,
                  focusSignalKernelAction,
                  focusWavePreviewAction,
                  leaveInsightFocusAction,
                  foldShelfAction,
                  settingsAction,
                  resetLayoutAction}) {
                refreshAvailability(action);
            }

            const QString activeId =
                panelLayoutController
                ? panelLayoutController
                      ->activeBottomPanelId()
                : QString();
            if (focusModeAction) {
                focusModeAction->setChecked(
                    panelLayoutController
                    && panelLayoutController
                           ->isFocusModeActive());
            }
            if (collapseBottomAction) {
                collapseBottomAction->setChecked(
                    panelLayoutController
                    && panelLayoutController
                           ->isBottomCollapsed());
            }
            const bool hasActive =
                !activeId.isEmpty();
            if (pinBottomAction) {
                pinBottomAction->setEnabled(
                    hasActive
                    && panelLayoutController);
                pinBottomAction->setText(
                    panelLayoutController
                            && panelLayoutController
                                   ->isPanelPinned(activeId)
                    ? tr("Unpin Active Bottom Page")
                    : tr("Pin Active Bottom Page"));
            }
            if (closeBottomAction) {
                closeBottomAction->setEnabled(
                    hasActive
                    && panelLayoutController
                    && !panelLayoutController
                            ->isPanelPinned(activeId));
            }
            if (leaveInsightFocusAction) {
                leaveInsightFocusAction->setEnabled(
                    insightFocusController
                    && insightFocusController
                           ->isFocused());
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
            setPanelChecked(
                rtlInsightsAction,
                QStringLiteral("rtlInsights"));
            setPanelChecked(
                signalKernelAction,
                QStringLiteral("signalKernelGraph"));
            setPanelChecked(
                wavePreviewAction,
                QStringLiteral("wavePreview"));
            setPanelChecked(
                foldShelfAction,
                QStringLiteral("foldShelf"));
        });

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

    openWorkspacesMenu = workspaceMenu->addMenu(tr("Open Workspaces"));
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
        || index < 0
        || index >= workspaceManager->workspaceEntries().size()
        || index == workspaceManager->activeWorkspaceIndex()) {
        return;
    }

    saveWorkspaceSession(false);
    if (workspaceManager->switchWorkspace(index)) {
        refreshSettingsCenterWorkspace(
            workspaceManager->getWorkspacePath());
    }
}

void MainWindow::closeActiveWorkspace()
{
    if (!workspaceManager || !tabManager)
        return;

    const int index = workspaceManager->activeWorkspaceIndex();
    const QList<WorkspaceManager::WorkspaceEntry> entries =
        workspaceManager->workspaceEntries();
    if (index < 0 || index >= entries.size())
        return;

    saveWorkspaceSession(false);
    if (!tabManager->closeTabsInWorkspace(entries.at(index).path))
        return;
    if (workspaceManager->closeWorkspace(index)) {
        refreshSettingsCenterWorkspace(
            workspaceManager->getWorkspacePath());
    }
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
        toolsMenu->addMenu(
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
         rtlRenameAction,
         rtlConnectionTransformAction,
         connectInstancePairAction,
         propagateMultipleSignalsAction,
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
            MyCodeEditor* editor =
                tabManager
                ? tabManager->getCurrentEditor()
                : nullptr;
            EditorSemanticContext semanticContext;
            EditorActionContext actionContext;
            bool cursorSignalAvailable = false;
            bool renameSubjectAvailable = false;
            bool instanceSubjectAvailable = false;
            int selectedSignalCount = 0;
            if (editor) {
                semanticContext =
                    editor
                        ->editorSemanticContextForPosition(
                            -1, true);
                actionContext =
                    resolveEditorActionContext(
                        semanticContext);
                TSDocument syntax;
                syntax.setText(
                    semanticContext.documentText);
                const TSIdentifierTarget identifier =
                    syntax.identifierAt(
                        semanticContext.cursorPosition);
                cursorSignalAvailable =
                    identifier.ok();
                if (identifier.ok()) {
                    const EditorSemanticContext
                        identifierContext =
                            editor
                                ->editorSemanticContextForPosition(
                                    identifier.startChar,
                                    true);
                    const DefinitionResult definition =
                        resolveRtlRenameSubject(
                            identifierContext,
                            identifier);
                    renameSubjectAvailable =
                        definition.found
                        && isSupportedRtlRenameSubject(
                            definition
                                .symbolRecord);
                    QString ignoredFailure;
                    instanceSubjectAvailable =
                        resolveRtlInstanceSubject(
                            SemanticIndex::
                                getInstance()
                                    ->snapshotToken(),
                            identifierContext,
                            identifier,
                            &ignoredFailure)
                            .has_value();
                }
                selectedSignalCount =
                    editor->selectedSignalNames().size();
            }
            ActionAvailabilityContext availability;
            availability.editorAvailable =
                editor != nullptr;
            availability.workspaceAvailable =
                workspaceAvailable;
            availability.semanticCurrent =
                actionContext.semanticState
                == EditorActionSemanticState::Current;
            availability.hierarchyBound =
                actionContext.hierarchyBound();
            availability.symbolAvailable =
                cursorSignalAvailable
                || selectedSignalCount >= 2;
            const auto refreshRtlAction =
                [&availability](
                    QAction* action,
                    bool featureReady,
                    const QString& featureReason) {
                    if (!action)
                        return;
                    const ActionDescriptor* descriptor =
                        findActionById(
                            action->property(
                                "actionId").toString());
                    const ActionAvailabilityState state =
                        descriptor
                        ? evaluateActionAvailability(
                              *descriptor,
                              availability)
                        : ActionAvailabilityState{};
                    action->setEnabled(
                        state.executable
                        && featureReady);
                    if (!featureReady)
                        action->setToolTip(featureReason);
                    else if (!state.executable)
                        action->setToolTip(state.reason);
                    else if (descriptor)
                        action->setToolTip(
                            descriptor->description);
                };
            refreshRtlAction(
                rtlRenameAction,
                renameSubjectAvailable,
                QStringLiteral(
                    "Place the cursor on a module or "
                    "interface port, parameter, or "
                    "localparam."));
            refreshRtlAction(
                rtlConnectionTransformAction,
                instanceSubjectAvailable,
                QStringLiteral(
                    "Place the cursor on one exact "
                    "module instance declaration."));
            refreshRtlAction(
                connectInstancePairAction,
                cursorSignalAvailable,
                QStringLiteral(
                    "Place the cursor on the source signal."));
            refreshRtlAction(
                propagateMultipleSignalsAction,
                selectedSignalCount >= 2,
                QStringLiteral(
                    "Use Signal Selection to select at least two signals."));
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
            if (!result.succeeded
                && statusBar()) {
                statusBar()->showMessage(
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
    if (!statusMessage.isEmpty() && statusBar()) {
        statusBar()->showMessage(
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

            const bool focused =
                insightFocusController
                && insightFocusController->isFocused()
                && insightFocusController
                       ->focusedPanelId()
                       == panelId;
            if (focused)
                return true;

            const bool visible = dock->isVisible();
            if (visible) {
                if (panelLayoutController
                    && panelLayoutController
                           ->isBottomPanel(dock)) {
                    return panelLayoutController
                        ->closePanel(panelId);
                }
                dock->hide();
                return true;
            }

            showPanelById(panelId);
            return dock->isVisible()
                || (insightFocusController
                    && insightFocusController
                           ->isFocused()
                    && insightFocusController
                           ->focusedPanelId()
                           == panelId);
        };

    if (route
        == QStringLiteral(
            "rtledit.instancePair.connect")) {
        return executeInstancePairConnectionAction(
            invocation);
    }
    if (route
        == QStringLiteral(
            "rtledit.signal.propagateBatch")) {
        return executeMultiSignalPropagationAction(
            invocation);
    }
    if (route
        == QStringLiteral("rtledit.rename")) {
        return executeRtlRenameAction(invocation);
    }
    if (route
        == QStringLiteral(
            "rtledit.connection.transform")) {
        return executeRtlConnectionTransformAction(
            invocation);
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
        MyCodeEditor* editor = tabManager
            ? tabManager->getCurrentEditor()
            : nullptr;
        if (!editor) {
            return fail(QStringLiteral(
                "No editor tab is available."));
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

    if (route == QStringLiteral(
                     "editor.navigation.nextAssignment")
        || route == QStringLiteral(
                        "editor.navigation.previousAssignment")
        || route == QStringLiteral(
                        "editor.navigation.nextConditionalBranch")
        || route == QStringLiteral(
                        "editor.navigation.previousConditionalBranch")) {
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
                         "editor.navigation.nextAssignment")) {
            executed =
                editor->goToNextAssignmentForSelectedSignal(
                    &message);
        } else if (route == QStringLiteral(
                                "editor.navigation.previousAssignment")) {
            executed =
                editor->goToPreviousAssignmentForSelectedSignal(
                    &message);
        } else if (route == QStringLiteral(
                                "editor.navigation.nextConditionalBranch")) {
            executed = editor->goToNextConditionalBranch(
                &message);
        } else {
            executed = editor->goToPreviousConditionalBranch(
                &message);
        }
        if (!executed)
            return fail(message);
        result.message = message;
        return succeeded();
    }

    if (route == QStringLiteral("editor.lines.delete")
        || route == QStringLiteral("editor.lines.join")
        || route == QStringLiteral("editor.lines.moveUp")
        || route == QStringLiteral("editor.lines.moveDown")) {
        MyCodeEditor* editor = tabManager
            ? tabManager->getCurrentEditor()
            : nullptr;
        if (!editor) {
            return fail(QStringLiteral(
                "No editor tab is available."));
        }
        if (editor->isReadOnly()) {
            return fail(QStringLiteral(
                "The editor is read-only."));
        }
        QString message;
        bool executed = false;
        if (route == QStringLiteral(
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
        || route == QStringLiteral(
                        "editor.structure.createSignalDefinition")
        || route == QStringLiteral(
                        "editor.structure.editInstanceSlots")
        || route == QStringLiteral(
                        "editor.structure.createAssignmentQueue")
        || route.startsWith(
            QStringLiteral("editor.format."))) {
        MyCodeEditor* editor = tabManager
            ? tabManager->getCurrentEditor()
            : nullptr;
        if (!editor)
            return fail(QStringLiteral(
                "No editor tab is available."));

        const bool editsDocument =
            route == QStringLiteral(
                         "editor.edit.replace")
            || route == QStringLiteral(
                            "editor.format.commentLines")
            || route == QStringLiteral(
                            "editor.format.uncommentLines")
            || route == QStringLiteral(
                            "editor.format.indentLines")
            || route == QStringLiteral(
                            "editor.format.unindentLines")
            || route == QStringLiteral(
                            "editor.format.selection")
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
                                "editor.format.profile.structured")) {
            editor->setFormatterProfile(
                FormatterProfile::Structured);
        } else if (route == QStringLiteral(
                                "editor.format.profile.indentOnly")) {
            editor->setFormatterProfile(
                FormatterProfile::IndentOnly);
        } else if (route == QStringLiteral(
                                "editor.format.onSave")) {
            editor->setFormatOnSaveEnabled(
                !editor->formatOnSaveEnabled());
        } else if (route == QStringLiteral(
                                "editor.format.selection")) {
            if (!editor->textCursor().hasSelection()) {
                return fail(QStringLiteral(
                    "Select text to format."));
            }
            editor->formatSelection();
        } else if (route == QStringLiteral(
                                "editor.format.document")) {
            editor->formatDocument();
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

    if (route == QStringLiteral(
                     "ui.globalControl.show")) {
        if (!globalControlCoordinator)
            return fail();
        globalControlCoordinator->open();
        return succeeded();
    }

    if (route
        == QStringLiteral(
            "ui.panelLayout.focusMode.toggle")) {
        if (!panelLayoutController)
            return fail();
        const bool enter =
            !panelLayoutController
                 ->isFocusModeActive();
        if (enter
            && insightFocusController
            && insightFocusController->isFocused()) {
            insightFocusController->leaveToEditor();
        }
        panelLayoutController
            ->setFocusModeActive(enter);
        return succeeded();
    }

    if (route
        == QStringLiteral("ui.temporaryEditor.open")) {
        if (!tabManager
            || !temporaryEditorDrawerController) {
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
        if (!temporaryEditorDrawerController
                 ->openLocation(location)) {
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

    if (route
        == QStringLiteral(
            "ui.foldShelf.deleteSelected")) {
        if (!foldShelfPanel)
            return fail();
        QString failureReason;
        if (!foldShelfPanel->deleteSelectedItem(
                &failureReason)) {
            return fail(failureReason);
        }
        return succeeded();
    }

    const QHash<QString, QString> panelIdsByRoute = {
        {QStringLiteral("ui.panel.problems.toggle"),
         QStringLiteral("problems")},
        {QStringLiteral("ui.panel.activity.toggle"),
         QStringLiteral("activity")},
        {QStringLiteral("ui.panel.rtlInsights.toggle"),
         QStringLiteral("rtlInsights")},
        {QStringLiteral(
             "ui.panel.signalKernelGraph.toggle"),
         QStringLiteral("signalKernelGraph")},
        {QStringLiteral("ui.panel.wavePreview.toggle"),
         QStringLiteral("wavePreview")},
        {QStringLiteral("ui.panel.foldShelf.toggle"),
         QStringLiteral("foldShelf")},
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

    const QHash<QString, QString> focusIdsByRoute = {
        {QStringLiteral(
             "ui.insightFocus.rtlInsights.enter"),
         QStringLiteral("rtlInsights")},
        {QStringLiteral(
             "ui.insightFocus.signalKernelGraph.enter"),
         QStringLiteral("signalKernelGraph")},
        {QStringLiteral(
             "ui.insightFocus.wavePreview.enter"),
         QStringLiteral("wavePreview")},
    };
    const auto focusRoute =
        focusIdsByRoute.constFind(route);
    if (focusRoute != focusIdsByRoute.constEnd()) {
        if (!insightFocusController)
            return fail();
        if (focusRoute.value()
            == QStringLiteral("wavePreview")) {
            refreshActiveEditorWavePreview();
        }
        if (!insightFocusController
                 ->enter(focusRoute.value())) {
            return fail();
        }
        return succeeded();
    }
    if (route
        == QStringLiteral(
            "ui.insightFocus.leave")) {
        if (!insightFocusController
            || !insightFocusController->isFocused()) {
            return fail();
        }
        insightFocusController->leaveToEditor();
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
        settingsCenterDock->show();
        settingsCenterDock->raise();
        result.succeeded = true;
        return result;
    }

    if (route
        == QStringLiteral(
            "ui.crashRecovery.review")) {
        const QString workspaceRoot =
            workspaceManager
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

ActionExecutionResult
MainWindow::executeRtlRenameAction(
    const ActionInvocation& invocation)
{
    ActionExecutionResult result;
    result.handled = true;
    result.dryRun =
        invocation.mode
        == ActionExecutionMode::DryRun;
    const auto fail =
        [&result](const QString& reason) {
            result.failureReason = reason;
            return result;
        };
    if (!tabManager || !workspaceManager
        || !workspaceManager->isWorkspaceOpen()
        || !semanticDocks
        || !semanticDocks
                ->rtlHighRiskEditPanelCoordinator()
        || !semanticDocks
                ->rtlActionDocumentManager()) {
        return fail(QStringLiteral(
            "The unified RTL High+Diff workspace "
            "workflow is unavailable."));
    }

    MyCodeEditor* editor =
        tabManager->getCurrentEditor();
    if (!editor) {
        return fail(QStringLiteral(
            "Open a SystemVerilog editor before "
            "renaming an RTL declaration."));
    }
    EditorSemanticContext context =
        editor->editorSemanticContextForPosition(
            -1, true);
    const EditorActionContext actionContext =
        resolveEditorActionContext(context);
    if (actionContext.semanticState
        != EditorActionSemanticState::Current) {
        return fail(
            actionContext.semanticError.isEmpty()
            ? QStringLiteral(
                  "A current Slang semantic snapshot "
                  "is required for RTL rename.")
            : actionContext.semanticError);
    }

    const SemanticSnapshotToken semanticToken =
        SemanticIndex::getInstance()
            ->snapshotToken();
    if (!semanticToken.isValid()
        || semanticToken.revision == 0
        || (actionContext
                    .semanticSnapshotRevision
                != 0
            && actionContext
                    .semanticSnapshotRevision
                != semanticToken.revision)) {
        return fail(QStringLiteral(
            "The active editor does not match the "
            "current Slang semantic generation."));
    }

    TSDocument syntax;
    syntax.setText(context.documentText);
    const TSIdentifierTarget identifier =
        syntax.identifierAt(
            context.cursorPosition);
    if (!identifier.ok()) {
        return fail(QStringLiteral(
            "Place the cursor on a port, parameter, "
            "or localparam identifier."));
    }
    context =
        editor->editorSemanticContextForPosition(
            identifier.startChar, true);
    const DefinitionResult definition =
        resolveRtlRenameSubject(
            context, identifier);
    if (!definition.found
        || !isSupportedRtlRenameSubject(
            definition.symbolRecord)) {
        return fail(QStringLiteral(
            "Only one exact Slang module or "
            "interface port, parameter, or "
            "localparam can be renamed."));
    }

    QSet<QString> workspaceFileSet;
    for (const QString& file :
         workspaceManager
             ->getSystemVerilogFiles()) {
        const QString normalized =
            normalizedRtlActionFileName(file);
        if (!normalized.isEmpty())
            workspaceFileSet.insert(normalized);
    }
    const QString subjectFile =
        normalizedRtlActionFileName(
            definition.symbolRecord
                .location.fileName);
    if (!subjectFile.isEmpty())
        workspaceFileSet.insert(subjectFile);

    QHash<QString, RtlRenameDocumentSnapshot>
        capturedDocuments;
    QString captureFailure;
    if (!captureRtlActionDocuments(
            workspaceFileSet,
            semanticToken,
            *semanticDocks
                 ->rtlActionDocumentManager(),
            &capturedDocuments,
            &captureFailure)) {
        return fail(captureFailure);
    }

    QVariantMap parameters =
        invocation.parameters;
    if (parameters.isEmpty()) {
        parameters =
            applicationActionExecutionHistory()
                .rememberedParameters(
                    workspaceManager
                        ->getWorkspacePath(),
                    RtlRenameWorkflow::
                        actionFamilyId());
    }
    RtlRenamePanelSession session;
    session.baseQuery.subjectStableKey =
        definition.symbolRecord.stableKey;
    session.baseQuery.semanticToken =
        semanticToken;
    session.baseQuery.documents =
        capturedDocuments;
    session.baseQuery.workspaceFiles =
        workspaceFileSet.values();
    session.baseQuery.workspaceFiles.sort();
    session.baseQuery.dryRun = result.dryRun;
    session.subjectLabel =
        definition.symbolRecord.owner.name
            .isEmpty()
        ? definition.symbolRecord.name
        : QStringLiteral("%1.%2")
              .arg(
                  definition.symbolRecord
                      .owner.name,
                  definition.symbolRecord.name);
    session.oldName =
        definition.symbolRecord.name;
    session.suggestedNewName =
        parameters
            .value(QStringLiteral("newName"))
            .toString()
            .trimmed();

    QString beginFailure;
    RtlHighRiskEditPanelCoordinator*
        coordinator =
            semanticDocks
                ->rtlHighRiskEditPanelCoordinator();
    if (!panelLayoutController
        || !coordinator->dock()
        || !panelLayoutController
                ->isBottomPanel(
                    coordinator->dock())
        || panelLayoutController
               ->panelIdForDock(
                   coordinator->dock())
            != RtlHighRiskEditPanelCoordinator::
                   panelId()) {
        return fail(QStringLiteral(
            "The RTL High+Diff bottom page is "
            "not managed by the panel layout."));
    }
    if (!coordinator->beginRename(
            std::move(session),
            &beginFailure)) {
        return fail(
            beginFailure.isEmpty()
            ? QStringLiteral(
                  "The RTL rename page rejected "
                  "the new session.")
            : beginFailure);
    }

    QPointer<QWidget> previousFocus =
        QApplication::focusWidget();
    if (!panelLayoutController->restorePanel(
            RtlHighRiskEditPanelCoordinator::
                panelId())) {
        coordinator->resetForWorkspaceClose();
        return fail(QStringLiteral(
            "The RTL High+Diff bottom page is "
            "not managed by the panel layout."));
    }
    if (previousFocus
        && QApplication::focusWidget()
            != previousFocus) {
        previousFocus->setFocus(
            Qt::OtherFocusReason);
    }

    result.succeeded = true;
    result.message = QStringLiteral(
        "RTL rename request opened in the "
        "High+Diff bottom page.");
    result.output.insert(
        QStringLiteral("panelId"),
        RtlHighRiskEditPanelCoordinator::
            panelId());
    result.output.insert(
        QStringLiteral("sessionId"),
        QVariant::fromValue<qulonglong>(
            coordinator->activeSessionId()));
    return result;
}

ActionExecutionResult
MainWindow::executeRtlConnectionTransformAction(
    const ActionInvocation& invocation)
{
    ActionExecutionResult result;
    result.handled = true;
    result.dryRun =
        invocation.mode
        == ActionExecutionMode::DryRun;
    const auto fail =
        [&result](const QString& reason) {
            result.failureReason = reason;
            return result;
        };
    if (!tabManager || !workspaceManager
        || !workspaceManager->isWorkspaceOpen()
        || !semanticDocks
        || !semanticDocks
                ->rtlHighRiskEditPanelCoordinator()
        || !semanticDocks
                ->rtlActionDocumentManager()) {
        return fail(QStringLiteral(
            "The unified RTL High+Diff workspace "
            "workflow is unavailable."));
    }

    MyCodeEditor* editor =
        tabManager->getCurrentEditor();
    if (!editor) {
        return fail(QStringLiteral(
            "Open a SystemVerilog editor before "
            "transforming instance connections."));
    }
    const EditorSemanticContext context =
        editor->editorSemanticContextForPosition(
            -1, true);
    const EditorActionContext actionContext =
        resolveEditorActionContext(context);
    if (actionContext.semanticState
            != EditorActionSemanticState::Current
        || !actionContext.hierarchyBound()) {
        return fail(
            !actionContext
                 .hierarchyResolutionReason
                 .isEmpty()
            ? actionContext
                  .hierarchyResolutionReason
            : actionContext.semanticError
                      .isEmpty()
                ? QStringLiteral(
                      "A current Slang snapshot "
                      "and one exact parent "
                      "hierarchy instance are "
                      "required.")
                : actionContext.semanticError);
    }

    const SemanticSnapshotToken semanticToken =
        SemanticIndex::getInstance()
            ->snapshotToken();
    if (!semanticToken.isValid()
        || semanticToken.revision == 0
        || (actionContext
                    .semanticSnapshotRevision
                != 0
            && actionContext
                    .semanticSnapshotRevision
                != semanticToken.revision)) {
        return fail(QStringLiteral(
            "The active editor does not match the "
            "current Slang semantic generation."));
    }

    TSDocument syntax;
    syntax.setText(context.documentText);
    const TSIdentifierTarget identifier =
        syntax.identifierAt(
            context.cursorPosition);
    QString instanceFailure;
    const auto instance =
        resolveRtlInstanceSubject(
            semanticToken,
            context,
            identifier,
            &instanceFailure);
    if (!instance)
        return fail(instanceFailure);

    const QString instanceFile =
        normalizedRtlActionFileName(
            instance->location.fileName);
    const auto document =
        semanticDocks
            ->rtlActionDocumentManager()
            ->snapshot(
                rtlActionUtf8String(
                    instanceFile));
    if (!document) {
        return fail(QStringLiteral(
            "The selected instance document "
            "snapshot is unavailable."));
    }

    QVariantMap parameters =
        invocation.parameters;
    if (parameters.isEmpty()) {
        parameters =
            applicationActionExecutionHistory()
                .rememberedParameters(
                    workspaceManager
                        ->getWorkspacePath(),
                    RtlConnectionTransformWorkflow::
                        actionId());
    }
    RtlConnectionTransformPanelSession session;
    session.baseRequest.instanceStableKey =
        instance->stableKey;
    session.baseRequest.parentInstancePath =
        actionContext.resolvedHierarchy
            .instancePath;
    session.baseRequest.selectedInstancePath =
        session.baseRequest.parentInstancePath
        + QLatin1Char('.')
        + instance->name;
    session.baseRequest
        .expectedSemanticGeneration =
            semanticToken.revision;
    session.baseRequest
        .expectedDocumentRevision =
            document->version.value;
    session.baseRequest
        .convertOrderedToNamed =
            parameters.value(
                QStringLiteral(
                    "convertOrderedToNamed"),
                true).toBool();
    session.baseRequest.addMissingPorts =
        parameters.value(
            QStringLiteral(
                "addMissingPorts"),
            false).toBool();
    const int missingPolicy =
        parameters.value(
            QStringLiteral(
                "missingPortPolicy"),
            static_cast<int>(
                RtlMissingPortConnectionPolicy::
                    LeaveUnconnected))
            .toInt();
    if (missingPolicy
            == static_cast<int>(
                RtlMissingPortConnectionPolicy::
                    ConnectSameNamedSignal)) {
        session.baseRequest.missingPortPolicy =
            RtlMissingPortConnectionPolicy::
                ConnectSameNamedSignal;
    }
    const int castPolicy =
        parameters.value(
            QStringLiteral("castPolicy"),
            static_cast<int>(
                RtlExplicitCastPolicy::
                    PreserveExistingExpression))
            .toInt();
    if (castPolicy
            == static_cast<int>(
                RtlExplicitCastPolicy::
                    InsertWhenRequired)) {
        session.baseRequest.castPolicy =
            RtlExplicitCastPolicy::
                InsertWhenRequired;
    }
    session.instanceLabel =
        QStringLiteral("%1  (%2)")
            .arg(
                session.baseRequest
                    .selectedInstancePath,
                instance->type
                    .resolvedTypeName);
    session.dryRun = result.dryRun;

    QString beginFailure;
    RtlHighRiskEditPanelCoordinator*
        coordinator =
            semanticDocks
                ->rtlHighRiskEditPanelCoordinator();
    if (!panelLayoutController
        || !coordinator->dock()
        || !panelLayoutController
                ->isBottomPanel(
                    coordinator->dock())
        || panelLayoutController
               ->panelIdForDock(
                   coordinator->dock())
            != RtlHighRiskEditPanelCoordinator::
                   panelId()) {
        return fail(QStringLiteral(
            "The RTL High+Diff bottom page is "
            "not managed by the panel layout."));
    }
    if (!coordinator
             ->beginConnectionTransform(
                 std::move(session),
                 &beginFailure)) {
        return fail(
            beginFailure.isEmpty()
            ? QStringLiteral(
                  "The connection transform "
                  "page rejected the new "
                  "session.")
            : beginFailure);
    }

    QPointer<QWidget> previousFocus =
        QApplication::focusWidget();
    if (!panelLayoutController->restorePanel(
            RtlHighRiskEditPanelCoordinator::
                panelId())) {
        coordinator->resetForWorkspaceClose();
        return fail(QStringLiteral(
            "The RTL High+Diff bottom page is "
            "not managed by the panel layout."));
    }
    if (previousFocus
        && QApplication::focusWidget()
            != previousFocus) {
        previousFocus->setFocus(
            Qt::OtherFocusReason);
    }

    result.succeeded = true;
    result.message = QStringLiteral(
        "Connection transform request opened "
        "in the High+Diff bottom page.");
    result.output.insert(
        QStringLiteral("panelId"),
        RtlHighRiskEditPanelCoordinator::
            panelId());
    result.output.insert(
        QStringLiteral("sessionId"),
        QVariant::fromValue<qulonglong>(
            coordinator->activeSessionId()));
    return result;
}

ActionExecutionResult
MainWindow::executeInstancePairConnectionAction(
    const ActionInvocation& invocation)
{
    ActionExecutionResult result;
    result.handled = true;
    result.dryRun =
        invocation.mode == ActionExecutionMode::DryRun;
    const auto fail =
        [&result](const QString& reason) {
            result.failureReason = reason;
            return result;
        };
    if (!tabManager || !workspaceManager
        || !workspaceManager->isWorkspaceOpen()
        || !semanticDocks
        || !semanticDocks
                ->instancePairConnectionCoordinator()
        || !semanticDocks
                ->instancePairConnectionWorkflow()) {
        return fail(QStringLiteral(
            "The instance-pair workspace workflow is unavailable."));
    }
    if (semanticDocks->instancePairConnectionWorkflow()
            ->canUndoAppliedTransaction()) {
        showPanelById(
            InstancePairConnectionCoordinator::panelId());
        return fail(QStringLiteral(
            "Undo the applied instance-pair transaction before "
            "starting another instance-pair action."));
    }

    MyCodeEditor* editor =
        tabManager->getCurrentEditor();
    if (!editor) {
        return fail(QStringLiteral(
            "Open a SystemVerilog editor before connecting instances."));
    }
    EditorSemanticContext sourceContext =
        editor->editorSemanticContextForPosition(
            -1, true);
    const EditorActionContext actionContext =
        resolveEditorActionContext(sourceContext);
    if (actionContext.semanticState
            != EditorActionSemanticState::Current
        || !actionContext.hierarchyBound()) {
        return fail(
            actionContext.hierarchyResolutionReason
                    .isEmpty()
                ? QStringLiteral(
                      "A current Slang snapshot and exact hierarchy "
                      "instance are required.")
                : actionContext.hierarchyResolutionReason);
    }

    const SemanticSnapshotToken semanticToken =
        SemanticIndex::getInstance()->snapshotToken();
    if (!semanticToken.isValid()) {
        return fail(QStringLiteral(
            "A current Slang semantic snapshot is required."));
    }
    QSet<QString> workspaceFiles;
    for (const QString& file :
         workspaceManager->getSystemVerilogFiles()) {
        const QString normalized =
            normalizedRtlActionFileName(file);
        if (!normalized.isEmpty())
            workspaceFiles.insert(normalized);
    }

    WorkspaceEditDocumentManager captureDocuments(
        tabManager.get());
    QHash<QString, InstancePairDocumentSnapshot>
        captured;
    QString captureFailure;
    if (!captureRtlActionDocuments(
            workspaceFiles,
            semanticToken,
            captureDocuments,
            &captured,
            &captureFailure)) {
        return fail(captureFailure);
    }
    const QString sourceFile =
        normalizedRtlActionFileName(
            sourceContext.fileName);
    const auto capturedSource =
        captured.constFind(sourceFile);
    if (capturedSource == captured.constEnd()) {
        return fail(QStringLiteral(
            "The source signal document was not captured."));
    }
    sourceContext.fileName = sourceFile;
    sourceContext.documentText =
        capturedSource->text;
    sourceContext.documentRevision =
        capturedSource->revision;

    const TSIdentifierTarget identifier =
        capturedSource->syntax
            ? capturedSource->syntax->identifierAt(
                  sourceContext.cursorPosition)
            : TSIdentifierTarget{};
    if (!identifier.ok()) {
        return fail(QStringLiteral(
            "Place the cursor on the source signal identifier."));
    }

    HierarchyService* hierarchy =
        HierarchyService::getInstance();
    QStringList roots =
        hierarchy->inferDesignTopModules(
            workspaceFiles);
    const QString activeTop =
        actionContext.resolvedHierarchy
            .activeTopModule;
    if (!roots.contains(activeTop))
        roots.append(activeTop);
    const DesignHierarchyReport design =
        hierarchy->getDesignHierarchyReport(
            roots, activeTop, workspaceFiles);
    if (design.snapshotGeneration
            != semanticToken.revision) {
        return fail(QStringLiteral(
            "The design hierarchy is stale relative to Slang."));
    }

    InstancePairUserSelection selection;
    selection.leftInstancePath =
        invocation.parameters.value(
            QStringLiteral("leftInstancePath"),
            actionContext.resolvedHierarchy
                .instancePath)
            .toString();
    selection.rightInstancePath =
        invocation.parameters.value(
            QStringLiteral("rightInstancePath"))
            .toString();
    selection.connectionName =
        invocation.parameters.value(
            QStringLiteral("connectionName"),
            identifier.text
                + QStringLiteral("_link"))
            .toString()
            .trimmed();
    const bool completeStructuredSelection =
        !selection.leftInstancePath.isEmpty()
        && !selection.rightInstancePath.isEmpty()
        && !selection.connectionName.isEmpty();
    if (!completeStructuredSelection) {
        const auto selected =
            selectInstancePair(
                editor,
                design.nodes,
                sourceContext.moduleName,
                selection);
        if (!selected) {
            return fail(QStringLiteral(
                "Instance-pair selection was cancelled or has no "
                "compatible destination."));
        }
        selection = *selected;
    }

    DesignHierarchyNode selectedLeft;
    DesignHierarchyNode selectedRight;
    int leftMatches = 0;
    int rightMatches = 0;
    for (const DesignHierarchyNode& node :
         design.nodes) {
        if (!node.inSelectedTop || node.unresolved)
            continue;
        if (node.instancePath
            == selection.leftInstancePath) {
            selectedLeft = node;
            ++leftMatches;
        }
        if (node.instancePath
            == selection.rightInstancePath) {
            selectedRight = node;
            ++rightMatches;
        }
    }
    if (leftMatches != 1 || rightMatches != 1
        || selectedLeft.isTop
        || selectedRight.isTop
        || selectedLeft.instancePath
               == selectedRight.instancePath
        || selectedLeft.moduleType
               != sourceContext.moduleName
        || selectedLeft.rootId
               != selectedRight.rootId) {
        return fail(QStringLiteral(
            "The structured instance selection does not identify "
            "two compatible instances in one design root."));
    }

    sourceContext.hierarchyInstance.workspacePath =
        workspaceManager->getWorkspacePath();
    sourceContext.hierarchyInstance.activeTopModule =
        activeTop;
    sourceContext.hierarchyInstance.instancePath =
        selectedLeft.instancePath;

    InstancePairConnectionQuery query;
    query.leftSignalContext =
        std::move(sourceContext);
    query.leftInstancePath =
        selectedLeft.instancePath;
    query.rightInstancePath =
        selectedRight.instancePath;
    query.connectionName =
        selection.connectionName;
    query.workspaceFiles =
        workspaceFiles;
    query.semanticToken =
        semanticToken;
    query.documents =
        std::move(captured);
    query.dryRun =
        invocation.mode
        == ActionExecutionMode::DryRun;

    InstancePairConnectionWorkflow* workflow =
        semanticDocks
            ->instancePairConnectionWorkflow();
    const InstancePairConnectionWorkflowResult
        analyzed =
            workflow->analyzeAndPresent(query);
    if (!analyzed.succeeded()) {
        return fail(
            analyzed.message.isEmpty()
                ? QStringLiteral(
                      "Instance-pair analysis was rejected.")
                : analyzed.message);
    }

    showPanelById(
        InstancePairConnectionCoordinator::panelId());
    if (query.dryRun) {
        InstancePairConnectionPanel* panel =
            semanticDocks
                ->instancePairConnectionCoordinator()
                ->panel();
        if (!panel) {
            return fail(QStringLiteral(
                "The instance-pair preview page is unavailable."));
        }
        const InstancePairConnectionWorkflowResult
            previewed =
                workflow->requestPreview(
                    panel->currentPlanRequest());
        if (!previewed.succeeded()) {
            return fail(
                previewed.message.isEmpty()
                    ? QStringLiteral(
                          "Instance-pair dry-run planning was rejected.")
                    : previewed.message);
        }
    }

    result.succeeded = true;
    result.message =
        query.dryRun
        ? QStringLiteral(
              "Instance-pair High+Diff dry-run preview is ready.")
        : QStringLiteral(
              "Drag the selected source signal to the destination "
              "block to build the High+Diff preview.");
    result.hasResolvedParameters = true;
    result.resolvedParameters.insert(
        QStringLiteral("leftInstancePath"),
        selectedLeft.instancePath);
    result.resolvedParameters.insert(
        QStringLiteral("rightInstancePath"),
        selectedRight.instancePath);
    result.resolvedParameters.insert(
        QStringLiteral("connectionName"),
        selection.connectionName);
    result.output.insert(
        QStringLiteral("panelId"),
        InstancePairConnectionCoordinator::panelId());
    result.output.insert(
        QStringLiteral("leftInstancePath"),
        selectedLeft.instancePath);
    result.output.insert(
        QStringLiteral("rightInstancePath"),
        selectedRight.instancePath);
    result.output.insert(
        QStringLiteral("semanticGeneration"),
        QVariant::fromValue<qulonglong>(
            semanticToken.revision));
    return result;
}

ActionExecutionResult
MainWindow::executeMultiSignalPropagationAction(
    const ActionInvocation& invocation)
{
    ActionExecutionResult result;
    result.handled = true;
    result.dryRun =
        invocation.mode == ActionExecutionMode::DryRun;
    const auto fail =
        [&result](const QString& reason) {
            result.failureReason = reason;
            return result;
        };
    if (!tabManager || !workspaceManager
        || !workspaceManager->isWorkspaceOpen()
        || !semanticDocks
        || !semanticDocks
                ->multiSignalPropagationPanel()
        || !semanticDocks
                ->multiSignalPropagationWorkflow()) {
        return fail(QStringLiteral(
            "The multi-signal workspace workflow is unavailable."));
    }
    if (semanticDocks->multiSignalPropagationWorkflow()
            ->canUndoAppliedTransaction()) {
        showPanelById(
            MultiSignalPropagationPanel::panelId());
        return fail(QStringLiteral(
            "Undo the applied multi-signal transaction before "
            "starting another batch propagation action."));
    }
    MyCodeEditor* editor =
        tabManager->getCurrentEditor();
    if (!editor) {
        return fail(QStringLiteral(
            "Open a SystemVerilog editor before propagating signals."));
    }

    EditorSemanticContext editorContext =
        editor->editorSemanticContextForPosition(
            -1, true);
    const EditorActionContext actionContext =
        resolveEditorActionContext(editorContext);
    if (actionContext.semanticState
            != EditorActionSemanticState::Current
        || !actionContext.hierarchyBound()) {
        return fail(
            actionContext.hierarchyResolutionReason
                    .isEmpty()
                ? QStringLiteral(
                      "A current Slang snapshot and exact hierarchy "
                      "instance are required.")
                : actionContext.hierarchyResolutionReason);
    }

    QStringList selectedNames =
        invocation.parameters.value(
            QStringLiteral("signalNames"))
            .toStringList();
    if (selectedNames.isEmpty())
        selectedNames = editor->selectedSignalNames();
    selectedNames.removeDuplicates();
    if (selectedNames.size() < 2) {
        return fail(QStringLiteral(
            "Use Signal Selection to select at least two signals."));
    }

    SemanticIndex* semanticIndex =
        SemanticIndex::getInstance();
    const SemanticSnapshotToken semanticToken =
        semanticIndex->snapshotToken();
    if (!semanticToken.isValid()) {
        return fail(QStringLiteral(
            "A current Slang semantic snapshot is required."));
    }
    QSet<QString> workspaceFiles;
    for (const QString& file :
         workspaceManager->getSystemVerilogFiles()) {
        const QString normalized =
            normalizedRtlActionFileName(file);
        if (!normalized.isEmpty())
            workspaceFiles.insert(normalized);
    }
    WorkspaceEditDocumentManager captureDocuments(
        tabManager.get());
    QHash<QString, MultiSignalPropagationDocumentSnapshot>
        captured;
    QString captureFailure;
    if (!captureRtlActionDocuments(
            workspaceFiles,
            semanticToken,
            captureDocuments,
            &captured,
            &captureFailure)) {
        return fail(captureFailure);
    }
    const QString activeFile =
        normalizedRtlActionFileName(
            editorContext.fileName);
    const auto capturedActive =
        captured.constFind(activeFile);
    if (capturedActive == captured.constEnd()) {
        return fail(QStringLiteral(
            "The selected signal document was not captured."));
    }

    QList<MultiSignalPropagationSignalChoice>
        signalChoices;
    QSet<QString> stableKeys;
    for (const QString& name : selectedNames) {
        QList<SemanticSymbolRecord> candidates;
        for (const SemanticSymbolRecord& record :
             semanticToken.snapshot
                 ->getSymbolRecordsByName(name)) {
            SymbolTaxonomy::SemanticMetadata metadata;
            metadata.declarationKind =
                record.declarationKind;
            metadata.usageRole =
                record.usageRole;
            metadata.ownerScope =
                record.owner.kind;
            metadata.visibility =
                record.visibility;
            metadata.sourceRole =
                record.sourceRole;
            metadata.collectorKind =
                record.collectorKind;
            metadata.interfaceLikeOwner =
                record.owner.interfaceLike;
            if (normalizedRtlActionFileName(
                    record.location.fileName)
                    != activeFile
                || record.owner.name
                       != editorContext.moduleName
                || (!SymbolTaxonomy::
                        isSignalDeclaration(metadata)
                    && !SymbolTaxonomy::
                        isPortDeclaration(metadata))
                || !record.stableKey.isValid()) {
                continue;
            }
            const QString stable =
                record.stableKey.toString();
            bool duplicate = false;
            for (const SemanticSymbolRecord& existing :
                 candidates) {
                if (existing.stableKey.toString()
                    == stable) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                candidates.append(record);
        }
        if (candidates.size() != 1) {
            return fail(QStringLiteral(
                "Slang did not resolve selected signal \"%1\" "
                "to one module declaration.")
                .arg(name));
        }
        const SemanticSymbolRecord record =
            candidates.constFirst();
        const QString stable =
            record.stableKey.toString();
        if (stableKeys.contains(stable)) {
            return fail(QStringLiteral(
                "The selected signal set contains a duplicate "
                "semantic identity."));
        }
        stableKeys.insert(stable);

        EditorSemanticContext memberContext =
            editor->editorSemanticContextForPosition(
                record.location.position,
                true);
        memberContext.fileName = activeFile;
        memberContext.documentText =
            capturedActive->text;
        memberContext.documentRevision =
            capturedActive->revision;
        memberContext.cursorPosition =
            record.location.position;
        memberContext.hierarchyInstance =
            actionContext.resolvedHierarchy;

        MultiSignalPropagationSignalChoice choice;
        choice.label =
            QStringLiteral("%1.%2")
                .arg(
                    actionContext.resolvedHierarchy
                        .instancePath,
                    name);
        choice.member.context =
            std::move(memberContext);
        choice.member.exportedPortName =
            name + QStringLiteral("_out");
        choice.member.groupMemberName = name;
        choice.selected = true;
        signalChoices.append(std::move(choice));
    }

    HierarchyService* hierarchy =
        HierarchyService::getInstance();
    QStringList roots =
        hierarchy->inferDesignTopModules(
            workspaceFiles);
    const QString activeTop =
        actionContext.resolvedHierarchy
            .activeTopModule;
    if (!roots.contains(activeTop))
        roots.append(activeTop);
    const DesignHierarchyReport design =
        hierarchy->getDesignHierarchyReport(
            roots, activeTop, workspaceFiles);
    if (design.snapshotGeneration
            != semanticToken.revision) {
        return fail(QStringLiteral(
            "The design hierarchy is stale relative to Slang."));
    }
    QHash<QString, DesignHierarchyNode> nodesById;
    DesignHierarchyNode sourceNode;
    int sourceMatches = 0;
    for (const DesignHierarchyNode& node :
         design.nodes) {
        nodesById.insert(node.id, node);
        if (node.inSelectedTop
            && node.instancePath
                   == actionContext
                          .resolvedHierarchy
                          .instancePath) {
            sourceNode = node;
            ++sourceMatches;
        }
    }
    if (sourceMatches != 1) {
        return fail(QStringLiteral(
            "The selected source instance is not unique in the "
            "current hierarchy."));
    }

    QList<MultiSignalPropagationAncestorChoice>
        ancestors;
    ancestors.append(
        {QStringLiteral("Active design top"),
         QString()});
    DesignHierarchyNode cursor = sourceNode;
    while (!cursor.parentId.isEmpty()) {
        const auto parent =
            nodesById.constFind(cursor.parentId);
        if (parent == nodesById.constEnd())
            break;
        cursor = parent.value();
        if (!cursor.isTop) {
            ancestors.append(
                {QStringLiteral("%1  (%2)")
                     .arg(cursor.instancePath,
                          cursor.moduleType),
                 cursor.instancePath});
        }
    }

    MultiSignalPropagationPanelInput input;
    input.signalChoices =
        std::move(signalChoices);
    input.ancestors =
        std::move(ancestors);
    input.workspaceFiles =
        workspaceFiles;
    input.semanticToken =
        semanticToken;
    input.capturedDocuments =
        std::move(captured);
    input.dryRun =
        invocation.mode
        == ActionExecutionMode::DryRun;
    const QString requestedMode =
        invocation.parameters.value(
            QStringLiteral("mode"))
            .toString();
    input.groupName =
        invocation.parameters.value(
            QStringLiteral("groupName"))
            .toString()
            .trimmed();
    input.mode =
        requestedMode.compare(
            QStringLiteral("portGroup"),
            Qt::CaseInsensitive) == 0
            || !input.groupName.isEmpty()
        ? MultiSignalPropagationMode::PortGroup
        : MultiSignalPropagationMode::IndependentPorts;
    const QString requestedAncestor =
        invocation.parameters.value(
            QStringLiteral(
                "targetAncestorInstancePath"))
            .toString();
    if (!requestedAncestor.isEmpty()) {
        for (int index = 0;
             index < input.ancestors.size();
             ++index) {
            if (input.ancestors.at(index).instancePath
                == requestedAncestor) {
                input.selectedAncestorIndex = index;
                break;
            }
        }
    }

    MultiSignalPropagationPanel* panel =
        semanticDocks
            ->multiSignalPropagationPanel();
    panel->setInput(input);
    showPanelById(
        MultiSignalPropagationPanel::panelId());
    if (input.dryRun
        && !panel->requestPreview()) {
        return fail(
            panel->statusText().isEmpty()
                ? QStringLiteral(
                      "Multi-signal dry-run planning was rejected.")
                : panel->statusText());
    }

    result.succeeded = true;
    result.message =
        input.dryRun
        ? QStringLiteral(
              "Multi-signal High+Diff dry-run preview is ready.")
        : QStringLiteral(
              "Review the selected signals, ancestor, and port "
              "layout before requesting the High+Diff preview.");
    result.hasResolvedParameters = true;
    result.resolvedParameters.insert(
        QStringLiteral("mode"),
        input.mode == MultiSignalPropagationMode::PortGroup
            ? QStringLiteral("portGroup")
            : QStringLiteral("independentPorts"));
    result.resolvedParameters.insert(
        QStringLiteral("groupName"),
        input.groupName);
    result.resolvedParameters.insert(
        QStringLiteral("targetAncestorInstancePath"),
        input.ancestors.at(input.selectedAncestorIndex)
            .instancePath);
    result.output.insert(
        QStringLiteral("panelId"),
        MultiSignalPropagationPanel::panelId());
    result.output.insert(
        QStringLiteral("signalCount"),
        selectedNames.size());
    result.output.insert(
        QStringLiteral("semanticGeneration"),
        QVariant::fromValue<qulonglong>(
            semanticToken.revision));
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

    auto* introduction = new QLabel(
        tr("Review each recovery snapshot against the current source. "
           "Restoring changes the in-memory document only; saving remains "
           "an explicit action."),
        crashRecoveryReviewDialog);
    introduction->setObjectName(
        QStringLiteral("crashRecoveryIntroduction"));
    introduction->setWordWrap(true);
    rootLayout->addWidget(introduction);

    crashRecoveryCandidateList =
        new QTreeWidget(crashRecoveryReviewDialog);
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
        new QPlainTextEdit(sourceGroup);
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
        new QPlainTextEdit(recoveredGroup);
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
        new QLabel(crashRecoveryReviewDialog);
    crashRecoveryReviewStatus->setObjectName(
        QStringLiteral("crashRecoveryReviewStatus"));
    crashRecoveryReviewStatus->setWordWrap(true);
    crashRecoveryReviewStatus->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    rootLayout->addWidget(crashRecoveryReviewStatus);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Close,
        crashRecoveryReviewDialog);
    buttons->setObjectName(
        QStringLiteral("crashRecoveryReviewButtons"));
    crashRecoveryRestoreButton = buttons->addButton(
        tr("Restore Selected"),
        QDialogButtonBox::AcceptRole);
    crashRecoveryRestoreButton->setObjectName(
        QStringLiteral("crashRecoveryRestoreButton"));
    crashRecoveryDiscardButton = buttons->addButton(
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
    if (statusBar()) {
        statusBar()->showMessage(
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
    if (statusBar()) {
        statusBar()->showMessage(
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
    if (tabManager)
        state.tabs = tabManager->workspaceSessionTabs(workspaceRoot);
    if (rememberWorkspacePanelState) {
        state.ui.mainWindowGeometry = saveGeometry();
        state.ui.mainWindowState = saveState();
    }
    if (navigationPane) {
        state.ui.navigationFilesQuery =
            navigationPane->filesSearchQuery();
        state.ui.navigationDesignQuery =
            navigationPane->designSearchQuery();
    }
    if (tabManager) {
        state.ui.tabGroupingMode =
            tabGroupingModeStableId(
                tabManager->tabGroupingMode());
    }
    if (panelLayoutController && rememberWorkspacePanelState)
        state.ui.panelLayout = panelLayoutController->layoutState();

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

    const QString workspaceRoot =
        workspaceManager->getWorkspacePath();
    const QString rootKey =
        workspaceSessionRootKey(workspaceRoot);
    if (workspaceSessionCleanRoots.contains(rootKey)) {
        if (!showStatus)
            return false;
        workspaceSessionCleanRoots.remove(rootKey);
    }

    WorkspaceSessionStateService service;
    const WorkspaceSessionSaveResult result =
        service.save(captureWorkspaceSessionState());
    if (showStatus && statusBar()) {
        statusBar()->showMessage(
            result.saved
                ? QStringLiteral(
                      "Local workspace session saved to %1; "
                      ".zeroslack/project.json is unchanged")
                      .arg(QDir::toNativeSeparators(
                          result.storagePath))
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
    WorkspaceSessionRestoreResult result =
        service.load(workspaceRoot);
    bool importedLegacy = false;
    if (!result.loaded
        && WorkspaceSessionStateService::
               legacySessionFileExists(workspaceRoot)) {
        const WorkspaceLegacyImportResult legacy =
            service.loadLegacy(workspaceRoot);
        if (legacy.loaded) {
            result.loaded = true;
            result.state = legacy.state;
            result.skippedTabs = legacy.skippedTabs;
            result.skippedScannedFiles =
                legacy.skippedScannedFiles;
            result.message = legacy.message;
            importedLegacy = true;
            service.save(legacy.state);
        } else {
            result.message = legacy.message;
        }
    }
    if (!result.loaded) {
        if (statusBar())
            statusBar()->showMessage(result.message, 5000);
        return false;
    }

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
    if (tabManager) {
        tabManager->setTabGroupingMode(
            tabGroupingModeFromStableId(
                result.state.ui.tabGroupingMode));
    }
    skippedTabs.append(tabRestoreSkips);
    skippedTabs.removeDuplicates();

    bool geometryRestored = true;
    bool dockStateRestored = true;
    if (rememberWorkspacePanelState
        && !result.state.ui.mainWindowGeometry.isEmpty()) {
        geometryRestored =
            restoreGeometry(result.state.ui.mainWindowGeometry);
    }
    if (rememberWorkspacePanelState
        && !result.state.ui.mainWindowState.isEmpty()) {
        dockStateRestored =
            restoreState(result.state.ui.mainWindowState);
    }
    if (navigationPane) {
        navigationPane->setSearchQueries(
            result.state.ui.navigationFilesQuery,
            result.state.ui.navigationDesignQuery);
    }
    if (rememberWorkspacePanelState && !dockStateRestored)
        resetPanelLayout();
    if (panelLayoutController && rememberWorkspacePanelState) {
        if (result.state.ui.panelLayout.valid) {
            panelLayoutController->restoreLayoutState(
                result.state.ui.panelLayout);
        } else {
            panelLayoutController->bindManagedTabBars();
        }
    }

    QStringList notes;
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
    if (importedLegacy)
        notes.append(
            QStringLiteral(
                "legacy .zs imported read-only"));

    QString message =
        QStringLiteral(
            "Local workspace session restored: "
            "%1 tab(s), %2 scanned file(s); "
            "portable project configuration unchanged")
            .arg(restoredTabs.size())
            .arg(result.state.scannedFiles.size());
    if (!notes.isEmpty())
        message += QStringLiteral(" (%1)").arg(notes.join(QStringLiteral("; ")));
    if (statusBar())
        statusBar()->showMessage(message, notes.isEmpty() ? 4000 : 7000);
    scheduleWorkspaceSessionSave();
    return scanRestored && dockStateRestored;
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

    const QString workspaceRoot =
        workspaceManager->getWorkspacePath();
    WorkspaceSessionStateService service;
    const bool cleared = service.clear(workspaceRoot);
    workspaceSessionCleanRoots.insert(
        workspaceSessionRootKey(workspaceRoot));
    if (statusBar())
        statusBar()->showMessage(
            cleared
                ? QStringLiteral(
                      "Local workspace session cleared; "
                      "portable project configuration and "
                      "legacy .zs are unchanged")
                : QStringLiteral(
                      "Failed to clear local workspace session; "
                      "portable project configuration is unchanged"),
            cleared ? 4000 : 5000);
}

void MainWindow::scheduleWorkspaceSessionSave()
{
    if (!workspaceManager || !workspaceManager->isWorkspaceOpen())
        return;
    if (workspaceSessionCleanRoots.contains(
            workspaceSessionRootKey(
                workspaceManager
                    ->getWorkspacePath()))) {
        return;
    }

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
    WorkspaceSessionStateService service;
    if (statusBar()
        && service.sessionExists(workspaceRoot)) {
        statusBar()->showMessage(
            QStringLiteral(
                "Local workspace session available: "
                "use ow s restore; project configuration "
                "loads separately"),
            5000);
    } else if (statusBar()
               && WorkspaceSessionStateService::
                      legacySessionFileExists(
                          workspaceRoot)) {
        statusBar()->showMessage(
            QStringLiteral(
                "Legacy .zs detected: ow s restore imports "
                "local state read-only; project settings "
                "load separately"),
            6000);
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

    const QString normalizedState = state.trimmed();
    if (diagnosticsAnalysisState == normalizedState)
        return;
    diagnosticsAnalysisState = normalizedState;

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

void MainWindow::setupEditorActionContextChip()
{
    if (!statusBar() || editorActionContextChip)
        return;
    if (!editorActionContextService) {
        editorActionContextService =
            std::make_unique<EditorActionContextService>();
    }

    editorActionContextChip = new QLabel(this);
    editorActionContextChip->setObjectName(
        QStringLiteral("editorActionContextChip"));
    editorActionContextChip->setTextInteractionFlags(Qt::NoTextInteraction);
    editorActionContextChip->setContentsMargins(8, 2, 8, 2);
    editorActionContextChip->setAccessibleName(
        tr("Editor action context"));
    statusBar()->addPermanentWidget(editorActionContextChip);
    refreshEditorActionContextChip();
}

void MainWindow::refreshEditorActionContextChip()
{
    if (!editorActionContextChip || !editorActionContextService)
        return;

    const EditorActionContext context =
        resolveEditorActionContext(EditorSemanticContext());
    const QString compactText = context.compactText();
    const QString detailText = context.detailText();
    const QString semanticState = context.semanticStateText();
    const bool hierarchyBound = context.hierarchyBound();

    InsightStatusTone tone = InsightStatusTone::Info;
    if (context.semanticState == EditorActionSemanticState::Failed) {
        tone = InsightStatusTone::Error;
    } else if (context.semanticState == EditorActionSemanticState::Stale
               || (context.hasEditor() && !context.hierarchyBound())) {
        tone = InsightStatusTone::Warning;
    } else if (context.semanticState == EditorActionSemanticState::Current
               && hierarchyBound) {
        tone = InsightStatusTone::Success;
    }
    const QString styleSheet =
        InsightVisualStyle::statusChipStyleSheet(
            tone, editorActionContextChip->objectName());

    if (editorActionContextChip->text() != compactText) {
        ++editorActionContextChipWriteCounts.text;
        editorActionContextChip->setText(compactText);
    }
    if (editorActionContextChip->toolTip() != detailText) {
        ++editorActionContextChipWriteCounts.toolTip;
        editorActionContextChip->setToolTip(detailText);
    }
    if (editorActionContextChip->accessibleDescription()
        != detailText) {
        ++editorActionContextChipWriteCounts.accessibleDescription;
        editorActionContextChip->setAccessibleDescription(detailText);
    }
    const QVariant semanticStateProperty =
        editorActionContextChip->property("semanticState");
    if (!semanticStateProperty.isValid()
        || semanticStateProperty.toString() != semanticState) {
        ++editorActionContextChipWriteCounts.semanticStateProperty;
        editorActionContextChip->setProperty(
            "semanticState", semanticState);
    }
    const QVariant hierarchyBoundProperty =
        editorActionContextChip->property("hierarchyBound");
    if (!hierarchyBoundProperty.isValid()
        || hierarchyBoundProperty.toBool() != hierarchyBound) {
        ++editorActionContextChipWriteCounts.hierarchyBoundProperty;
        editorActionContextChip->setProperty(
            "hierarchyBound", hierarchyBound);
    }
    if (editorActionContextChip->styleSheet() != styleSheet) {
        ++editorActionContextChipWriteCounts.styleSheet;
        editorActionContextChip->setStyleSheet(styleSheet);
    }
    if (editorActionContextChip->isHidden()) {
        ++editorActionContextChipWriteCounts.visible;
        editorActionContextChip->setVisible(true);
    }
}

EditorActionContextChipWriteCounts
MainWindow::editorActionContextChipWriteCountsForTesting() const
{
    return editorActionContextChipWriteCounts;
}

void MainWindow::resetEditorActionContextChipWriteCountsForTesting()
{
    editorActionContextChipWriteCounts = {};
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

void MainWindow::updateEditorModeChip(
    const EditorModeSnapshot& snapshot)
{
    if (!editorModeChip)
        return;

    if (!snapshot.hasActiveMode()) {
        editorModeChip->clear();
        editorModeChip->setToolTip(QString());
        editorModeChip->setVisible(false);
        setFoldShelfModeVisualActive(false);
        return;
    }

    const EditorModeDescriptor* descriptor =
        findEditorModeDescriptor(snapshot.primaryMode);
    QString text = snapshot.displayText;
    if (text.isEmpty() && descriptor)
        text = descriptor->displayName;
    QString detail = snapshot.detailText;
    if (detail.isEmpty() && descriptor)
        detail = descriptor->guidance;

    editorModeChip->setText(
        detail.isEmpty()
            ? text
            : QStringLiteral("%1 — %2").arg(text, detail));
    editorModeChip->setToolTip(
        descriptor
            ? QStringLiteral("Mode: %1\nOwner: %2\n%3")
                  .arg(descriptor->stableId)
                  .arg(editorModeOwnerText(descriptor->owner))
                  .arg(detail)
            : detail);
    const InsightStatusTone tone =
        snapshot.primaryMode == EditorModeId::FoldShelf
            || snapshot.primaryMode == EditorModeId::SignalSelection
        ? InsightStatusTone::Warning
        : InsightStatusTone::Success;
    editorModeChip->setStyleSheet(
        InsightVisualStyle::statusChipStyleSheet(
            tone,
            editorModeChip->objectName()));
    setFoldShelfModeVisualActive(
        snapshot.primaryMode == EditorModeId::FoldShelf);
    editorModeChip->setVisible(true);
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
            ? semanticDocks->instancePairConnectionDock()
            : nullptr;
    }
    if (panelId
        == MultiSignalPropagationPanel::panelId()) {
        return semanticDocks
            ? semanticDocks->multiSignalPropagationDock()
            : nullptr;
    }
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
    if (panelId == QStringLiteral("settingsCenter")
        || panelId == QStringLiteral("editorAppearance")) {
        return settingsCenterDock;
    }
    return nullptr;
}

bool MainWindow::insightPanelVisibleOrFocused(
    const QString& panelId) const
{
    const QDockWidget* dock = dockForPanelId(panelId);
    return (dock && dock->isVisible())
        || (insightFocusController
            && insightFocusController->isFocused()
            && insightFocusController->focusedPanelId()
                   == panelId);
}

void MainWindow::showDockWidget(QDockWidget* dock,
                                const QString& statusMessage)
{
    if (!dock)
        return;

    if (insightFocusController
        && dock->property("insightFocusActive").toBool()) {
        if (centralContentStack
            && insightFocusController->focusPage()) {
            centralContentStack->setCurrentWidget(
                insightFocusController->focusPage());
        }
        if (!statusMessage.isEmpty() && statusBar())
            statusBar()->showMessage(statusMessage, 3000);
        return;
    }

    if (panelLayoutController
        && panelLayoutController->isBottomPanel(dock)) {
        panelLayoutController->restorePanel(
            panelLayoutController->panelIdForDock(dock));
        if (!statusMessage.isEmpty() && statusBar())
            statusBar()->showMessage(statusMessage, 3000);
        return;
    }

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
    if (insightFocusController
        && insightFocusController->isFocused()
        && insightFocusController->focusedPanelId()
               != panelId) {
        insightFocusController->leaveToEditor();
    }
    if (panelId == QStringLiteral("wavePreview"))
        refreshActiveEditorWavePreview();

    QDockWidget* dock = dockForPanelId(panelId);
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
    if (insightFocusController
        && insightFocusController->isFocused()) {
        insightFocusController->returnToDock();
    }

    QDockWidget* navigationDock = dockForPanelId(QStringLiteral("navigation"));
    QDockWidget* problemsDock = dockForPanelId(QStringLiteral("problems"));
    QDockWidget* activityDock = dockForPanelId(QStringLiteral("activity"));
    QDockWidget* rtlInsightsDock = dockForPanelId(QStringLiteral("rtlInsights"));
    QDockWidget* signalKernelGraphDock =
        dockForPanelId(QStringLiteral("signalKernelGraph"));
    QDockWidget* wavePreviewDock = dockForPanelId(QStringLiteral("wavePreview"));
    QDockWidget* settingsCenterDockWidget =
        dockForPanelId(QStringLiteral("settingsCenter"));
    QDockWidget* foldShelfDockWidget = dockForPanelId(QStringLiteral("foldShelf"));

    if (navigationDock)
        addDockWidget(Qt::LeftDockWidgetArea, navigationDock);
    if (settingsCenterDockWidget)
        addDockWidget(Qt::RightDockWidgetArea, settingsCenterDockWidget);

    if (panelLayoutController) {
        panelLayoutController->resetLayout();
    } else {
        QDockWidget* bottomDocks[] = {
            problemsDock,
            activityDock,
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
        for (QDockWidget* dock : bottomDocks)
            showDockWidget(dock);
        if (problemsDock)
            problemsDock->raise();
    }

    showDockWidget(settingsCenterDockWidget);

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

void MainWindow::setupSettingsCenter()
{
    settingsCenterService =
        std::make_unique<SettingsCenterService>();

    // SettingsCenterService is the only persistence owner. These existing
    // settings objects remain the runtime adapters consumed by
    // EditorCoordinator, with null QSettings backends to prevent workspace
    // effective values from being written into the global layer.
    editorAppearanceSettings =
        std::make_unique<EditorAppearanceSettings>(
            std::unique_ptr<QSettings>());
    formatterSettings =
        std::make_unique<FormatterSettings>(
            std::unique_ptr<QSettings>());

    settingsCenterDock = new QDockWidget(tr("Settings"), this);
    // Keep the dock object name so QMainWindow::restoreState continues to
    // restore layouts saved before the Settings Center migration.
    settingsCenterDock->setObjectName(
        QStringLiteral("editorAppearanceDock"));
    settingsCenterDock->setProperty(
        "settingsCenterPanelId", QStringLiteral("settingsCenter"));
    settingsCenterDock->setProperty(
        "legacyPanelId", QStringLiteral("editorAppearance"));
    settingsCenterDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    settingsCenterPanel = new SettingsCenterPanel(
        settingsCenterService.get(),
        workspaceManager ? workspaceManager->getWorkspacePath()
                         : QString(),
        settingsCenterDock);
    settingsCenterDock->setWidget(settingsCenterPanel);
    addDockWidget(Qt::RightDockWidgetArea, settingsCenterDock);

    connect(&ApplicationThemeManager::instance(),
            &ApplicationThemeManager::themeChanged,
            this,
            [this](ThemeMode) {
                refreshThemePresentation();
            });

    connect(settingsCenterPanel,
            &SettingsCenterPanel::settingsApplied,
            this,
            [this](SettingsCenterScope) {
                if (settingsCenterPanel) {
                    applySettingsCenterSnapshot(
                        settingsCenterPanel->snapshot());
                }
            });
    applySettingsCenterSnapshot(settingsCenterPanel->snapshot());
}

void MainWindow::applySettingsCenterSnapshot(
    const SettingsCenterSnapshot& snapshot)
{
    const QString themeName =
        snapshot.value(
            QStringLiteral("appearance.theme"))
            .toString();
    ApplicationThemeManager::instance().setMode(
        themeName.compare(
            QStringLiteral("Dark"),
            Qt::CaseInsensitive) == 0
            ? ThemeMode::Dark
            : ThemeMode::Light);

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

    if (formatterSettings) {
        const QString profile =
            snapshot.value(
                QStringLiteral("formatter.profile")).toString();
        formatterSettings->setProfile(
            profile == QStringLiteral("indent_only")
                ? FormatterProfile::IndentOnly
                : FormatterProfile::Structured);
        formatterSettings->setFormatOnSaveEnabled(
            snapshot.value(
                QStringLiteral("formatter.formatOnSave")).toBool());
    }

    restoreWorkspaceSessionOnActivation =
        snapshot.value(
            QStringLiteral(
                "layout.restoreWorkspaceSession")).toBool();
    rememberWorkspacePanelState =
        snapshot.value(
            QStringLiteral(
                "layout.rememberPanelState")).toBool();

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
    else if (statusBar() && !shortcutIssues.isEmpty()) {
        statusBar()->showMessage(
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
    if (!settingsCenterPanel)
        return;
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
    editorCoordinator->setFormatterSettings(formatterSettings.get());
    editorCoordinator->setStatusMessageHandler(
        [this](const QString& message, int timeoutMs) {
            if (statusBar()) {
                if (message.isEmpty())
                    statusBar()->clearMessage();
                else
                    statusBar()->showMessage(message, timeoutMs);
            }
        });
    editorCoordinator->setModeStateHandler(
        [this](const EditorModeSnapshot& snapshot) {
            updateEditorModeChip(snapshot);
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
                    if (statusBar()) {
                        statusBar()->showMessage(
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
                if (statusBar()) {
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
                        statusBar()->showMessage(
                            message, 5000);
                    }
                }
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

void MainWindow::setupSemanticRuntime()
{
    semanticRuntime = std::make_unique<SemanticRuntimeCoordinator>(this);
}
