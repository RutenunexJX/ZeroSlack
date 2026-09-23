#include "uidialogs.h"
#include "rtlactioncoordinator.h"
#include "uicontrols.h"

#include "definitionservice.h"
#include "documentmodel.h"
#include "editorfileidentity.h"
#include "hierarchyservice.h"
#include "instancepairconnectionpanel.h"
#include "instancepairconnectionworkflow.h"
#include "multisignalpropagationpanel.h"
#include "mycodeeditor.h"
#include "notificationcenter.h"
#include "panellayoutcontroller.h"
#include "rtlhighriskeditpanel.h"
#include "saferenameservice.h"
#include "semanticdockcoordinator.h"
#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticrenamesupport.h"
#include "tabmanager.h"
#include "tsdocument.h"
#include "workspaceeditdocumentmanager.h"
#include "workspacemanager.h"

#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QSet>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace {
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
    return isSupportedSemanticRenameSubject(record);
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

bool applySingleFileSemanticRename(
    TabManager* tabs,
    const SafeRenameFileEdits& fileEdits,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!tabs || fileEdits.fileName.isEmpty()
        || fileEdits.edits.isEmpty()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The single-file rename plan is empty.");
        }
        return false;
    }
    DocumentModel* model = tabs->getDocumentModel();
    if (!model) {
        if (failureReason)
            *failureReason = QStringLiteral("The document model is unavailable.");
        return false;
    }
    MyCodeEditor* target =
        model->editorForFile(fileEdits.fileName);
    if (!target) {
        if (!tabs->openFileInTab(fileEdits.fileName)) {
            if (failureReason) {
                *failureReason = QStringLiteral(
                    "The rename target file could not be opened.");
            }
            return false;
        }
        target = model->editorForFile(fileEdits.fileName);
    }
    if (!target || target->isReadOnly()) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "The rename target document is read-only.");
        }
        return false;
    }

    QList<SafeRenameTextEdit> edits = fileEdits.edits;
    std::sort(edits.begin(), edits.end(),
              [](const SafeRenameTextEdit& left,
                 const SafeRenameTextEdit& right) {
        return left.startPosition > right.startPosition;
    });
    const QString& text = target->cachedDocumentText();
    int previousStart = text.size() + 1;
    for (const SafeRenameTextEdit& edit : edits) {
        if (!edit.isValid()
            || edit.startPosition + edit.length > text.size()
            || edit.startPosition + edit.length > previousStart
            || text.mid(edit.startPosition, edit.length)
                   != edit.oldText) {
            if (failureReason) {
                *failureReason = QStringLiteral(
                    "The document changed before the rename could be applied.");
            }
            return false;
        }
        previousStart = edit.startPosition;
    }

    {
        auto transaction =
            target->beginSynchronousEditTransaction();
        QTextCursor cursor(target->document());
        cursor.beginEditBlock();
        for (const SafeRenameTextEdit& edit : edits) {
            cursor.setPosition(edit.startPosition);
            cursor.setPosition(
                edit.startPosition + edit.length,
                QTextCursor::KeepAnchor);
            cursor.insertText(edit.newText);
        }
        cursor.endEditBlock();
    }
    tabs->updateTabTitle(target);
    return true;
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

    UiDialog dialog(parent);
    dialog.setObjectName(
        QStringLiteral("instancePairSelectionDialog"));
    dialog.setWindowTitle(
        QStringLiteral("Connect Instance Pair"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* leftCombo = UiControls::comboBox(&dialog);
    leftCombo->setObjectName(
        QStringLiteral("instancePairLeftSelection"));
    auto* rightCombo = UiControls::comboBox(&dialog);
    rightCombo->setObjectName(
        QStringLiteral("instancePairRightSelection"));
    auto* connectionEdit = UiControls::lineEdit(
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

    UiControls::addFormRow(form,
        QStringLiteral("Source instance"),
        leftCombo);
    UiControls::addFormRow(form,
        QStringLiteral("Destination instance"),
        rightCombo);
    UiControls::addFormRow(form,
        QStringLiteral("Connection identifier"),
        connectionEdit);
    layout->addLayout(form);
    auto* buttons = UiDialogs::buttonBox(
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

RtlActionCoordinator::RtlActionCoordinator(
    TabManager* tabManager,
    WorkspaceManager* workspaceManager,
    SemanticDockCoordinator* semanticDocks,
    PanelLayoutController* panelLayoutController,
    QWidget* dialogParent,
    RtlActionCoordinatorCallbacks callbacks,
    NotificationCenter* notificationCenter,
    QObject* parent)
    : QObject(parent)
    , tabManager(tabManager)
    , workspaceManager(workspaceManager)
    , semanticDocks(semanticDocks)
    , panelLayoutController(panelLayoutController)
    , dialogParent(dialogParent)
    , notificationCenter(notificationCenter)
    , callbacks(std::move(callbacks))
{
    connectPanelSignals();
}

bool RtlActionCoordinator::handlesRoute(
    const QString& executionRoute)
{
    return executionRoute
               == QStringLiteral("rtledit.rename")
        || executionRoute
               == QStringLiteral(
                   "rtledit.connection.transform")
        || executionRoute
               == QStringLiteral(
                   "rtledit.instancePair.connect")
        || executionRoute
               == QStringLiteral(
                   "rtledit.signal.propagateBatch");
}

ActionExecutionResult RtlActionCoordinator::execute(
    const ActionDescriptor& descriptor,
    const ActionInvocation& invocation)
{
    const QString& route = descriptor.executionRoute;
    if (route == QStringLiteral("rtledit.rename"))
        return executeRtlRenameAction(invocation);
    if (route
        == QStringLiteral(
            "rtledit.connection.transform")) {
        return executeRtlConnectionTransformAction(
            invocation);
    }
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

    ActionExecutionResult result;
    result.handled = false;
    result.failureReason =
        QStringLiteral(
            "RtlActionCoordinator does not own route %1.")
            .arg(route);
    return result;
}

void RtlActionCoordinator::connectActionAvailability(
    QMenu* menu,
    QAction* renameAction,
    QAction* connectionTransformAction,
    QAction* instancePairAction,
    QAction* multiSignalAction)
{
    if (!menu)
        return;
    connect(
        menu,
        &QMenu::aboutToShow,
        this,
        [this,
         renameAction,
         connectionTransformAction,
         instancePairAction,
         multiSignalAction]() {
            const bool workspaceAvailable =
                workspaceManager
                && workspaceManager->isWorkspaceOpen();
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
                    resolveContext(semanticContext);
                TSDocument syntax;
                syntax.setText(
                    semanticContext.documentText);
                const TSIdentifierTarget identifier =
                    syntax.identifierAt(
                        semanticContext.cursorPosition);
                cursorSignalAvailable = identifier.ok();
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
                            definition.symbolRecord);
                    QString ignoredFailure;
                    instanceSubjectAvailable =
                        resolveRtlInstanceSubject(
                            SemanticIndex::getInstance()
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
            availability.editorAvailable = editor != nullptr;
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
            const auto refreshAction =
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
                        state.executable && featureReady);
                    if (!featureReady)
                        action->setToolTip(featureReason);
                    else if (!state.executable)
                        action->setToolTip(state.reason);
                    else if (descriptor)
                        action->setToolTip(
                            descriptor->description);
                };
            refreshAction(
                renameAction,
                renameSubjectAvailable,
                QStringLiteral(
                    "Place the cursor on a supported SystemVerilog "
                    "declaration or bound reference."));
            refreshAction(
                connectionTransformAction,
                instanceSubjectAvailable,
                QStringLiteral(
                    "Place the cursor on one exact "
                    "module instance declaration."));
            refreshAction(
                instancePairAction,
                cursorSignalAvailable,
                QStringLiteral(
                    "Place the cursor on the source signal."));
            refreshAction(
                multiSignalAction,
                selectedSignalCount >= 2,
                QStringLiteral(
                    "Use Signal Selection to select at least two signals."));
        });
}

EditorActionContext RtlActionCoordinator::resolveContext(
    const EditorSemanticContext& context) const
{
    if (callbacks.resolveContext)
        return callbacks.resolveContext(context);

    EditorActionContext unresolved;
    unresolved.semanticState =
        EditorActionSemanticState::Unavailable;
    unresolved.semanticError =
        QStringLiteral(
            "The RTL action context resolver is unavailable.");
    return unresolved;
}

void RtlActionCoordinator::showPanel(
    const QString& panelId) const
{
    if (callbacks.showPanel) {
        callbacks.showPanel(panelId);
        return;
    }
    if (!semanticDocks)
        return;

    QDockWidget* dock = nullptr;
    if (panelId
        == RtlHighRiskEditPanelCoordinator::panelId()) {
        RtlHighRiskEditPanelCoordinator* coordinator =
            semanticDocks
                ->rtlHighRiskEditPanelCoordinator();
        dock = coordinator ? coordinator->dock() : nullptr;
    } else if (
        panelId
        == InstancePairConnectionCoordinator::panelId()) {
        dock = semanticDocks
                   ->instancePairConnectionDock();
    } else if (
        panelId
        == MultiSignalPropagationPanel::panelId()) {
        dock = semanticDocks
                   ->multiSignalPropagationDock();
    }
    if (!dock)
        return;

    if (panelLayoutController
        && panelLayoutController->isBottomPanel(dock)) {
        panelLayoutController->restorePanel(
            panelLayoutController->panelIdForDock(dock));
        return;
    }
    dock->show();
    dock->raise();
    dock->activateWindow();
}

void RtlActionCoordinator::connectPanelSignals()
{
    if (!semanticDocks)
        return;
    RtlHighRiskEditPanelCoordinator* rtlEdit =
        semanticDocks
            ->rtlHighRiskEditPanelCoordinator();
    if (!rtlEdit)
        return;

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
                ? RtlRenameWorkflow::actionFamilyId()
                : workflowActionId;
            const ActionDescriptor* descriptor =
                findActionById(actionId);
            if (!descriptor)
                return;
            ActionInvocation invocation;
            invocation.workspaceId =
                workspaceManager
                ? workspaceManager->getWorkspacePath()
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
            result.resolvedParameters = parameters;
            applicationActionExecutionHistory()
                .recordSuccessful(
                    *descriptor,
                    invocation,
                    result);
        });
    connect(
        rtlEdit,
        &RtlHighRiskEditPanelCoordinator::stateChanged,
        this,
        [this](
            const RtlHighRiskEditPanelOutcome& outcome) {
            if (callbacks.refreshSemanticDocuments
                && (outcome.panelState
                        == RtlHighRiskEditPanelState::Applied
                    || outcome.panelState
                        == RtlHighRiskEditPanelState::Undone)) {
                QStringList fileNames;
                for (const rtledit::SourceDiffFile& file :
                     outcome.sourceDiff.files) {
                    const QString fileName =
                        rtlActionFromUtf8(file.filePath);
                    if (!fileName.isEmpty()
                        && !fileNames.contains(fileName,
                                               Qt::CaseInsensitive)) {
                        fileNames.append(fileName);
                    }
                }
                if (!fileNames.isEmpty())
                    callbacks.refreshSemanticDocuments(fileNames);
            }
            if (!notificationCenter)
                return;
            const QString actionId =
                outcome.actionId.startsWith(
                    QStringLiteral("rtl.rename"))
                ? RtlRenameWorkflow::actionFamilyId()
                : outcome.actionId;
            const QString workspaceId =
                workspaceManager
                ? workspaceManager->getWorkspacePath()
                : QString();
            const QString key =
                QStringLiteral("rtl-edit:%1:%2")
                    .arg(
                        actionId.isEmpty()
                            ? QStringLiteral("unknown")
                            : actionId,
                        workspaceId);
            if (!rtlTransactionFailureNeedsNotification(
                    outcome)) {
                if (outcome.panelState
                        == RtlHighRiskEditPanelState::Applied
                    || outcome.panelState
                        == RtlHighRiskEditPanelState::Undone
                    || outcome.panelState
                        == RtlHighRiskEditPanelState::Cancelled) {
                    notificationCenter->dismissByKey(key);
                }
                return;
            }
            NotificationDraft draft;
            draft.key = key;
            draft.topic =
                NotificationTopic::TransactionConflict;
            draft.severity =
                outcome.panelState
                    == RtlHighRiskEditPanelState::Conflict
                ? NotificationSeverity::Critical
                : NotificationSeverity::Error;
            draft.source =
                QStringLiteral("RtlHighRiskEdit");
            draft.message =
                outcome.message.isEmpty()
                ? QStringLiteral(
                      "The RTL edit transaction failed.")
                : outcome.message;
            notificationCenter->post(draft);
        });
}

