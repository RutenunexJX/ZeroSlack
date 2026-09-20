#include "uitypography.h"
#include "settingscenterpanel.h"

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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
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

#include <limits>
#include <utility>

namespace {
constexpr int kCategoryIdRole = Qt::UserRole + 1;

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
        != loadedValues(requestedScope);
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
    if (activeWorkspaceRoot == workspaceRoot)
        return;
    activeWorkspaceRoot = workspaceRoot;
    if (activeWorkspaceRoot.trimmed().isEmpty()
        && activeScope == SettingsCenterScope::Workspace) {
        setScope(SettingsCenterScope::Global);
    }
    updateWorkspaceScopeAvailability();
    reload();
}

void SettingsCenterPanel::reload()
{
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
    QPointer<QWidget> previousFocus = QApplication::focusWidget();
    const QVariantMap values = activeDraftValues();

    SettingsCenterLayerValidation validation =
        SettingsCenterSchema::validateLayer(values, activeScope);
    validation.issues.append(localStringMapIssues(activeScope));

    bool hasValidationError = false;
    for (const SettingsCenterValidationIssue& issue :
         validation.issues) {
        if (issue.kind == SettingsCenterIssueKind::InvalidValue) {
            hasValidationError = true;
            break;
        }
    }
    if (hasValidationError) {
        reportIssues(validation.issues,
                     tr("Settings contain invalid values."),
                     true);
        if (previousFocus)
            previousFocus->setFocus(Qt::OtherFocusReason);
        return;
    }

    if (!isScopeDirty(activeScope)) {
        reportIssues(validation.issues,
                     tr("No settings changes to apply."));
        if (previousFocus)
            previousFocus->setFocus(Qt::OtherFocusReason);
        return;
    }
    if (!settingsService) {
        reportStatus(tr("Settings service is unavailable."), true);
        if (previousFocus)
            previousFocus->setFocus(Qt::OtherFocusReason);
        return;
    }

    SettingsCenterSaveResult result;
    if (activeScope == SettingsCenterScope::Global) {
        result = settingsService->saveGlobal(
            validation.values,
            loadedSnapshot.globalRevision);
    } else {
        result = settingsService->saveWorkspace(
            activeWorkspaceRoot,
            validation.values,
            loadedSnapshot.workspaceRevision);
    }

    if (!result.saved) {
        reportIssues(result.issues,
                     result.message.isEmpty()
                         ? tr("Settings could not be saved.")
                         : result.message,
                     true);
        if (previousFocus)
            previousFocus->setFocus(Qt::OtherFocusReason);
        return;
    }

    if (activeScope == SettingsCenterScope::Global) {
        globalDraft = result.normalizedValues;
        loadedSnapshot.globalValues = result.normalizedValues;
        loadedSnapshot.globalRevision = result.revision;
    } else {
        workspaceDraft = result.normalizedValues;
        loadedSnapshot.workspaceValues = result.normalizedValues;
        loadedSnapshot.workspaceRevision = result.revision;
        loadedSnapshot.workspaceDocumentExists = true;
    }
    loadedSnapshot.effectiveValues = SettingsCenterSchema::merge(
        loadedSnapshot.globalValues,
        loadedSnapshot.workspaceValues);
    populateFields();
    reportIssues(result.issues, result.message);
    emit settingsApplied(activeScope);
    if (previousFocus)
        previousFocus->setFocus(Qt::OtherFocusReason);
}

