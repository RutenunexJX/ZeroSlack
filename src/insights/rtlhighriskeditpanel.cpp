#include "uitypography.h"
#include "rtlhighriskeditpanel.h"
#include "deferredpanel.h"

#include "workspaceedittransactionservice.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

QString fromUtf8(const std::string& text)
{
    return QString::fromUtf8(
        text.data(), static_cast<qsizetype>(text.size()));
}

QString sourceLinePrefix(
    rtledit::SourceDiffLineKind kind)
{
    switch (kind) {
    case rtledit::SourceDiffLineKind::Added:
        return QStringLiteral("+");
    case rtledit::SourceDiffLineKind::Removed:
        return QStringLiteral("-");
    case rtledit::SourceDiffLineKind::Context:
        return QStringLiteral(" ");
    }
    return QStringLiteral(" ");
}

QString panelStateText(RtlHighRiskEditPanelState state)
{
    switch (state) {
    case RtlHighRiskEditPanelState::Empty:
        return QStringLiteral("No RTL edit session.");
    case RtlHighRiskEditPanelState::Editing:
        return QStringLiteral("Edit the structured request, then preview.");
    case RtlHighRiskEditPanelState::Preparing:
        return QStringLiteral("Preparing Change Preview preview.");
    case RtlHighRiskEditPanelState::PreviewReady:
        return QStringLiteral("Change Preview preview is ready.");
    case RtlHighRiskEditPanelState::DryRunPreviewReady:
        return QStringLiteral("Dry-run Change Preview preview is ready.");
    case RtlHighRiskEditPanelState::Confirming:
        return QStringLiteral("Checking conflicts and confirming.");
    case RtlHighRiskEditPanelState::Applied:
        return QStringLiteral("The atomic RTL edit was applied.");
    case RtlHighRiskEditPanelState::DryRunComplete:
        return QStringLiteral("Dry-run validation completed without writes.");
    case RtlHighRiskEditPanelState::Cancelled:
        return QStringLiteral("The pending preview was cancelled.");
    case RtlHighRiskEditPanelState::Undone:
        return QStringLiteral("The complete RTL edit was undone.");
    case RtlHighRiskEditPanelState::NoChanges:
        return QStringLiteral("The request produces no changes.");
    case RtlHighRiskEditPanelState::Rejected:
        return QStringLiteral("The RTL edit request was rejected.");
    case RtlHighRiskEditPanelState::Conflict:
        return QStringLiteral("The RTL edit is in conflict.");
    }
    return QStringLiteral("Unknown RTL edit state.");
}

} // namespace

RtlHighRiskEditPanel::RtlHighRiskEditPanel(
    QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    clearSession();
}

