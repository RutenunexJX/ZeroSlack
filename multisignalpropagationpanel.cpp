#include "uitypography.h"
#include "multisignalpropagationpanel.h"

#include "semanticindex.h"
#include "workspaceedittransactionservice.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace {

QString failureFallback(
    MultiSignalPropagationFailure failure)
{
    switch (failure) {
    case MultiSignalPropagationFailure::InvalidGroupName:
        return QStringLiteral(
            "The port-group name is not a valid SystemVerilog identifier.");
    case MultiSignalPropagationFailure::InvalidMemberName:
        return QStringLiteral(
            "A port or group-member name is invalid.");
    case MultiSignalPropagationFailure::DuplicateSignal:
        return QStringLiteral(
            "The same semantic signal was selected more than once.");
    case MultiSignalPropagationFailure::DuplicatePortName:
        return QStringLiteral(
            "Multiple selected signals resolve to the same port name.");
    case MultiSignalPropagationFailure::TargetNotAncestor:
        return QStringLiteral(
            "The selected target is not an ancestor of every signal.");
    case MultiSignalPropagationFailure::InconsistentSourceInstance:
        return QStringLiteral(
            "All selected signals must belong to one concrete instance.");
    case MultiSignalPropagationFailure::SingleSignalRejected:
    case MultiSignalPropagationFailure::IncompatibleMemberPlans:
        return QStringLiteral(
            "At least one signal cannot participate in this atomic preview.");
    case MultiSignalPropagationFailure::MissingSemanticSnapshot:
    case MultiSignalPropagationFailure::StaleSemanticGeneration:
    case MultiSignalPropagationFailure::MissingDocumentSnapshot:
    case MultiSignalPropagationFailure::StaleDocumentRevision:
    case MultiSignalPropagationFailure::StaleSemanticSource:
        return QStringLiteral(
            "The captured semantic or document context is unavailable or stale.");
    case MultiSignalPropagationFailure::InvalidTreeSnapshot:
    case MultiSignalPropagationFailure::SyntaxError:
        return QStringLiteral(
            "A captured source file has no usable structural syntax tree.");
    case MultiSignalPropagationFailure::TransactionPreparationFailed:
        return QStringLiteral(
            "The atomic Change Preview transaction could not be prepared.");
    case MultiSignalPropagationFailure::InvalidRequest:
    case MultiSignalPropagationFailure::None:
        break;
    }
    return QStringLiteral(
        "The multi-signal propagation preview was rejected.");
}

QString choiceLabel(
    const MultiSignalPropagationSignalChoice& choice)
{
    if (!choice.label.trimmed().isEmpty())
        return choice.label.trimmed();
    return QStringLiteral("%1:%2")
        .arg(choice.member.context.fileName)
        .arg(choice.member.context.cursorPosition);
}

QString fromUtf8String(const std::string& text)
{
    return QString::fromUtf8(
        text.data(),
        static_cast<qsizetype>(text.size()));
}

bool sameSemanticObject(
    const rtledit::SemanticObjectId& left,
    const rtledit::SemanticObjectId& right)
{
    return left.kind == right.kind
        && left.qualifiedName == right.qualifiedName
        && left.ownerScope == right.ownerScope
        && left.filePath == right.filePath
        && left.range.start == right.range.start
        && left.range.end == right.range.end
        && left.signatureHash == right.signatureHash;
}

bool sameTextEdit(
    const rtledit::WorkspaceTextEdit& left,
    const rtledit::WorkspaceTextEdit& right)
{
    return left.filePath == right.filePath
        && left.expectedDocumentVersion
            == right.expectedDocumentVersion
        && left.range.start == right.range.start
        && left.range.end == right.range.end
        && left.expectedText == right.expectedText
        && left.newText == right.newText;
}

bool sameProvenance(
    const rtledit::TextEditProvenance& left,
    const rtledit::TextEditProvenance& right)
{
    return left.editIndex == right.editIndex
        && left.actionId == right.actionId
        && left.anchorName == right.anchorName
        && left.description == right.description
        && left.anchor.source == right.anchor.source
        && left.anchor.resolver == right.anchor.resolver
        && left.anchor.semanticSnapshotId
            == right.anchor.semanticSnapshotId
        && left.signalQualifiedName
            == right.signalQualifiedName
        && left.sourceInstancePath
            == right.sourceInstancePath
        && left.hierarchyStepIndex
            == right.hierarchyStepIndex
        && left.sourceFilePath == right.sourceFilePath
        && left.sourceRange.start
            == right.sourceRange.start
        && left.sourceRange.end
            == right.sourceRange.end;
}

