#include "xipsassetsyncdialog.h"
#include "../integrations/xips/xipsassetsync.h"
#include "../integrations/xips/xipscontextprovider.h"
#include "uicontrols.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QtWidgets>
#include <zeroslack/documents/documentfileread.h>
#include <zeroslack/documents/editorfileidentity.h>

XipsAssetSyncDialog::XipsAssetSyncDialog(const QString &root, const QStringList &sources,
                                         const QString &assetName, const QString &defaultLibrary,
                                         QWidget *parent)
    : UiDialog(parent), workspace(root), files(sources)
{
    setObjectName("xipsAssetSync");
    setWindowTitle("Update in xIPs");
    setWindowModality(Qt::WindowModal);
    resize(720, 480);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    binding = UiControls::comboBox(this);
    binding->setObjectName("assetBinding");
    for (const auto &entry : XipsHostBridge::sourceBindings(workspace, files))
    {
        const auto ref = entry.toMap();
        binding->addItem(ref.value("name").toString() + " · " + ref.value("revision").toString(), ref);
    }
    binding->addItem("Register as a new asset", QVariantMap{});
    form->addRow("Asset", binding);
    auto *libraryRow = new QHBoxLayout;
    library = UiControls::lineEdit(this);
    library->setObjectName("assetLibrary");
    library->setText(defaultLibrary);
    libraryRow->addWidget(library, 1);
    browseButton = UiControls::pushButton("Choose…", this);
    libraryRow->addWidget(browseButton);
    form->addRow("Library", libraryRow);
    name = UiControls::lineEdit(this);
    name->setObjectName("assetName");
    name->setText(assetName);
    form->addRow("Name", name);
    details = new QPlainTextEdit(this);
    details->setObjectName("assetPreview");
    details->setReadOnly(true);
    details->setPlainText(files.join('\n'));
    layout->addWidget(details, 1);
    status = UiControls::label(
        "Preview the complete package before saving a version. Other projects keep their pinned versions.",
        this);
    status->setObjectName("assetSyncStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    previewButton = UiControls::pushButton("Preview", this);
    commitButton = UiControls::pushButton("Save version", this);
    recoverButton = UiControls::pushButton("Recover operation…", this);
    cancelButton = UiControls::pushButton("Cancel", this);
    previewButton->setObjectName("assetPreviewButton");
    commitButton->setObjectName("assetCommitButton");
    recoverButton->setObjectName("assetRecoverButton");
    commitButton->setEnabled(false);
    for (auto *button : {previewButton, commitButton, recoverButton, cancelButton})
        buttons->addWidget(button);
    const auto selection = [this]
    {
        operation.clear();
        commitButton->setEnabled(false);
        const auto ref = binding->currentData().toMap();
        name->setEnabled(ref.isEmpty());
        library->setEnabled(ref.isEmpty());
        browseButton->setEnabled(ref.isEmpty());
        if (!ref.isEmpty())
        {
            library->setText(ref.value("library").toString());
            name->setText(ref.value("name").toString());
        }
    };
    connect(binding, &QComboBox::currentIndexChanged, this, selection);
    selection();
    connect(browseButton, &QPushButton::clicked, this,
            [this]
            {
                const auto selected =
                    QFileDialog::getExistingDirectory(this, "Choose xIPs library", library->text());
                if (!selected.isEmpty())
                    library->setText(selected);
            });
    const auto invalidate = [this]
    {
        operation.clear();
        commitButton->setEnabled(false);
    };
    connect(library, &QLineEdit::textEdited, this, invalidate);
    connect(name, &QLineEdit::textEdited, this, invalidate);
    connect(previewButton, &QPushButton::clicked, this, &XipsAssetSyncDialog::preview);
    connect(commitButton, &QPushButton::clicked, this, [this] { run("commit"); });
    connect(recoverButton, &QPushButton::clicked, this, &XipsAssetSyncDialog::recover);
    connect(cancelButton, &QPushButton::clicked, this,
            [this]
            {
                if (busy)
                {
                    cancelled->store(true);
                    status->setText("Cancellation requested. Waiting for the publication result.");
                }
                else if (operation.value("status") == "applied")
                    reject();
                else if (!operation.isEmpty())
                    run("cancel");
                else
                    reject();
            });
}
XipsAssetSyncDialog::~XipsAssetSyncDialog()
{
    if (cancelled)
        cancelled->store(true);
}
void XipsAssetSyncDialog::reject()
{
    if (busy)
    {
        closeRequested = true;
        cancelled->store(true);
        status->setText("Waiting for the asset operation to settle.");
        return;
    }
    UiDialog::reject();
}
void XipsAssetSyncDialog::setBusy(bool value)
{
    busy = value;
    previewButton->setEnabled(!value);
    recoverButton->setEnabled(!value);
    binding->setEnabled(!value);
    const bool creating = binding->currentData().toMap().isEmpty();
    library->setEnabled(!value && creating);
    name->setEnabled(!value && creating);
    browseButton->setEnabled(!value && creating);
    const auto state = operation.value("lastResponse").toMap().value("state").toString();
    commitButton->setEnabled(!value && (state == "previewed" || state == "interrupted"));
}
void XipsAssetSyncDialog::preview()
{
    if (busy)
        return;
    if (!QFileInfo(XipsAssetSync::executable()).isFile())
    {
        status->setText("The xIPs asset adapter is unavailable.");
        return;
    }
    operation.clear();
    const auto context = currentContext ? currentContext() : SimulationCommandRequest{};
    const auto ref = binding->currentData().toMap();
    const auto root = workspace, target = library->text(), label = name->text();
    const auto sources = files;
    cancelled = std::make_shared<std::atomic_bool>(false);
    const auto flag = cancelled;
    setBusy(true);
    status->setText("Checking saved package inputs and dependencies…");
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
            [this, watcher]
            {
                const auto result = watcher->result();
                watcher->deleteLater();
                completed(result);
            });
    watcher->setFuture(QtConcurrent::run(
        [root, sources, context, ref, target, label, flag]
        {
            const auto plan = XipsAssetSync::prepare(root, sources, context, ref, target, label, flag);
            if (!plan.error.isEmpty())
                return QVariantMap{{"error", QVariantMap{{"message", plan.error}}}};
            return XipsAssetSync::execute(plan.operation, "preview", flag);
        }));
}
QString XipsAssetSyncDialog::currentError() const
{
    if (!currentContext)
        return "The project context is unavailable.";
    const auto context = currentContext();
    const auto saved = operation.value("projectContext").toMap();
    QVariantMap defines;
    for (auto it = context.defines.cbegin(); it != context.defines.cend(); ++it)
        defines.insert(it.key(), it.value());
    if (saved.value("includeDirectories").toStringList() != context.includeDirectories ||
        saved.value("defines").toMap() != defines)
        return "Project includes or defines changed. Create a new preview.";
    for (const auto &entry : operation.value("request").toMap().value("files").toList())
    {
        const auto source = entry.toMap().value("source").toString();
        for (auto it = context.contents.cbegin(); it != context.contents.cend(); ++it)
            if (EditorFileIdentity::same(it.key(), source))
            {
                const auto disk = readDocumentFile(source);
                if (!disk.available || disk.text != it.value())
                    return "Save open package edits, then create a new preview.";
            }
    }
    return XipsAssetSync::inputsCurrent(operation);
}
void XipsAssetSyncDialog::run(const QString &action)
{
    if (busy || operation.isEmpty())
        return;
    if (action == "commit")
    {
        const auto error = currentError();
        if (!error.isEmpty())
        {
            status->setText(error);
            commitButton->setEnabled(false);
            return;
        }
    }
    cancelled = std::make_shared<std::atomic_bool>(false);
    setBusy(true);
    status->setText(action == "commit" ? "Saving the asset version…" : "Checking the retained operation…");
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
            [this, watcher]
            {
                const auto result = watcher->result();
                watcher->deleteLater();
                completed(result);
            });
    watcher->setFuture(QtConcurrent::run([saved = operation, action, flag = cancelled]
                                         { return XipsAssetSync::execute(saved, action, flag); }));
}
void XipsAssetSyncDialog::recover()
{
    QString error;
    const auto operations = XipsAssetSync::recoverable(workspace, &error);
    if (!error.isEmpty())
    {
        status->setText(error);
        return;
    }
    if (operations.isEmpty())
    {
        status->setText("No incomplete asset operations.");
        return;
    }
    QStringList labels;
    for (const auto &value : operations)
    {
        const auto saved = value.toMap();
        labels << saved.value("requestId").toString() + " · " + saved.value("status").toString();
    }
    bool accepted = false;
    const auto chosen =
        QInputDialog::getItem(this, "Recover asset operation", "Operation", labels, 0, false, &accepted);
    if (!accepted)
        return;
    operation = operations.value(labels.indexOf(chosen)).toMap();
    run("operationStatus");
}
void XipsAssetSyncDialog::completed(const QVariantMap &result)
{
    if (result.contains("operation"))
        operation = result.value("operation").toMap();
    setBusy(false);
    const auto state = result.value("state").toString();
    QStringList lines;
    for (const auto &entry : operation.value("request").toMap().value("files").toList())
    {
        const auto file = entry.toMap();
        lines << file.value("path").toString() + "  " + file.value("sha256").toString().left(12);
    }
    if (!lines.isEmpty())
        details->setPlainText(lines.join('\n'));
    QString message = result.value("error").toMap().value("message").toString();
    if (result.value("referenceApplied").toBool())
    {
        message = result.value("publication") == "reused"
                      ? "The asset was unchanged; its existing version is pinned."
                      : "Version saved. This project now pins the published version.";
        previewButton->setEnabled(false);
        binding->setEnabled(false);
        library->setEnabled(false);
        name->setEnabled(false);
        browseButton->setEnabled(false);
        cancelButton->setText("Close");
        emit referenceChanged();
    }
    else if (!result.value("referenceError").toString().isEmpty())
        message = "Version published; the project reference still needs recovery. " +
                  result.value("referenceError").toString();
    else if (state == "previewed")
        message = "Preview ready. Save version publishes these exact inputs.";
    else if (state == "cancelled")
        message = "Operation cancelled before publication.";
    else if (state == "interrupted")
        message = "No version was published. The same operation may be resumed.";
    else if (message.isEmpty())
        message = "Publication is not confirmed. Recover this operation before retrying.";
    if (!result.value("recoveryWarning").toString().isEmpty())
        message += '\n' + result.value("recoveryWarning").toString();
    status->setText(message);
    commitButton->setEnabled(state == "previewed" || state == "interrupted");
    if (QStringList{"indeterminate", "unknown", "publishing", "committing"}.contains(state) ||
        result.contains("referenceError"))
        previewButton->setEnabled(false);
    if (closeRequested)
        UiDialog::reject();
}