void RtlHighRiskEditPanel::setupUi()
{
    setObjectName(QStringLiteral("rtlHighRiskEditPanel"));
    setFocusPolicy(Qt::NoFocus);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(7);

    auto* heading = new QLabel(
        QStringLiteral("RTL Change Preview Edit"), this);
    heading->setObjectName(
        QStringLiteral("rtlHighRiskEditHeading"));
    QFont headingFont = UiTypography::font(UiTypography::Role::PanelTitle);
    heading->setFont(headingFont);
    root->addWidget(heading);

    stateLabel = new QLabel(this);
    stateLabel->setObjectName(
        QStringLiteral("rtlHighRiskEditState"));
    stateLabel->setWordWrap(true);
    root->addWidget(stateLabel);

    inputStack = new QStackedWidget(this);
    inputStack->setObjectName(
        QStringLiteral("rtlHighRiskEditInputStack"));

    emptyInputPage = new QWidget(inputStack);
    auto* emptyLayout = new QVBoxLayout(emptyInputPage);
    emptyLayout->addWidget(
        new QLabel(
            QStringLiteral(
                "Invoke an RTL rename or connection transform "
                "action to populate this page."),
            emptyInputPage));
    emptyLayout->addStretch(1);
    inputStack->addWidget(emptyInputPage);

    renameInputPage = new QGroupBox(
        QStringLiteral("Rename request"), inputStack);
    auto* renameForm = new QFormLayout(renameInputPage);
    renameSubjectLabel = new QLabel(renameInputPage);
    renameSubjectLabel->setObjectName(
        QStringLiteral("rtlHighRiskRenameSubject"));
    renameSubjectLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    renameForm->addRow(
        QStringLiteral("Semantic target"),
        renameSubjectLabel);
    renameOldNameLabel = new QLabel(renameInputPage);
    renameOldNameLabel->setObjectName(
        QStringLiteral("rtlHighRiskRenameOldName"));
    renameOldNameLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    renameForm->addRow(
        QStringLiteral("Current name"),
        renameOldNameLabel);
    renameNewNameEdit = new QLineEdit(renameInputPage);
    renameNewNameEdit->setObjectName(
        QStringLiteral("rtlHighRiskRenameNewName"));
    renameNewNameEdit->setPlaceholderText(
        QStringLiteral("SystemVerilog identifier"));
    renameForm->addRow(
        QStringLiteral("New name"),
        renameNewNameEdit);
    inputStack->addWidget(renameInputPage);

    connectionInputPage = new QGroupBox(
        QStringLiteral("Connection transform request"),
        inputStack);
    auto* connectionForm =
        new QFormLayout(connectionInputPage);
    connectionInstanceLabel =
        new QLabel(connectionInputPage);
    connectionInstanceLabel->setObjectName(
        QStringLiteral(
            "rtlHighRiskConnectionInstance"));
    connectionInstanceLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    connectionForm->addRow(
        QStringLiteral("Semantic instance"),
        connectionInstanceLabel);
    convertOrderedCheck = new QCheckBox(
        QStringLiteral("Convert ordered associations to named"),
        connectionInputPage);
    convertOrderedCheck->setObjectName(
        QStringLiteral(
            "rtlHighRiskConvertOrdered"));
    connectionForm->addRow(QString(), convertOrderedCheck);
    addMissingPortsCheck = new QCheckBox(
        QStringLiteral("Add missing formal ports"),
        connectionInputPage);
    addMissingPortsCheck->setObjectName(
        QStringLiteral(
            "rtlHighRiskAddMissingPorts"));
    connectionForm->addRow(QString(), addMissingPortsCheck);
    removeUnknownPortsCheck = new QCheckBox(
        QStringLiteral("Remove connections to deleted formal ports"),
        connectionInputPage);
    removeUnknownPortsCheck->setObjectName(
        QStringLiteral("rtlHighRiskRemoveUnknownPorts"));
    connectionForm->addRow(QString(), removeUnknownPortsCheck);
    synchronizeAllInstancesCheck = new QCheckBox(
        QStringLiteral("Apply to every source instance of this module"),
        connectionInputPage);
    synchronizeAllInstancesCheck->setObjectName(
        QStringLiteral("rtlHighRiskSynchronizeAllInstances"));
    connectionForm->addRow(QString(), synchronizeAllInstancesCheck);

    missingPortPolicyCombo =
        new QComboBox(connectionInputPage);
    missingPortPolicyCombo->setObjectName(
        QStringLiteral(
            "rtlHighRiskMissingPortPolicy"));
    missingPortPolicyCombo->addItem(
        QStringLiteral("Leave unconnected"),
        static_cast<int>(
            RtlMissingPortConnectionPolicy::
                LeaveUnconnected));
    missingPortPolicyCombo->addItem(
        QStringLiteral("Connect same-named signal"),
        static_cast<int>(
            RtlMissingPortConnectionPolicy::
                ConnectSameNamedSignal));
    connectionForm->addRow(
        QStringLiteral("Missing ports"),
        missingPortPolicyCombo);

    castPolicyCombo = new QComboBox(connectionInputPage);
    castPolicyCombo->setObjectName(
        QStringLiteral("rtlHighRiskCastPolicy"));
    castPolicyCombo->addItem(
        QStringLiteral("Preserve existing expression"),
        static_cast<int>(
            RtlExplicitCastPolicy::
                PreserveExistingExpression));
    castPolicyCombo->addItem(
        QStringLiteral("Insert provable required cast"),
        static_cast<int>(
            RtlExplicitCastPolicy::
                InsertWhenRequired));
    connectionForm->addRow(
        QStringLiteral("Width/signed cast"),
        castPolicyCombo);
    inputStack->addWidget(connectionInputPage);
    root->addWidget(inputStack);

    auto* summaryRow = new QHBoxLayout();
    transactionSummary = new QLabel(this);
    transactionSummary->setObjectName(
        QStringLiteral(
            "rtlHighRiskTransactionSummary"));
    transactionSummary->setWordWrap(true);
    tokenFingerprintLabel = new QLabel(this);
    tokenFingerprintLabel->setObjectName(
        QStringLiteral(
            "rtlHighRiskConfirmationFingerprint"));
    tokenFingerprintLabel->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    summaryRow->addWidget(transactionSummary, 1);
    summaryRow->addWidget(tokenFingerprintLabel);
    root->addLayout(summaryRow);

    conflictLabel = new QLabel(this);
    conflictLabel->setObjectName(
        QStringLiteral("rtlHighRiskConflict"));
    conflictLabel->setWordWrap(true);
    conflictLabel->setVisible(false);
    root->addWidget(conflictLabel);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(
        QStringLiteral("rtlHighRiskDiffSplitter"));
    diffTree = new QTreeWidget(splitter);
    diffTree->setObjectName(
        QStringLiteral("rtlHighRiskDiffTree"));
    diffTree->setColumnCount(3);
    diffTree->setHeaderLabels(
        {QStringLiteral("File / Hunk"),
         QStringLiteral("Revision"),
         QStringLiteral("Edits")});
    diffTree->setSelectionMode(
        QAbstractItemView::SingleSelection);
    diffTree->header()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    diffTree->header()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    diffTree->header()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    splitter->addWidget(diffTree);

    diffView = new QPlainTextEdit(splitter);
    diffView->setObjectName(
        QStringLiteral("rtlHighRiskDiffView"));
    diffView->setReadOnly(true);
    diffView->setLineWrapMode(
        QPlainTextEdit::NoWrap);
    diffView->setFont(
        QFontDatabase::systemFont(
            QFontDatabase::FixedFont));
    diffView->setPlaceholderText(
        QStringLiteral(
            "Structured file and hunk changes will appear here."));
    splitter->addWidget(diffView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    root->addWidget(splitter, 1);

    auto* actionRow = new QHBoxLayout();
    previewButton = new QPushButton(
        QStringLiteral("Preview Change Preview"), this);
    previewButton->setObjectName(
        QStringLiteral("rtlHighRiskPreviewButton"));
    confirmButton = new QPushButton(
        QStringLiteral("Apply Confirmed Plan"), this);
    confirmButton->setObjectName(
        QStringLiteral("rtlHighRiskConfirmButton"));
    cancelButton = new QPushButton(
        QStringLiteral("Cancel Preview"), this);
    cancelButton->setObjectName(
        QStringLiteral("rtlHighRiskCancelButton"));
    undoButton = new QPushButton(
        QStringLiteral("Undo Applied Plan"), this);
    undoButton->setObjectName(
        QStringLiteral("rtlHighRiskUndoButton"));
    actionRow->addStretch(1);
    actionRow->addWidget(previewButton);
    actionRow->addWidget(confirmButton);
    actionRow->addWidget(cancelButton);
    actionRow->addWidget(undoButton);
    root->addLayout(actionRow);

    connect(
        renameNewNameEdit,
        &QLineEdit::textChanged,
        this,
        [this](const QString&) {
            emitDraftChanged();
        });
    connect(
        convertOrderedCheck,
        &QCheckBox::toggled,
        this,
        [this](bool) {
            emitDraftChanged();
        });
    connect(
        addMissingPortsCheck,
        &QCheckBox::toggled,
        this,
        [this](bool) {
            emitDraftChanged();
        });
    connect(
        removeUnknownPortsCheck,
        &QCheckBox::toggled,
        this,
        [this](bool) {
            emitDraftChanged();
        });
    connect(
        synchronizeAllInstancesCheck,
        &QCheckBox::toggled,
        this,
        [this](bool) {
            emitDraftChanged();
        });
    connect(
        missingPortPolicyCombo,
        qOverload<int>(
            &QComboBox::currentIndexChanged),
        this,
        [this](int) {
            emitDraftChanged();
        });
    connect(
        castPolicyCombo,
        qOverload<int>(
            &QComboBox::currentIndexChanged),
        this,
        [this](int) {
            emitDraftChanged();
        });
    connect(
        previewButton,
        &QPushButton::clicked,
        this,
        [this]() {
            emit previewRequested(currentSessionId);
        });
    connect(
        confirmButton,
        &QPushButton::clicked,
        this,
        [this]() {
            emit confirmRequested(currentSessionId);
        });
    connect(
        cancelButton,
        &QPushButton::clicked,
        this,
        [this]() {
            emit cancelRequested(currentSessionId);
        });
    connect(
        undoButton,
        &QPushButton::clicked,
        this,
        [this]() {
            emit undoRequested(currentSessionId);
        });
    connect(
        diffTree,
        &QTreeWidget::currentItemChanged,
        this,
        [this](
            QTreeWidgetItem* current,
            QTreeWidgetItem*) {
            renderDiffSelection(current);
        });
}

void RtlHighRiskEditPanel::beginRename(
    std::uint64_t sessionIdValue,
    const QString& subject,
    const QString& oldName,
    const QString& suggestedNewName,
    bool dryRun)
{
    rebuilding = true;
    currentKind = RtlHighRiskEditKind::Rename;
    currentSessionId = sessionIdValue;
    renameSubjectLabel->setText(subject);
    renameOldNameLabel->setText(oldName);
    renameNewNameEdit->setText(suggestedNewName);
    inputStack->setCurrentWidget(renameInputPage);
    rebuilding = false;

    RtlHighRiskEditPanelOutcome outcome;
    outcome.panelState =
        RtlHighRiskEditPanelState::Editing;
    outcome.actionId =
        RtlRenameWorkflow::actionFamilyId();
    outcome.message = panelStateText(outcome.panelState);
    outcome.dryRun = dryRun;
    outcome.canPreview = true;
    presentOutcome(outcome);
}

void RtlHighRiskEditPanel::beginConnectionTransform(
    std::uint64_t sessionIdValue,
    const QString& instance,
    const RtlConnectionTransformRequest& defaults,
    bool dryRun)
{
    rebuilding = true;
    currentKind =
        RtlHighRiskEditKind::ConnectionTransform;
    currentSessionId = sessionIdValue;
    connectionInstanceLabel->setText(instance);
    convertOrderedCheck->setChecked(
        defaults.convertOrderedToNamed);
    addMissingPortsCheck->setChecked(
        defaults.addMissingPorts);
    removeUnknownPortsCheck->setChecked(
        defaults.removeUnknownPorts);
    synchronizeAllInstancesCheck->setChecked(
        defaults.synchronizeAllInstances);
    missingPortPolicyCombo->setCurrentIndex(
        qMax(
            0,
            missingPortPolicyCombo->findData(
                static_cast<int>(
                    defaults.missingPortPolicy))));
    castPolicyCombo->setCurrentIndex(
        qMax(
            0,
            castPolicyCombo->findData(
                static_cast<int>(
                    defaults.castPolicy))));
    inputStack->setCurrentWidget(connectionInputPage);
    rebuilding = false;

    RtlHighRiskEditPanelOutcome outcome;
    outcome.panelState =
        RtlHighRiskEditPanelState::Editing;
    outcome.actionId =
        RtlConnectionTransformWorkflow::actionId();
    outcome.message = panelStateText(outcome.panelState);
    outcome.dryRun = dryRun;
    outcome.canPreview = true;
    presentOutcome(outcome);
}

void RtlHighRiskEditPanel::presentOutcome(
    const RtlHighRiskEditPanelOutcome& outcome)
{
    currentState = outcome.panelState;
    stateLabel->setText(
        outcome.message.isEmpty()
            ? panelStateText(outcome.panelState)
            : outcome.message);

    if (outcome.hasStructuredPreview) {
        transactionSummary->setText(
            QStringLiteral(
                "High risk · Diff preview · %1 file(s) · "
                "%2 edit(s)%3")
                .arg(outcome.fileCount)
                .arg(outcome.editCount)
                .arg(
                    outcome.dryRun
                        ? QStringLiteral(" · Dry run")
                        : QString()));
        rebuildDiff(outcome.sourceDiff);
    } else {
        transactionSummary->setText(
            outcome.dryRun
                ? QStringLiteral(
                      "High risk · Diff required · Dry run")
                : QStringLiteral(
                      "High risk · Diff required"));
        clearDiff();
    }

    tokenFingerprintLabel->setText(
        outcome.confirmationFingerprint.isEmpty()
            ? QString()
            : QStringLiteral("Confirmation %1")
                  .arg(
                      outcome.confirmationFingerprint));
    conflictLabel->setVisible(
        outcome.panelState
            == RtlHighRiskEditPanelState::Conflict);
    conflictLabel->setText(
        outcome.panelState
                == RtlHighRiskEditPanelState::Conflict
            ? (outcome.conflictFile.isEmpty()
                   ? QStringLiteral(
                         "Conflict: rebuild the preview from "
                         "the current workspace state.")
                   : QStringLiteral("Conflict file: %1")
                         .arg(outcome.conflictFile))
            : QString());
    updateInputAvailability(outcome);
}

void RtlHighRiskEditPanel::clearSession()
{
    rebuilding = true;
    currentKind = RtlHighRiskEditKind::None;
    currentState = RtlHighRiskEditPanelState::Empty;
    currentSessionId = 0;
    renameSubjectLabel->clear();
    renameOldNameLabel->clear();
    renameNewNameEdit->clear();
    connectionInstanceLabel->clear();
    inputStack->setCurrentWidget(emptyInputPage);
    rebuilding = false;

    RtlHighRiskEditPanelOutcome outcome;
    outcome.message = panelStateText(outcome.panelState);
    presentOutcome(outcome);
}

RtlHighRiskEditDraft RtlHighRiskEditPanel::draft() const
{
    RtlHighRiskEditDraft value;
    value.sessionId = currentSessionId;
    value.kind = currentKind;
    value.newName =
        renameNewNameEdit->text().trimmed();
    value.convertOrderedToNamed =
        convertOrderedCheck->isChecked();
    value.addMissingPorts =
        addMissingPortsCheck->isChecked();
    value.removeUnknownPorts =
        removeUnknownPortsCheck->isChecked();
    value.synchronizeAllInstances =
        synchronizeAllInstancesCheck->isChecked();
    value.missingPortPolicy =
        static_cast<RtlMissingPortConnectionPolicy>(
            missingPortPolicyCombo
                ->currentData().toInt());
    value.castPolicy =
        static_cast<RtlExplicitCastPolicy>(
            castPolicyCombo->currentData().toInt());
    return value;
}

RtlHighRiskEditKind
RtlHighRiskEditPanel::editKind() const
{
    return currentKind;
}

RtlHighRiskEditPanelState
RtlHighRiskEditPanel::state() const
{
    return currentState;
}

std::uint64_t RtlHighRiskEditPanel::sessionId() const
{
    return currentSessionId;
}

QString RtlHighRiskEditPanel::statusText() const
{
    return stateLabel ? stateLabel->text() : QString();
}

QString
RtlHighRiskEditPanel::confirmationFingerprint() const
{
    return tokenFingerprintLabel
        ? tokenFingerprintLabel->text()
        : QString();
}

int RtlHighRiskEditPanel::diffFileCount() const
{
    return diffTree
        ? diffTree->topLevelItemCount() : 0;
}

int RtlHighRiskEditPanel::diffHunkCount() const
{
    if (!diffTree)
        return 0;
    int count = 0;
    for (int index = 0;
         index < diffTree->topLevelItemCount();
         ++index) {
        count +=
            diffTree->topLevelItem(index)
                ->childCount();
    }
    return count;
}

QString RtlHighRiskEditPanel::displayedDiffText() const
{
    return diffView
        ? diffView->toPlainText() : QString();
}

void RtlHighRiskEditPanel::setRenameNewName(
    const QString& name)
{
    renameNewNameEdit->setText(name);
}

void RtlHighRiskEditPanel::setConnectionOptions(
    bool convertOrderedToNamed,
    bool addMissingPorts,
    RtlMissingPortConnectionPolicy missingPortPolicy,
    RtlExplicitCastPolicy castPolicy,
    bool removeUnknownPorts,
    bool synchronizeAllInstances)
{
    const RtlHighRiskEditDraft before = draft();
    rebuilding = true;
    convertOrderedCheck->setChecked(
        convertOrderedToNamed);
    addMissingPortsCheck->setChecked(addMissingPorts);
    removeUnknownPortsCheck->setChecked(removeUnknownPorts);
    synchronizeAllInstancesCheck->setChecked(
        synchronizeAllInstances);
    missingPortPolicyCombo->setCurrentIndex(
        qMax(
            0,
            missingPortPolicyCombo->findData(
                static_cast<int>(
                    missingPortPolicy))));
    castPolicyCombo->setCurrentIndex(
        qMax(
            0,
            castPolicyCombo->findData(
                static_cast<int>(castPolicy))));
    rebuilding = false;
    const RtlHighRiskEditDraft after = draft();
    if (before.convertOrderedToNamed
            != after.convertOrderedToNamed
        || before.addMissingPorts
            != after.addMissingPorts
        || before.removeUnknownPorts
            != after.removeUnknownPorts
        || before.synchronizeAllInstances
            != after.synchronizeAllInstances
        || before.missingPortPolicy
            != after.missingPortPolicy
        || before.castPolicy != after.castPolicy) {
        emitDraftChanged();
    }
}

void RtlHighRiskEditPanel::emitDraftChanged()
{
    if (!rebuilding
        && currentKind != RtlHighRiskEditKind::None
        && currentSessionId != 0) {
        emit draftChanged(currentSessionId);
    }
}

void RtlHighRiskEditPanel::clearDiff()
{
    currentSourceDiff =
        rtledit::WorkspaceEditSourceDiff{};
    if (diffTree)
        diffTree->clear();
    if (diffView)
        diffView->clear();
}

void RtlHighRiskEditPanel::rebuildDiff(
    const rtledit::WorkspaceEditSourceDiff& diff)
{
    currentSourceDiff = diff;
    diffTree->clear();
    for (int fileIndex = 0;
         fileIndex
             < static_cast<int>(diff.files.size());
         ++fileIndex) {
        const rtledit::SourceDiffFile& file =
            diff.files[static_cast<std::size_t>(
                fileIndex)];
        auto* fileItem = new QTreeWidgetItem(
            diffTree,
            {fromUtf8(file.filePath),
             QString::number(file.version.value),
             QString::number(file.editCount)});
        fileItem->setData(
            0, Qt::UserRole, fileIndex);
        fileItem->setData(
            0, Qt::UserRole + 1, -1);
        for (int hunkIndex = 0;
             hunkIndex
                 < static_cast<int>(
                     file.hunks.size());
             ++hunkIndex) {
            const rtledit::SourceDiffHunk& hunk =
                file.hunks[
                    static_cast<std::size_t>(
                        hunkIndex)];
            auto* hunkItem = new QTreeWidgetItem(
                fileItem,
                {QStringLiteral(
                     "@@ -%1,%2 +%3,%4 @@")
                     .arg(hunk.oldStartLine)
                     .arg(hunk.oldLineCount)
                     .arg(hunk.newStartLine)
                     .arg(hunk.newLineCount),
                 QString(),
                 QString::number(
                     hunk.lines.size())});
            hunkItem->setData(
                0, Qt::UserRole, fileIndex);
            hunkItem->setData(
                0, Qt::UserRole + 1, hunkIndex);
        }
        fileItem->setExpanded(true);
    }
    if (diffTree->topLevelItemCount() > 0) {
        QTreeWidgetItem* first =
            diffTree->topLevelItem(0);
        diffTree->setCurrentItem(
            first->childCount() > 0
                ? first->child(0)
                : first);
    } else {
        diffView->clear();
    }
}

void RtlHighRiskEditPanel::renderDiffSelection(
    QTreeWidgetItem* item)
{
    if (!item) {
        diffView->clear();
        return;
    }
    const int fileIndex =
        item->data(0, Qt::UserRole).toInt();
    const int hunkIndex =
        item->data(0, Qt::UserRole + 1).toInt();
    if (fileIndex < 0
        || fileIndex
            >= static_cast<int>(
                currentSourceDiff.files.size())) {
        diffView->clear();
        return;
    }
    const rtledit::SourceDiffFile& file =
        currentSourceDiff.files[
            static_cast<std::size_t>(fileIndex)];
    if (hunkIndex >= 0) {
        diffView->setPlainText(
            renderHunk(file, hunkIndex));
        return;
    }
    QStringList rendered;
    for (int index = 0;
         index
             < static_cast<int>(file.hunks.size());
         ++index) {
        rendered.append(renderHunk(file, index));
    }
    diffView->setPlainText(
        rendered.join(QStringLiteral("\n")));
}

void RtlHighRiskEditPanel::updateInputAvailability(
    const RtlHighRiskEditPanelOutcome& outcome)
{
    const bool inputEnabled =
        outcome.panelState
            != RtlHighRiskEditPanelState::Empty
        && outcome.panelState
            != RtlHighRiskEditPanelState::Preparing
        && outcome.panelState
            != RtlHighRiskEditPanelState::Confirming
        && outcome.panelState
            != RtlHighRiskEditPanelState::Applied;
    renameInputPage->setEnabled(inputEnabled);
    connectionInputPage->setEnabled(inputEnabled);
    previewButton->setEnabled(outcome.canPreview);
    confirmButton->setEnabled(outcome.canConfirm);
    confirmButton->setText(
        outcome.dryRun
            ? QStringLiteral("Validate Dry Run")
            : QStringLiteral("Apply Confirmed Plan"));
    cancelButton->setEnabled(outcome.canCancel);
    undoButton->setEnabled(outcome.canUndo);
}

QString RtlHighRiskEditPanel::renderHunk(
    const rtledit::SourceDiffFile& file,
    int hunkIndex)
{
    if (hunkIndex < 0
        || hunkIndex
            >= static_cast<int>(file.hunks.size())) {
        return QString();
    }
    const rtledit::SourceDiffHunk& hunk =
        file.hunks[
            static_cast<std::size_t>(hunkIndex)];
    QStringList lines;
    lines.append(
        QStringLiteral("--- %1")
            .arg(fromUtf8(file.filePath)));
    lines.append(
        QStringLiteral(
            "@@ -%1,%2 +%3,%4 @@")
            .arg(hunk.oldStartLine)
            .arg(hunk.oldLineCount)
            .arg(hunk.newStartLine)
            .arg(hunk.newLineCount));
    for (const rtledit::SourceDiffLine& line :
         hunk.lines) {
        lines.append(
            sourceLinePrefix(line.kind)
            + fromUtf8(line.text));
    }
    return lines.join(QLatin1Char('\n'));
}

RtlHighRiskEditPanelCoordinator::
RtlHighRiskEditPanelCoordinator(
    QWidget* dockParent,
    SemanticIndex* semanticIndex,
    rtledit::WorkspaceDocumentManager* documents,
    WorkspaceEditTransactionService* transactions,
    QObject* parent)
    : QObject(parent ? parent : dockParent)
    , ownedTransactionService(
          transactions
              ? nullptr
              : std::make_unique<
                    WorkspaceEditTransactionService>())
{
    WorkspaceEditTransactionService* sharedTransactions =
        transactions
        ? transactions
        : ownedTransactionService.get();
    rename = std::make_unique<RtlRenameWorkflow>(
        semanticIndex,
        documents,
        sharedTransactions);
    connection =
        std::make_unique<
            RtlConnectionTransformWorkflow>(
            semanticIndex,
            documents,
            sharedTransactions);
    setupUi(dockParent);
    wirePanel();
}

RtlHighRiskEditPanelCoordinator::
RtlHighRiskEditPanelCoordinator(
    QWidget* dockParent,
    std::unique_ptr<RtlRenameWorkflow>
        renameWorkflow,
    std::unique_ptr<RtlConnectionTransformWorkflow>
        connectionWorkflow,
    QObject* parent)
    : QObject(parent ? parent : dockParent)
    , rename(std::move(renameWorkflow))
    , connection(std::move(connectionWorkflow))
{
    setupUi(dockParent);
    wirePanel();
}

RtlHighRiskEditPanelCoordinator::
~RtlHighRiskEditPanelCoordinator() = default;

QString RtlHighRiskEditPanelCoordinator::panelId()
{
    return QStringLiteral("rtlHighRiskEdit");
}

void RtlHighRiskEditPanelCoordinator::setupUi(
    QWidget* dockParent)
{
    dockWidget = new QDockWidget(
        QStringLiteral("RTL Change Preview"), dockParent);
    dockWidget->setObjectName(
        QStringLiteral("rtlHighRiskEditDock"));
    dockWidget->setAttribute(
        Qt::WA_DeleteOnClose, false);
    dockWidget->setAllowedAreas(
        Qt::BottomDockWidgetArea);
    dockWidget->setFeatures(
        QDockWidget::DockWidgetMovable
        | QDockWidget::DockWidgetFloatable
        | QDockWidget::DockWidgetClosable);
    deferredPanel = new DeferredPanel(dockWidget, this,
        [this](QWidget* parent) -> QWidget* {
            panelWidget = new RtlHighRiskEditPanel(parent);
            wirePanel();
            return panelWidget;
        });
    deferredPanel->setObjectName(QStringLiteral("deferredRtlChangePanel"));
    dockWidget->setWidget(deferredPanel);
}

void RtlHighRiskEditPanelCoordinator::wirePanel()
{
    if (!panelWidget)
        return;
    connect(
        panelWidget,
        &RtlHighRiskEditPanel::draftChanged,
        this,
        &RtlHighRiskEditPanelCoordinator::
            draftChanged,
        Qt::UniqueConnection);
    connect(
        panelWidget,
        &RtlHighRiskEditPanel::previewRequested,
        this,
        [this](std::uint64_t sessionId) {
            requestPreview(sessionId);
        });
    connect(
        panelWidget,
        &RtlHighRiskEditPanel::confirmRequested,
        this,
        [this](std::uint64_t sessionId) {
            confirm(sessionId);
        });
    connect(
        panelWidget,
        &RtlHighRiskEditPanel::cancelRequested,
        this,
        [this](std::uint64_t sessionId) {
            cancel(sessionId);
        });
    connect(
        panelWidget,
        &RtlHighRiskEditPanel::undoRequested,
        this,
        [this](std::uint64_t sessionId) {
            undo(sessionId);
        });
}

QDockWidget*
RtlHighRiskEditPanelCoordinator::dock() const
{
    return dockWidget;
}

RtlHighRiskEditPanel*
RtlHighRiskEditPanelCoordinator::panel() const
{
    if (deferredPanel)
        deferredPanel->ensureCreated();
    return panelWidget;
}

std::uint64_t
RtlHighRiskEditPanelCoordinator::activeSessionId() const
{
    return currentSessionId;
}

RtlHighRiskEditKind
RtlHighRiskEditPanelCoordinator::activeKind() const
{
    return currentKind;
}

const RtlHighRiskEditPanelOutcome&
RtlHighRiskEditPanelCoordinator::lastOutcome() const
{
    return currentOutcome;
}

bool RtlHighRiskEditPanelCoordinator::
hasPendingPreview() const
{
    const RtlHighRiskEditWorkflow* workflow =
        currentTransactionWorkflow();
    return workflow && workflow->hasPendingPreview();
}

bool RtlHighRiskEditPanelCoordinator::
hasProtectedUndoPosition() const
{
    return appliedOwner != RtlHighRiskEditKind::None;
}

bool RtlHighRiskEditPanelCoordinator::
hasPendingConfirmationToken() const
{
    return !pendingConfirmationToken.isEmpty();
}

bool RtlHighRiskEditPanelCoordinator::beginRename(
    RtlRenamePanelSession session,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!rename) {
        if (failureReason) {
            *failureReason =
                QStringLiteral(
                    "The RTL rename workflow is unavailable.");
        }
        return false;
    }
    if (appliedOwner != RtlHighRiskEditKind::None) {
        if (failureReason) {
            *failureReason =
                QStringLiteral(
                    "Undo the applied RTL edit before switching "
                    "the Change Preview page.");
        }
        return false;
    }
    if (!session.baseQuery.subjectStableKey.isValid()
        || !session.baseQuery.semanticToken.isValid()
        || session.baseQuery.semanticToken.revision == 0) {
        if (failureReason) {
            *failureReason =
                QStringLiteral(
                    "A stable RTL rename subject and semantic "
                    "generation are required.");
        }
        return false;
    }

    cancelActivePendingPreview();
    currentKind = RtlHighRiskEditKind::Rename;
    currentSessionId = nextSessionId++;
    renameSession = std::move(session);
    connectionSession.reset();
    pendingConfirmationToken.clear();
    panel()->beginRename(
        currentSessionId,
        renameSession->subjectLabel,
        renameSession->oldName,
        renameSession->suggestedNewName,
        renameSession->baseQuery.dryRun);
    publishEditing(
        QStringLiteral(
            "Edit the rename request, then prepare its "
            "Change Preview preview."));
    return true;
}