ActionExecutionResult
RtlActionCoordinator::executeRtlRenameAction(
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
    const QString requestedNewName =
        invocation.parameters
            .value(QStringLiteral("newName"))
            .toString()
            .trimmed();
    if (requestedNewName.isEmpty()) {
        QString popupFailure;
        if (!editor->beginSemanticRename(&popupFailure)) {
            return fail(
                popupFailure.isEmpty()
                    ? QStringLiteral(
                          "The semantic rename popup could not be opened.")
                    : popupFailure);
        }
        result.succeeded = true;
        result.message = QStringLiteral(
            "Semantic rename editor opened at the caret.");
        return result;
    }
    EditorSemanticContext context =
        editor->editorSemanticContextForPosition(
            -1, true);
    const EditorActionContext actionContext =
        resolveContext(context);
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
            "Place the cursor on a supported SystemVerilog identifier."));
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
            "The selected Slang symbol kind cannot be renamed safely."));
    }
    const QString expectedStableKey =
        invocation.parameters
            .value(QStringLiteral("subjectStableKey"))
            .toString();
    if (!expectedStableKey.isEmpty()
        && expectedStableKey
               != definition.symbolRecord.stableKey.toString()) {
        return fail(QStringLiteral(
            "The selected symbol changed while the rename popup was open."));
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

    if (!isStructuralSemanticRenameSubject(
            definition.symbolRecord)) {
        SafeRenamePlanQuery renameQuery;
        renameQuery.symbolName = definition.symbolRecord.name;
        renameQuery.newName = requestedNewName;
        renameQuery.fileName = context.fileName;
        renameQuery.moduleName = context.moduleName;
        renameQuery.documentText = context.documentText;
        renameQuery.cursorPosition = identifier.startChar;
        for (auto it = capturedDocuments.constBegin();
             it != capturedDocuments.constEnd(); ++it) {
            renameQuery.openFileContents.insert(
                it.value().fileName,
                it.value().text);
        }
        const SafeRenamePlan safePlan =
            SafeRenameService(SemanticIndex::getInstance())
                .createRenamePlan(renameQuery);
        if (!safePlan.isReady()
            || safePlan.subjectStableKey
                   != definition.symbolRecord.stableKey) {
            return fail(
                safePlan.message.isEmpty()
                    ? QStringLiteral(
                          "The semantic rename plan could not be proven.")
                    : safePlan.message);
        }
        if (safePlan.fileEdits.size() == 1) {
            QString applyFailure;
            if (!applySingleFileSemanticRename(
                    tabManager,
                    safePlan.fileEdits.constFirst(),
                    &applyFailure)) {
                return fail(applyFailure);
            }
            if (callbacks.refreshSemanticDocuments) {
                callbacks.refreshSemanticDocuments(
                    {safePlan.fileEdits.constFirst().fileName});
            }
            result.succeeded = true;
            result.message = QStringLiteral(
                "Renamed %1 reference(s) in one undoable file edit.")
                    .arg(safePlan.editCount());
            result.output.insert(
                QStringLiteral("editCount"),
                safePlan.editCount());
            result.output.insert(
                QStringLiteral("fileCount"), 1);
            return result;
        }
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
        requestedNewName;

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

    const bool applyOnEnter =
        invocation.parameters
            .value(QStringLiteral("applyOnEnter"))
            .toBool();
    if (applyOnEnter && !result.dryRun) {
        const std::uint64_t sessionId =
            coordinator->activeSessionId();
        const RtlHighRiskEditPanelOutcome preview =
            coordinator->requestPreview(sessionId);
        if (preview.panelState
            != RtlHighRiskEditPanelState::PreviewReady) {
            panelLayoutController->restorePanel(
                RtlHighRiskEditPanelCoordinator::panelId());
            return fail(
                preview.message.isEmpty()
                    ? QStringLiteral(
                          "The exact RTL rename preview could not be prepared.")
                    : preview.message);
        }
        const RtlHighRiskEditPanelOutcome applied =
            coordinator->confirm(sessionId);
        if (applied.panelState
            != RtlHighRiskEditPanelState::Applied) {
            panelLayoutController->restorePanel(
                RtlHighRiskEditPanelCoordinator::panelId());
            return fail(
                applied.message.isEmpty()
                    ? QStringLiteral(
                          "The exact RTL rename transaction was not applied.")
                    : applied.message);
        }
        result.succeeded = true;
        result.message = QStringLiteral(
            "Renamed %1 reference(s) in %2 file(s).")
                .arg(applied.editCount)
                .arg(applied.fileCount);
        result.output.insert(QStringLiteral("editCount"),
                             applied.editCount);
        result.output.insert(QStringLiteral("fileCount"),
                             applied.fileCount);
        result.output.insert(
            QStringLiteral("sessionId"),
            QVariant::fromValue<qulonglong>(sessionId));
        return result;
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
RtlActionCoordinator::executeRtlConnectionTransformAction(
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
        resolveContext(context);
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
                true).toBool();
    session.baseRequest.removeUnknownPorts =
        parameters.value(
                QStringLiteral("removeUnknownPorts"),
                true).toBool();
    session.baseRequest.synchronizeAllInstances =
        parameters.value(
                QStringLiteral("synchronizeAllInstances"),
                true).toBool();
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
RtlActionCoordinator::executeInstancePairConnectionAction(
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
                ->instancePairConnectionWorkflow()
        || !semanticDocks
                ->rtlActionDocumentManager()) {
        return fail(QStringLiteral(
            "The instance-pair workspace workflow is unavailable."));
    }
    if (semanticDocks->instancePairConnectionWorkflow()
            ->canUndoAppliedTransaction()) {
        showPanel(
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
        resolveContext(sourceContext);
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

    QHash<QString, InstancePairDocumentSnapshot>
        captured;
    QString captureFailure;
    if (!captureRtlActionDocuments(
            workspaceFiles,
            semanticToken,
            *semanticDocks
                 ->rtlActionDocumentManager(),
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
                dialogParent,
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

    showPanel(
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
RtlActionCoordinator::executeMultiSignalPropagationAction(
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
                ->multiSignalPropagationWorkflow()
        || !semanticDocks
                ->rtlActionDocumentManager()) {
        return fail(QStringLiteral(
            "The multi-signal workspace workflow is unavailable."));
    }
    if (semanticDocks->multiSignalPropagationWorkflow()
            ->canUndoAppliedTransaction()) {
        showPanel(
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
        resolveContext(editorContext);
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
    QHash<QString, MultiSignalPropagationDocumentSnapshot>
        captured;
    QString captureFailure;
    if (!captureRtlActionDocuments(
            workspaceFiles,
            semanticToken,
            *semanticDocks
                 ->rtlActionDocumentManager(),
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
    showPanel(
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
