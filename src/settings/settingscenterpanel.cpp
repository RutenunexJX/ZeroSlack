#include "uicontrols.h"
#include "uitypography.h"
#include "settingscenterpanel.h"
#include "insightvisualstyle.h"
#include "blocknotestyle.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>
#include <QStyle>
#include <QTextEdit>
#include <QFontInfo>

#include <limits>
#include <utility>

namespace {
constexpr int kCategoryIdRole = Qt::UserRole + 1;

QString workspaceDraftKey(const QString& root)
{
    if (root.trimmed().isEmpty()) return {};
    QString key = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

void copySettingsLayer(SettingsCenterSnapshot& target,
                       const SettingsCenterSnapshot& source,
                       SettingsCenterScope scope)
{
    if (scope == SettingsCenterScope::Global) {
        target.globalStoragePath = source.globalStoragePath;
        target.globalRevision = source.globalRevision;
        target.globalValues = source.globalValues;
        target.globalCompatible = source.globalCompatible;
    } else {
        target.workspaceRoot = source.workspaceRoot;
        target.workspaceStoragePath = source.workspaceStoragePath;
        target.workspaceRevision = source.workspaceRevision;
        target.workspaceValues = source.workspaceValues;
        target.workspaceCompatible = source.workspaceCompatible;
        target.workspaceDocumentExists = source.workspaceDocumentExists;
    }
    target.issues.removeIf([scope](const auto& issue) { return issue.scope == scope; });
    for (const auto& issue : source.issues)
        if (issue.scope == scope) target.issues.append(issue);
    target.effectiveValues = SettingsCenterSchema::merge(target.globalValues, target.workspaceValues);
}

bool isErrorIssue(SettingsCenterIssueKind kind)
{
    return kind == SettingsCenterIssueKind::InvalidValue
        || kind == SettingsCenterIssueKind::UnsupportedDocument
        || kind == SettingsCenterIssueKind::StorageError
        || kind == SettingsCenterIssueKind::Conflict;
}

QString formattedValue(const QVariant& value)
{
    if (!value.isValid())
        return QString(QChar(0x2014));
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("On")
                              : QStringLiteral("Off");
    if (value.canConvert<QVariantMap>()) {
        const int count = value.toMap().size();
        return QStringLiteral("%1 entr%2")
            .arg(count)
            .arg(count == 1 ? QStringLiteral("y")
                            : QStringLiteral("ies"));
    }
    return value.toString();
}

QString issueText(const SettingsCenterValidationIssue& issue)
{
    QString title = issue.fieldId;
    if (const SettingsCenterFieldDescriptor* descriptor =
            SettingsCenterSchema::field(issue.fieldId)) {
        title = descriptor->title;
    }
    if (title.isEmpty())
        return issue.message;
    return QStringLiteral("%1: %2").arg(title, issue.message);
}
}

SettingsCenterPanel::SettingsCenterPanel(
    const SettingsCenterService* service,
    const QString& workspaceRoot,
    QWidget* parent)
    : QWidget(parent)
    , settingsService(service)
    , activeWorkspaceRoot(workspaceRoot)
{
    setObjectName(QStringLiteral("settingsCenterPanel"));
    setWindowFlag(Qt::WindowStaysOnTopHint, false);
    buildUi();
    updateWorkspaceScopeAvailability();
    reload();
}

SettingsCenterScope SettingsCenterPanel::scope() const
{
    return activeScope;
}

QString SettingsCenterPanel::workspaceRoot() const
{
    return activeWorkspaceRoot;
}

QString SettingsCenterPanel::currentCategoryId() const
{
    if (!categoryList || !categoryList->currentItem())
        return QString();
    return categoryList->currentItem()
        ->data(kCategoryIdRole)
        .toString();
}

SettingsCenterSnapshot SettingsCenterPanel::snapshot() const
{
    return loadedSnapshot;
}

QVariantMap SettingsCenterPanel::draftValues(
    SettingsCenterScope requestedScope) const
{
    return requestedScope == SettingsCenterScope::Global
        ? globalDraft
        : workspaceDraft;
}

QVariant SettingsCenterPanel::effectiveValue(
    const QString& fieldId) const
{
    return effectiveDraftValues().value(fieldId);
}

bool SettingsCenterPanel::hasOverride(
    const QString& fieldId,
    SettingsCenterScope requestedScope) const
{
    return (requestedScope == SettingsCenterScope::Global
                ? globalDraft
                : workspaceDraft)
        .contains(fieldId);
}

bool SettingsCenterPanel::isScopeDirty(
    SettingsCenterScope requestedScope) const
{
    return draftValues(requestedScope)
        != loadedValues(requestedScope) || !localStringMapIssues(requestedScope).isEmpty();
}

QList<SettingsCenterValidationIssue>
SettingsCenterPanel::currentIssues() const
{
    return displayedIssues;
}

QWidget* SettingsCenterPanel::fieldEditor(
    const QString& fieldId) const
{
    const auto it = fieldBindings.constFind(fieldId);
    return it == fieldBindings.cend() ? nullptr : it->editor;
}

QAbstractItemModel* SettingsCenterPanel::stringMapModel(
    const QString& fieldId) const
{
    const auto it = fieldBindings.constFind(fieldId);
    return it == fieldBindings.cend()
        ? nullptr
        : it->stringMapModel;
}

QString SettingsCenterPanel::categoryPageObjectName(
    const QString& categoryId)
{
    return QStringLiteral("settingsCenterCategoryPage.%1")
        .arg(categoryId);
}

QString SettingsCenterPanel::fieldEditorObjectName(
    const QString& fieldId)
{
    return QStringLiteral("settingsCenterField.%1").arg(fieldId);
}

QString SettingsCenterPanel::fieldOverrideObjectName(
    const QString& fieldId)
{
    return QStringLiteral("settingsCenterOverride.%1").arg(fieldId);
}

QString SettingsCenterPanel::fieldStateObjectName(
    const QString& fieldId)
{
    return QStringLiteral("settingsCenterFieldState.%1").arg(fieldId);
}

QString SettingsCenterPanel::stringMapModelObjectName(
    const QString& fieldId)
{
    return QStringLiteral("settingsCenterStringMapModel.%1")
        .arg(fieldId);
}

void SettingsCenterPanel::setWorkspaceRoot(
    const QString& workspaceRoot)
{
    if (workspaceDraftKey(activeWorkspaceRoot) == workspaceDraftKey(workspaceRoot))
        return;
    auto latest = settingsService ? settingsService->load(workspaceRoot) : SettingsCenterSnapshot{};
    latest.workspaceRoot = workspaceRoot;
    setWorkspaceSnapshot(latest);
}

void SettingsCenterPanel::setWorkspaceSnapshot(const SettingsCenterSnapshot& snapshot)
{
    const auto previousKey = workspaceDraftKey(activeWorkspaceRoot);
    const auto nextKey = workspaceDraftKey(snapshot.workspaceRoot);
    if (previousKey == nextKey) return;
    commitPendingEditors();
    if (!previousKey.isEmpty() && (isScopeDirty(SettingsCenterScope::Workspace)
        || !rowIssues(workspaceRows, workspaceDraft, SettingsCenterScope::Workspace).isEmpty()))
        workspaceDrafts.insert(previousKey, {loadedSnapshot, workspaceDraft, workspaceRows});
    else
        workspaceDrafts.remove(previousKey);

    auto next = snapshot;
    if (isScopeDirty(SettingsCenterScope::Global))
        copySettingsLayer(next, loadedSnapshot, SettingsCenterScope::Global);
    else
        globalDraft = next.globalValues;
    if (workspaceDrafts.contains(nextKey)) {
        const auto saved = workspaceDrafts.take(nextKey);
        copySettingsLayer(next, saved.baseline, SettingsCenterScope::Workspace);
        workspaceDraft = saved.values;
        workspaceRows = saved.rows;
    } else {
        workspaceDraft = next.workspaceValues;
        workspaceRows.clear();
    }
    loadedSnapshot = std::move(next);
    activeWorkspaceRoot = snapshot.workspaceRoot;
    if (activeWorkspaceRoot.trimmed().isEmpty()
        && activeScope == SettingsCenterScope::Workspace) {
        setScope(SettingsCenterScope::Global);
    }
    updateWorkspaceScopeAvailability();
    populateFields();
    reportIssues(loadedSnapshot.issues, tr("Workspace settings selected."),
                 !loadedSnapshot.globalCompatible || !loadedSnapshot.workspaceCompatible);
}

void SettingsCenterPanel::reload()
{
    globalRows.clear();
    workspaceRows.clear();
    workspaceDrafts.remove(workspaceDraftKey(activeWorkspaceRoot));
    QPointer<QWidget> previousFocus = QApplication::focusWidget();
    if (!settingsService) {
        loadedSnapshot = {};
        loadedSnapshot.workspaceRoot = activeWorkspaceRoot;
        globalDraft.clear();
        workspaceDraft.clear();
        populateFields();
        reportStatus(tr("Settings service is unavailable."), true);
        if (previousFocus)
            previousFocus->setFocus(Qt::OtherFocusReason);
        return;
    }

    loadedSnapshot = settingsService->load(activeWorkspaceRoot);
    globalDraft = loadedSnapshot.globalValues;
    workspaceDraft = loadedSnapshot.workspaceValues;
    populateFields();
    reportIssues(loadedSnapshot.issues,
                 tr("Settings loaded."),
                 !loadedSnapshot.globalCompatible
                     || !loadedSnapshot.workspaceCompatible);
    if (previousFocus)
        previousFocus->setFocus(Qt::OtherFocusReason);
}

void SettingsCenterPanel::setScope(
    SettingsCenterScope requestedScope)
{
    if (requestedScope == SettingsCenterScope::Workspace
        && activeWorkspaceRoot.trimmed().isEmpty()) {
        reportStatus(
            tr("Open a workspace to edit workspace settings."),
            true);
        const QSignalBlocker blocker(scopeCombo);
        scopeCombo->setCurrentIndex(0);
        return;
    }
    if (activeScope == requestedScope)
        return;

    commitPendingEditors();
    activeScope = requestedScope;
    const QSignalBlocker blocker(scopeCombo);
    const int index = scopeCombo->findData(
        static_cast<int>(activeScope));
    if (index >= 0)
        scopeCombo->setCurrentIndex(index);
    updateScopePresentation();
    populateFields();
    emit scopeChanged(activeScope);
}

void SettingsCenterPanel::selectCategory(
    const QString& categoryId)
{
    if (!categoryList)
        return;
    for (int row = 0; row < categoryList->count(); ++row) {
        QListWidgetItem* item = categoryList->item(row);
        if (item
            && item->data(kCategoryIdRole).toString()
                == categoryId) {
            categoryList->setCurrentRow(row);
            return;
        }
    }
}

void SettingsCenterPanel::applyCurrentScope()
{
    commitPendingEditors();
    applyDraft(activeScope == SettingsCenterScope::Global ? QStringLiteral("global")
        : QStringLiteral("workspace:") + workspaceDraftKey(activeWorkspaceRoot));
}

SettingsCenterSaveResult SettingsCenterPanel::applyDraft(const QString& key)
{
    QPointer<QWidget> previousFocus = QApplication::focusWidget();
    SettingsCenterSaveResult result;
    const bool global = key == QStringLiteral("global");
    const auto requestedScope = global ? SettingsCenterScope::Global : SettingsCenterScope::Workspace;
    const QString workspaceKey = key.mid(QStringLiteral("workspace:").size());
    const bool currentWorkspace = !global && key.startsWith(QStringLiteral("workspace:"))
        && workspaceKey == workspaceDraftKey(activeWorkspaceRoot) && !workspaceKey.isEmpty();
    auto cached = workspaceDrafts.find(workspaceKey);
    if (!global && !currentWorkspace && cached == workspaceDrafts.end()) {
        result.message = tr("The settings draft is no longer available.");
        return result;
    }
    const auto& baseline = global || currentWorkspace ? loadedSnapshot : cached->baseline;
    const auto& values = global ? globalDraft : currentWorkspace ? workspaceDraft : cached->values;
    const auto& rows = global ? globalRows : currentWorkspace ? workspaceRows : cached->rows;
    auto validation = SettingsCenterSchema::validateLayer(values, requestedScope);
    validation.issues.append(rowIssues(rows, values, requestedScope));
    bool invalid = false;
    for (const auto& issue : validation.issues) invalid |= isErrorIssue(issue.kind);
    if (invalid) {
        result.issues = validation.issues;
        result.message = tr("Settings contain invalid values.");
    } else if (values == (global ? baseline.globalValues : baseline.workspaceValues)) {
        result.saved = true;
        result.message = tr("No settings changes to apply.");
        reportIssues(validation.issues, result.message);
        return result;
    } else if (!settingsService) {
        result.message = tr("Settings service is unavailable.");
    } else {
        result = global ? settingsService->saveGlobal(validation.values, baseline.globalRevision)
            : settingsService->saveWorkspace(baseline.workspaceRoot, validation.values, baseline.workspaceRevision);
    }
    if (result.saved) {
        if (global) {
            globalDraft = result.normalizedValues;
            globalRows.clear();
            loadedSnapshot.globalValues = result.normalizedValues;
            loadedSnapshot.globalRevision = result.revision;
        } else if (currentWorkspace) {
            workspaceDraft = result.normalizedValues;
            workspaceRows.clear();
            loadedSnapshot.workspaceValues = result.normalizedValues;
            loadedSnapshot.workspaceRevision = result.revision;
            loadedSnapshot.workspaceDocumentExists = true;
        } else {
            workspaceDrafts.erase(cached);
        }
        loadedSnapshot.effectiveValues = SettingsCenterSchema::merge(
            loadedSnapshot.globalValues, loadedSnapshot.workspaceValues);
        if (global || currentWorkspace) populateFields();
        emit settingsApplied(requestedScope);
    }
    reportIssues(result.issues, result.message, !result.saved);
    if (previousFocus) previousFocus->setFocus(Qt::OtherFocusReason);
    return result;
}

void SettingsCenterPanel::revertCurrentScope()
{
    QPointer<QWidget> previousFocus = QApplication::focusWidget();
    activeDraftValues() = loadedValues(activeScope);
    activeRows().clear();
    populateFields();
    displayedIssues.clear();
    reportStatus(
        activeScope == SettingsCenterScope::Global
            ? tr("Global changes reverted.")
            : tr("Workspace changes reverted."),
        false);
    emit issuesReported({});
    if (previousFocus)
        previousFocus->setFocus(Qt::OtherFocusReason);
}

void SettingsCenterPanel::commitPendingEditors()
{
    QPointer<QWidget> focus = QApplication::focusWidget();
    if (!focus || (focus != this && !isAncestorOf(focus)))
        return;
    QPointer<QTableView> table;
    for (QWidget* editor = focus; editor && editor != this; editor = editor->parentWidget()) {
        if (auto* spin = qobject_cast<QAbstractSpinBox*>(editor))
            spin->interpretText();
        if (auto* view=qobject_cast<QTableView*>(editor))
            table=view;
    }
    // Only an active item delegate needs focus-out. Ordinary fields already
    // update their draft synchronously; clearing their focus changes the page.
    if (table && focus!=table && focus!=table->viewport()) {
        focus->clearFocus();
        if (table) table->setFocus(Qt::OtherFocusReason);
    }
}

QStringList SettingsCenterPanel::unappliedScopes() const
{
    QStringList scopes;
    for (const auto& pending : pendingScopes()) scopes.append(pending.label);
    return scopes;
}

QList<SettingsCenterPanel::PendingScope> SettingsCenterPanel::pendingScopes() const
{
    QList<PendingScope> scopes;
    if (isScopeDirty(SettingsCenterScope::Global)
        || !localStringMapIssues(SettingsCenterScope::Global).isEmpty())
        scopes.append({QStringLiteral("global"), tr("Global"), activeScope == SettingsCenterScope::Global});
    if (!activeWorkspaceRoot.isEmpty()
        && (isScopeDirty(SettingsCenterScope::Workspace)
            || !localStringMapIssues(SettingsCenterScope::Workspace).isEmpty()))
        scopes.append({QStringLiteral("workspace:") + workspaceDraftKey(activeWorkspaceRoot),
            tr("Workspace: %1").arg(QDir::toNativeSeparators(activeWorkspaceRoot)),
            activeScope == SettingsCenterScope::Workspace});
    auto keys = workspaceDrafts.keys();
    keys.sort();
    for (const auto& key : keys) {
        const auto& cached = workspaceDrafts[key];
        if (cached.values != cached.baseline.workspaceValues
            || !rowIssues(cached.rows, cached.values, SettingsCenterScope::Workspace).isEmpty())
            scopes.append({QStringLiteral("workspace:") + key,
                tr("Workspace: %1").arg(QDir::toNativeSeparators(cached.baseline.workspaceRoot)), false});
    }
    return scopes;
}

void SettingsCenterPanel::discardUnappliedChanges()
{
    globalDraft = loadedSnapshot.globalValues;
    workspaceDraft = loadedSnapshot.workspaceValues;
    workspaceDrafts.clear();
    globalRows.clear();
    workspaceRows.clear();
    populateFields();
    displayedIssues.clear();
    reportStatus(tr("Unapplied changes discarded."), false);
    emit issuesReported({});
}

void SettingsCenterPanel::buildUi()
{
    auto* rootLayout = new QHBoxLayout(this);
    rootBox = rootLayout;
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(20);

    auto* navigationSurface = new QWidget(this);
    navigationSurface->setObjectName(QStringLiteral("settingsCenterNavigationSurface"));
    auto* navigationLayout = new QVBoxLayout(navigationSurface);
    navigationLayout->setContentsMargins(10, 10, 10, 10);
    navigationBox = navigationLayout;
    auto* scopeLabel = UiControls::label(tr("Scope"), this);
    scopeLabel->setObjectName(
        QStringLiteral("settingsCenterScopeLabel"));
    UiTypography::apply(scopeLabel, UiTypography::Role::Section);
    navigationLayout->setSpacing(8);
    navigationLayout->addWidget(scopeLabel);

    scopeCombo = UiControls::comboBox(this);
    scopeCombo->setObjectName(
        QStringLiteral("settingsCenterScopeCombo"));
    scopeCombo->addItem(tr("Global"),
                        static_cast<int>(
                            SettingsCenterScope::Global));
    scopeCombo->addItem(tr("Workspace"),
                        static_cast<int>(
                            SettingsCenterScope::Workspace));
    navigationLayout->addWidget(scopeCombo);

    categoryList = UiControls::listWidget(this);
    UiControls::enableSmoothScrolling(categoryList);
    categoryList->setObjectName(
        QStringLiteral("settingsCenterCategoryList"));
    categoryList->setSelectionMode(
        QAbstractItemView::SingleSelection);
    categoryList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    navigationLayout->addWidget(categoryList, 1);
    rootLayout->addWidget(navigationSurface);

    auto* contentSurface = new QWidget(this);
    contentSurface->setObjectName(QStringLiteral("settingsCenterContentSurface"));
    auto* contentLayout = new QVBoxLayout(contentSurface);
    contentLayout->setContentsMargins(10, 10, 10, 10);
    scopeSummaryLabel = UiControls::label(this);
    scopeSummaryLabel->setObjectName(
        QStringLiteral("settingsCenterScopeSummary"));
    scopeSummaryLabel->setWordWrap(true);
    UiTypography::apply(scopeSummaryLabel, UiTypography::Role::Metadata);
    contentLayout->setSpacing(12);
    contentLayout->addWidget(scopeSummaryLabel);

    auto* categorySurface = UiControls::pageStack(categoryStack, this);
    categoryStack->setObjectName(
        QStringLiteral("settingsCenterCategoryStack"));

    const QList<SettingsCenterCategoryDescriptor>& categories =
        SettingsCenterSchema::categories();
    for (const SettingsCenterCategoryDescriptor& category :
         categories) {
        auto* item = new QListWidgetItem(category.title);
        item->setData(kCategoryIdRole, category.id);
        item->setToolTip(category.description);
        categoryList->addItem(item);

        auto* pageContent = new QWidget;
        pageContent->setObjectName(
            categoryPageObjectName(category.id));
        auto* pageLayout = new QVBoxLayout(pageContent);
        pageLayout->setContentsMargins(8, 8, 8, 8);
        pageLayout->setSpacing(8);
        auto* pageTitle = UiControls::label(category.title, pageContent);
        UiTypography::apply(pageTitle, UiTypography::Role::PageTitle);
        pageLayout->addWidget(pageTitle);

        pageTitle->setToolTip(category.description);

        for (const SettingsCenterFieldDescriptor& descriptor :
             category.fields) {
            if (category.id == QStringLiteral("font")
                && (descriptor.id == QStringLiteral("font.family") || descriptor.id == QStringLiteral("blockNotes.fontFamily"))) {
                auto* section = UiControls::label(descriptor.id.startsWith(QStringLiteral("blockNotes."))
                    ? tr("Block notes") : tr("Code"), pageContent);
                UiTypography::apply(section, UiTypography::Role::Section);
                pageLayout->addWidget(section);
            }
            auto* group = new QWidget(pageContent);
            group->setObjectName(
                QStringLiteral("settingsCenterFieldGroup.%1")
                    .arg(descriptor.id));
            auto* rowLayout = new QHBoxLayout(group);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->setSpacing(16);

            FieldBinding binding;
            binding.descriptor = descriptor;
            binding.rowLayout = rowLayout;
            binding.titleLabel = UiControls::label(descriptor.title, group);
            binding.titleLabel->setWordWrap(true);
            binding.titleLabel->setToolTip(descriptor.description);
            binding.titleLabel->setMinimumWidth(180);
            binding.titleLabel->setMaximumWidth(180);
            rowLayout->addWidget(binding.titleLabel, 0, Qt::AlignTop);
            auto* controls = new QWidget(group);
            auto* fieldLayout = new QVBoxLayout(controls);
            fieldLayout->setContentsMargins(0, 0, 0, 0);
            fieldLayout->setSpacing(4);
            rowLayout->addWidget(controls, 1);
            auto* metadata = new QHBoxLayout;
            metadata->setContentsMargins(0, 0, 0, 0);

            binding.overrideCheck = UiControls::checkBox(group);
            binding.overrideCheck->setObjectName(
                fieldOverrideObjectName(descriptor.id));
            binding.overrideCheck->setVisible(
                !descriptor.alwaysActive);
            metadata->addWidget(binding.overrideCheck);

            binding.editor = createEditor(
                descriptor, group, &binding);
            binding.editor->setObjectName(
                fieldEditorObjectName(descriptor.id));
            binding.editor->setAccessibleName(descriptor.title);
            binding.editor->setAccessibleDescription(
                descriptor.description);
            binding.editor->setToolTip(descriptor.description);
            fieldLayout->addWidget(binding.editor);

            binding.stateLabel = UiControls::label(group);
            binding.stateLabel->setWordWrap(true);
            UiTypography::apply(binding.stateLabel, UiTypography::Role::Metadata);
            binding.stateLabel->setObjectName(
                fieldStateObjectName(descriptor.id));
            binding.stateLabel->setFocusPolicy(Qt::NoFocus);
            metadata->addStretch();
            metadata->addWidget(binding.stateLabel);
            fieldLayout->addLayout(metadata);

            pageLayout->addWidget(group);
            fieldBindings.insert(descriptor.id, binding);
            connectEditor(descriptor.id);
        }
        if (category.id == QStringLiteral("font")) {
            blockPreview = new QTextEdit(pageContent);
            blockPreview->setObjectName(QStringLiteral("settingsBlockNotesPreview"));
            blockPreview->setReadOnly(true);
            blockPreview->setAcceptRichText(false);
            blockPreview->setPlainText(tr("Block notes\nDescribe the design intent beside the code.\n说明示例：记录设计意图与实现约束。"));
            blockPreview->setMinimumHeight(120);
            blockPreview->setMaximumHeight(160);
            pageLayout->addWidget(blockPreview);
            auto* previewFooter = new QHBoxLayout;
            blockFontResult = UiControls::label(pageContent);
            blockFontResult->setWordWrap(true);
            UiTypography::apply(blockFontResult, UiTypography::Role::Metadata);
            previewFooter->addWidget(blockFontResult, 1);
            blockFontReset = UiControls::pushButton(tr("Reset block appearance"), pageContent);
            blockFontReset->setObjectName(QStringLiteral("settingsResetBlockFont"));
            previewFooter->addWidget(blockFontReset);
            connect(blockFontReset, &QPushButton::clicked, this, [this] {
                if (activeScope != SettingsCenterScope::Global) return;
                for (const auto* key : {"blockNotes.fontFamily", "blockNotes.sizePx", "blockNotes.lineHeight", "blockNotes.style", "blockNotes.background", "blockNotes.textColor", "blockNotes.borderColor"})
                    globalDraft.remove(QString::fromLatin1(key));
                populateFields();
            });
            pageLayout->addLayout(previewFooter);
        }
        pageLayout->addStretch(1);

        auto* scroll = UiControls::scrollArea(this);
        scroll->setObjectName(
            QStringLiteral("settingsCenterCategoryScroll.%1")
                .arg(category.id));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(pageContent);
        UiControls::enableSmoothScrolling(scroll);
        scroll->viewport()->installEventFilter(this);
        categoryStack->addWidget(scroll);
    }
    contentLayout->addWidget(categorySurface, 1);

    statusLabel = UiControls::label(this);
    statusLabel->setObjectName(
        QStringLiteral("settingsCenterStatusLabel"));
    statusLabel->setWordWrap(true);
    statusLabel->setFocusPolicy(Qt::NoFocus);
    contentLayout->addWidget(statusLabel);

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);
    revertButton = UiControls::pushButton(tr("Revert"), this);
    revertButton->setObjectName(
        QStringLiteral("settingsCenterRevertButton"));
    buttonLayout->addWidget(revertButton);
    applyButton = UiControls::pushButton(tr("Apply"), this);
    applyButton->setObjectName(
        QStringLiteral("settingsCenterApplyButton"));
    InsightVisualStyle::applyPrimaryButton(applyButton);
    buttonLayout->addWidget(applyButton);
    contentLayout->addLayout(buttonLayout);
    rootLayout->addWidget(contentSurface, 1);