bool sameWorkspacePlan(
    const rtledit::WorkspaceEditPlan& left,
    const rtledit::WorkspaceEditPlan& right)
{
    if (left.intent.kind != right.intent.kind
        || !sameSemanticObject(
            left.intent.target, right.intent.target)
        || left.semanticSnapshot.id
            != right.semanticSnapshot.id
        || left.semanticIndexFilePaths
            != right.semanticIndexFilePaths
        || left.riskLevel != right.riskLevel
        || left.previewPolicy != right.previewPolicy
        || left.hasMixedDocumentVersions
            != right.hasMixedDocumentVersions
        || left.baselines.size()
            != right.baselines.size()
        || left.edits.size() != right.edits.size()
        || left.provenance.size()
            != right.provenance.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.baselines.size();
         ++index) {
        if (left.baselines[index].filePath
                != right.baselines[index].filePath
            || left.baselines[index].version
                != right.baselines[index].version) {
            return false;
        }
    }
    for (std::size_t index = 0;
         index < left.edits.size();
         ++index) {
        if (!sameTextEdit(
                left.edits[index],
                right.edits[index])) {
            return false;
        }
    }
    for (std::size_t index = 0;
         index < left.provenance.size();
         ++index) {
        if (!sameProvenance(
                left.provenance[index],
                right.provenance[index])) {
            return false;
        }
    }
    return true;
}

QString canonicalDiff(
    const rtledit::WorkspaceEditSourceDiff& diff)
{
    return fromUtf8String(
        rtledit::renderWorkspaceEditSourceDiffHunks(
            diff));
}

std::size_t sourceDiffEditCount(
    const rtledit::WorkspaceEditSourceDiff& diff)
{
    std::size_t count = 0;
    for (const rtledit::SourceDiffFile& file :
         diff.files) {
        count += file.editCount;
    }
    return count;
}

} // namespace

MultiSignalPropagationPanel::MultiSignalPropagationPanel(
    MultiSignalPropagationPlanner& planner,
    rtledit::WorkspaceDocumentManager& documents,
    QWidget* parent)
    : MultiSignalPropagationPanel(
          [&planner](
              const MultiSignalPropagationQuery& query,
              rtledit::WorkspaceDocumentManager& manager) {
              return planner.plan(query, manager);
          },
          documents,
          parent)
{
}

MultiSignalPropagationPanel::MultiSignalPropagationPanel(
    PlanCallback planner,
    rtledit::WorkspaceDocumentManager& documents,
    QWidget* parent)
    : QWidget(parent),
      planCallback(std::move(planner)),
      documentManager(&documents)
{
    setupUi();
    rebuildInputControls();
}

QString MultiSignalPropagationPanel::panelId()
{
    return QStringLiteral("multiSignalPropagation");
}

