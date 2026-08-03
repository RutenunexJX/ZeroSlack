#include "workspaceconfigurationdialog.h"

#include <QDialogButtonBox>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString normalizedDefineKey(QString text)
{
    const int equals = text.indexOf(QLatin1Char('='));
    if (equals >= 0)
        text = text.left(equals);
    return text.trimmed();
}

QString normalizedDefineValue(const QString& text)
{
    const int equals = text.indexOf(QLatin1Char('='));
    if (equals < 0)
        return QString();
    return text.mid(equals + 1).trimmed();
}
}

WorkspaceConfigurationDialog::WorkspaceConfigurationDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Workspace Configuration"));
    setObjectName(QStringLiteral("workspaceConfigurationDialog"));
    resize(720, 720);

    auto* rootLayout = new QVBoxLayout(this);
    auto* hint = new QLabel(
        QStringLiteral("Configuration is stored per workspace and used by workspace analysis."),
        this);
    hint->setWordWrap(true);
    rootLayout->addWidget(hint);

    auto* grid = new QGridLayout();
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    rootLayout->addLayout(grid, 1);

    includeDirsList = createStringListEditor(
        this,
        QStringLiteral("Include Dirs"),
        QStringLiteral("Include directory"),
        true,
        true);
    grid->addWidget(includeDirsList->parentWidget(), 0, 0);

    ignoredDirsList = createStringListEditor(
        this,
        QStringLiteral("Ignored Dirs"),
        QStringLiteral("Ignored directory"),
        true,
        false);
    grid->addWidget(ignoredDirsList->parentWidget(), 0, 1);

    fileExtensionsList = createStringListEditor(
        this,
        QStringLiteral("File Extensions"),
        QStringLiteral("File extension, for example .sv"),
        false,
        true);
    grid->addWidget(fileExtensionsList->parentWidget(), 1, 0);

    auto* definesGroup = new QGroupBox(QStringLiteral("Defines"), this);
    auto* definesLayout = new QVBoxLayout(definesGroup);
    definesTable = new QTableWidget(0, 2, definesGroup);
    definesTable->setObjectName(QStringLiteral("workspaceDefinesTable"));
    definesTable->setHorizontalHeaderLabels({QStringLiteral("Key"),
                                             QStringLiteral("Value")});
    definesTable->horizontalHeader()->setStretchLastSection(true);
    definesTable->verticalHeader()->hide();
    definesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    definesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    definesLayout->addWidget(definesTable, 1);

    auto* defineButtons = new QHBoxLayout();
    auto* addDefineButton = new QPushButton(QStringLiteral("Add"), definesGroup);
    auto* removeDefineButton = new QPushButton(QStringLiteral("Remove"), definesGroup);
    defineButtons->addWidget(addDefineButton);
    defineButtons->addWidget(removeDefineButton);
    defineButtons->addStretch(1);
    definesLayout->addLayout(defineButtons);
    connect(addDefineButton, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString text = QInputDialog::getText(
            this,
            QStringLiteral("Add Define"),
            QStringLiteral("Define, for example FOO or WIDTH=32"),
            QLineEdit::Normal,
            QString(),
            &ok);
        if (!ok)
            return;
        const QString key = normalizedDefineKey(text);
        if (!key.isEmpty())
            addDefineRow(key, normalizedDefineValue(text));
    });
    connect(removeDefineButton, &QPushButton::clicked,
            this, &WorkspaceConfigurationDialog::removeSelectedDefineRows);
    grid->addWidget(definesGroup, 1, 1);

    auto* topLayout = new QFormLayout();
    topModuleEdit = new QLineEdit(this);
    topModuleEdit->setObjectName(QStringLiteral("workspaceTopModuleEdit"));
    topModuleEdit->setPlaceholderText(QStringLiteral("optional"));
    topLayout->addRow(QStringLiteral("Top module / active top"),
                      topModuleEdit);
    rootLayout->addLayout(topLayout);

    auto* sourceGroups =
        new QGroupBox(
            QStringLiteral(
                "Virtual Source Groups"),
            this);
    auto* sourceGroupsLayout =
        new QVBoxLayout(sourceGroups);
    virtualSourceGroupsTable =
        new QTableWidget(0, 2, sourceGroups);
    virtualSourceGroupsTable->setObjectName(
        QStringLiteral(
            "workspaceVirtualSourceGroupsTable"));
    virtualSourceGroupsTable
        ->setHorizontalHeaderLabels(
            {QStringLiteral("Group"),
             QStringLiteral("Files")});
    virtualSourceGroupsTable
        ->horizontalHeader()
        ->setStretchLastSection(true);
    virtualSourceGroupsTable
        ->verticalHeader()
        ->hide();
    virtualSourceGroupsTable
        ->setSelectionBehavior(
            QAbstractItemView::SelectRows);
    virtualSourceGroupsTable
        ->setSelectionMode(
            QAbstractItemView::SingleSelection);
    sourceGroupsLayout->addWidget(
        virtualSourceGroupsTable, 1);

    auto* sourceGroupButtons =
        new QHBoxLayout();
    auto* addGroupButton =
        new QPushButton(
            QStringLiteral("Add Group"),
            sourceGroups);
    addGroupButton->setObjectName(
        QStringLiteral(
            "workspaceAddVirtualSourceGroupButton"));
    auto* addFilesButton =
        new QPushButton(
            QStringLiteral("Add Files"),
            sourceGroups);
    addFilesButton->setObjectName(
        QStringLiteral(
            "workspaceAddVirtualSourceFilesButton"));
    auto* removeGroupButton =
        new QPushButton(
            QStringLiteral("Remove"),
            sourceGroups);
    removeGroupButton->setObjectName(
        QStringLiteral(
            "workspaceRemoveVirtualSourceGroupButton"));
    auto* moveGroupUpButton =
        new QPushButton(
            QStringLiteral("Up"),
            sourceGroups);
    moveGroupUpButton->setObjectName(
        QStringLiteral(
            "workspaceMoveVirtualSourceGroupUpButton"));
    auto* moveGroupDownButton =
        new QPushButton(
            QStringLiteral("Down"),
            sourceGroups);
    moveGroupDownButton->setObjectName(
        QStringLiteral(
            "workspaceMoveVirtualSourceGroupDownButton"));
    sourceGroupButtons->addWidget(
        addGroupButton);
    sourceGroupButtons->addWidget(
        addFilesButton);
    sourceGroupButtons->addWidget(
        removeGroupButton);
    sourceGroupButtons->addWidget(
        moveGroupUpButton);
    sourceGroupButtons->addWidget(
        moveGroupDownButton);
    sourceGroupButtons->addStretch(1);
    sourceGroupsLayout->addLayout(
        sourceGroupButtons);
    rootLayout->addWidget(sourceGroups, 1);

    connect(
        addGroupButton,
        &QPushButton::clicked,
        this,
        [this]() {
            bool accepted = false;
            const QString name =
                QInputDialog::getText(
                    this,
                    QStringLiteral(
                        "Add Virtual Source Group"),
                    QStringLiteral("Group name"),
                    QLineEdit::Normal,
                    QString(),
                    &accepted)
                    .trimmed();
            if (accepted && !name.isEmpty()) {
                addVirtualSourceGroupRow(
                    WorkspaceVirtualSourceGroup{
                        name, {}});
            }
        });
    connect(
        addFilesButton,
        &QPushButton::clicked,
        this,
        &WorkspaceConfigurationDialog::
            addFilesToSelectedVirtualSourceGroup);
    connect(
        removeGroupButton,
        &QPushButton::clicked,
        this,
        &WorkspaceConfigurationDialog::
            removeSelectedVirtualSourceGroup);
    connect(
        moveGroupUpButton,
        &QPushButton::clicked,
        this,
        [this]() {
            moveSelectedVirtualSourceGroup(-1);
        });
    connect(
        moveGroupDownButton,
        &QPushButton::clicked,
        this,
        [this]() {
            moveSelectedVirtualSourceGroup(1);
        });

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);
}

