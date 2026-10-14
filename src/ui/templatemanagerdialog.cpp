#include "templatemanagerdialog.h"
#include "../integrations/xips/xipscontextprovider.h"
#include "editorfileidentity.h"
#include "slangmanager.h"
#include "tsdocument.h"
#include "uicontrols.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QtWidgets>

namespace
{
QString reportText(const UserTemplateLoadReport &report)
{
    QStringList text;
    if (!report.failureReason.isEmpty())
        text << report.failureReason;
    for (const auto &issue : report.issues)
        text << issue.reason;
    text.removeDuplicates();
    return text.join('\n');
}
QComboBox *choices(const QStringList &values, QWidget *parent)
{
    auto *combo = UiControls::comboBox(parent);
    combo->addItems(values);
    return combo;
}
QString cell(QTableWidget *table, int row, int col)
{
    auto *item = table->item(row, col);
    return item ? item->text() : QString();
}
void put(QTableWidget *table, int row, int col, const QString &value)
{
    table->setItem(row, col, new QTableWidgetItem(value));
}
QString digest(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
}
} // namespace
TemplateManagerDialog::TemplateManagerDialog(UserTemplateService *s, const SimulationCommandRequest &r,
                                             const QString &root, QWidget *parent)
    : UiDialog(parent), service(s), request(r), workspace(root)
{
    setWindowTitle(QStringLiteral("Templates"));
    setObjectName(QStringLiteral("templateManager"));
    resize(900, 600);
    auto *layout = new QVBoxLayout(this);
    auto *split = new QSplitter(this);
    layout->addWidget(split, 1);
    auto *left = new QWidget(split);
    auto *leftLayout = new QVBoxLayout(left);
    list = UiControls::listWidget(left);
    list->setObjectName("templateList");
    leftLayout->addWidget(list, 1);
    auto *buttons = new QHBoxLayout;
    leftLayout->addLayout(buttons);
    auto *add = UiControls::pushButton("New", left);
    auto *erase = UiControls::pushButton("Delete", left);
    buttons->addWidget(add);
    buttons->addWidget(erase);
    auto *import = UiControls::pushButton("Import legacy mappings…", left);
    leftLayout->addWidget(import);
    auto *roles = UiControls::pushButton("Signal roles…", left);
    leftLayout->addWidget(roles);
    auto *right = new QWidget(split);
    auto *edit = new QVBoxLayout(right);
    auto *form = new QFormLayout;
    edit->addLayout(form);
    name = UiControls::lineEdit(right);
    name->setObjectName("templateName");
    name->setPlaceholderText("@q1");
    scope = choices({"This file", "Workspace", "User global"}, right);
    scope->setObjectName("templateScope");
    kind = choices({"snippet", "task", "function", "call", "print", "driver"}, right);
    kind->setObjectName("templateKind");
    description = UiControls::lineEdit(right);
    aliases = UiControls::lineEdit(right);
    aliases->setPlaceholderText("Comma-separated search aliases");
    form->addRow("Command", name);
    form->addRow("Aliases", aliases);
    form->addRow("Scope", scope);
    form->addRow("Kind", kind);
    form->addRow("Description", description);
    auto *tabs = UiControls::tabWidget(right);
    edit->addWidget(tabs, 1);
    body = new QPlainTextEdit(tabs);
    body->setObjectName("templateBody");
    body->setPlaceholderText("Code as written. Optional inputs: ${signal}, ${cycles}");
    tabs->addTab(body, "Code");
    auto *sourcePage = new QWidget(tabs);
    auto *sourceLayout = new QVBoxLayout(sourcePage);
    symbol = UiControls::lineEdit(sourcePage);
    symbol->setReadOnly(true);
    symbol->setPlaceholderText("Bind a Slang declaration");
    sourceLayout->addWidget(symbol);
    auto *sourceButtons = new QHBoxLayout;
    sourceLayout->addLayout(sourceButtons);
    auto *bind = UiControls::pushButton("Bind task/function…", sourcePage);
    auto *create = UiControls::pushButton("Create implementation…", sourcePage);
    sourceButtons->addWidget(bind);
    sourceButtons->addWidget(create);
    sourceLayout->addWidget(UiControls::label("Package inputs, in compilation order", sourcePage));
    packages = new QPlainTextEdit(sourcePage);
    packages->setPlaceholderText("One source file per line");
    sourceLayout->addWidget(packages, 1);
    auto *addPackage = UiControls::pushButton("Add package files…", sourcePage);
    sourceLayout->addWidget(addPackage);
    auto *update = UiControls::pushButton("Update in xIPs…", sourcePage);
    update->setObjectName("updateInXips");
    connect(update, &QPushButton::clicked, this,
            [this]
            {
                if (publishAssets)
                    publishAssets(kind->currentText(), name->text(),
                                  packages->toPlainText().split('\n', Qt::SkipEmptyParts));
            });
    sourceLayout->addWidget(update);
    assetNotice = UiControls::label(sourcePage);
    assetNotice->setWordWrap(true);
    sourceLayout->addWidget(assetNotice);
    auto *browse = UiControls::pushButton("Browse assets…", sourcePage);
    sourceLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this,
            [this]
            {
                if (openAssets)
                    openAssets();
            });
    assetWatcher = new QFileSystemWatcher(this);
    connect(assetWatcher, &QFileSystemWatcher::fileChanged, this, [this] { refreshAssetState(); });
    connect(assetWatcher, &QFileSystemWatcher::directoryChanged, this, [this] { refreshAssetState(); });
    connect(packages, &QPlainTextEdit::textChanged, this, &TemplateManagerDialog::refreshAssetState);
    tabs->addTab(sourcePage, "Package");
    auto *inputPage = new QWidget(tabs);
    auto *inputLayout = new QVBoxLayout(inputPage);
    inputTable = UiControls::tableWidget(0, 3, inputPage);
    inputTable->setObjectName("templateInputs");
    inputTable->setHorizontalHeaderLabels({"Input", "Kind", "Default"});
    inputTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    inputLayout->addWidget(inputTable, 1);
    auto *inputButtons = new QHBoxLayout;
    inputLayout->addLayout(inputButtons);
    auto *inputAdd = UiControls::pushButton("Add", inputPage);
    auto *inputRemove = UiControls::pushButton("Remove", inputPage);
    auto *configure = UiControls::pushButton("Constraints…", inputPage);
    inputButtons->addWidget(inputAdd);
    inputButtons->addWidget(inputRemove);
    inputButtons->addWidget(configure);
    tabs->addTab(inputPage, "Inputs");
    auto *eventPage = new QWidget(tabs);
    auto *eventLayout = new QVBoxLayout(eventPage);
    auto *eventForm = new QFormLayout;
    eventLayout->addLayout(eventForm);
    level = choices({"Info", "Warning", "Error"}, eventPage);
    tag = UiControls::lineEdit(eventPage);
    eventMessage = UiControls::lineEdit(eventPage);
    condition = UiControls::lineEdit(eventPage);
    messageInput = UiControls::checkBox("Fill message on insertion", eventPage);
    eventForm->addRow("Level", level);
    eventForm->addRow("Tag", tag);
    eventForm->addRow("Message", eventMessage);
    eventForm->addRow(messageInput);
    eventForm->addRow("Condition", condition);
    fieldTable = UiControls::tableWidget(0, 4, eventPage);
    fieldTable->setHorizontalHeaderLabels({"Name", "Expression (blank = input)", "Radix", "Unit"});
    fieldTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    eventLayout->addWidget(fieldTable, 1);
    auto *fields = new QHBoxLayout;
    eventLayout->addLayout(fields);
    auto *fieldAdd = UiControls::pushButton("Add value", eventPage);
    auto *fieldRemove = UiControls::pushButton("Remove value", eventPage);
    fields->addWidget(fieldAdd);
    fields->addWidget(fieldRemove);
    tabs->addTab(eventPage, "Event preset");
    notice = UiControls::label(this);
    notice->setWordWrap(true);
    notice->setObjectName("templateNotice");
    layout->addWidget(notice);
    auto *actions = UiDialogs::buttonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::accepted, this, &TemplateManagerDialog::save);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list, &QListWidget::currentRowChanged, this, &TemplateManagerDialog::load);
    connect(add, &QPushButton::clicked, this,
            [this]
            {
                list->setCurrentRow(-1);
                load(-1);
                name->setFocus();
            });
    connect(erase, &QPushButton::clicked, this, &TemplateManagerDialog::remove);
    connect(bind, &QPushButton::clicked, this, &TemplateManagerDialog::bindCallable);
    connect(create, &QPushButton::clicked, this, &TemplateManagerDialog::createImplementation);
    connect(roles, &QPushButton::clicked, this, &TemplateManagerDialog::editRoles);
    connect(addPackage, &QPushButton::clicked, this,
            [this]
            {
                const auto files = QFileDialog::getOpenFileNames(this, "Package inputs", workspace,
                                                                 "SystemVerilog (*.sv *.v *.svh)");
                for (const auto &file : files)
                    packages->appendPlainText(file);
            });
    connect(inputAdd, &QPushButton::clicked, this,
            [this] { addInput(QStringLiteral("input%1").arg(inputTable->rowCount() + 1)); });
    connect(inputRemove, &QPushButton::clicked, this,
            [this] { inputTable->removeRow(inputTable->currentRow()); });
    connect(configure, &QPushButton::clicked, this, [this] { configureInput(inputTable->currentRow()); });
    connect(inputTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { configureInput(row); });
    connect(fieldAdd, &QPushButton::clicked, this,
            [this]
            {
                int row = fieldTable->rowCount();
                fieldTable->insertRow(row);
                put(fieldTable, row, 0, "value");
                put(fieldTable, row, 2, "hex");
            });
    connect(fieldRemove, &QPushButton::clicked, this,
            [this] { fieldTable->removeRow(fieldTable->currentRow()); });
    connect(import, &QPushButton::clicked, this,
            [this]
            {
                const auto file =
                    QFileDialog::getOpenFileName(this, "Import legacy command mappings", workspace,
                                                 "Command metadata (xips-commands.json)");
                if (file.isEmpty())
                    return;
                const auto report = service->migrateXipsCommands(file);
                if (report.valid)
                {
                    reload();
                    status("Mappings imported. Original files were retained.");
                }
                else
                    status(reportText(report));
            });
    split->setSizes({240, 640});
    reload();
    load(-1);
    refreshAssetState();
}
void TemplateManagerDialog::setPackageInputs(const QStringList &files)
{
    packages->setPlainText(files.join('\n'));
}
void TemplateManagerDialog::refreshAssetState()
{
    const auto watched = assetWatcher->files() + assetWatcher->directories();
    if (!watched.isEmpty())
        assetWatcher->removePaths(watched);
    QStringList paths;
    for (const auto &path : packages->toPlainText().split('\n', Qt::SkipEmptyParts))
        if (QFileInfo(path).isFile())
            paths << path;
    QStringList states;
    for (const auto &value :
         XipsHostBridge::sourceBindings(workspace, packages->toPlainText().split('\n', Qt::SkipEmptyParts)))
    {
        const auto binding = value.toMap();
        const auto root = QDir(workspace).filePath(binding.value("path").toString());
        for (const auto &file : binding.value("files").toStringList())
        {
            const auto path = binding.value("singleFile").toBool() ? root : QDir(root).filePath(file);
            if (QFileInfo(path).isFile())
                paths << path;
        }
        states << QStringLiteral("%1 · Pinned %2 · Local copy %3")
                      .arg(binding.value("name").toString(), binding.value("revision").toString(),
                           binding.value("localState").toString());
    }
    const auto references = QDir(workspace).filePath(".zeroslack/xips-references.json");
    if (QFileInfo::exists(references))
        paths << references;
    const auto filesToWatch = paths;
    for (const auto &path : filesToWatch)
        paths << QFileInfo(path).absolutePath();
    paths.removeDuplicates();
    if (!paths.isEmpty())
        assetWatcher->addPaths(paths);
    assetNotice->setText(
        states.isEmpty()
            ? QStringLiteral("No xIPs asset binding. Use Update in xIPs to register this package.")
            : states.join('\n'));
}
void TemplateManagerDialog::status(const QString &text)
{
    notice->setText(text);
}
void TemplateManagerDialog::reload()
{
    const QSignalBlocker block(list);
    list->clear();
    records = service->recordsForFile(request.context.fileName, true);
    for (const auto &record : records)
        list->addItem(record.commandToken + (record.command.value("disabled").toBool() ? " [hidden]" : "") +
                      (record.storageSource == "builtin"                              ? "   Built-in"
                       : !record.command.value("file").toString().isEmpty()           ? "   This file"
                       : record.storageSource == service->workspaceTemplateLocation() ? "   Workspace"
                                                                                      : "   Global"));
    revision = service->catalogRevision();
}
void TemplateManagerDialog::load(int row)
{
    ++operation;
    selected = row >= 0 && row < records.size() ? records[row] : UserTemplateRecord{};
    name->setText(selected.commandToken);
    description->setText(selected.description);
    const auto type = selected.command.value("kind", "snippet").toString();
    kind->setCurrentText(type);
    scope->setCurrentIndex(selected.id.isEmpty() || !selected.command.value("file").toString().isEmpty() ? 0
                           : selected.storageSource == service->workspaceTemplateLocation()              ? 1
                                                                                                         : 2);
    if (selected.storageSource == "builtin")
        scope->setCurrentIndex(workspace.isEmpty() ? 2 : 1);
    symbol->setText(selected.command.value("symbol").toString());
    packages->setPlainText(selected.command.value("compileOrder").toStringList().join('\n'));
    auto text = selected.insertText;
    auto placeholders = selected.templateSlots;
    std::sort(placeholders.begin(), placeholders.end(),
              [](const auto &a, const auto &b) { return a.start > b.start; });
    for (const auto &slot : placeholders)
        text.replace(slot.start, slot.length, "${" + slot.name + "}");
    body->setPlainText(text);
    inputTable->setRowCount(0);
    QSet<QString> added;
    for (const auto &slot : selected.templateSlots)
        if (!added.contains(slot.name))
        {
            auto input = slot.input;
            input.insert("default", selected.insertText.mid(slot.start, slot.length));
            addInput(slot.name, input);
            added.insert(slot.name);
        }
    QStringList searchAliases;
    for (const auto &value : selected.command.value("aliases").toList())
        searchAliases << value.toString();
    if (searchAliases.isEmpty())
        searchAliases = selected.command.value("aliases").toStringList();
    aliases->setText(searchAliases.join(", "));
    const auto configs = selected.command.value("inputs").toMap();
    for (auto it = configs.cbegin(); it != configs.cend(); ++it)
        if (!added.contains(it.key()))
            addInput(it.key(), it.value().toMap());
    const auto preset = selected.command.value("print").toMap();
    level->setCurrentText(preset.value("level", "Info").toString());
    tag->setText(preset.value("tag").toString());
    eventMessage->setText(preset.value("message").toString());
    messageInput->setChecked(!preset.contains("message"));
    condition->setText(preset.value("condition").toString());
    fieldTable->setRowCount(0);
    for (const auto &value : preset.value("fields").toList())
    {
        auto f = value.toMap();
        int row = fieldTable->rowCount();
        fieldTable->insertRow(row);
        put(fieldTable, row, 0, f.value("name").toString());
        put(fieldTable, row, 1, f.value("expression").toString());
        put(fieldTable, row, 2, f.value("radix", "hex").toString());
        put(fieldTable, row, 3, f.value("unit").toString());
    }
    revision = service->catalogRevision();
    status("File commands apply only to the current saved file. Use ${name} for snippet inputs.");
}
void TemplateManagerDialog::addInput(const QString &inputName, const QVariantMap &input)
{
    const int row = inputTable->rowCount();
    inputTable->insertRow(row);
    put(inputTable, row, 0, inputName);
    inputTable->item(row, 0)->setData(Qt::UserRole, input);
    auto *combo =
        choices({"Instance", "Port", "Internal", "Signal", "Parameter", "Choice", "Value", "Code"}, inputTable);
    combo->setCurrentText(input.value("kind", "Value").toString());
    inputTable->setCellWidget(row, 1, combo);
    put(inputTable, row, 2, input.value("default").toString());
}
QVariantMap TemplateManagerDialog::inputs() const
{
    QVariantMap result;
    for (int row = 0; row < inputTable->rowCount(); ++row)
    {
        auto input = inputTable->item(row, 0)->data(Qt::UserRole).toMap();
        input.insert("kind", qobject_cast<QComboBox *>(inputTable->cellWidget(row, 1))->currentText());
        input.insert("default", cell(inputTable, row, 2));
        result.insert(cell(inputTable, row, 0), input);
    }
    return result;
}
void TemplateManagerDialog::configureInput(int row)
{
    if (row < 0)
        return;
    auto input = inputs().value(cell(inputTable, row, 0)).toMap();
    UiDialog dialog(this);
    dialog.setWindowTitle("Input constraints");
    auto *layout = new QVBoxLayout(&dialog);
    auto *columns = new QHBoxLayout;
    auto *form = new QFormLayout;
    auto *limits = new QFormLayout;
    columns->addLayout(form);
    columns->addSpacing(16);
    columns->addLayout(limits);
    layout->addLayout(columns);
    const QMap<QString, QString> labels{{"scope", "Object scope"},
                                        {"role", "Signal role"},
                                        {"direction", "Port direction"},
                                        {"access", "Required access"},
                                        {"content", "Code container content"},
                                        {"unit", "Range time unit"},
                                        {"instanceSlot", "Instance input"},
                                        {"enumOf", "Enum signal input"},
                                        {"minWidth", "Minimum bit width"},
                                        {"maxWidth", "Maximum bit width"},
                                        {"packedRank", "Packed dimensions"},
                                        {"unpackedRank", "Unpacked dimensions"},
                                        {"minimum", "Minimum value"},
                                        {"maximum", "Maximum value"},
                                        {"recursive", "Include nested instances"},
                                        {"integral", "Integral type"},
                                        {"integer", "Integer value"},
                                        {"known", "Require known value (no X/Z)"}};
    QMap<QString, QComboBox *> combos;
    for (const auto &entry :
         QList<QPair<QString, QStringList>>{{"scope", {"visible", "instance", "hierarchy", "package"}},
                                            {"role", {"", "Clock", "Reset"}},
                                            {"content", {"statements", "declarations"}},
                                            {"direction", {"any", "input", "output", "inout", "ref"}},
                                            {"access", {"read", "write", "readwrite"}},
                                            {"unit", {"", "s", "ms", "us", "ns", "ps", "fs"}}})
    {
        auto *combo = choices(entry.second, &dialog);
        combo->setCurrentText(input.value(entry.first, entry.second.first()).toString());
        form->addRow(labels.value(entry.first), combo);
        combos.insert(entry.first, combo);
    }
    QMap<QString, QLineEdit *> fields;
    for (const auto *key : {"instanceSlot", "enumOf", "minWidth", "maxWidth", "packedRank", "unpackedRank",
                            "minimum", "maximum"})
    {
        auto *field = UiControls::lineEdit(input.value(key).toString(), &dialog);
        limits->addRow(labels.value(key), field);
        fields.insert(key, field);
    }
    auto *values = UiControls::lineEdit(input.value("choices").toStringList().join(", "), &dialog);
    form->addRow("Choice values (comma separated)", values);
    QMap<QString, QCheckBox *> checks;
    auto *options = new QGridLayout;
    layout->addLayout(options);
    int optionIndex = 0;
    for (const auto *key : {"recursive", "integral", "integer", "known"})
    {
        auto *check = UiControls::checkBox(labels.value(key), &dialog);
        check->setChecked(input.value(key, QString::fromLatin1(key) == "known").toBool());
        options->addWidget(check, optionIndex / 2, optionIndex % 2);
        ++optionIndex;
        checks.insert(key, check);
    }
    auto *actions = UiDialogs::buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    for (auto it = combos.cbegin(); it != combos.cend(); ++it)
    {
        if (it.value()->currentText().isEmpty())
            input.remove(it.key());
        else
            input.insert(it.key(), it.value()->currentText());
    }
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
    {
        if (it.value()->text().isEmpty())
            input.remove(it.key());
        else
            input.insert(it.key(), it.value()->text());
    }
    for (auto it = checks.cbegin(); it != checks.cend(); ++it)
        input.insert(it.key(), it.value()->isChecked());
    QStringList choices;
    for (const auto &value : values->text().split(',', Qt::SkipEmptyParts))
        choices << value.trimmed();
    input.insert("choices", choices);
    const auto issue = TemplateInputService::specificationError(input);
    if (!issue.isEmpty())
    {
        status(issue);
        return;
    }
    inputTable->item(row, 0)->setData(Qt::UserRole, input);
}
void TemplateManagerDialog::save()
{
    if (revision != service->catalogRevision())
    {
        status("Templates changed outside this dialog. Reopen before saving; your edits were not committed.");
        return;
    }
    auto record = selected;
    if (record.id.isEmpty() || record.storageSource == "builtin")
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.commandToken = name->text().trimmed();
    if (!record.commandToken.startsWith('@'))
        record.commandToken.prepend('@');
    if (record.label.isEmpty() || record.label == selected.commandToken)
        record.label = record.commandToken;
    record.description = description->text();
    record.command.remove("disabled");
    QStringList searchAliases;
    for (const auto &value : aliases->text().split(',', Qt::SkipEmptyParts))
        searchAliases << value.trimmed();
    searchAliases.removeDuplicates();
    record.command.insert("aliases", searchAliases);
    record.selectionStart = -1;
    record.selectionLength = 0;
    if (scope->currentIndex() == 0)
    {
        if (request.context.fileName.isEmpty() || !QFileInfo(request.context.fileName).isAbsolute())
        {
            status("Save this file before creating a file command.");
            return;
        }
        record.command.insert("file", request.context.fileName);
    }
    else
        record.command.remove("file");
    const bool toWorkspace = scope->currentIndex() != 2 && !workspace.isEmpty();
    if (scope->currentIndex() == 1 && workspace.isEmpty())
    {
        status("Open a workspace before choosing Workspace scope.");
        return;
    }
    if (!selected.storageSource.isEmpty() && selected.storageSource != "builtin" &&
        selected.storageSource !=
            (toWorkspace ? service->workspaceTemplateLocation() : service->globalTemplateLocation()))
    {
        status(
            "Save a New template to copy it to another storage scope; the existing definition is preserved.");
        return;
    }
    const auto inputConfig = inputs();
    if (inputConfig.size() != inputTable->rowCount())
    {
        status("Input names must be unique.");
        return;
    }
    record.command.insert("kind", kind->currentText());
    record.command.insert("inputs", inputConfig);
    record.command.insert("summary", inputConfig.keys().mid(0, 3).join(" · "));
    record.command.insert("compileOrder", packages->toPlainText().split('\n', Qt::SkipEmptyParts));
    record.templateSlots.clear();
    record.insertText.clear();
    if (kind->currentText() == "snippet")
    {
        const auto source = body->toPlainText();
        QRegularExpression marker("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}");
        auto matches = marker.globalMatch(source);
        int previous = 0;
        QHash<QString, int> stops;
        while (matches.hasNext())
        {
            auto match = matches.next();
            record.insertText += source.mid(previous, match.capturedStart() - previous);
            const auto inputName = match.captured(1);
            auto input = inputConfig.value(inputName).toMap();
            if (input.isEmpty())
            {
                status("Configure input " + inputName + " in Inputs before saving.");
                return;
            }
            const auto value = input.value("default", input.value("kind").toString() == "Code" ? QString() : "__" + inputName + "__").toString();
            if (!stops.contains(inputName))
                stops.insert(inputName, stops.size() + 1);
            record.templateSlots << CodeTemplateSlot{
                inputName, int(record.insertText.size()), int(value.size()), stops.value(inputName), true,
                input};
            record.insertText += value;
            previous = match.capturedEnd();
        }
        record.insertText += source.mid(previous);
        record.command.remove("symbol");
        record.command.remove("sourceFile");
    }
    else if (kind->currentText() == "print")
    {
        QVariantMap preset{
            {"level", level->currentText()}, {"tag", tag->text()}, {"condition", condition->text()}};
        if (!messageInput->isChecked())
            preset.insert("message", eventMessage->text());
        QVariantList values;
        for (int row = 0; row < fieldTable->rowCount(); ++row)
        {
            if (cell(fieldTable, row, 0).isEmpty() ||
                !QStringList{"hex", "binary", "decimal", "text", "real"}.contains(cell(fieldTable, row, 2)))
            {
                status(
                    "Each event value needs a name and a valid radix: hex, binary, decimal, text or real.");
                return;
            }
            values << QVariantMap{{"name", cell(fieldTable, row, 0)},
                                  {"expression", cell(fieldTable, row, 1)},
                                  {"radix", cell(fieldTable, row, 2)},
                                  {"unit", cell(fieldTable, row, 3)}};
        }
        preset.insert("fields", values);
        record.command.insert("print", preset);
    }
    else
    {
        if (symbol->text().isEmpty())
        {
            status("Bind a Slang task/function declaration first.");
            return;
        }
        record.command.insert("symbol", symbol->text());
    }
    const auto report = service->saveRecord(record, toWorkspace, revision);
    if (!report.valid)
    {
        status(reportText(report));
        return;
    }
    reload();
    for (int i = 0; i < records.size(); ++i)
        if (records[i].id == record.id)
        {
            list->setCurrentRow(i);
            break;
        }
    status("Template saved.");
}
void TemplateManagerDialog::remove()
{
    if (selected.id.isEmpty())
        return;
    if (revision != service->catalogRevision())
    {
        status("Templates changed. Reopen before deleting.");
        return;
    }
    if (selected.storageSource == "builtin")
    {
        auto hidden = selected;
        hidden.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        hidden.command.insert("disabled", true);
        const auto report = service->saveRecord(hidden, !workspace.isEmpty(), revision);
        if (!report.valid)
        {
            status(reportText(report));
            return;
        }
        reload();
        load(-1);
        status("Built-in command hidden. Delete its override to restore it; source assets were retained.");
        return;
    }
    const auto report = service->deleteRecord(
        selected.id, selected.storageSource == service->workspaceTemplateLocation(), revision);
    if (!report.valid)
    {
        status(reportText(report));
        return;
    }
    reload();
    load(-1);
    status("Template deleted. Source packages were retained.");
}
void TemplateManagerDialog::inspect(const std::function<void(const QList<SemanticSymbolRecord> &)> &done)
{
    const auto generation = ++operation;
    auto analysis = request;
    analysis.orderedFiles = packages->toPlainText().split('\n', Qt::SkipEmptyParts) + analysis.orderedFiles;
    analysis.orderedFiles.removeDuplicates();
    status("Resolving source declarations with Slang…");
    auto *watcher = new QFutureWatcher<SlangWorkspaceAnalysis>(this);
    connect(watcher, &QFutureWatcher<SlangWorkspaceAnalysis>::finished, this,
            [this, watcher, done, generation]
            {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (generation != operation)
                    return;
                if (!result.error.isEmpty())
                {
                    status(result.error);
                    return;
                }
                status("Source declarations resolved.");
                done(result.symbols);
            });
    watcher->setFuture(QtConcurrent::run(
        [analysis]
        {
            SemanticAnalysisRequest sources;
            sources.sourceOverrides = analysis.contents;
            SemanticInputCapture capture(sources);
            SlangManager slang;
            auto result =
                slang.analyzeCapturedWorkspace(capture, analysis.orderedFiles, analysis.includeDirectories,
                                               analysis.defines, {}, {}, {true, true, false});
            if (!capture.stillMatchesDisk({}))
                result.error = "Sources changed while resolving declarations.";
            return result;
        }));
}
void TemplateManagerDialog::bindCallable()
{
    inspect(
        [this](const QList<SemanticSymbolRecord> &symbols)
        {
            QStringList labels;
            QList<SemanticSymbolRecord> callables;
            QSet<QString> seen;
            for (const auto &record : symbols)
            {
                if (!record.presentation.callableSignatureAvailable)
                    continue;
                const auto key = symbolStableKeyText(record.stableKey);
                if (seen.contains(key))
                    continue;
                seen.insert(key);
                callables << record;
                labels << record.presentation.callableQualifiedName + " — " +
                              QFileInfo(record.location.fileName).fileName() + ":" +
                              QString::number(record.location.startLine);
            }
            if (labels.isEmpty())
            {
                status("No callable was found. Add its package inputs in compilation order.");
                return;
            }
            bool ok = false;
            auto choice =
                UiDialogs::getItem(this, "Bind declaration", "Task or function", labels, 0, false, &ok);
            if (!ok)
                return;
            const auto record = callables[labels.indexOf(choice)];
            symbol->setText(record.presentation.callableQualifiedName);
            selected.command.insert("sourceFile", record.location.fileName);
            selected.command.insert("sourceIdentity", symbolStableKeyText(record.stableKey));
            kind->setCurrentText(
                record.declarationKind == SymbolTaxonomy::DeclarationKind::Task ? "task" : "function");
            auto files = packages->toPlainText().split('\n', Qt::SkipEmptyParts);
            const auto bindings = XipsHostBridge::sourceBindings(workspace, {record.location.fileName});
            for (const auto &value : bindings)
            {
                const auto binding = value.toMap();
                QStringList ordered;
                const auto root = QDir(workspace).filePath(binding.value("path").toString());
                for (const auto &file : binding.value("compileOrder").toStringList())
                {
                    const auto path = binding.value("singleFile").toBool() ? root : QDir(root).filePath(file);
                    if (QFileInfo(path).isFile())
                        ordered << path;
                }
                if (!ordered.isEmpty())
                {
                    files = ordered + files;
                    files.removeDuplicates();
                }
            }
            QSet<QString> packageFiles;
            for (const auto &candidate : symbols)
                if (candidate.declarationKind == SymbolTaxonomy::DeclarationKind::Package)
                    packageFiles.insert(candidate.location.fileName);
            for (const auto &file : request.orderedFiles)
                if (packageFiles.contains(file) && !files.contains(file))
                    files << file;
            if (!files.contains(record.location.fileName))
                files << record.location.fileName;
            selected.command.insert("includeDirectories", request.includeDirectories);
            packages->setPlainText(files.join('\n'));
            inputTable->setRowCount(0);
            for (const auto &argument : record.presentation.callableArguments)
                addInput(argument.name, {{"kind", argument.input ? "Value" : "Signal"},
                                         {"access", argument.input ? "read" : "write"},
                                         {"default", argument.input ? "0" : ""}});
        });
}
void TemplateManagerDialog::editRoles()
{
    inspect(
        [this](const QList<SemanticSymbolRecord> &symbols)
        {
            UiDialog dialog(this);
            dialog.setWindowTitle("Signal roles");
            dialog.resize(720, 460);
            auto *layout = new QVBoxLayout(&dialog);
            layout->addWidget(UiControls::label(
                "Assign Clock or Reset explicitly. Unassigned signals have no inferred role.", &dialog));
            auto *table = UiControls::tableWidget(0, 2, &dialog);
            table->setHorizontalHeaderLabels({"Slang object", "Role"});
            table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
            layout->addWidget(table, 1);
            auto roles = service->signalRoles();
            QSet<QString> seen;
            for (const auto &record : symbols)
            {
                if (record.declarationKind != SymbolTaxonomy::DeclarationKind::Signal &&
                    record.declarationKind != SymbolTaxonomy::DeclarationKind::Port)
                    continue;
                QStringList paths = record.presentation.objectInfoByPath.keys();
                paths << record.presentation.qualifiedScopePath + '.' + record.name;
                for (const auto &path : paths)
                {
                    auto key = TemplateInputService::roleKey(record, path);
                    if (seen.contains(key))
                        continue;
                    seen.insert(key);
                    int row = table->rowCount();
                    table->insertRow(row);
                    put(table, row, 0, path + " — " + QFileInfo(record.location.fileName).fileName());
                    table->item(row, 0)->setData(Qt::UserRole, key);
                    table->item(row, 0)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                    auto *combo = choices({"", "Clock", "Reset"}, table);
                    combo->setCurrentText(roles.value(key).toString());
                    table->setCellWidget(row, 1, combo);
                }
            }
            auto *buttons = UiDialogs::buttonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            for (int row = 0; row < table->rowCount(); ++row)
            {
                const auto key = table->item(row, 0)->data(Qt::UserRole).toString();
                const auto value = qobject_cast<QComboBox *>(table->cellWidget(row, 1))->currentText();
                if (value.isEmpty())
                    roles.remove(key);
                else
                    roles.insert(key, value);
            }
            const auto error = service->setSignalRoles(roles);
            status(error.isEmpty() ? "Signal roles saved." : error);
            revision = service->catalogRevision();
        });
}
void TemplateManagerDialog::createImplementation()
{
    if (workspace.isEmpty())
    {
        status("Open a workspace before creating a package implementation.");
        return;
    }
    inspect(
        [this](const QList<SemanticSymbolRecord> &symbols)
        {
            QList<SemanticSymbolRecord> packagesFound;
            QStringList destinations{"New package"};
            for (const auto &record : symbols)
                if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Package)
                {
                    packagesFound << record;
                    destinations << record.name + " — " + record.location.fileName;
                }
            UiDialog dialog(this);
            dialog.setWindowTitle("Create implementation");
            dialog.setObjectName("createTemplateImplementation");
            dialog.resize(660, 430);
            auto *layout = new QVBoxLayout(&dialog);
            auto *destination = choices(destinations, &dialog);
            destination->setObjectName("implementationPackage");
            layout->addWidget(destination);
            auto *packageName = UiControls::lineEdit("user_package", &dialog);
            packageName->setPlaceholderText("New package name");
            packageName->setObjectName("implementationPackageName");
            layout->addWidget(packageName);
            auto *code = new QPlainTextEdit(&dialog);
            code->setObjectName("implementationCode");
            code->setPlainText(
                "task automatic user_task(input int cycles);\n  repeat (cycles) #1ns;\nendtask\n");
            layout->addWidget(code, 1);
            auto *buttons = UiDialogs::buttonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            const bool existing = destination->currentIndex() > 0;
            QString target, original, updated, expected, packageIdentity;
            if (existing)
            {
                const auto record = packagesFound[destination->currentIndex() - 1];
                target = record.location.fileName;
                packageIdentity = record.name;
                QFile file(target);
                if (!file.open(QIODevice::ReadOnly))
                {
                    status(file.errorString());
                    return;
                }
                const auto originalBytes = file.readAll();
                expected = QString::fromLatin1(
                    QCryptographicHash::hash(originalBytes, QCryptographicHash::Sha256).toHex());
                original = QString::fromUtf8(originalBytes);
                if (request.contents.contains(target) && request.contents.value(target) != original)
                {
                    status("Save the open package before extending its implementation.");
                    return;
                }
                TSDocument syntax;
                syntax.setText(original);
                auto node = ts_node_descendant_for_byte_range(syntax.rootNode(), record.location.position * 2,
                                                              record.location.position * 2 + 2);
                while (!ts_node_is_null(node) && qstrcmp(ts_node_type(node), "package_declaration") != 0)
                    node = ts_node_parent(node);
                int insertion = -1;
                for (uint32_t i = 0; !ts_node_is_null(node) && i < ts_node_child_count(node); ++i)
                {
                    auto child = ts_node_child(node, i);
                    if (qstrcmp(ts_node_type(child), "endpackage") == 0)
                        insertion = ts_node_start_byte(child) / 2;
                }
                if (insertion < 0)
                {
                    status("The resolved package syntax changed. Rebind before editing.");
                    return;
                }
                updated = original;
                updated.insert(insertion, "\n" + code->toPlainText() + "\n");
            }
            else
            {
                const auto package = packageName->text().trimmed();
                if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_$]*$").match(package).hasMatch())
                {
                    status("Use a valid SV package identifier.");
                    return;
                }
                target = QFileDialog::getSaveFileName(
                    this, "New package", QDir(workspace).filePath(package + ".sv"), "SystemVerilog (*.sv)");
                if (target.isEmpty())
                    return;
                if (QFileInfo::exists(target))
                {
                    status("The selected path already exists. Bind its actual package instead.");
                    return;
                }
                updated = "package " + package + ";\n  timeunit 1ns; timeprecision 1ps;\n" +
                          code->toPlainText() + "\nendpackage\n";
            }
            const auto canonicalRoot = QFileInfo(workspace).canonicalFilePath() + '/';
            const auto parentPath = QFileInfo(QFileInfo(target).absolutePath()).canonicalFilePath() + '/';
            if (!parentPath.startsWith(canonicalRoot, Qt::CaseInsensitive))
            {
                status("Create or extend a package inside the workspace.");
                return;
            }
            const auto generation = ++operation;
            auto analysis = request;
            analysis.orderedFiles =
                packages->toPlainText().split('\n', Qt::SkipEmptyParts) + analysis.orderedFiles;
            analysis.orderedFiles.removeAll(target);
            const auto sourcePosition = analysis.orderedFiles.indexOf(request.context.fileName);
            analysis.orderedFiles.insert(sourcePosition < 0 ? analysis.orderedFiles.size() : sourcePosition,
                                         target);
            analysis.contents.insert(target, updated);
            auto *watcher = new QFutureWatcher<SlangWorkspaceAnalysis>(this);
            connect(watcher, &QFutureWatcher<SlangWorkspaceAnalysis>::finished, this,
                    [=, this]
                    {
                        const auto checked = watcher->result();
                        watcher->deleteLater();
                        if (generation != operation)
                            return;
                        if (!checked.error.isEmpty())
                        {
                            status(checked.error);
                            return;
                        }
                        for (const auto &issue : checked.diagnostics)
                            if (issue.severity == SemanticDiagnostic::Error &&
                                !EditorFileIdentity::same(issue.fileName, request.context.fileName))
                            {
                                status(issue.message);
                                return;
                            }
                        if (existing)
                        {
                            bool found = false;
                            for (const auto &record : checked.symbols)
                                found |= record.declarationKind == SymbolTaxonomy::DeclarationKind::Package &&
                                         record.name == packageIdentity &&
                                         EditorFileIdentity::same(record.location.fileName, target);
                            if (!found)
                            {
                                status("The package declaration changed. No file was written.");
                                return;
                            }
                        }
                        if (digest(target) != expected || (!existing && QFileInfo::exists(target)))
                        {
                            status("The package changed during validation. No file was written.");
                            return;
                        }
                        QSaveFile file(target);
                        file.setDirectWriteFallback(false);
                        const auto bytes = updated.toUtf8();
                        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
                            !file.commit())
                        {
                            status("The package could not be committed; previous source was retained.");
                            return;
                        }
                        request.contents.insert(target, updated);
                        auto files = packages->toPlainText().split('\n', Qt::SkipEmptyParts);
                        if (!files.contains(target))
                            files << target;
                        packages->setPlainText(files.join('\n'));
                        status(
                            "Implementation saved. Bind its resolved declaration, then save the template.");
                    });
            watcher->setFuture(QtConcurrent::run(
                [analysis]
                {
                    SemanticAnalysisRequest sources;
                    sources.sourceOverrides = analysis.contents;
                    SemanticInputCapture capture(sources);
                    SlangManager slang;
                    auto result = slang.analyzeCapturedWorkspace(
                        capture, analysis.orderedFiles, analysis.includeDirectories, analysis.defines, {}, {},
                        {true, true, false});
                    if (!capture.stillMatchesDisk({}))
                        result.error = "A package dependency changed during validation.";
                    return result;
                }));
        });
}