void MultiSignalPropagationPanel::setupUi()
{
    setObjectName(
        QStringLiteral("multiSignalPropagationPanel"));
    setFocusPolicy(Qt::NoFocus);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(7);

    auto* heading = new QLabel(
        QStringLiteral("Propagate Multiple Signals"),
        this);
    heading->setObjectName(
        QStringLiteral("multiSignalPropagationHeading"));
    QFont headingFont = UiTypography::font(UiTypography::Role::PanelTitle);
    heading->setFont(headingFont);
    root->addWidget(heading);

    stateLabel = new QLabel(this);
    stateLabel->setObjectName(
        QStringLiteral("multiSignalPropagationState"));
    stateLabel->setWordWrap(true);
    root->addWidget(stateLabel);

    auto* inputGroup = new QGroupBox(
        QStringLiteral("Structured request"),
        this);
    inputGroup->setObjectName(
        QStringLiteral("multiSignalPropagationInputGroup"));
    auto* inputLayout = new QVBoxLayout(inputGroup);

    signalTable = new QTableWidget(inputGroup);
    signalTable->setObjectName(
        QStringLiteral("multiSignalPropagationSignals"));
    signalTable->setColumnCount(3);
    signalTable->setHorizontalHeaderLabels(
        {QStringLiteral("Signal"),
         QStringLiteral("Exported Port"),
         QStringLiteral("Group Member")});
    signalTable->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    signalTable->setSelectionMode(
        QAbstractItemView::ExtendedSelection);
    signalTable->setEditTriggers(
        QAbstractItemView::DoubleClicked
        | QAbstractItemView::EditKeyPressed
        | QAbstractItemView::SelectedClicked);
    signalTable->verticalHeader()->setVisible(false);
    signalTable->horizontalHeader()
        ->setSectionResizeMode(
            0, QHeaderView::Stretch);
    signalTable->horizontalHeader()
        ->setSectionResizeMode(
            1, QHeaderView::ResizeToContents);
    signalTable->horizontalHeader()
        ->setSectionResizeMode(
            2, QHeaderView::ResizeToContents);
    inputLayout->addWidget(signalTable, 1);

    auto* form = new QFormLayout();
    ancestorCombo = new QComboBox(inputGroup);
    ancestorCombo->setObjectName(
        QStringLiteral("multiSignalPropagationAncestor"));
    ancestorCombo->setEditable(false);
    form->addRow(
        QStringLiteral("Target ancestor"),
        ancestorCombo);

    modeCombo = new QComboBox(inputGroup);
    modeCombo->setObjectName(
        QStringLiteral("multiSignalPropagationMode"));
    modeCombo->setEditable(false);
    modeCombo->addItem(
        QStringLiteral("Independent ports"),
        static_cast<int>(
            MultiSignalPropagationMode::
                IndependentPorts));
    modeCombo->addItem(
        QStringLiteral("Port group"),
        static_cast<int>(
            MultiSignalPropagationMode::PortGroup));
    form->addRow(
        QStringLiteral("Port layout"),
        modeCombo);

    groupNameEdit = new QLineEdit(inputGroup);
    groupNameEdit->setObjectName(
        QStringLiteral("multiSignalPropagationGroupName"));
    groupNameEdit->setPlaceholderText(
        QStringLiteral("SystemVerilog group identifier"));
    form->addRow(
        QStringLiteral("Port group"),
        groupNameEdit);
    inputLayout->addLayout(form);

    auto* actionRow = new QHBoxLayout();
    previewButton = new QPushButton(
        QStringLiteral("Preview Change Preview"),
        inputGroup);
    previewButton->setObjectName(
        QStringLiteral("multiSignalPropagationPreviewButton"));
    confirmButton = new QPushButton(
        QStringLiteral("Apply Confirmed Plan"),
        inputGroup);
    confirmButton->setObjectName(
        QStringLiteral("multiSignalPropagationConfirmButton"));
    confirmButton->setEnabled(false);
    undoButton = new QPushButton(
        QStringLiteral("Undo Applied Plan"),
        inputGroup);
    undoButton->setObjectName(
        QStringLiteral("multiSignalPropagationUndoButton"));
    undoButton->setEnabled(false);
    actionRow->addStretch(1);
    actionRow->addWidget(previewButton);
    actionRow->addWidget(confirmButton);
    actionRow->addWidget(undoButton);
    inputLayout->addLayout(actionRow);
    root->addWidget(inputGroup, 1);

    auto* previewSplitter =
        new QSplitter(Qt::Vertical, this);
    previewSplitter->setObjectName(
        QStringLiteral("multiSignalPropagationPreviewSplitter"));

    auto* previewMeta = new QWidget(previewSplitter);
    auto* previewMetaLayout =
        new QVBoxLayout(previewMeta);
    previewMetaLayout->setContentsMargins(0, 0, 0, 0);
    transactionSummary = new QLabel(previewMeta);
    transactionSummary->setObjectName(
        QStringLiteral(
            "multiSignalPropagationTransactionSummary"));
    transactionSummary->setWordWrap(true);
    previewMetaLayout->addWidget(transactionSummary);

    blockersList = new QListWidget(previewMeta);
    blockersList->setObjectName(
        QStringLiteral(
            "multiSignalPropagationBlockers"));
    blockersList->setSelectionMode(
        QAbstractItemView::NoSelection);
    previewMetaLayout->addWidget(blockersList);
    previewSplitter->addWidget(previewMeta);

    diffView = new QPlainTextEdit(previewSplitter);
    diffView->setObjectName(
        QStringLiteral("multiSignalPropagationDiff"));
    diffView->setReadOnly(true);
    diffView->setLineWrapMode(
        QPlainTextEdit::NoWrap);
    diffView->setFont(
        QFontDatabase::systemFont(
            QFontDatabase::FixedFont));
    diffView->setPlaceholderText(
        QStringLiteral(
            "The prepared workspace diff will appear here."));
    previewSplitter->addWidget(diffView);
    previewSplitter->setStretchFactor(1, 1);
    root->addWidget(previewSplitter, 1);

    connect(
        signalTable,
        &QTableWidget::itemChanged,
        this,
        [this](QTableWidgetItem*) {
            inputChanged();
        });
    connect(
        ancestorCombo,
        qOverload<int>(
            &QComboBox::currentIndexChanged),
        this,
        [this](int) {
            inputChanged();
        });
    connect(
        modeCombo,
        qOverload<int>(
            &QComboBox::currentIndexChanged),
        this,
        [this](int) {
            updateModeControls();
            inputChanged();
        });
    connect(
        groupNameEdit,
        &QLineEdit::textChanged,
        this,
        [this](const QString&) {
            inputChanged();
        });
    connect(
        previewButton,
        &QPushButton::clicked,
        this,
        [this]() {
            requestPreview();
        });
    connect(
        confirmButton,
        &QPushButton::clicked,
        this,
        [this]() {
            if (hasHighDiffPreview()
                && currentProposal
                && !currentProposal->dryRun
                && !currentProposal->transaction.dryRun) {
                emit confirmRequested();
            }
        });
    connect(
        undoButton,
        &QPushButton::clicked,
        this,
        &MultiSignalPropagationPanel::undoRequested);
}