bool RtlHighRiskEditPanelCoordinator::
beginConnectionTransform(
    RtlConnectionTransformPanelSession session,
    QString* failureReason)
{
    if (failureReason)
        failureReason->clear();
    if (!connection) {
        if (failureReason) {
            *failureReason =
                QStringLiteral(
                    "The RTL connection transform workflow is "
                    "unavailable.");
        }
        return false;
    }
    if (appliedOwner != RtlHighRiskEditKind::None) {
        if (failureReason) {
            *failureReason =
                QStringLiteral(
                    "Undo the applied RTL edit before switching "
                    "the Change Preview page.");
        }
        return false;
    }
    if (!session.baseRequest.instanceStableKey.isValid()
        || session.baseRequest
               .selectedInstancePath.isEmpty()
        || session.baseRequest
               .expectedSemanticGeneration == 0
        || session.baseRequest
               .expectedDocumentRevision == 0) {
        if (failureReason) {
            *failureReason =
                QStringLiteral(
                    "A stable instance, semantic generation, and "
                    "document revision are required.");
        }
        return false;
    }

    cancelActivePendingPreview();
    currentKind =
        RtlHighRiskEditKind::ConnectionTransform;
    currentSessionId = nextSessionId++;
    connectionSession = std::move(session);
    renameSession.reset();
    pendingConfirmationToken.clear();
    panel()->beginConnectionTransform(
        currentSessionId,
        connectionSession->instanceLabel,
        connectionSession->baseRequest,
        connectionSession->dryRun);
    publishEditing(
        QStringLiteral(
            "Edit the connection transform request, then "
            "prepare its Change Preview preview."));
    return true;
}