void WorkspaceConfigurationDialog::setConfiguration(
    const WorkspaceConfiguration& configuration)
{
    workspaceRoot = configuration.workspaceRoot;
    setListValues(includeDirsList, configuration.includeDirs);
    setListValues(ignoredDirsList, configuration.ignoredDirs);
    setListValues(fileExtensionsList, configuration.fileExtensions);
    if (topModuleEdit)
        topModuleEdit->setText(configuration.topModule);
    if (virtualSourceGroupsTable) {
        virtualSourceGroupsTable->setRowCount(0);
        for (const WorkspaceVirtualSourceGroup& group :
             configuration.virtualSourceGroups) {
            addVirtualSourceGroupRow(group);
        }
    }

    if (definesTable) {
        definesTable->setRowCount(0);
        QStringList keys = configuration.defines.keys();
        keys.sort(Qt::CaseInsensitive);
        for (const QString& key : keys)
            addDefineRow(key, configuration.defines.value(key));
    }
}

WorkspaceConfiguration WorkspaceConfigurationDialog::configuration() const
{
    WorkspaceConfiguration configuration;
    configuration.workspaceRoot = workspaceRoot;
    configuration.includeDirs = listValues(includeDirsList);
    configuration.ignoredDirs = listValues(ignoredDirsList);
    configuration.fileExtensions = listValues(fileExtensionsList);
    configuration.topModule = topModuleEdit ? topModuleEdit->text().trimmed()
                                            : QString();
    configuration.virtualSourceGroups =
        virtualSourceGroupsFromTable();
    if (definesTable) {
        for (int row = 0; row < definesTable->rowCount(); ++row) {
            const QTableWidgetItem* keyItem = definesTable->item(row, 0);
            const QTableWidgetItem* valueItem = definesTable->item(row, 1);
            const QString key = keyItem ? keyItem->text().trimmed() : QString();
            if (key.isEmpty())
                continue;
            configuration.defines.insert(
                key,
                valueItem ? valueItem->text().trimmed() : QString());
        }
    }
    return configuration;
}

