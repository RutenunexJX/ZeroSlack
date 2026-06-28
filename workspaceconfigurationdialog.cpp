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
    resize(720, 560);

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
    auto* defineUpButton = new QPushButton(QStringLiteral("Up"), definesGroup);
    auto* defineDownButton = new QPushButton(QStringLiteral("Down"), definesGroup);
    defineButtons->addWidget(addDefineButton);
    defineButtons->addWidget(removeDefineButton);
    defineButtons->addWidget(defineUpButton);
    defineButtons->addWidget(defineDownButton);
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
    connect(defineUpButton, &QPushButton::clicked, this, [this]() {
        moveSelectedDefineRow(-1);
    });
    connect(defineDownButton, &QPushButton::clicked, this, [this]() {
        moveSelectedDefineRow(1);
    });
    grid->addWidget(definesGroup, 1, 1);

    auto* topLayout = new QFormLayout();
    topModuleEdit = new QLineEdit(this);
    topModuleEdit->setObjectName(QStringLiteral("workspaceTopModuleEdit"));
    topModuleEdit->setPlaceholderText(QStringLiteral("optional"));
    topLayout->addRow(QStringLiteral("Top module / active top"),
                      topModuleEdit);
    rootLayout->addLayout(topLayout);

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

void WorkspaceConfigurationDialog::moveSelectedDefineRow(int delta)
{
    if (!definesTable)
        return;
    const int row = definesTable->currentRow();
    const int nextRow = row + delta;
    if (row < 0 || nextRow < 0 || nextRow >= definesTable->rowCount())
        return;

    const QString key =
        definesTable->item(row, 0) ? definesTable->item(row, 0)->text()
                                   : QString();
    const QString value =
        definesTable->item(row, 1) ? definesTable->item(row, 1)->text()
                                   : QString();
    definesTable->removeRow(row);
    definesTable->insertRow(nextRow);
    definesTable->setItem(nextRow, 0, new QTableWidgetItem(key));
    definesTable->setItem(nextRow, 1, new QTableWidgetItem(value));
    definesTable->setCurrentCell(nextRow, 0);
}