RtlHighRiskEditPanelOutcome
RtlHighRiskEditPanelCoordinator::requestPreview()
{
    return requestPreview(currentSessionId);
}

RtlHighRiskEditPanelOutcome
RtlHighRiskEditPanelCoordinator::requestPreview(
    std::uint64_t sessionId)
{
    if (!sessionMatches(sessionId)) {
        return ignoredRequest(
            QStringLiteral(
                "The preview request belongs to an old RTL edit "
                "session."));
    }
    if (appliedOwner != RtlHighRiskEditKind::None) {
        return ignoredRequest(
            QStringLiteral(
                "Undo the applied RTL edit before preparing "
                "another preview."));
    }

    currentOutcome.panelState =
        RtlHighRiskEditPanelState::Preparing;
    currentOutcome.message =
        panelStateText(currentOutcome.panelState);
    currentOutcome.canPreview = false;
    currentOutcome.canConfirm = false;
    currentOutcome.canCancel = false;
    currentOutcome.canUndo = false;
    currentOutcome.confirmationFingerprint.clear();
    panelWidget->presentOutcome(currentOutcome);
    emit stateChanged(currentOutcome);

    const RtlHighRiskEditDraft request =
        panelWidget->draft();
    RtlHighRiskEditWorkflowResult result;
    QVariantMap accepted;
    bool dryRun = false;
    if (currentKind == RtlHighRiskEditKind::Rename
        && renameSession && rename) {
        RtlRenamePlanQuery query =
            renameSession->baseQuery;
        query.newName = request.newName;
        dryRun = query.dryRun;
        result = rename->preparePreview(query);
        accepted.insert(
            QStringLiteral("newName"),
            request.newName);
    } else if (
        currentKind
            == RtlHighRiskEditKind::ConnectionTransform
        && connectionSession && connection) {
        RtlConnectionTransformRequest query =
            connectionSession->baseRequest;
        query.convertOrderedToNamed =
            request.convertOrderedToNamed;
        query.addMissingPorts =
            request.addMissingPorts;
        query.removeUnknownPorts =
            request.removeUnknownPorts;
        query.synchronizeAllInstances =
            request.synchronizeAllInstances;
        query.missingPortPolicy =
            request.missingPortPolicy;
        query.castPolicy = request.castPolicy;
        dryRun = connectionSession->dryRun;
        result = connection->preparePreview(
            query, dryRun);
        accepted.insert(
            QStringLiteral("convertOrderedToNamed"),
            request.convertOrderedToNamed);
        accepted.insert(
            QStringLiteral("addMissingPorts"),
            request.addMissingPorts);
        accepted.insert(
            QStringLiteral("removeUnknownPorts"),
            request.removeUnknownPorts);
        accepted.insert(
            QStringLiteral("synchronizeAllInstances"),
            request.synchronizeAllInstances);
        accepted.insert(
            QStringLiteral("missingPortPolicy"),
            static_cast<int>(
                request.missingPortPolicy));
        accepted.insert(
            QStringLiteral("castPolicy"),
            static_cast<int>(
                request.castPolicy));
    } else {
        return ignoredRequest(
            QStringLiteral(
                "The active RTL edit session is incomplete."));
    }

    RtlHighRiskEditPanelOutcome outcome =
        publish(result);
    if (outcome.panelState
            == RtlHighRiskEditPanelState::PreviewReady
        || outcome.panelState
            == RtlHighRiskEditPanelState::
                DryRunPreviewReady) {
        emit acceptedParameters(
            outcome.actionId,
            accepted,
            dryRun);
    }
    return outcome;
}