    connect(scopeCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                if (index < 0)
                    return;
                setScope(static_cast<SettingsCenterScope>(
                    scopeCombo->itemData(index).toInt()));
            });
    connect(categoryList,
            &QListWidget::currentRowChanged,
            this, [this](int index) { UiControls::selectPage(categoryStack, index); });
    connect(categoryList, &QListWidget::currentRowChanged, this,
            [this] { updateResponsiveLayout(); });
    connect(applyButton,
            &QPushButton::clicked,
            this,
            &SettingsCenterPanel::applyCurrentScope);
    connect(revertButton,
            &QPushButton::clicked,
            this,
            &SettingsCenterPanel::revertCurrentScope);

    if (categoryList->count() > 0)
        categoryList->setCurrentRow(0);
    updateScopePresentation();
}

void SettingsCenterPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

bool SettingsCenterPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize) {
        auto* viewport = qobject_cast<QWidget*>(watched);
        auto* scroll = viewport ? qobject_cast<QScrollArea*>(viewport->parentWidget()) : nullptr;
        if (scroll && scroll->widget()) {
            for (auto* table : scroll->widget()->findChildren<QTableView*>())
                table->setMaximumHeight(qMax(table->minimumSizeHint().height(), viewport->height()));
            updateResponsiveLayout();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SettingsCenterPanel::updateResponsiveLayout()
{
    if (updatingLayout || !rootBox || !navigationBox || !categoryList)
        return;
    updatingLayout = true;
    const int frame = style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, this);
    const int scrollBar = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
    const int categoryWidth = qMax(categoryList->minimumSizeHint().width(),
        categoryList->sizeHintForColumn(0) + scrollBar + categoryList->frameWidth() * 2);
    categoryList->setMinimumWidth(categoryWidth);
    int fieldWidth = applyButton->minimumSizeHint().width() + revertButton->minimumSizeHint().width()
        + rootBox->spacing();
    if (auto* page = qobject_cast<QScrollArea*>(categoryStack->currentWidget()))
        fieldWidth = qMax(fieldWidth, page->widget()->minimumSizeHint().width() + scrollBar);
    fieldWidth=qMax(fieldWidth,440);
    const QMargins margins = rootBox->contentsMargins();
    const bool narrow = width() < qMax(categoryWidth, scopeCombo->minimumSizeHint().width())
        + fieldWidth + rootBox->spacing() + margins.left() + margins.right();
    for (auto& binding : fieldBindings) {
        const int available = binding.rowLayout->parentWidget()->width();
        const bool stack = available < 180 + 16 + qMax(220, binding.editor->minimumSizeHint().width());
        binding.rowLayout->setDirection(stack ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        binding.titleLabel->setMinimumWidth(stack ? 0 : 180);
        binding.titleLabel->setMaximumWidth(stack ? QWIDGETSIZE_MAX : 180);
    }
    const auto direction=narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    const bool directionChanged=rootBox->direction()!=direction;
    rootBox->setDirection(direction);
    navigationBox->setDirection(narrow ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    categoryList->setMaximumWidth(narrow ? QWIDGETSIZE_MAX : categoryWidth);
    categoryList->setMaximumHeight(narrow
        ? qMax(categoryList->minimumSizeHint().height(), categoryList->fontMetrics().lineSpacing() * 4 + frame * 2)
        : QWIDGETSIZE_MAX);
    if (directionChanged) QTimer::singleShot(0,categoryList,[list=categoryList] {
        if (auto* item=list->currentItem()) list->scrollToItem(item,QAbstractItemView::EnsureVisible);
    });
    updatingLayout = false;
}

QWidget* SettingsCenterPanel::createEditor(
    const SettingsCenterFieldDescriptor& descriptor,
    QWidget* parent,
    FieldBinding* binding)
{
    switch (descriptor.valueKind) {
    case SettingsCenterValueKind::Boolean:
        return UiControls::checkBox(tr("Enabled"), parent);
    case SettingsCenterValueKind::Integer: {
        if (descriptor.useSlider) {
            auto* slider = UiControls::slider(Qt::Horizontal, parent);
            slider->setRange(descriptor.minimumValue.toInt(), descriptor.maximumValue.toInt());
            slider->setTickInterval(10);
            slider->setTickPosition(QSlider::TicksBelow);
            return slider;
        }
        auto* editor = UiControls::spinBox(parent);
        editor->setRange(
            descriptor.minimumValue.isValid()
                ? descriptor.minimumValue.toInt()
                : std::numeric_limits<int>::min(),
            descriptor.maximumValue.isValid()
                ? descriptor.maximumValue.toInt()
                : std::numeric_limits<int>::max());
        return editor;
    }
    case SettingsCenterValueKind::Real: {
        auto* editor = UiControls::doubleSpinBox(parent);
        editor->setDecimals(3);
        editor->setSingleStep(0.05);
        editor->setRange(
            descriptor.minimumValue.isValid()
                ? descriptor.minimumValue.toDouble()
                : -1.0e12,
            descriptor.maximumValue.isValid()
                ? descriptor.maximumValue.toDouble()
                : 1.0e12);
        return editor;
    }
    case SettingsCenterValueKind::String:
        if (descriptor.id == QStringLiteral("blockNotes.fontFamily")) {
            auto* editor = UiControls::comboBox(parent);
            editor->setEditable(true);
            editor->addItem(tr("UI default"), QString());
            editor->addItems(QFontDatabase::families());
            return editor;
        }
        if (!descriptor.choices.isEmpty()) {
            auto* editor = UiControls::comboBox(parent);
            editor->addItems(descriptor.choices);
            return editor;
        }
        return UiControls::lineEdit(parent);
    case SettingsCenterValueKind::FilePath: {
        auto* container = new QWidget(parent);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* editor = UiControls::lineEdit(container);
        editor->setObjectName(
            QStringLiteral("settingsCenterFilePathEdit.%1")
                .arg(descriptor.id));
        const bool backgroundImage = descriptor.id == QStringLiteral("appearance.editorBackgroundImagePath");
        editor->setPlaceholderText(backgroundImage ? tr("Choose an image...") : tr("Automatic discovery"));
        layout->addWidget(editor, 1);

        auto* browseButton = UiControls::pushButton(tr("Browse..."), container);
        browseButton->setObjectName(
            QStringLiteral("settingsCenterFilePathBrowse.%1")
                .arg(descriptor.id));
        layout->addWidget(browseButton);
        container->setFocusProxy(editor);
        binding->filePathEditor = editor;

        connect(browseButton,
                &QPushButton::clicked,
                this,
                [this, descriptor, editor, backgroundImage]() {
                    const QString current = editor->text().trimmed();
                    const QString initialDirectory = current.isEmpty()
                        ? QString()
                        : QFileInfo(current).absolutePath();
#ifdef Q_OS_WIN
                    QString filter = tr(
                        "Executables (*.exe);;All files (*)");
#else
                    QString filter = tr("All files (*)");
#endif
                    if (backgroundImage)
                        filter = tr("Images (*.png *.jpg *.jpeg *.webp *.bmp);;All files (*)");
                    const QString selected = QFileDialog::getOpenFileName(
                        this,
                        tr("Select %1").arg(descriptor.title),
                        initialDirectory,
                        filter);
                    if (!selected.isEmpty())
                        editor->setText(QDir::toNativeSeparators(selected));
                });
        return container;
    }
    case SettingsCenterValueKind::StringMap: {
        auto* container = new QWidget(parent);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* table = UiControls::tableView(container);
        UiControls::enableSmoothScrolling(table);
        table->setObjectName(
            QStringLiteral("settingsCenterStringMapView.%1")
                .arg(descriptor.id));
        table->setSelectionBehavior(
            QAbstractItemView::SelectRows);
        table->setSelectionMode(
            QAbstractItemView::SingleSelection);
        table->setEditTriggers(
            QAbstractItemView::DoubleClicked
            | QAbstractItemView::EditKeyPressed
            | QAbstractItemView::SelectedClicked);
        auto* model = new QStandardItemModel(table);
        model->setObjectName(
            stringMapModelObjectName(descriptor.id));
        model->setColumnCount(2);
        model->setHorizontalHeaderLabels(
            {tr("Action ID"), tr("Shortcut")});
        table->setModel(model);
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        layout->addWidget(table);
        container->setFocusProxy(table);

        auto* buttons = new QHBoxLayout;
        auto* addButton = UiControls::pushButton(tr("Add"), container);
        addButton->setObjectName(
            QStringLiteral("settingsCenterStringMapAdd.%1")
                .arg(descriptor.id));
        auto* removeButton =
            UiControls::pushButton(tr("Remove"), container);
        removeButton->setObjectName(
            QStringLiteral("settingsCenterStringMapRemove.%1")
                .arg(descriptor.id));
        buttons->addWidget(addButton);
        buttons->addWidget(removeButton);
        buttons->addStretch(1);
        layout->addLayout(buttons);

        binding->stringMapModel = model;
        connect(addButton,
                &QPushButton::clicked,
                this,
                [this, descriptor, table, model]() {
                    QList<QStandardItem*> row;
                    row.append(new QStandardItem);
                    row.append(new QStandardItem);
                    model->appendRow(row);
                    table->setCurrentIndex(
                        model->index(model->rowCount() - 1, 0));
                    table->edit(table->currentIndex());
                    updateDraftFromEditor(descriptor.id);
                });
        connect(removeButton,
                &QPushButton::clicked,
                this,
                [this, descriptor, table, model]() {
                    const QModelIndex current =
                        table->currentIndex();
                    if (current.isValid())
                        model->removeRow(current.row());
                    updateDraftFromEditor(descriptor.id);
                });
        return container;
    }
    }
    return UiControls::lineEdit(parent);
}

void SettingsCenterPanel::connectEditor(
    const QString& fieldId)
{
    auto it = fieldBindings.find(fieldId);
    if (it == fieldBindings.end())
        return;

    FieldBinding& binding = it.value();
    connect(binding.overrideCheck,
            &QCheckBox::toggled,
            this,
            [this, fieldId](bool checked) {
                if (!populating)
                    setOverride(fieldId, checked);
            });

    switch (binding.descriptor.valueKind) {
    case SettingsCenterValueKind::Boolean:
        connect(qobject_cast<QCheckBox*>(binding.editor),
                &QCheckBox::toggled,
                this,
                [this, fieldId]() {
                    updateDraftFromEditor(fieldId);
                });
        break;
    case SettingsCenterValueKind::Integer:
        if (auto* slider = qobject_cast<QSlider*>(binding.editor)) {
            connect(slider, &QSlider::valueChanged, this, [this, fieldId, slider](int value) {
                slider->setToolTip(tr("%1%").arg(value));
                updateDraftFromEditor(fieldId);
            });
            break;
        }
        connect(qobject_cast<QSpinBox*>(binding.editor),
                qOverload<int>(&QSpinBox::valueChanged),
                this,
                [this, fieldId]() {
                    updateDraftFromEditor(fieldId);
                });
        break;
    case SettingsCenterValueKind::Real:
        connect(qobject_cast<QDoubleSpinBox*>(binding.editor),
                qOverload<double>(
                    &QDoubleSpinBox::valueChanged),
                this,
                [this, fieldId]() {
                    updateDraftFromEditor(fieldId);
                });
        break;
    case SettingsCenterValueKind::String:
        if (auto* combo =
                qobject_cast<QComboBox*>(binding.editor)) {
            connect(combo,
                    &QComboBox::currentTextChanged,
                    this,
                    [this, fieldId]() {
                        updateDraftFromEditor(fieldId);
                    });
        } else {
            connect(qobject_cast<QLineEdit*>(binding.editor),
                    &QLineEdit::textChanged,
                    this,
                    [this, fieldId]() {
                        updateDraftFromEditor(fieldId);
                    });
        }
        break;
    case SettingsCenterValueKind::FilePath:
        connect(binding.filePathEditor,
                &QLineEdit::textChanged,
                this,
                [this, fieldId]() {
                    updateDraftFromEditor(fieldId);
                });
        break;
    case SettingsCenterValueKind::StringMap:
        connect(binding.stringMapModel,
                &QStandardItemModel::itemChanged,
                this,
                [this, fieldId]() {
                    updateDraftFromEditor(fieldId);
                });
        connect(binding.stringMapModel,
                &QAbstractItemModel::rowsInserted,
                this,
                [this, fieldId]() {
                    updateDraftFromEditor(fieldId);
                });
        connect(binding.stringMapModel,
                &QAbstractItemModel::rowsRemoved,
                this,
                [this, fieldId]() {
                    updateDraftFromEditor(fieldId);
                });
        break;
    }
}

void SettingsCenterPanel::updateWorkspaceScopeAvailability()
{
    if (!scopeCombo)
        return;
    const int workspaceIndex = scopeCombo->findData(
        static_cast<int>(SettingsCenterScope::Workspace));
    auto* model =
        qobject_cast<QStandardItemModel*>(scopeCombo->model());
    if (!model || workspaceIndex < 0)
        return;
    QStandardItem* item = model->item(workspaceIndex);
    if (!item)
        return;
    const bool enabled =
        !activeWorkspaceRoot.trimmed().isEmpty();
    item->setEnabled(enabled);
    item->setToolTip(
        enabled
            ? tr("Edit settings stored for the active workspace.")
            : tr("Open a workspace to enable workspace settings."));
}

void SettingsCenterPanel::populateFields()
{
    populating = true;
    for (auto it = fieldBindings.begin();
         it != fieldBindings.end();
         ++it) {
        populateField(&it.value());
    }
    populating = false;
    updateFieldStates();
    updateScopePresentation();
    updateButtons();
}

void SettingsCenterPanel::populateField(
    FieldBinding* binding)
{
    if (!binding || !binding->editor
        || !binding->overrideCheck) {
        return;
    }
    const bool allowed =
        activeScope == SettingsCenterScope::Global
        ? binding->descriptor.globalAllowed
        : binding->descriptor.workspaceAllowed;
    const bool overridden =
        activeDraftValues().contains(binding->descriptor.id);
    const bool active = overridden
        || binding->descriptor.alwaysActive;
    binding->overrideCheck->setChecked(active);
    binding->overrideCheck->setEnabled(
        allowed && !binding->descriptor.alwaysActive);
    binding->overrideCheck->setVisible(
        !binding->descriptor.alwaysActive);
    binding->editor->setEnabled(allowed && active);
    setEditorValue(
        binding,
        overridden
            ? activeDraftValues().value(binding->descriptor.id)
            : inheritedValue(binding->descriptor, activeScope));
}

void SettingsCenterPanel::updateFieldStates()
{
    for (auto it = fieldBindings.begin();
         it != fieldBindings.end();
         ++it) {
        updateFieldState(&it.value());
    }
    updateBlockPreview();
}

void SettingsCenterPanel::updateBlockPreview()
{
    if (!blockPreview) return;
    const auto appearance = BlockNoteStyle::resolve(effectiveDraftValues());
    BlockNoteStyle::format(blockPreview->document(), appearance);
    auto colors = blockPreview->palette();
    colors.setColor(QPalette::Text, appearance.text);
    colors.setColor(QPalette::Base, appearance.background);
    blockPreview->setPalette(colors);
    blockPreview->setStyleSheet(QString("QTextEdit {background:%1;color:%2;border:1px solid %3;border-radius:6px;padding:10px;}")
        .arg(appearance.background.name(QColor::HexArgb),appearance.text.name(),appearance.border.name(QColor::HexArgb)));
    blockFontResult->setText(tr("Rendered: %1 · %2 px · %3×").arg(QFontInfo(appearance.font).family())
        .arg(appearance.font.pixelSize()).arg(appearance.lineHeight));
    blockFontReset->setEnabled(activeScope == SettingsCenterScope::Global);
}

void SettingsCenterPanel::updateFieldState(
    FieldBinding* binding)
{
    if (!binding || !binding->stateLabel)
        return;
    const bool overridden =
        activeDraftValues().contains(binding->descriptor.id);
    const QString effective =
        formattedValue(
            effectiveDraftValues().value(binding->descriptor.id));
    if (binding->descriptor.id == QStringLiteral("appearance.editorBackgroundImagePath")) {
        binding->editor->setEnabled(activeScope == SettingsCenterScope::Global
            && effectiveDraftValues().value(QStringLiteral("appearance.editorBackground")).toString()
                   == QStringLiteral("Custom image"));
    }
    if (binding->descriptor.alwaysActive) {
        binding->stateLabel->setText(
            activeScope == SettingsCenterScope::Global
                ? (binding->descriptor.immediateApply
                       ? tr("Immediate") : tr("Global")) : tr("Global only"));
    } else if (activeScope == SettingsCenterScope::Global) {
        binding->overrideCheck->setText(tr("Override"));
        binding->stateLabel->setText(overridden ? tr("Global") : tr("Default"));
    } else {
        binding->overrideCheck->setText(tr("Override"));
        binding->stateLabel->setText(overridden ? tr("Workspace") : tr("Inherited"));
    }
    binding->stateLabel->setToolTip(tr("Effective: %1").arg(effective));
    binding->overrideCheck->setToolTip(activeScope == SettingsCenterScope::Global
        ? tr("Override the default value") : tr("Override the global value in this workspace"));
    binding->stateLabel->setProperty(
        "settingsCenterOverride", overridden);
}

void SettingsCenterPanel::updateButtons()
{
    if (!applyButton || !revertButton)
        return;
    const bool dirty = isScopeDirty(activeScope);
    const bool compatible =
        activeScope == SettingsCenterScope::Global
        ? loadedSnapshot.globalCompatible
        : loadedSnapshot.workspaceCompatible;
    const bool scopeAvailable =
        activeScope == SettingsCenterScope::Global
        || !activeWorkspaceRoot.trimmed().isEmpty();
    applyButton->setEnabled(
        dirty && compatible && scopeAvailable
        && settingsService);
    revertButton->setEnabled(dirty);
}

void SettingsCenterPanel::updateScopePresentation()
{
    if (!scopeSummaryLabel || !applyButton)
        return;
    if (activeScope == SettingsCenterScope::Global) {
        scopeSummaryLabel->setText(
            tr("Global"));
        applyButton->setText(tr("Apply Global"));
    } else {
        scopeSummaryLabel->setText(
            tr("Workspace · %1").arg(QFileInfo(activeWorkspaceRoot).fileName()));
        applyButton->setText(tr("Apply Workspace"));
    }
    scopeSummaryLabel->setToolTip(activeScope == SettingsCenterScope::Global
        ? tr("Applies to every workspace") : QDir::toNativeSeparators(activeWorkspaceRoot));
}

void SettingsCenterPanel::updateDraftFromEditor(
    const QString& fieldId)
{
    if (populating)
        return;
    auto it = fieldBindings.find(fieldId);
    if (it == fieldBindings.end()) {
        return;
    }
    if (it->descriptor.alwaysActive) {
        if (activeScope != SettingsCenterScope::Global
            || !it->descriptor.globalAllowed) {
            return;
        }
        globalDraft.insert(fieldId, editorValue(it.value()));
        if (it->descriptor.immediateApply)
            applyImmediateField(fieldId);
        else {
            updateFieldStates();
            updateButtons();
        }
        return;
    }
    if (!activeDraftValues().contains(fieldId))
        return;
    activeDraftValues().insert(fieldId, editorValue(it.value()));
    if (it->stringMapModel) {
        QList<QStringList> rows;
        for (int row = 0; row < it->stringMapModel->rowCount(); ++row)
            rows.append({it->stringMapModel->index(row, 0).data(Qt::EditRole).toString(),
                         it->stringMapModel->index(row, 1).data(Qt::EditRole).toString()});
        activeRows().insert(fieldId, rows);
    }
    updateFieldStates();
    updateButtons();
}

void SettingsCenterPanel::applyImmediateField(
    const QString& fieldId)
{
    auto it = fieldBindings.find(fieldId);
    if (it == fieldBindings.end()
        || !settingsService
        || activeScope != SettingsCenterScope::Global) {
        return;
    }

    QVariantMap values = loadedSnapshot.globalValues;
    const QVariant requestedValue = globalDraft.value(fieldId);
    values.insert(fieldId, requestedValue);
    SettingsCenterSaveResult result =
        settingsService->saveGlobal(
            values,
            loadedSnapshot.globalRevision);
    if (!result.saved && result.conflict) {
        // Immediate fields represent one explicit user choice. If another
        // Settings Center-owned field changed concurrently, merge only this
        // choice into the latest layer and retry once with its revision.
        const QVariantMap previousLoadedValues =
            loadedSnapshot.globalValues;
        const QVariantMap previousDraftValues = globalDraft;
        SettingsCenterSnapshot latestSnapshot =
            settingsService->load(activeWorkspaceRoot);
        QVariantMap rebasedDraft =
            latestSnapshot.globalValues;
        QSet<QString> candidateFields;
        for (auto draft = previousDraftValues.cbegin();
             draft != previousDraftValues.cend();
             ++draft) {
            candidateFields.insert(draft.key());
        }
        for (auto loaded = previousLoadedValues.cbegin();
             loaded != previousLoadedValues.cend();
             ++loaded) {
            candidateFields.insert(loaded.key());
        }
        for (const QString& candidateField : candidateFields) {
            const bool loadedContains =
                previousLoadedValues.contains(candidateField);
            const bool draftContains =
                previousDraftValues.contains(candidateField);
            const bool locallyChanged =
                loadedContains != draftContains
                || (loadedContains
                    && previousLoadedValues.value(candidateField)
                           != previousDraftValues.value(candidateField));
            if (!locallyChanged)
                continue;
            if (draftContains) {
                rebasedDraft.insert(
                    candidateField,
                    previousDraftValues.value(candidateField));
            } else {
                rebasedDraft.remove(candidateField);
            }
        }
        // A global retry must not replace the workspace revision paired with
        // its existing draft. Its next Apply must still detect external edits.
        copySettingsLayer(loadedSnapshot, latestSnapshot, SettingsCenterScope::Global);
        globalDraft = std::move(rebasedDraft);
        if (loadedSnapshot.globalCompatible) {
            QVariantMap retryValues =
                loadedSnapshot.globalValues;
            retryValues.insert(fieldId, requestedValue);
            result = settingsService->saveGlobal(
                retryValues,
                loadedSnapshot.globalRevision);
        } else {
            result.saved = false;
            result.conflict = false;
            result.issues = loadedSnapshot.issues;
            result.message = tr(
                "The latest global settings are incompatible.");
        }
    }
    if (!result.saved) {
        if (loadedSnapshot.globalValues.contains(fieldId)) {
            globalDraft.insert(
                fieldId,
                loadedSnapshot.globalValues.value(fieldId));
        } else {
            globalDraft.remove(fieldId);
        }
        populateFields();
        reportIssues(
            result.issues,
            result.message.isEmpty()
                ? tr("The setting could not be saved.")
                : result.message,
            true);
        return;
    }

    loadedSnapshot.globalValues = result.normalizedValues;
    loadedSnapshot.globalRevision = result.revision;
    loadedSnapshot.effectiveValues = SettingsCenterSchema::merge(
        loadedSnapshot.globalValues,
        loadedSnapshot.workspaceValues);
    globalDraft.insert(
        fieldId,
        result.normalizedValues.value(fieldId));
    updateFieldStates();
    updateButtons();
    reportIssues(result.issues, result.message);
    emit settingsApplied(SettingsCenterScope::Global);
}

void SettingsCenterPanel::setOverride(
    const QString& fieldId,
    bool enabled)
{
    auto it = fieldBindings.find(fieldId);
    if (it == fieldBindings.end())
        return;

    QVariantMap& values = activeDraftValues();
    if (enabled) {
        values.insert(
            fieldId,
            inheritedValue(it->descriptor, activeScope));
    } else {
        values.remove(fieldId);
        activeRows().remove(fieldId);
    }
    populateFields();
}

QVariant SettingsCenterPanel::editorValue(
    const FieldBinding& binding) const
{
    switch (binding.descriptor.valueKind) {
    case SettingsCenterValueKind::Boolean:
        return qobject_cast<QCheckBox*>(
                   binding.editor)->isChecked();
    case SettingsCenterValueKind::Integer:
        if (auto* slider = qobject_cast<QSlider*>(binding.editor))
            return slider->value();
        return qobject_cast<QSpinBox*>(
                   binding.editor)->value();
    case SettingsCenterValueKind::Real:
        return qobject_cast<QDoubleSpinBox*>(
                   binding.editor)->value();
    case SettingsCenterValueKind::String:
        if (auto* combo =
                qobject_cast<QComboBox*>(binding.editor)) {
            if (binding.descriptor.id == QStringLiteral("blockNotes.fontFamily")
                && combo->currentIndex() == 0 && combo->currentText() == combo->itemText(0)) return QString();
            return combo->currentText();
        }
        return qobject_cast<QLineEdit*>(
                   binding.editor)->text();
    case SettingsCenterValueKind::FilePath:
        return binding.filePathEditor
            ? binding.filePathEditor->text()
            : QString();
    case SettingsCenterValueKind::StringMap: {
        QVariantMap result;
        if (!binding.stringMapModel)
            return result;
        for (int row = 0;
             row < binding.stringMapModel->rowCount();
             ++row) {
            const QString actionId =
                binding.stringMapModel
                    ->index(row, 0)
                    .data(Qt::EditRole)
                    .toString();
            if (actionId.trimmed().isEmpty())
                continue;
            result.insert(
                actionId,
                binding.stringMapModel
                    ->index(row, 1)
                    .data(Qt::EditRole)
                    .toString());
        }
        return result;
    }
    }
    return {};
}

void SettingsCenterPanel::setEditorValue(
    FieldBinding* binding,
    const QVariant& value)
{
    if (!binding || !binding->editor)
        return;

    switch (binding->descriptor.valueKind) {
    case SettingsCenterValueKind::Boolean: {
        auto* editor =
            qobject_cast<QCheckBox*>(binding->editor);
        const QSignalBlocker blocker(editor);
        editor->setChecked(value.toBool());
        break;
    }
    case SettingsCenterValueKind::Integer: {
        if (auto* slider = qobject_cast<QSlider*>(binding->editor)) {
            const QSignalBlocker blocker(slider);
            slider->setValue(value.toInt());
            slider->setToolTip(tr("%1%").arg(value.toInt()));
            break;
        }
        auto* editor =
            qobject_cast<QSpinBox*>(binding->editor);
        const QSignalBlocker blocker(editor);
        editor->setValue(value.toInt());
        break;
    }
    case SettingsCenterValueKind::Real: {
        auto* editor =
            qobject_cast<QDoubleSpinBox*>(binding->editor);
        const QSignalBlocker blocker(editor);
        editor->setValue(value.toDouble());
        break;
    }
    case SettingsCenterValueKind::String:
        if (auto* combo =
                qobject_cast<QComboBox*>(binding->editor)) {
            const QSignalBlocker blocker(combo);
            if (binding->descriptor.id == QStringLiteral("blockNotes.fontFamily") && value.toString().isEmpty())
                combo->setCurrentIndex(0);
            else combo->setCurrentText(value.toString());
        } else {
            auto* editor =
                qobject_cast<QLineEdit*>(binding->editor);
            const QSignalBlocker blocker(editor);
            editor->setText(value.toString());
        }
        break;
    case SettingsCenterValueKind::FilePath: {
        if (!binding->filePathEditor)
            break;
        const QSignalBlocker blocker(binding->filePathEditor);
        binding->filePathEditor->setText(value.toString());
        break;
    }
    case SettingsCenterValueKind::StringMap: {
        if (!binding->stringMapModel)
            break;
        const QSignalBlocker blocker(binding->stringMapModel);
        binding->stringMapModel->removeRows(
            0, binding->stringMapModel->rowCount());
        const QVariantMap map = value.toMap();
        if (activeDraftValues().contains(binding->descriptor.id) && activeRows().contains(binding->descriptor.id)) {
            for (const auto& values : activeRows().value(binding->descriptor.id))
                binding->stringMapModel->appendRow({new QStandardItem(values.value(0)), new QStandardItem(values.value(1))});
            break;
        }
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            QList<QStandardItem*> row;
            row.append(new QStandardItem(it.key()));
            row.append(new QStandardItem(it.value().toString()));
            binding->stringMapModel->appendRow(row);
        }
        break;
    }
    }
}

QVariant SettingsCenterPanel::inheritedValue(
    const SettingsCenterFieldDescriptor& descriptor,
    SettingsCenterScope requestedScope) const
{
    if (requestedScope == SettingsCenterScope::Global)
        return descriptor.defaultValue;

    QVariantMap withoutField = workspaceDraft;
    withoutField.remove(descriptor.id);
    return SettingsCenterSchema::merge(
               globalDraft, withoutField)
        .value(descriptor.id, descriptor.defaultValue);
}

QVariantMap SettingsCenterPanel::effectiveDraftValues() const
{
    return SettingsCenterSchema::merge(
        globalDraft, workspaceDraft);
}

QVariantMap& SettingsCenterPanel::activeDraftValues()
{
    return activeScope == SettingsCenterScope::Global
        ? globalDraft
        : workspaceDraft;
}

const QVariantMap& SettingsCenterPanel::activeDraftValues() const
{
    return activeScope == SettingsCenterScope::Global
        ? globalDraft
        : workspaceDraft;
}

const QVariantMap& SettingsCenterPanel::loadedValues(
    SettingsCenterScope requestedScope) const
{
    return requestedScope == SettingsCenterScope::Global
        ? loadedSnapshot.globalValues
        : loadedSnapshot.workspaceValues;
}

QList<SettingsCenterValidationIssue>
SettingsCenterPanel::localStringMapIssues(
    SettingsCenterScope requestedScope) const
{
    return rowIssues(requestedScope == SettingsCenterScope::Global ? globalRows : workspaceRows,
        draftValues(requestedScope), requestedScope);
}

SettingsCenterPanel::StringMapRows& SettingsCenterPanel::activeRows()
{
    return activeScope == SettingsCenterScope::Global ? globalRows : workspaceRows;
}

QList<SettingsCenterValidationIssue> SettingsCenterPanel::rowIssues(
    const StringMapRows& rows, const QVariantMap& values, SettingsCenterScope requestedScope)
{
    QList<SettingsCenterValidationIssue> result;
    for (auto it = rows.cbegin(); it != rows.cend(); ++it) {
        if (!values.contains(it.key())) continue;
        QSet<QString> actionIds;
        for (const auto& row : it.value()) {
            const QString actionId = row.value(0).trimmed();
            if (actionId.isEmpty()) {
                result.append({
                    requestedScope,
                    SettingsCenterIssueKind::InvalidValue,
                    it.key(),
                    tr("Shortcut action identifiers cannot be empty."),
                });
                continue;
            }
            const QString key = actionId.toCaseFolded();
            if (actionIds.contains(key)) {
                result.append({
                    requestedScope,
                    SettingsCenterIssueKind::InvalidValue,
                    it.key(),
                    tr("Shortcut action identifiers must be unique."),
                });
            }
            actionIds.insert(key);
        }
    }
    return result;
}

void SettingsCenterPanel::reportIssues(
    const QList<SettingsCenterValidationIssue>& issues,
    const QString& fallbackMessage,
    bool forceError)
{
    displayedIssues = issues;
    QStringList messages;
    bool hasError = forceError;
    for (const SettingsCenterValidationIssue& issue : issues) {
        messages.append(issueText(issue));
        hasError = hasError || isErrorIssue(issue.kind);
    }
    emit issuesReported(messages);
    reportStatus(messages.isEmpty()
                     ? fallbackMessage
                     : messages.join(QLatin1Char('\n')),
                 hasError);
}

void SettingsCenterPanel::reportStatus(
    const QString& message,
    bool hasError)
{
    if (statusLabel) {
        statusLabel->setText(message);
        statusLabel->setProperty(
            "settingsCenterError", hasError);
        statusLabel->style()->unpolish(statusLabel);
        statusLabel->style()->polish(statusLabel);
    }
    emit statusChanged(message, hasError);
}