void MultiSignalPropagationPanel::setInput(
    const MultiSignalPropagationPanelInput& inputValue)
{
    currentInput = inputValue;
    if (currentInput.ancestors.isEmpty()) {
        currentInput.ancestors.append(
            {QStringLiteral("Active design top"),
             QString()});
    }
    rebuildInputControls();
}

const MultiSignalPropagationPanelInput&
MultiSignalPropagationPanel::input() const
{
    return currentInput;
}

void MultiSignalPropagationPanel::rebuildInputControls()
{
    rebuilding = true;
    const QSignalBlocker tableBlocker(signalTable);
    const QSignalBlocker ancestorBlocker(
        ancestorCombo);
    const QSignalBlocker modeBlocker(modeCombo);
    const QSignalBlocker groupBlocker(
        groupNameEdit);

    signalTable->clearContents();
    signalTable->setRowCount(
        static_cast<int>(
            currentInput.signalChoices.size()));
    for (int row = 0;
         row < static_cast<int>(
                   currentInput.signalChoices.size());
         ++row) {
        const MultiSignalPropagationSignalChoice&
            choice = currentInput.signalChoices.at(row);
        auto* signalItem =
            new QTableWidgetItem(
                choiceLabel(choice));
        signalItem->setFlags(
            (signalItem->flags()
             | Qt::ItemIsUserCheckable)
            & ~Qt::ItemIsEditable);
        signalItem->setCheckState(
            choice.selected
                ? Qt::Checked : Qt::Unchecked);
        signalItem->setToolTip(
            QStringLiteral("%1 @ %2")
                .arg(
                    choice.member.context.fileName)
                .arg(
                    choice.member.context
                        .cursorPosition));
        signalTable->setItem(
            row, 0, signalItem);
        signalTable->setItem(
            row,
            1,
            new QTableWidgetItem(
                choice.member.exportedPortName));
        signalTable->setItem(
            row,
            2,
            new QTableWidgetItem(
                choice.member.groupMemberName));
    }

    ancestorCombo->clear();
    for (const MultiSignalPropagationAncestorChoice&
         ancestor : currentInput.ancestors) {
        const QString label =
            !ancestor.label.trimmed().isEmpty()
            ? ancestor.label.trimmed()
            : (ancestor.instancePath.isEmpty()
                   ? QStringLiteral("Active design top")
                   : ancestor.instancePath);
        ancestorCombo->addItem(label);
    }
    const int ancestorIndex =
        currentInput.selectedAncestorIndex >= 0
            && currentInput.selectedAncestorIndex
                   < ancestorCombo->count()
        ? currentInput.selectedAncestorIndex : 0;
    ancestorCombo->setCurrentIndex(
        ancestorIndex);
    currentInput.selectedAncestorIndex =
        ancestorIndex;

    const int modeIndex =
        modeCombo->findData(
            static_cast<int>(
                currentInput.mode));
    modeCombo->setCurrentIndex(
        modeIndex >= 0 ? modeIndex : 0);
    currentInput.mode =
        static_cast<
            MultiSignalPropagationMode>(
            modeCombo->currentData().toInt());
    groupNameEdit->setText(
        currentInput.groupName);
    rebuilding = false;

    updateModeControls();
    clearPreview();
}

void MultiSignalPropagationPanel::setSignalSelected(
    int index,
    bool selected)
{
    if (!signalTable
        || index < 0
        || index >= signalTable->rowCount()) {
        return;
    }
    if (QTableWidgetItem* item =
            signalTable->item(index, 0)) {
        item->setCheckState(
            selected
                ? Qt::Checked : Qt::Unchecked);
    }
}

void MultiSignalPropagationPanel::setExportedPortName(
    int index,
    const QString& name)
{
    if (!signalTable
        || index < 0
        || index >= signalTable->rowCount()) {
        return;
    }
    if (QTableWidgetItem* item =
            signalTable->item(index, 1)) {
        item->setText(name);
    }
}

void MultiSignalPropagationPanel::setGroupMemberName(
    int index,
    const QString& name)
{
    if (!signalTable
        || index < 0
        || index >= signalTable->rowCount()) {
        return;
    }
    if (QTableWidgetItem* item =
            signalTable->item(index, 2)) {
        item->setText(name);
    }
}

void MultiSignalPropagationPanel::
setSelectedAncestorIndex(int index)
{
    if (!ancestorCombo
        || index < 0
        || index >= ancestorCombo->count()) {
        return;
    }
    ancestorCombo->setCurrentIndex(index);
}