void SettingsCenterPanel::revertCurrentScope()
{
    QPointer<QWidget> previousFocus = QApplication::focusWidget();
    activeDraftValues() = loadedValues(activeScope);
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

void SettingsCenterPanel::buildUi()
{
    auto* rootLayout = new QHBoxLayout(this);
    rootBox = rootLayout;
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(20);

    auto* navigationLayout = new QVBoxLayout;
    navigationBox = navigationLayout;
    auto* scopeLabel = new QLabel(tr("Scope"), this);
    scopeLabel->setObjectName(
        QStringLiteral("settingsCenterScopeLabel"));
    UiTypography::apply(scopeLabel, UiTypography::Role::Section);
    navigationLayout->setSpacing(8);
    navigationLayout->addWidget(scopeLabel);

    scopeCombo = new QComboBox(this);
    scopeCombo->setObjectName(
        QStringLiteral("settingsCenterScopeCombo"));
    scopeCombo->addItem(tr("Global"),
                        static_cast<int>(
                            SettingsCenterScope::Global));
    scopeCombo->addItem(tr("Workspace"),
                        static_cast<int>(
                            SettingsCenterScope::Workspace));
    navigationLayout->addWidget(scopeCombo);

    categoryList = new QListWidget(this);
    categoryList->setObjectName(
        QStringLiteral("settingsCenterCategoryList"));
    categoryList->setSelectionMode(
        QAbstractItemView::SingleSelection);
    categoryList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    navigationLayout->addWidget(categoryList, 1);
    rootLayout->addLayout(navigationLayout);

    auto* contentLayout = new QVBoxLayout;
    scopeSummaryLabel = new QLabel(this);
    scopeSummaryLabel->setObjectName(
        QStringLiteral("settingsCenterScopeSummary"));
    scopeSummaryLabel->setWordWrap(true);
    UiTypography::apply(scopeSummaryLabel, UiTypography::Role::Metadata);
    contentLayout->setSpacing(12);
    contentLayout->addWidget(scopeSummaryLabel);

    categoryStack = new QStackedWidget(this);
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
        pageLayout->setSpacing(16);
        auto* pageTitle = new QLabel(category.title, pageContent);
        UiTypography::apply(pageTitle, UiTypography::Role::PageTitle);
        pageLayout->addWidget(pageTitle);

        auto* description = new QLabel(category.description,
                                       pageContent);
        description->setWordWrap(true);
        description->setObjectName(
            QStringLiteral("settingsCenterCategoryDescription.%1")
                .arg(category.id));
        UiTypography::apply(description, UiTypography::Role::Metadata);
        pageLayout->addWidget(description);

        for (const SettingsCenterFieldDescriptor& descriptor :
             category.fields) {
            auto* group = new QGroupBox(descriptor.title,
                                        pageContent);
            group->setObjectName(
                QStringLiteral("settingsCenterFieldGroup.%1")
                    .arg(descriptor.id));
            auto* fieldLayout = new QVBoxLayout(group);
            fieldLayout->setContentsMargins(12, 16, 12, 12);
            fieldLayout->setSpacing(8);

            FieldBinding binding;
            binding.descriptor = descriptor;

            binding.overrideCheck = new QCheckBox(group);
            binding.overrideCheck->setObjectName(
                fieldOverrideObjectName(descriptor.id));
            binding.overrideCheck->setVisible(
                !descriptor.alwaysActive);
            fieldLayout->addWidget(binding.overrideCheck);

            binding.editor = createEditor(
                descriptor, group, &binding);
            binding.editor->setObjectName(
                fieldEditorObjectName(descriptor.id));
            binding.editor->setAccessibleName(descriptor.title);
            binding.editor->setAccessibleDescription(
                descriptor.description);
            fieldLayout->addWidget(binding.editor);

            auto* fieldDescription =
                new QLabel(descriptor.description, group);
            fieldDescription->setWordWrap(true);
            fieldDescription->setObjectName(
                QStringLiteral("settingsCenterFieldDescription.%1")
                    .arg(descriptor.id));
            UiTypography::apply(fieldDescription, UiTypography::Role::Metadata);
            fieldLayout->addWidget(fieldDescription);

            binding.stateLabel = new QLabel(group);
            binding.stateLabel->setWordWrap(true);
            UiTypography::apply(binding.stateLabel, UiTypography::Role::Metadata);
            binding.stateLabel->setObjectName(
                fieldStateObjectName(descriptor.id));
            binding.stateLabel->setFocusPolicy(Qt::NoFocus);
            fieldLayout->addWidget(binding.stateLabel);

            pageLayout->addWidget(group);
            fieldBindings.insert(descriptor.id, binding);
            connectEditor(descriptor.id);
        }
        pageLayout->addStretch(1);

        auto* scroll = new QScrollArea(this);
        scroll->setObjectName(
            QStringLiteral("settingsCenterCategoryScroll.%1")
                .arg(category.id));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(pageContent);
        scroll->viewport()->installEventFilter(this);
        categoryStack->addWidget(scroll);
    }
    contentLayout->addWidget(categoryStack, 1);

    statusLabel = new QLabel(this);
    statusLabel->setObjectName(
        QStringLiteral("settingsCenterStatusLabel"));
    statusLabel->setWordWrap(true);
    statusLabel->setFocusPolicy(Qt::NoFocus);
    contentLayout->addWidget(statusLabel);

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(1);
    revertButton = new QPushButton(tr("Revert"), this);
    revertButton->setObjectName(
        QStringLiteral("settingsCenterRevertButton"));
    buttonLayout->addWidget(revertButton);
    applyButton = new QPushButton(tr("Apply"), this);
    applyButton->setObjectName(
        QStringLiteral("settingsCenterApplyButton"));
    buttonLayout->addWidget(applyButton);
    contentLayout->addLayout(buttonLayout);
    rootLayout->addLayout(contentLayout, 1);

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
            categoryStack,
            &QStackedWidget::setCurrentIndex);
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
    const QMargins margins = rootBox->contentsMargins();
    const bool narrow = width() < qMax(categoryWidth, scopeCombo->minimumSizeHint().width())
        + fieldWidth + rootBox->spacing() + margins.left() + margins.right();
    rootBox->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    navigationBox->setDirection(narrow ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    categoryList->setMaximumWidth(narrow ? QWIDGETSIZE_MAX : categoryWidth);
    categoryList->setMaximumHeight(narrow
        ? qMax(categoryList->minimumSizeHint().height(), categoryList->fontMetrics().lineSpacing() * 4 + frame * 2)
        : QWIDGETSIZE_MAX);
    updatingLayout = false;
}