RtlHighRiskEditPanelOutcome
RtlHighRiskEditPanelCoordinator::confirm()
{
    return confirm(currentSessionId);
}

RtlHighRiskEditPanelOutcome
RtlHighRiskEditPanelCoordinator::confirm(
    std::uint64_t sessionId)
{
    if (!sessionMatches(sessionId)) {
        return ignoredRequest(
            QStringLiteral(
                "The confirmation belongs to an old RTL edit "
                "session."));
    }
    if (pendingConfirmationToken.isEmpty()
        || (currentOutcome.panelState
                != RtlHighRiskEditPanelState::PreviewReady
            && currentOutcome.panelState
                != RtlHighRiskEditPanelState::
                    DryRunPreviewReady)) {
        return ignoredRequest(
            QStringLiteral(
                "No current Change Preview confirmation token is "
                "available."));
    }

    currentOutcome.panelState =
        RtlHighRiskEditPanelState::Confirming;
    currentOutcome.message =
        panelStateText(currentOutcome.panelState);
    currentOutcome.canPreview = false;
    currentOutcome.canConfirm = false;
    currentOutcome.canCancel = false;
    currentOutcome.canUndo = false;
    panelWidget->presentOutcome(currentOutcome);
    emit stateChanged(currentOutcome);

    const QString token = pendingConfirmationToken;
    RtlHighRiskEditWorkflowResult result;
    if (currentKind == RtlHighRiskEditKind::Rename
        && rename) {
        result = rename->confirm(token);
    } else if (
        currentKind
            == RtlHighRiskEditKind::ConnectionTransform
        && connection) {
        result = connection->confirm(token);
    } else {
        return ignoredRequest(
            QStringLiteral(
                "The active RTL edit workflow is unavailable."));
    }
    pendingConfirmationToken.clear();
    return publish(result);
}