QListWidget* WorkspaceConfigurationDialog::createStringListEditor(
    QWidget* parent,
    const QString& title,
    const QString& addPrompt,
    bool directoryPicker,
    bool allowMove)
{
    auto* group = new QGroupBox(title, parent);
    auto* layout = new QVBoxLayout(group);
    auto* list = new QListWidget(group);
    list->setObjectName(QStringLiteral("workspace%1List")
                            .arg(title.simplified().remove(QLatin1Char(' '))));
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list, 1);

    auto* buttons = new QHBoxLayout();
    auto* addButton = new QPushButton(QStringLiteral("Add"), group);
    auto* editButton = new QPushButton(QStringLiteral("Edit"), group);
    auto* removeButton = new QPushButton(QStringLiteral("Remove"), group);
    buttons->addWidget(addButton);
    buttons->addWidget(editButton);
    buttons->addWidget(removeButton);
    QPushButton* upButton = nullptr;
    QPushButton* downButton = nullptr;
    if (allowMove) {
        upButton = new QPushButton(QStringLiteral("Up"), group);
        downButton = new QPushButton(QStringLiteral("Down"), group);
        buttons->addWidget(upButton);
        buttons->addWidget(downButton);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(addButton,
            &QPushButton::clicked,
            this,
            [this, list, directoryPicker, addPrompt]() {
        addListValue(list, QString(), directoryPicker, addPrompt);
    });
    connect(editButton,
            &QPushButton::clicked,
            this,
            [this, list, addPrompt]() {
        editSelectedListValue(list, addPrompt);
    });
    connect(removeButton, &QPushButton::clicked, this, [this, list]() {
        removeSelectedListValue(list);
    });
    if (upButton) {
        connect(upButton, &QPushButton::clicked, this, [this, list]() {
            moveSelectedListValue(list, -1);
        });
    }
    if (downButton) {
        connect(downButton, &QPushButton::clicked, this, [this, list]() {
            moveSelectedListValue(list, 1);
        });
    }
    return list;
}