void MultiSignalPropagationPanel::setMode(
    MultiSignalPropagationMode mode)
{
    if (!modeCombo)
        return;
    const int index =
        modeCombo->findData(
            static_cast<int>(mode));
    if (index >= 0)
        modeCombo->setCurrentIndex(index);
}

void MultiSignalPropagationPanel::setGroupName(
    const QString& name)
{
    if (groupNameEdit)
        groupNameEdit->setText(name);
}

int MultiSignalPropagationPanel::
selectedSignalCount() const
{
    if (!signalTable)
        return 0;
    int count = 0;
    for (int row = 0;
         row < signalTable->rowCount();
         ++row) {
        const QTableWidgetItem* item =
            signalTable->item(row, 0);
        if (item
            && item->checkState() == Qt::Checked) {
            ++count;
        }
    }
    return count;
}

void MultiSignalPropagationPanel::updateModeControls()
{
    if (!modeCombo
        || !groupNameEdit
        || !signalTable) {
        return;
    }
    const bool grouped =
        static_cast<MultiSignalPropagationMode>(
            modeCombo->currentData().toInt())
        == MultiSignalPropagationMode::PortGroup;
    groupNameEdit->setEnabled(grouped);
    signalTable->setColumnHidden(1, grouped);
    signalTable->setColumnHidden(2, !grouped);
}

void MultiSignalPropagationPanel::
updatePreviewEnabled()
{
    if (!previewButton)
        return;
    previewButton->setEnabled(
        static_cast<bool>(planCallback)
        && documentManager
        && ancestorCombo
        && ancestorCombo->count() > 0
        && selectedSignalCount() >= 2);
}

void MultiSignalPropagationPanel::inputChanged()
{
    if (rebuilding)
        return;

    const int count =
        std::min(
            signalTable
                ? signalTable->rowCount() : 0,
            static_cast<int>(
                currentInput.signalChoices.size()));
    for (int row = 0; row < count; ++row) {
        MultiSignalPropagationSignalChoice&
            choice = currentInput.signalChoices[row];
        const QTableWidgetItem* signalItem =
            signalTable->item(row, 0);
        const QTableWidgetItem* exportedItem =
            signalTable->item(row, 1);
        const QTableWidgetItem* memberItem =
            signalTable->item(row, 2);
        choice.selected =
            signalItem
            && signalItem->checkState()
                   == Qt::Checked;
        choice.member.exportedPortName =
            exportedItem
            ? exportedItem->text() : QString();
        choice.member.groupMemberName =
            memberItem
            ? memberItem->text() : QString();
    }
    currentInput.selectedAncestorIndex =
        ancestorCombo
        ? ancestorCombo->currentIndex() : 0;
    if (modeCombo) {
        currentInput.mode =
            static_cast<
                MultiSignalPropagationMode>(
                modeCombo->currentData().toInt());
    }
    currentInput.groupName =
        groupNameEdit
        ? groupNameEdit->text() : QString();
    clearPreview();
}

MultiSignalPropagationQuery
MultiSignalPropagationPanel::buildQuery() const
{
    MultiSignalPropagationQuery query;
    query.mode = currentInput.mode;
    if (query.mode
        == MultiSignalPropagationMode::PortGroup) {
        query.groupName =
            currentInput.groupName.trimmed();
    }
    const int ancestorIndex =
        currentInput.selectedAncestorIndex;
    if (ancestorIndex >= 0
        && ancestorIndex
               < static_cast<int>(
                   currentInput.ancestors.size())) {
        query.targetAncestorInstancePath =
            currentInput.ancestors.at(
                ancestorIndex).instancePath;
    }
    query.workspaceFiles =
        currentInput.workspaceFiles;
    query.semanticToken =
        currentInput.semanticToken;
    query.documents =
        currentInput.capturedDocuments;
    query.dryRun = currentInput.dryRun;

    for (const MultiSignalPropagationSignalChoice&
         choice : currentInput.signalChoices) {
        if (!choice.selected)
            continue;
        MultiSignalPropagationMemberRequest member =
            choice.member;
        if (query.mode
            == MultiSignalPropagationMode::PortGroup) {
            member.exportedPortName.clear();
        } else {
            member.groupMemberName.clear();
        }
        query.members.append(std::move(member));
    }
    return query;
}

bool MultiSignalPropagationPanel::requestPreview()
{
    clearPreview();
    if (!planCallback || !documentManager) {
        stateLabel->setText(
            QStringLiteral(
                "No multi-signal propagation planner is connected."));
        return false;
    }
    if (selectedSignalCount() < 2) {
        stateLabel->setText(
            QStringLiteral(
                "Select at least two signals before requesting a preview."));
        return false;
    }
    if (!ancestorCombo
        || ancestorCombo->currentIndex() < 0) {
        stateLabel->setText(
            QStringLiteral(
                "Select a target ancestor before requesting a preview."));
        return false;
    }

    lastBuiltQuery = buildQuery();
    currentProposal =
        planCallback(
            *lastBuiltQuery,
            *documentManager);
    renderProposal(*currentProposal);
    return highDiffPreview;
}