RtlHighRiskEditPanelOutcome
RtlHighRiskEditPanelCoordinator::cancel()
{
    return cancel(currentSessionId);
}

RtlHighRiskEditPanelOutcome
RtlHighRiskEditPanelCoordinator::cancel(
    std::uint64_t sessionId)
{
    if (!sessionMatches(sessionId)) {
        return ignoredRequest(
            QStringLiteral(
                "The cancellation belongs to an old RTL edit "
                "session."));
    }
    RtlHighRiskEditWorkflow* workflow =
        currentTransactionWorkflow();
    if (!workflow || !workflow->hasPendingPreview()) {
        return ignoredRequest(
            QStringLiteral(
                "No pending Change Preview preview is available to "
                "cancel."));
    }

    RtlHighRiskEditWorkflowResult result;
    if (currentKind == RtlHighRiskEditKind::Rename)
        result = rename->cancel();
    else
        result = connection->cancel();
    pendingConfirmationToken.clear();
    return publish(result);
}

RtlHighRiskEditPanelOutcome
RtlHighRiskEditPanelCoordinator::undo()
{
    return undo(currentSessionId);
}

RtlHighRiskEditPanelOutcome
RtlHighRiskEditPanelCoordinator::undo(
    std::uint64_t sessionId)
{
    if (!sessionMatches(sessionId)) {
        return ignoredRequest(
            QStringLiteral(
                "The undo request belongs to an old RTL edit "
                "session."));
    }
    if (appliedOwner == RtlHighRiskEditKind::None) {
        RtlHighRiskEditPanelOutcome unavailable =
            currentOutcome;
        unavailable.failure =
            RtlHighRiskEditWorkflowFailure::
                NothingToUndo;
        unavailable.transactionStatus =
            rtledit::TransactionStatus::NothingToUndo;
        unavailable.message =
            QStringLiteral(
                "No applied RTL edit is available for single-step "
                "undo.");
        return unavailable;
    }

    RtlHighRiskEditWorkflowResult result;
    if (appliedOwner == RtlHighRiskEditKind::Rename
        && rename) {
        result = rename->undo();
    } else if (
        appliedOwner
            == RtlHighRiskEditKind::ConnectionTransform
        && connection) {
        result = connection->undo();
    } else {
        return ignoredRequest(
            QStringLiteral(
                "The owning RTL edit workflow is unavailable."));
    }
    if (result.state
            == RtlHighRiskEditWorkflowState::Undone
        || !currentWorkflowCanUndo()) {
        appliedOwner = RtlHighRiskEditKind::None;
    }
    return publish(result);
}