void WorkspaceConfigurationDialog::addListValue(QListWidget* list,
                                                const QString& value,
                                                bool directoryPicker,
                                                const QString& prompt)
{
    if (!list)
        return;
    QString next = value;
    if (next.isEmpty() && directoryPicker) {
        next = QFileDialog::getExistingDirectory(this, prompt, workspaceRoot);
    } else if (next.isEmpty()) {
        bool ok = false;
        next = QInputDialog::getText(this,
                                     prompt,
                                     prompt,
                                     QLineEdit::Normal,
                                     QString(),
                                     &ok)
                   .trimmed();
        if (!ok)
            return;
    }
    next = next.trimmed();
    if (next.isEmpty())
        return;
    list->addItem(next);
    list->setCurrentRow(list->count() - 1);
}

void WorkspaceConfigurationDialog::editSelectedListValue(QListWidget* list,
                                                         const QString& prompt)
{
    if (!list || !list->currentItem())
        return;
    bool ok = false;
    const QString next = QInputDialog::getText(
        this,
        prompt,
        prompt,
        QLineEdit::Normal,
        list->currentItem()->text(),
        &ok).trimmed();
    if (ok && !next.isEmpty())
        list->currentItem()->setText(next);
}

void WorkspaceConfigurationDialog::removeSelectedListValue(QListWidget* list)
{
    if (!list || list->currentRow() < 0)
        return;
    delete list->takeItem(list->currentRow());
}

void WorkspaceConfigurationDialog::moveSelectedListValue(QListWidget* list,
                                                         int delta)
{
    if (!list)
        return;
    const int row = list->currentRow();
    const int nextRow = row + delta;
    if (row < 0 || nextRow < 0 || nextRow >= list->count())
        return;
    QListWidgetItem* item = list->takeItem(row);
    list->insertItem(nextRow, item);
    list->setCurrentRow(nextRow);
}

QStringList WorkspaceConfigurationDialog::listValues(QListWidget* list) const
{
    QStringList values;
    if (!list)
        return values;
    values.reserve(list->count());
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem* item = list->item(i);
        if (item && !item->text().trimmed().isEmpty())
            values.append(item->text().trimmed());
    }
    return values;
}

void WorkspaceConfigurationDialog::setListValues(QListWidget* list,
                                                 const QStringList& values)
{
    if (!list)
        return;
    list->clear();
    for (const QString& value : values) {
        if (!value.trimmed().isEmpty())
            list->addItem(value.trimmed());
    }
}

void WorkspaceConfigurationDialog::addDefineRow(const QString& key,
                                                const QString& value)
{
    if (!definesTable)
        return;
    const int row = definesTable->rowCount();
    definesTable->insertRow(row);
    definesTable->setItem(row, 0, new QTableWidgetItem(key));
    definesTable->setItem(row, 1, new QTableWidgetItem(value));
    definesTable->setCurrentCell(row, 0);
}

void WorkspaceConfigurationDialog::removeSelectedDefineRows()
{
    if (!definesTable)
        return;
    const int row = definesTable->currentRow();
    if (row >= 0)
        definesTable->removeRow(row);
}

void WorkspaceConfigurationDialog::
    addVirtualSourceGroupRow(
        const WorkspaceVirtualSourceGroup& group)
{
    if (!virtualSourceGroupsTable)
        return;
    const int row =
        virtualSourceGroupsTable->rowCount();
    virtualSourceGroupsTable->insertRow(row);
    auto* nameItem =
        new QTableWidgetItem(group.name);
    auto* filesItem =
        new QTableWidgetItem();
    filesItem->setFlags(
        filesItem->flags()
        & ~Qt::ItemIsEditable);
    filesItem->setData(
        Qt::UserRole,
        group.files);
    virtualSourceGroupsTable->setItem(
        row, 0, nameItem);
    virtualSourceGroupsTable->setItem(
        row, 1, filesItem);
    refreshVirtualSourceGroupFilesCell(row);
    virtualSourceGroupsTable->setCurrentCell(
        row, 0);
}