QWidget* SettingsCenterPanel::createEditor(
    const SettingsCenterFieldDescriptor& descriptor,
    QWidget* parent,
    FieldBinding* binding)
{
    switch (descriptor.valueKind) {
    case SettingsCenterValueKind::Boolean:
        return new QCheckBox(tr("Enabled"), parent);
    case SettingsCenterValueKind::Integer: {
        if (descriptor.useSlider) {
            auto* slider = new QSlider(Qt::Horizontal, parent);
            slider->setRange(descriptor.minimumValue.toInt(), descriptor.maximumValue.toInt());
            slider->setTickInterval(10);
            slider->setTickPosition(QSlider::TicksBelow);
            return slider;
        }
        auto* editor = new QSpinBox(parent);
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
        auto* editor = new QDoubleSpinBox(parent);
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
        if (!descriptor.choices.isEmpty()) {
            auto* editor = new QComboBox(parent);
            editor->addItems(descriptor.choices);
            return editor;
        }
        return new QLineEdit(parent);
    case SettingsCenterValueKind::FilePath: {
        auto* container = new QWidget(parent);
        auto* layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* editor = new QLineEdit(container);
        editor->setObjectName(
            QStringLiteral("settingsCenterFilePathEdit.%1")
                .arg(descriptor.id));
        editor->setPlaceholderText(tr("Automatic discovery"));
        layout->addWidget(editor, 1);

        auto* browseButton = new QPushButton(tr("Browse..."), container);
        browseButton->setObjectName(
            QStringLiteral("settingsCenterFilePathBrowse.%1")
                .arg(descriptor.id));
        layout->addWidget(browseButton);
        container->setFocusProxy(editor);
        binding->filePathEditor = editor;

        connect(browseButton,
                &QPushButton::clicked,
                this,
                [this, descriptor, editor]() {
                    const QString current = editor->text().trimmed();
                    const QString initialDirectory = current.isEmpty()
                        ? QString()
                        : QFileInfo(current).absolutePath();
#ifdef Q_OS_WIN
                    const QString filter = tr(
                        "Executables (*.exe);;All files (*)");
#else
                    const QString filter = tr("All files (*)");
#endif
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

        auto* table = new QTableView(container);
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
        auto* addButton = new QPushButton(tr("Add"), container);
        addButton->setObjectName(
            QStringLiteral("settingsCenterStringMapAdd.%1")
                .arg(descriptor.id));
        auto* removeButton =
            new QPushButton(tr("Remove"), container);
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
    return new QLineEdit(parent);
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
    if (binding->descriptor.alwaysActive) {
        binding->stateLabel->setText(
            activeScope == SettingsCenterScope::Global
                ? tr("Applied immediately - Effective: %1")
                      .arg(effective)
                : tr("Global only - Effective: %1")
                      .arg(effective));
    } else if (activeScope == SettingsCenterScope::Global) {
        binding->overrideCheck->setText(tr("Override schema default"));
        binding->stateLabel->setText(
            overridden
                ? tr("Global override - Effective: %1")
                      .arg(effective)
                : tr("Schema default - Effective: %1")
                      .arg(effective));
    } else {
        binding->overrideCheck->setText(
            tr("Override global/default value"));
        binding->stateLabel->setText(
            overridden
                ? tr("Workspace override - Effective: %1")
                      .arg(effective)
                : tr("Inherited - Effective: %1")
                      .arg(effective));
    }
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
            tr("Global settings apply to every workspace. "
               "Unchecked fields use schema defaults."));
        applyButton->setText(tr("Apply Global"));
    } else {
        scopeSummaryLabel->setText(
            tr("Workspace settings apply only to: %1. "
               "Unchecked fields inherit the global/default value.")
                .arg(activeWorkspaceRoot));
        applyButton->setText(tr("Apply Workspace"));
    }
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
        loadedSnapshot = std::move(latestSnapshot);
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
            combo->setCurrentText(value.toString());
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
    QList<SettingsCenterValidationIssue> result;
    for (auto it = fieldBindings.cbegin();
         it != fieldBindings.cend();
         ++it) {
        const FieldBinding& binding = it.value();
        if (binding.descriptor.valueKind
                != SettingsCenterValueKind::StringMap
            || !activeDraftValues().contains(
                binding.descriptor.id)
            || !binding.stringMapModel) {
            continue;
        }

        QSet<QString> actionIds;
        for (int row = 0;
             row < binding.stringMapModel->rowCount();
             ++row) {
            const QString actionId =
                binding.stringMapModel
                    ->index(row, 0)
                    .data(Qt::EditRole)
                    .toString()
                    .trimmed();
            if (actionId.isEmpty()) {
                result.append({
                    requestedScope,
                    SettingsCenterIssueKind::InvalidValue,
                    binding.descriptor.id,
                    tr("Shortcut action identifiers cannot be empty."),
                });
                continue;
            }
            const QString key = actionId.toCaseFolded();
            if (actionIds.contains(key)) {
                result.append({
                    requestedScope,
                    SettingsCenterIssueKind::InvalidValue,
                    binding.descriptor.id,
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