void RtlHighRiskEditPanelCoordinator::
resetForWorkspaceClose()
{
    cancelActivePendingPreview();
    if (appliedOwner == RtlHighRiskEditKind::Rename
        && rename) {
        rename->retireUndoPosition();
    } else if (
        appliedOwner
            == RtlHighRiskEditKind::ConnectionTransform
        && connection) {
        connection->retireUndoPosition();
    }
    appliedOwner = RtlHighRiskEditKind::None;
    currentKind = RtlHighRiskEditKind::None;
    currentSessionId = 0;
    renameSession.reset();
    connectionSession.reset();
    pendingConfirmationToken.clear();
    currentOutcome = RtlHighRiskEditPanelOutcome{};
    if (panelWidget)
        panelWidget->clearSession();
    emit stateChanged(currentOutcome);
}

void RtlHighRiskEditPanelCoordinator::draftChanged(
    std::uint64_t sessionId)
{
    if (!sessionMatches(sessionId)
        || appliedOwner != RtlHighRiskEditKind::None) {
        return;
    }
    cancelActivePendingPreview();
    publishEditing(
        QStringLiteral(
            "The request changed. Prepare a new Change Preview "
            "preview before confirmation."));
}

void RtlHighRiskEditPanelCoordinator::
cancelActivePendingPreview()
{
    RtlHighRiskEditWorkflow* workflow =
        currentTransactionWorkflow();
    if (!workflow || !workflow->hasPendingPreview()) {
        pendingConfirmationToken.clear();
        return;
    }
    if (currentKind == RtlHighRiskEditKind::Rename
        && rename) {
        rename->cancel();
    } else if (
        currentKind
            == RtlHighRiskEditKind::ConnectionTransform
        && connection) {
        connection->cancel();
    }
    pendingConfirmationToken.clear();
}

void RtlHighRiskEditPanelCoordinator::publishEditing(
    const QString& message)
{
    currentOutcome = RtlHighRiskEditPanelOutcome{};
    currentOutcome.panelState =
        RtlHighRiskEditPanelState::Editing;
    currentOutcome.actionId =
        currentKind == RtlHighRiskEditKind::Rename
        ? RtlRenameWorkflow::actionFamilyId()
        : currentKind
                == RtlHighRiskEditKind::
                    ConnectionTransform
            ? RtlConnectionTransformWorkflow::actionId()
            : QString();
    currentOutcome.message = message;
    currentOutcome.dryRun =
        currentKind == RtlHighRiskEditKind::Rename
            && renameSession
        ? renameSession->baseQuery.dryRun
        : currentKind
                  == RtlHighRiskEditKind::
                      ConnectionTransform
              && connectionSession
            ? connectionSession->dryRun
            : false;
    currentOutcome.canPreview =
        currentKind != RtlHighRiskEditKind::None
        && appliedOwner == RtlHighRiskEditKind::None;
    panelWidget->presentOutcome(currentOutcome);
    emit stateChanged(currentOutcome);
}