void WorkspaceConfigurationDialog::
    addFilesToSelectedVirtualSourceGroup()
{
    if (!virtualSourceGroupsTable)
        return;
    const int row =
        virtualSourceGroupsTable->currentRow();
    if (row < 0)
        return;
    const QStringList selected =
        QFileDialog::getOpenFileNames(
            this,
            QStringLiteral(
                "Add Files to Virtual Source Group"),
            workspaceRoot);
    if (selected.isEmpty())
        return;
    QTableWidgetItem* item =
        virtualSourceGroupsTable->item(row, 1);
    if (!item)
        return;
    QStringList files =
        item->data(Qt::UserRole)
            .toStringList();
    files.append(selected);
    item->setData(Qt::UserRole, files);
    refreshVirtualSourceGroupFilesCell(row);
}

void WorkspaceConfigurationDialog::
    removeSelectedVirtualSourceGroup()
{
    if (!virtualSourceGroupsTable)
        return;
    const int row =
        virtualSourceGroupsTable->currentRow();
    if (row >= 0)
        virtualSourceGroupsTable->removeRow(row);
}

void WorkspaceConfigurationDialog::
    moveSelectedVirtualSourceGroup(int delta)
{
    if (!virtualSourceGroupsTable)
        return;
    const int row =
        virtualSourceGroupsTable->currentRow();
    const int target = row + delta;
    if (row < 0
        || target < 0
        || target
               >= virtualSourceGroupsTable
                      ->rowCount()) {
        return;
    }
    for (int column = 0;
         column
         < virtualSourceGroupsTable
               ->columnCount();
         ++column) {
        QTableWidgetItem* current =
            virtualSourceGroupsTable
                ->takeItem(row, column);
        QTableWidgetItem* other =
            virtualSourceGroupsTable
                ->takeItem(target, column);
        virtualSourceGroupsTable
            ->setItem(row, column, other);
        virtualSourceGroupsTable
            ->setItem(target, column, current);
    }
    virtualSourceGroupsTable
        ->setCurrentCell(target, 0);
}

void WorkspaceConfigurationDialog::
    refreshVirtualSourceGroupFilesCell(int row)
{
    if (!virtualSourceGroupsTable
        || row < 0
        || row
               >= virtualSourceGroupsTable
                      ->rowCount()) {
        return;
    }
    QTableWidgetItem* item =
        virtualSourceGroupsTable->item(row, 1);
    if (!item)
        return;
    const QStringList files =
        item->data(Qt::UserRole)
            .toStringList();
    QStringList labels;
    labels.reserve(files.size());
    const QDir root(workspaceRoot);
    for (const QString& file : files) {
        const QString relative =
            root.relativeFilePath(file);
        labels.append(
            QDir::fromNativeSeparators(
                relative));
    }
    item->setText(
        labels.join(
            QStringLiteral("; ")));
    item->setToolTip(
        files.join(QLatin1Char('\n')));
}

QList<WorkspaceVirtualSourceGroup>
WorkspaceConfigurationDialog::
    virtualSourceGroupsFromTable() const
{
    QList<WorkspaceVirtualSourceGroup> groups;
    if (!virtualSourceGroupsTable)
        return groups;
    groups.reserve(
        virtualSourceGroupsTable->rowCount());
    for (int row = 0;
         row
         < virtualSourceGroupsTable
               ->rowCount();
         ++row) {
        const QTableWidgetItem* nameItem =
            virtualSourceGroupsTable
                ->item(row, 0);
        const QTableWidgetItem* filesItem =
            virtualSourceGroupsTable
                ->item(row, 1);
        WorkspaceVirtualSourceGroup group;
        group.name =
            nameItem
            ? nameItem->text().trimmed()
            : QString();
        group.files =
            filesItem
            ? filesItem->data(
                  Qt::UserRole)
                  .toStringList()
            : QStringList();
        if (!group.name.isEmpty())
            groups.append(group);
    }
    return groups;
}