void MultiSignalPropagationPanel::renderProposal(
    const MultiSignalPropagationProposal& proposalValue)
{
    const QString preparedDiff =
        canonicalDiff(
            proposalValue.transaction.sourceDiff);
    const bool preparedHighDiff =
        lastBuiltQuery
        && proposalValue.ready()
        && proposalValue.dryRun
               == lastBuiltQuery->dryRun
        && proposalValue.workspaceEdit.riskLevel
               == rtledit::RiskLevel::High
        && proposalValue.workspaceEdit.previewPolicy
               == rtledit::PreviewPolicy::Diff
        && proposalValue.transaction.dryRun
               == lastBuiltQuery->dryRun
        && !proposalValue.transaction
                .previewConfirmed
        && proposalValue.transaction.preview.riskLevel
               == rtledit::RiskLevel::High
        && proposalValue.transaction.preview.previewPolicy
               == rtledit::PreviewPolicy::Diff
        && sameWorkspacePlan(
            proposalValue.workspaceEdit,
            proposalValue.transaction.plan)
        && proposalValue.sourceDiff.built()
        && canonicalDiff(proposalValue.sourceDiff)
               == preparedDiff
        && proposalValue.renderedDiff
               == preparedDiff
        && proposalValue.transaction.preview.fileCount
               == proposalValue.transaction
                      .sourceDiff.files.size()
        && proposalValue.transaction.preview.editCount
               == sourceDiffEditCount(
                   proposalValue.transaction.sourceDiff)
        && !preparedDiff.trimmed().isEmpty();
    if (!preparedHighDiff) {
        renderFailure(proposalValue);
        if (proposalValue.status
                == MultiSignalPropagationStatus::Ready) {
            stateLabel->setText(
                QStringLiteral(
                    "Planner output was refused because it is not an "
                    "internally consistent unconfirmed Change Preview "
                    "transaction."));
        }
        return;
    }

    highDiffPreview = true;
    stateLabel->setText(
        proposalValue.message.isEmpty()
            ? QStringLiteral(
                  "Multi-signal propagation preview is ready.")
            : proposalValue.message);
    const QString target =
        proposalValue.targetAncestorInstancePath
                .isEmpty()
            ? (lastBuiltQuery
                       ->targetAncestorInstancePath
                       .isEmpty()
                   ? QStringLiteral(
                         "active design top")
                   : lastBuiltQuery
                         ->targetAncestorInstancePath)
            : proposalValue
                  .targetAncestorInstancePath;
    transactionSummary->setText(
        QStringLiteral(
            "Risk: High | Preview: Diff | Dry run: %1 | "
            "Signals: %2 | Files: %3 | Target: %4")
            .arg(
                lastBuiltQuery->dryRun
                    ? QStringLiteral("yes")
                    : QStringLiteral("no"))
            .arg(lastBuiltQuery->members.size())
            .arg(
                static_cast<qulonglong>(
                    proposalValue.transaction.sourceDiff
                        .files.size()))
            .arg(target));
    blockersList->clear();
    blockersList->setVisible(false);
    diffView->setPlainText(
        preparedDiff);
    if (confirmButton) {
        confirmButton->setEnabled(
            !lastBuiltQuery->dryRun
            && !proposalValue.dryRun
            && !proposalValue.transaction.dryRun);
    }
    if (undoButton)
        undoButton->setEnabled(false);
}

void MultiSignalPropagationPanel::renderFailure(
    const MultiSignalPropagationProposal& proposalValue)
{
    highDiffPreview = false;
    transactionSummary->clear();
    diffView->clear();
    blockersList->clear();

    const QString message =
        proposalValue.message.trimmed().isEmpty()
        ? failureFallback(proposalValue.failure)
        : proposalValue.message.trimmed();
    stateLabel->setText(message);
    for (const QString& blocker :
         proposalValue.blockers) {
        const QString normalized =
            blocker.trimmed();
        if (normalized.isEmpty()
            || normalized == message) {
            continue;
        }
        blockersList->addItem(normalized);
    }
    blockersList->setVisible(
        blockersList->count() > 0);
    if (confirmButton)
        confirmButton->setEnabled(false);
    if (undoButton)
        undoButton->setEnabled(false);
}