RtlHighRiskEditPanelOutcome
RtlHighRiskEditPanelCoordinator::publish(
    const RtlHighRiskEditWorkflowResult& result)
{
    if (result.state
            == RtlHighRiskEditWorkflowState::PreviewReady
        && result.failure
            == RtlHighRiskEditWorkflowFailure::None
        && result.preview.ready()) {
        pendingConfirmationToken =
            result.preview.confirmationToken;
    } else {
        pendingConfirmationToken.clear();
    }

    if (result.state
            == RtlHighRiskEditWorkflowState::Applied
        && result.failure
            == RtlHighRiskEditWorkflowFailure::None) {
        appliedOwner = currentKind;
    } else if (
        result.state
            == RtlHighRiskEditWorkflowState::Undone
        && result.failure
            == RtlHighRiskEditWorkflowFailure::None) {
        appliedOwner = RtlHighRiskEditKind::None;
    }

    RtlHighRiskEditPanelOutcome outcome;
    outcome.workflowState = result.state;
    outcome.failure = result.failure;
    outcome.transactionStatus =
        result.transactionStatus;
    outcome.actionId = result.actionId;
    outcome.message = result.message;
    outcome.conflictFile = result.conflictFile;
    outcome.dryRun =
        result.preview.ready()
        ? result.preview.dryRun
        : currentKind == RtlHighRiskEditKind::Rename
                  && renameSession
            ? renameSession->baseQuery.dryRun
            : currentKind
                      == RtlHighRiskEditKind::
                          ConnectionTransform
                  && connectionSession
                ? connectionSession->dryRun
                : false;
    outcome.fileCount = result.preview.fileCount;
    outcome.editCount = result.preview.editCount;
    outcome.hasStructuredPreview =
        result.preview.ready();
    if (outcome.hasStructuredPreview) {
        outcome.structuredPreview =
            result.preview.structuredPreview;
        outcome.sourceDiff =
            result.preview.sourceDiff;
        outcome.renderedDiff =
            result.preview.renderedDiff;
    }

    if (result.failure
            != RtlHighRiskEditWorkflowFailure::None) {
        outcome.panelState =
            isConflict(result.failure)
            ? RtlHighRiskEditPanelState::Conflict
            : RtlHighRiskEditPanelState::Rejected;
    } else {
        switch (result.state) {
        case RtlHighRiskEditWorkflowState::Idle:
            outcome.panelState =
                RtlHighRiskEditPanelState::Editing;
            break;
        case RtlHighRiskEditWorkflowState::PreviewReady:
            outcome.panelState =
                result.preview.dryRun
                ? RtlHighRiskEditPanelState::
                      DryRunPreviewReady
                : RtlHighRiskEditPanelState::
                      PreviewReady;
            break;
        case RtlHighRiskEditWorkflowState::NoChanges:
            outcome.panelState =
                RtlHighRiskEditPanelState::NoChanges;
            break;
        case RtlHighRiskEditWorkflowState::Cancelled:
            outcome.panelState =
                RtlHighRiskEditPanelState::Cancelled;
            break;
        case RtlHighRiskEditWorkflowState::Applied:
            outcome.panelState =
                RtlHighRiskEditPanelState::Applied;
            break;
        case RtlHighRiskEditWorkflowState::DryRunComplete:
            outcome.panelState =
                RtlHighRiskEditPanelState::
                    DryRunComplete;
            break;
        case RtlHighRiskEditWorkflowState::Undone:
            outcome.panelState =
                RtlHighRiskEditPanelState::Undone;
            break;
        case RtlHighRiskEditWorkflowState::Failed:
            outcome.panelState =
                RtlHighRiskEditPanelState::Rejected;
            break;
        }
    }

    const bool pending =
        outcome.panelState
            == RtlHighRiskEditPanelState::PreviewReady
        || outcome.panelState
            == RtlHighRiskEditPanelState::
                DryRunPreviewReady;
    outcome.confirmationFingerprint =
        pending
        ? confirmationFingerprintFor(
              pendingConfirmationToken)
        : QString();
    outcome.canConfirm =
        pending
        && !pendingConfirmationToken.isEmpty();
    outcome.canCancel = outcome.canConfirm;
    outcome.canUndo =
        outcome.panelState
            == RtlHighRiskEditPanelState::Applied
        && currentWorkflowCanUndo();
    outcome.canPreview =
        !pending
        && appliedOwner == RtlHighRiskEditKind::None
        && currentKind != RtlHighRiskEditKind::None
        && outcome.panelState
            != RtlHighRiskEditPanelState::Preparing
        && outcome.panelState
            != RtlHighRiskEditPanelState::Confirming;

    currentOutcome = outcome;
    panelWidget->presentOutcome(currentOutcome);
    emit stateChanged(currentOutcome);
    return currentOutcome;
}

RtlHighRiskEditPanelOutcome
RtlHighRiskEditPanelCoordinator::ignoredRequest(
    const QString& message) const
{
    RtlHighRiskEditPanelOutcome ignored =
        currentOutcome;
    ignored.failure =
        RtlHighRiskEditWorkflowFailure::InvalidState;
    ignored.message = message;
    return ignored;
}

bool RtlHighRiskEditPanelCoordinator::sessionMatches(
    std::uint64_t sessionId) const
{
    return sessionId != 0
        && sessionId == currentSessionId
        && currentKind != RtlHighRiskEditKind::None;
}

bool RtlHighRiskEditPanelCoordinator::
currentWorkflowCanUndo() const
{
    if (appliedOwner == RtlHighRiskEditKind::Rename)
        return rename
            && rename->transactionWorkflow()
                   .canUndoAppliedTransaction();
    if (appliedOwner
        == RtlHighRiskEditKind::ConnectionTransform) {
        return connection
            && connection->transactionWorkflow()
                   .canUndoAppliedTransaction();
    }
    return false;
}

RtlHighRiskEditWorkflow*
RtlHighRiskEditPanelCoordinator::
currentTransactionWorkflow()
{
    if (currentKind == RtlHighRiskEditKind::Rename)
        return rename
            ? &rename->transactionWorkflow()
            : nullptr;
    if (currentKind
        == RtlHighRiskEditKind::ConnectionTransform) {
        return connection
            ? &connection->transactionWorkflow()
            : nullptr;
    }
    return nullptr;
}

const RtlHighRiskEditWorkflow*
RtlHighRiskEditPanelCoordinator::
currentTransactionWorkflow() const
{
    if (currentKind == RtlHighRiskEditKind::Rename)
        return rename
            ? &rename->transactionWorkflow()
            : nullptr;
    if (currentKind
        == RtlHighRiskEditKind::ConnectionTransform) {
        return connection
            ? &connection->transactionWorkflow()
            : nullptr;
    }
    return nullptr;
}

bool RtlHighRiskEditPanelCoordinator::isConflict(
    RtlHighRiskEditWorkflowFailure failure)
{
    switch (failure) {
    case RtlHighRiskEditWorkflowFailure::PreviewConflict:
    case RtlHighRiskEditWorkflowFailure::
        ConfirmationTokenMismatch:
    case RtlHighRiskEditWorkflowFailure::
        StaleSemanticGeneration:
    case RtlHighRiskEditWorkflowFailure::
        StaleDocumentRevision:
    case RtlHighRiskEditWorkflowFailure::
        ExternalModification:
    case RtlHighRiskEditWorkflowFailure::
        TransactionGenerationConflict:
    case RtlHighRiskEditWorkflowFailure::AtomicRollbackFailed:
    case RtlHighRiskEditWorkflowFailure::UndoConflict:
        return true;
    case RtlHighRiskEditWorkflowFailure::None:
    case RtlHighRiskEditWorkflowFailure::MissingDependency:
    case RtlHighRiskEditWorkflowFailure::InvalidState:
    case RtlHighRiskEditWorkflowFailure::PlanningRejected:
    case RtlHighRiskEditWorkflowFailure::InvalidPlan:
    case RtlHighRiskEditWorkflowFailure::ApplyFailed:
    case RtlHighRiskEditWorkflowFailure::NothingToUndo:
    case RtlHighRiskEditWorkflowFailure::UndoFailed:
        return false;
    }
    return false;
}

QString RtlHighRiskEditPanelCoordinator::
confirmationFingerprintFor(const QString& token)
{
    if (token.isEmpty())
        return QString();
    return QString::fromLatin1(
        QCryptographicHash::hash(
            token.toUtf8(),
            QCryptographicHash::Sha256)
            .toHex()
            .left(12));
}