void MultiSignalPropagationPanel::clearPreview()
{
    lastBuiltQuery.reset();
    currentProposal.reset();
    highDiffPreview = false;
    if (stateLabel) {
        stateLabel->setText(
            QStringLiteral(
                "Select at least two signals and request a "
                "non-applying preview."));
    }
    if (transactionSummary)
        transactionSummary->clear();
    if (blockersList) {
        blockersList->clear();
        blockersList->setVisible(false);
    }
    if (diffView)
        diffView->clear();
    if (confirmButton)
        confirmButton->setEnabled(false);
    if (undoButton)
        undoButton->setEnabled(false);
    updatePreviewEnabled();
}

const MultiSignalPropagationQuery*
MultiSignalPropagationPanel::lastQuery() const
{
    return lastBuiltQuery
        ? &*lastBuiltQuery : nullptr;
}

const MultiSignalPropagationProposal*
MultiSignalPropagationPanel::proposal() const
{
    return currentProposal
        ? &*currentProposal : nullptr;
}

bool MultiSignalPropagationPanel::
hasHighDiffPreview() const
{
    return highDiffPreview;
}

QString MultiSignalPropagationPanel::statusText() const
{
    return stateLabel
        ? stateLabel->text() : QString();
}

QString MultiSignalPropagationPanel::renderedDiff() const
{
    return diffView
        ? diffView->toPlainText() : QString();
}

void MultiSignalPropagationPanel::setTransactionOutcome(
    const QString& message,
    bool applied,
    bool undoAvailable)
{
    if (stateLabel)
        stateLabel->setText(message);
    if (confirmButton)
        confirmButton->setEnabled(false);
    if (undoButton)
        undoButton->setEnabled(applied && undoAvailable);
}

bool MultiSignalPropagationWorkflowResult::succeeded() const
{
    return successful;
}

MultiSignalPropagationWorkflow::
MultiSignalPropagationWorkflow(
    MultiSignalPropagationPanel* panel,
    SemanticIndex* semanticIndex,
    rtledit::WorkspaceDocumentManager* documents,
    WorkspaceEditTransactionService* transactions,
    QObject* parent)
    : QObject(parent),
      panelWidget(panel),
      index(semanticIndex),
      documentManager(documents),
      transactionService(transactions)
{
    qRegisterMetaType<
        MultiSignalPropagationWorkflowState>();
    qRegisterMetaType<
        MultiSignalPropagationWorkflowResult>();
    if (panelWidget) {
        connect(
            panelWidget,
            &MultiSignalPropagationPanel::confirmRequested,
            this,
            [this]() { confirm(); });
        connect(
            panelWidget,
            &MultiSignalPropagationPanel::undoRequested,
            this,
            [this]() { undo(); });
    }
}

MultiSignalPropagationWorkflowResult
MultiSignalPropagationWorkflow::publish(
    MultiSignalPropagationWorkflowState state,
    rtledit::TransactionStatus status,
    const QString& message,
    bool succeededValue)
{
    currentResult.state = state;
    currentResult.transactionStatus = status;
    currentResult.message = message;
    currentResult.successful = succeededValue;
    if (panelWidget) {
        const bool appliedState =
            state
            == MultiSignalPropagationWorkflowState::Applied;
        panelWidget->setTransactionOutcome(
            message,
            appliedState,
            appliedState
                && workflowUndoAvailable
                && transactionService
                && transactionService->canUndo()
                && transactionService->historyGeneration()
                       == appliedTransactionGeneration);
    }
    emit stateChanged(currentResult);
    return currentResult;
}

MultiSignalPropagationWorkflowResult
MultiSignalPropagationWorkflow::confirm()
{
    if (!panelWidget || !index || !documentManager
        || !transactionService) {
        return publish(
            MultiSignalPropagationWorkflowState::Failed,
            rtledit::TransactionStatus::InvalidPreparation,
            QStringLiteral(
                "Multi-signal transaction dependencies are unavailable."),
            false);
    }
    if (currentResult.state
            == MultiSignalPropagationWorkflowState::Applied
        && workflowUndoAvailable) {
        if (canUndoAppliedTransaction()) {
            return publish(
                MultiSignalPropagationWorkflowState::Applied,
                rtledit::TransactionStatus::InvalidPreparation,
                QStringLiteral(
                    "Undo or retire the applied multi-signal transaction "
                    "before confirming another plan."),
                false);
        }
        workflowUndoAvailable = false;
    }

    const MultiSignalPropagationQuery* query =
        panelWidget->lastQuery();
    const MultiSignalPropagationProposal* proposal =
        panelWidget->proposal();
    if (!query || !proposal
        || !panelWidget->hasHighDiffPreview()
        || !proposal->ready()
        || proposal->transaction.previewConfirmed
        || proposal->workspaceEdit.riskLevel
               != rtledit::RiskLevel::High
        || proposal->workspaceEdit.previewPolicy
               != rtledit::PreviewPolicy::Diff
        || proposal->transaction.plan.riskLevel
               != rtledit::RiskLevel::High
        || proposal->transaction.plan.previewPolicy
               != rtledit::PreviewPolicy::Diff) {
        return publish(
            MultiSignalPropagationWorkflowState::Failed,
            rtledit::TransactionStatus::InvalidPreparation,
            QStringLiteral(
                "No current unconfirmed Change Preview plan is available."),
            false);
    }

    const SemanticSnapshotToken liveToken =
        index->snapshotToken();
    if (!liveToken.isValid()
        || !query->semanticToken.isValid()
        || liveToken.revision
               != query->semanticToken.revision
        || liveToken.snapshot
               != query->semanticToken.snapshot) {
        return publish(
            MultiSignalPropagationWorkflowState::Failed,
            rtledit::TransactionStatus::Stale,
            QStringLiteral(
                "The Slang semantic generation changed before confirmation."),
            false);
    }

    if (query->dryRun || proposal->dryRun
        || proposal->transaction.dryRun) {
        workflowUndoAvailable = false;
        return publish(
            MultiSignalPropagationWorkflowState::DryRunComplete,
            rtledit::TransactionStatus::DryRunOnly,
            QStringLiteral(
                "Dry-run completed with no workspace mutation."),
            true);
    }

    const rtledit::SemanticIndexSnapshot currentSemantic{
        std::to_string(liveToken.revision)};
    const rtledit::EditPlanStaleStatus stale =
        rtledit::staleStatus(
            proposal->transaction.plan,
            currentSemantic,
            *documentManager);
    if (stale.stale()) {
        return publish(
            MultiSignalPropagationWorkflowState::Failed,
            rtledit::TransactionStatus::Stale,
            QStringLiteral(
                "The workspace changed after preview; the plan was not applied."),
            false);
    }

    const rtledit::WorkspaceEditTransactionResult result =
        transactionService->applyConfirmed(
            proposal->transaction,
            currentSemantic,
            *documentManager);
    const QString message =
        result.message.empty()
        ? QStringLiteral(
              "The atomic multi-signal transaction failed.")
        : QString::fromUtf8(
              result.message.data(),
              static_cast<qsizetype>(
                  result.message.size()));
    if (result.status
            != rtledit::TransactionStatus::Applied) {
        return publish(
            MultiSignalPropagationWorkflowState::Failed,
            result.status,
            message,
            false);
    }
    appliedTransactionGeneration =
        transactionService->historyGeneration();
    workflowUndoAvailable = true;
    return publish(
        MultiSignalPropagationWorkflowState::Applied,
        result.status,
        message,
        true);
}

MultiSignalPropagationWorkflowResult
MultiSignalPropagationWorkflow::undo()
{
    if (currentResult.state
            != MultiSignalPropagationWorkflowState::Applied
        || !workflowUndoAvailable
        || !panelWidget || !documentManager
        || !transactionService
        || !transactionService->canUndo()) {
        return publish(
            MultiSignalPropagationWorkflowState::Failed,
            rtledit::TransactionStatus::InvalidPreparation,
            QStringLiteral(
                "No applied multi-signal transaction is available to undo."),
            false);
    }
    if (transactionService->historyGeneration()
            != appliedTransactionGeneration) {
        workflowUndoAvailable = false;
        return publish(
            MultiSignalPropagationWorkflowState::Failed,
            rtledit::TransactionStatus::Conflict,
            QStringLiteral(
                "A newer workspace transaction replaced this "
                "multi-signal undo position."),
            false);
    }
    const rtledit::WorkspaceEditTransactionResult result =
        transactionService->undo(*documentManager);
    const QString message =
        result.message.empty()
        ? QStringLiteral(
              "The multi-signal transaction could not be undone.")
        : QString::fromUtf8(
              result.message.data(),
              static_cast<qsizetype>(
                  result.message.size()));
    if (result.status
            != rtledit::TransactionStatus::Undone) {
        return publish(
            MultiSignalPropagationWorkflowState::Applied,
            result.status,
            message,
            false);
    }
    workflowUndoAvailable = false;
    appliedTransactionGeneration =
        transactionService->historyGeneration();
    return publish(
        MultiSignalPropagationWorkflowState::Undone,
        result.status,
        message,
        true);
}

const MultiSignalPropagationWorkflowResult&
MultiSignalPropagationWorkflow::lastResult() const
{
    return currentResult;
}

bool MultiSignalPropagationWorkflow::
canUndoAppliedTransaction() const
{
    return currentResult.state
            == MultiSignalPropagationWorkflowState::Applied
        && workflowUndoAvailable
        && transactionService
        && transactionService->canUndo()
        && transactionService->historyGeneration()
               == appliedTransactionGeneration;
}
