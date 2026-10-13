#include "xipshdlcommandservice.h"
#include "xipscontextprovider.h"
#include "../native/nativecontextview.h"
#include "../../simulation/simdock/ui/workbench.h"
#include "../../simulation/simdock/core/workspace.h"
#include "tabmanager.h"
#include "workspacemanager.h"
#include "mycodeeditor.h"
#include "editorsemanticcontextservice.h"
#include "shareddocument.h"
#include "editorfileidentity.h"
#include "semanticindexsnapshot.h"
#include "uicontrols.h"
#include "uidialogs.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace {
QStringList strings(const QVariant& value) {
    QStringList result;
    for (const auto& item : value.toList()) result << item.toString();
    if (result.isEmpty()) result = value.toStringList();
    return result;
}
QStringList absoluteInputs(const QVariantMap& receipt, const QString& key, QString* error) {
    const auto root = receipt.value(QStringLiteral("path")).toString();
    QStringList result;
    for (const auto& relative : strings(receipt.value(QStringLiteral("hdlCommand")).toMap().value(key))) {
        const auto file = QDir::cleanPath(QDir(root).filePath(relative));
        if (relative.isEmpty() || QDir::isAbsolutePath(relative) || !simdock::insideWorkspace(root, file)) {
            *error = QStringLiteral("The exported dependency path leaves the asset: %1").arg(relative); return {};
        }
        if (!result.contains(file)) result << file;
    }
    return result;
}
QVariantMap commandReceipt(const QVariantMap& entry) {
    QVariantMap command{{QStringLiteral("schema"), QStringLiteral("xips.hdl-use/v1")}};
    for (const auto* key : {"commandId", "mappingId", "symbol", "kind", "sourceFile", "compileOrder", "includeDirectories", "sourceIdentity"})
        command.insert(QString::fromLatin1(key), entry.value(QString::fromLatin1(key)));
    return command;
}
bool signalCandidate(const SemanticSymbolRecord& record, bool scalar) {
    const auto& facts = record.presentation.defaultInfo;
    return record.presentation.directModuleVariable && facts.available && facts.integral && facts.fixedSize
        && facts.bitWidth && (!scalar || facts.bitWidth == 1) && !facts.unpackedArray
        && !facts.interfaceType && !facts.structuredLeavesAvailable && facts.enumValues.isEmpty();
}
}

XipsHdlCommandService::XipsHdlCommandService(TabManager* t, WorkspaceManager* w,
    simdock::Workbench* b, QWidget* parent) : QObject(parent), tabs(t), workspaces(w), buildInputs(b), owner(parent) {
    connect(w, &WorkspaceManager::workspaceActivated, this, [this] {
        cancel(); references.clear();
        if (nativeView && !nativeView->property("nativeComponentRetired").toBool()) nativeView->setWorkspace(workspaces->getWorkspacePath());
    });
    connect(w, &WorkspaceManager::workspaceClosed, this, [this] {
        cancel(); references.clear();
        if (nativeView && !nativeView->property("nativeComponentRetired").toBool()) nativeView->setWorkspace(workspaces->getWorkspacePath());
    });
}
XipsHdlCommandService::~XipsHdlCommandService() { cancel(); }
void XipsHdlCommandService::cancel() {
    if (pending && pending->cancelled) pending->cancelled->store(true);
    pending.reset();
}
void XipsHdlCommandService::fail(const QString& reason) { cancel(); if (!reason.isEmpty()) emit message(reason); }
NativeContextView* XipsHdlCommandService::view(QWidget* parent) {
    if (!nativeView || nativeView->property("nativeComponentRetired").toBool()) {
        nativeView = new NativeContextView(owner);
        nativeView->hide();
        auto* bridge = new XipsHostBridge(tabs, workspaces, nativeView);
        const QPointer<XipsHdlCommandService> self(this);
        bridge->adoptHdlReceipt = [self](const QVariantMap& receipt) {
            return self ? self->adoptReceipt(receipt) : QStringLiteral("The host closed; exported files were retained.");
        };
        nativeView->setHostBridge(bridge);
        connect(nativeView, &NativeContextView::componentReady, this, &XipsHdlCommandService::attachComponent);
        XipsContextProvider provider(tabs, workspaces);
        nativeView->activate(provider.activationResource(workspaces ? workspaces->getWorkspacePath() : QString()));
    }
    if (parent && nativeView->parentWidget() != parent) nativeView->setParent(parent);
    attachComponent();
    return nativeView;
}
void XipsHdlCommandService::attachComponent() {
    auto* next = nativeView && nativeView->isReady() ? nativeView->component() : nullptr;
    if (component == next && !catalog.isEmpty()) return;
    if (component) disconnect(component, nullptr, this, nullptr);
    component = next;
    catalog.clear();
    const auto capabilities = nativeView ? QJsonDocument::fromVariant(nativeView->property("nativeComponentCapabilities")).toJson() : QByteArray();
    if (!component || !capabilities.contains("\"hdl-commands-v1\"")
        || component->metaObject()->indexOfMethod("hdlCommandCatalog()") < 0
        || component->metaObject()->indexOfMethod("useHdlCommand(QVariantMap)") < 0) {
        unavailable = QStringLiteral("This xIPs component does not provide HDL commands. Load a matching component with HDL command support.");
        emit catalogChanged(); return;
    }
    unavailable.clear();
    connect(component, SIGNAL(hdlCommandCatalogChanged(QString)), this, SLOT(refreshCatalog()));
    connect(component, SIGNAL(hdlCommandUseFinished(QVariantMap)), this, SLOT(useFinished(QVariantMap)));
    connect(component, &QObject::destroyed, this, [this] {
        component = nullptr; catalog.clear();
        if (pending) fail(QStringLiteral("xIPs closed; the pending insertion was cancelled."));
    });
    refreshCatalog();
}
void XipsHdlCommandService::refreshCatalog() {
    QVariantMap value;
    if (!component || !unavailable.isEmpty() || !QMetaObject::invokeMethod(component, "hdlCommandCatalog", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, value))) return;
    if (value.value(QStringLiteral("schema")).toString() != QStringLiteral("xips.hdl-catalog/v1")) {
        unavailable = QStringLiteral("The xIPs HDL catalog format is unsupported."); catalog.clear();
    } else catalog = value;
    emit catalogChanged();
}
void XipsHdlCommandService::loadReferences() {
    references.clear();
    if (!workspaces || !workspaces->isWorkspaceOpen()) return;
    QFile file(QDir(workspaces->getWorkspacePath()).filePath(QStringLiteral(".zeroslack/xips-references.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) return;
    const auto object = QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
    if (object.value(QStringLiteral("schema")).toString() == QStringLiteral("zeroslack.xips-references/v1")) references = object.value(QStringLiteral("assets")).toList();
}
void XipsHdlCommandService::prepare() { view(); loadReferences(); }
QList<GlobalControlItem> XipsHdlCommandService::items(const QString& filter, const GlobalControlQueryContext& context) const {
    QList<GlobalControlItem> result;
    if (!context.editorAvailable) return result;
    if (!siteContext || siteContext->documentInstance != context.documentInstance
        || siteContext->documentRevision != context.documentRevision || siteContext->cursorPosition != context.cursorPosition
        || siteContext->fileName != context.fileName) {
        siteContext = context;
        procedural = XipsHdlCallBuilder::proceduralSite(context);
    }
    if (!unavailable.isEmpty() || catalog.value(QStringLiteral("state")).toString() != QStringLiteral("ready")) {
        if (filter.isEmpty() || QStringLiteral("xips hdl commands 指令").contains(filter, Qt::CaseInsensitive)) {
            GlobalControlItem item; item.title = QStringLiteral("xIPs HDL commands");
            item.disabledReason = unavailable.isEmpty() ? QStringLiteral("The xIPs command catalog is loading or unavailable.") : unavailable;
            result << item;
        }
        return result;
    }
    for (const auto& value : catalog.value(QStringLiteral("entries")).toList()) {
        const auto entry = value.toMap();
        const auto searchable = entry.value(QStringLiteral("name")).toString() + QLatin1Char(' ')
            + entry.value(QStringLiteral("symbol")).toString() + QLatin1Char(' ')
            + entry.value(QStringLiteral("description")).toString() + QLatin1Char(' ')
            + strings(entry.value(QStringLiteral("aliases"))).join(QLatin1Char(' '));
        if (!filter.trimmed().isEmpty() && !searchable.contains(filter.trimmed(), Qt::CaseInsensitive)) continue;
        GlobalControlItem item;
        item.kind = GlobalControlItemKind::Symbol; item.operation = GlobalControlItemOperation::UseXipsCommand;
        item.id = QStringLiteral("xips:") + entry.value(QStringLiteral("commandId")).toString();
        item.title = entry.value(QStringLiteral("name")).toString();
        item.subtitle = QStringLiteral("xIPs · %1 · %2 · %3 · %4\n%5").arg(
            entry.value(QStringLiteral("origin")).toString() == QStringLiteral("builtin") ? QStringLiteral("Built-in") : QStringLiteral("User"),
            entry.value(QStringLiteral("assetName")).toString(), entry.value(QStringLiteral("symbol")).toString(),
            entry.value(QStringLiteral("revision")).toString(), entry.value(QStringLiteral("description")).toString());
        item.parameters = entry;
        item.parameters.insert(QStringLiteral("catalogGeneration"), catalog.value(QStringLiteral("generation")));
        item.sourceDocumentRevision = context.documentRevision; item.sourceDocumentInstance = context.documentInstance;
        item.sourceFileName = context.fileName; item.sourceCursorPosition = context.cursorPosition;
        item.replacementStart = context.replacementStart; item.replacementLength = context.replacementLength;
        if (entry.value(QStringLiteral("availability")).toString() != QStringLiteral("available"))
            item.disabledReason = strings(entry.value(QStringLiteral("diagnostics"))).join(QStringLiteral("; "));
        if (entry.value(QStringLiteral("availability")).toString() != QStringLiteral("available") && item.disabledReason.isEmpty())
            item.disabledReason = QStringLiteral("Save a valid xIPs command version before using it.");
        if (!workspaces || !workspaces->isWorkspaceOpen() || !buildInputs || buildInputs->activeProjectId().isEmpty())
            item.disabledReason = QStringLiteral("Open a workspace and select a project in Build inputs.");
        else if (context.fileName.isEmpty() || !QDir::cleanPath(context.fileName).startsWith(
            QDir::cleanPath(workspaces->getWorkspacePath()) + QLatin1Char('/'), Qt::CaseInsensitive))
            item.disabledReason = QStringLiteral("Save this document inside the current workspace before using a library command.");
        if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("task") && !procedural)
            item.disabledReason = QStringLiteral("Tasks require a procedural statement position outside a function.");
        result << item;
    }
    return result;
}
bool XipsHdlCommandService::canClose(QString* error) const { return !nativeView || nativeView->canClose(error); }
bool XipsHdlCommandService::insertionCurrent() const {
    if (!pending || !tabs || !workspaces || !buildInputs || !pending->editor
        || workspaces->getWorkspacePath() != pending->workspace || buildInputs->activeProjectId() != pending->project) return false;
    auto* editor = pending->editor.data();
    const auto* shared = tabs->sharedDocumentForEditor(editor);
    if (tabs->getCurrentEditor() != editor || !shared || shared->instanceSerial() != pending->item.sourceDocumentInstance
        || !EditorFileIdentity::same(editor->documentFileName(), pending->item.sourceFileName)
        || editor->document()->revision() != pending->item.sourceDocumentRevision
        || editor->textCursor().position() != pending->item.sourceCursorPosition) return false;
    if (pending->compilationInputs) {
        const auto project = workspaces->projectSnapshot();
        if (*pending->compilationInputs != buildInputs->activeCompilationInputs()
            || pending->includeDirectories != project.includeDirs || pending->defines != project.defines) return false;
    }
    for (const auto* open : tabs->openEditors())
        if (!pending->openContents.contains(open->documentFileName())
            || pending->openContents.value(open->documentFileName()) != open->cachedDocumentText()) return false;
    return true;
}
void XipsHdlCommandService::activate(const GlobalControlItem& item) {
    if (pending) { emit message(QStringLiteral("Finish or cancel the current xIPs command insertion first.")); return; }
    if (!item.disabledReason.isEmpty()) { emit message(item.disabledReason); return; }
    if (!workspaces || !simdock::insideWorkspace(workspaces->getWorkspacePath(), item.sourceFileName)) {
        emit message(QStringLiteral("Choose a document inside the active workspace.")); return;
    }
    if (!component || item.parameters.value(QStringLiteral("catalogGeneration")) != catalog.value(QStringLiteral("generation"))) {
        emit message(QStringLiteral("The command catalog changed. Reopen the insertion palette.")); return;
    }
    pending.emplace(); pending->item = item; pending->editor = tabs->getCurrentEditor();
    pending->workspace = workspaces->getWorkspacePath(); pending->project = buildInputs->activeProjectId();
    pending->requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pending->cancelled = std::make_shared<std::atomic_bool>(false);
    for (const auto* editor : tabs->openEditors()) pending->openContents.insert(editor->documentFileName(), editor->cachedDocumentText());
    if (!insertionCurrent()) { fail(QStringLiteral("The document, cursor, or project changed. Reopen the insertion palette.")); return; }
    loadReferences();
    for (const auto& value : references) {
        auto receipt = value.toMap();
        if (receipt.value(QStringLiteral("assetId")) != item.parameters.value(QStringLiteral("assetId"))
            || receipt.value(QStringLiteral("revision")) != item.parameters.value(QStringLiteral("revision"))) continue;
        receipt.insert(QStringLiteral("path"), QDir(pending->workspace).filePath(receipt.value(QStringLiteral("path")).toString()));
        receipt.insert(QStringLiteral("workspace"), pending->workspace);
        receipt.insert(QStringLiteral("hdlCommand"), commandReceipt(item.parameters));
        const auto error = adoptReceipt(receipt);
        if (!error.isEmpty()) { fail(error); return; }
        beginInsertion(receipt); return;
    }
    for (const auto& record : SemanticIndex::getInstance()->getSymbolRecordsByDeclarationKind(SymbolTaxonomy::DeclarationKind::Package)) {
        if (record.name == item.parameters.value(QStringLiteral("package")).toString()) {
            fail(QStringLiteral("Package %1 is already declared at %2. Its existing version was preserved; resolve the package conflict before exporting another copy.")
                .arg(record.name, record.location.fileName));
            return;
        }
    }
    if (showBrowser) showBrowser();
    QVariantMap response;
    const QVariantMap request{{QStringLiteral("requestId"), pending->requestId},
        {QStringLiteral("generation"), catalog.value(QStringLiteral("generation"))},
        {QStringLiteral("commandId"), item.parameters.value(QStringLiteral("commandId"))},
        {QStringLiteral("revision"), item.parameters.value(QStringLiteral("revision"))}};
    if (!QMetaObject::invokeMethod(component, "useHdlCommand", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, response), Q_ARG(QVariantMap, request))) {
        fail(QStringLiteral("The xIPs component could not start Use.")); return;
    }
    if (response.value(QStringLiteral("status")).toString() != QStringLiteral("accepted")) {
        const auto message = response.value(QStringLiteral("message")).toString();
        fail(message.isEmpty() ? QStringLiteral("xIPs Use was rejected: %1").arg(response.value(QStringLiteral("status")).toString()) : message);
    }
}
QString XipsHdlCommandService::adoptReceipt(const QVariantMap& receipt) {
    if (!workspaces || receipt.value(QStringLiteral("workspace")).toString() != workspaces->getWorkspacePath()
        || !buildInputs || (pending && !insertionCurrent())) return QStringLiteral("The workspace or insertion context changed; exported files were retained.");
    const auto command = receipt.value(QStringLiteral("hdlCommand")).toMap();
    if (command.value(QStringLiteral("schema")).toString() != QStringLiteral("xips.hdl-use/v1")) return QStringLiteral("Unsupported HDL export receipt.");
    QString error;
    const auto files = absoluteInputs(receipt, QStringLiteral("compileOrder"), &error);
    const auto directories = absoluteInputs(receipt, QStringLiteral("includeDirectories"), &error);
    if (!error.isEmpty() || files.isEmpty()) return error.isEmpty() ? QStringLiteral("The exported command has no compilation inputs.") : error;
    const auto source = QDir(receipt.value(QStringLiteral("path")).toString()).filePath(command.value(QStringLiteral("sourceFile")).toString());
    if (!files.contains(QDir::cleanPath(source))) return QStringLiteral("The command source is absent from its compilation inputs.");
    error = buildInputs->adoptHdlInputs(pending ? pending->project : buildInputs->activeProjectId(), files, directories);
    if (!error.isEmpty()) return error;
    auto configuration = workspaces->workspaceConfiguration();
    bool changed = false;
    for (const auto& directory : directories) if (!configuration.includeDirs.contains(directory)) { configuration.includeDirs << directory; changed = true; }
    if (changed && !workspaces->setWorkspaceConfiguration(configuration, &error)) return error;
    loadReferences();
    return {};
}
void XipsHdlCommandService::useFinished(const QVariantMap& result) {
    if (!pending || result.value(QStringLiteral("requestId")).toString() != pending->requestId) return;
    const auto status = result.value(QStringLiteral("status")).toString();
    if (status != QStringLiteral("exported")) { fail(status == QStringLiteral("cancelled") ? QString() : result.value(QStringLiteral("message")).toString()); return; }
    if (!insertionCurrent()) { fail(QStringLiteral("The document changed during Use. Files were retained; reopen the insertion palette.")); return; }
    beginInsertion(result.value(QStringLiteral("receipt")).toMap());
}

void XipsHdlCommandService::beginInsertion(const QVariantMap& receipt) {
    if (!insertionCurrent()) { fail(QStringLiteral("The insertion context changed.")); return; }
    XipsHdlCallRequest request;
    const auto semantic = pending->editor->editorSemanticContextForPosition(-1, true);
    request.context.editorAvailable = true; request.context.fileName = pending->item.sourceFileName;
    request.context.documentText = pending->editor->cachedDocumentText();
    request.context.moduleName = semantic.moduleName; request.context.packageName = semantic.packageName;
    request.context.cursorPosition = pending->item.sourceCursorPosition; request.context.cursorLine = semantic.cursorLine;
    request.context.replacementStart = pending->item.replacementStart; request.context.replacementLength = pending->item.replacementLength;
    request.entry = pending->item.parameters; request.cancelled = pending->cancelled;
    QString error;
    request.orderedFiles = absoluteInputs(receipt, QStringLiteral("compileOrder"), &error);
    request.includeDirectories = absoluteInputs(receipt, QStringLiteral("includeDirectories"), &error);
    if (!error.isEmpty()) { fail(error); return; }
    const auto project = workspaces->projectSnapshot();
    pending->compilationInputs = buildInputs->activeCompilationInputs();
    pending->includeDirectories = project.includeDirs; pending->defines = project.defines;
    // Use the project's compilation selection; workspace discovery can include
    // unselected designs and transient files from an in-progress export.
    for (const auto& file : *pending->compilationInputs) if (!request.orderedFiles.contains(file)) request.orderedFiles << file;
    if (!request.orderedFiles.contains(request.context.fileName)) request.orderedFiles << request.context.fileName;
    request.includeDirectories += project.includeDirs; request.includeDirectories.removeDuplicates(); request.defines = project.defines;
    request.sourceFile = QDir(receipt.value(QStringLiteral("path")).toString()).filePath(request.entry.value(QStringLiteral("sourceFile")).toString());
    for (auto it = pending->openContents.cbegin(); it != pending->openContents.cend(); ++it)
        if (request.orderedFiles.contains(it.key()) || EditorFileIdentity::same(it.key(), request.context.fileName))
            request.contents.insert(it.key(), it.value());
    inspectCall(std::move(request));
}
void XipsHdlCommandService::bindSignals(XipsHdlCallRequest request, const XipsHdlCallInspection& inspection) {
        UiDialog dialog(owner); dialog.setObjectName(QStringLiteral("xipsSignalBinding")); dialog.setWindowTitle(QStringLiteral("Bind %1").arg(request.entry.value(QStringLiteral("name")).toString()));
        auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
        auto* clock = UiControls::comboBox(&dialog); clock->setObjectName(QStringLiteral("xipsClock")); clock->addItem(QStringLiteral("Choose clock"), QString());
        auto* target = UiControls::comboBox(&dialog); target->setObjectName(QStringLiteral("xipsTarget")); target->addItem(QStringLiteral("Choose target"), QString());
        const bool reset = request.entry.value(QStringLiteral("symbol")).toString() == QStringLiteral("tb_reset_package::reset_driver::run");
        for (const auto& record : inspection.bindingCandidates) {
            if (signalCandidate(record, true)) clock->addItem(record.name, record.name);
            if (signalCandidate(record, reset)) target->addItem(record.name, record.name);
        }
        auto* edge = UiControls::comboBox(&dialog); edge->setObjectName(QStringLiteral("xipsEdge"));
        edge->addItem(QStringLiteral("Choose edge")); edge->addItem(QStringLiteral("Rising edge"), true); edge->addItem(QStringLiteral("Falling edge"), false);
        UiControls::addFormRow(form, QStringLiteral("Clock"), clock); UiControls::addFormRow(form, QStringLiteral("Target"), target); UiControls::addFormRow(form, QStringLiteral("Drive on"), edge);
        auto* level = UiControls::comboBox(&dialog); level->setObjectName(QStringLiteral("xipsAssertedLevel"));
        if (reset) { level->addItem(QStringLiteral("Choose asserted level")); level->addItem(QStringLiteral("1 (release to 0)"), true); level->addItem(QStringLiteral("0 (release to 1)"), false); UiControls::addFormRow(form, QStringLiteral("Asserted"), level); } else level->hide();
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(&dialog); layout->addWidget(buttons);
        auto* acceptButton = UiDialogs::addButton(buttons, QDialogButtonBox::Ok);
        UiDialogs::addButton(buttons, QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        auto update = [=] { acceptButton->setEnabled(!clock->currentData().toString().isEmpty() && !target->currentData().toString().isEmpty() && clock->currentData() != target->currentData() && edge->currentIndex() > 0 && (!reset || level->currentIndex() > 0)); };
        for (auto* control : {clock, target, edge, level}) connect(control, &QComboBox::currentIndexChanged, &dialog, update);
        update();
        dialog.resize(qMax(480, dialog.sizeHint().width()), dialog.sizeHint().height());
        if (dialog.exec() != QDialog::Accepted || !insertionCurrent()) { cancel(); return; }
        request.clock = clock->currentData().toString(); request.target = target->currentData().toString();
        request.rising = edge->currentData().toBool(); request.asserted = level->currentData().toBool();
    inspectCall(std::move(request));
}
void XipsHdlCommandService::inspectCall(XipsHdlCallRequest request) {
    auto* watcher = new QFutureWatcher<XipsHdlCallInspection>(this);
    connect(watcher, &QFutureWatcher<XipsHdlCallInspection>::finished, this, [this, watcher, request] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (!pending || pending->cancelled != request.cancelled) return;
        if (!insertionCurrent()) { fail(QStringLiteral("The document changed while Slang prepared the call.")); return; }
        if (!result.error.isEmpty()) { fail(result.error); return; }
        if (result.needsSignalBinding) bindSignals(request, result);
        else completeCall(request, result);
    });
    watcher->setFuture(QtConcurrent::run([request] { return XipsHdlCallBuilder::inspect(request); }));
}
void XipsHdlCommandService::completeCall(XipsHdlCallRequest request, const XipsHdlCallInspection& inspection) {
    QHash<QString, QString> arguments;
    if (!inspection.bindingName.isEmpty()) {
        arguments = {{QStringLiteral("target"), QStringLiteral("driver")}, {QStringLiteral("status"), QStringLiteral("status")},
            {QStringLiteral("cycles"), QStringLiteral("3")}, {QStringLiteral("count"), QStringLiteral("4")},
            {QStringLiteral("start_value"), QStringLiteral("'0")}, {QStringLiteral("step"), QStringLiteral("1")},
            {QStringLiteral("minimum"), QStringLiteral("'0")}, {QStringLiteral("maximum"), QStringLiteral("1")},
            {QStringLiteral("start_at_ps"), QStringLiteral("150ns/1ps")}, {QStringLiteral("stop_at_ps"), QStringLiteral("200ns/1ps")},
            {QStringLiteral("cancel"), QStringLiteral("null")}, {QStringLiteral("rising"), request.rising ? QStringLiteral("1'b1") : QStringLiteral("1'b0")},
            {QStringLiteral("asserted_value"), request.asserted ? QStringLiteral("1'b1") : QStringLiteral("1'b0")},
            {QStringLiteral("released_value"), request.asserted ? QStringLiteral("1'b0") : QStringLiteral("1'b1")}};
    } else if (!inspection.callable.presentation.callableArguments.isEmpty()) {
        UiDialog dialog(owner); dialog.setObjectName(QStringLiteral("xipsCallArguments")); dialog.setWindowTitle(QStringLiteral("Call %1").arg(inspection.callable.presentation.callableQualifiedName));
        auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout; QHash<QString, QLineEdit*> fields;
        for (const auto& argument : inspection.callable.presentation.callableArguments) {
            auto* field = UiControls::lineEdit(argument.hasDefault ? QString() : argument.name, &dialog);
            field->setObjectName(QStringLiteral("xipsArgument_%1").arg(argument.name));
            if (argument.hasDefault) field->setPlaceholderText(QStringLiteral("Use declared default"));
            fields.insert(argument.name, field);
            UiControls::addFormRow(form, argument.name + QStringLiteral(" · ")
                + (argument.input ? QStringLiteral("input ") : QStringLiteral("variable ")) + argument.typeText, field);
        }
        layout->addLayout(form); auto* buttons = UiDialogs::buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(qMax(480, dialog.sizeHint().width()), dialog.sizeHint().height());
        if (dialog.exec() != QDialog::Accepted || !insertionCurrent()) { cancel(); return; }
        for (auto it = fields.cbegin(); it != fields.cend(); ++it) arguments.insert(it.key(), it.value()->text());
    }
    auto* watcher = new QFutureWatcher<XipsHdlCallPlan>(this);
    connect(watcher, &QFutureWatcher<XipsHdlCallPlan>::finished, this, [this, watcher, request] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (!pending || pending->cancelled != request.cancelled) return;
        if (!insertionCurrent()) { fail(QStringLiteral("The document changed during call validation.")); return; }
        if (!result.error.isEmpty()) { fail(result.error); return; }
        auto* editor = pending->editor.data();
        QTextCursor cursor(editor->document()); cursor.setPosition(result.start); cursor.setPosition(result.end, QTextCursor::KeepAnchor); editor->setTextCursor(cursor);
        QString error;
        const bool inserted = editor->insertCompletionText(result.text, -1, 0, result.placeholders, &error);
        cancel();
        if (inserted) { editor->setFocus(); emit message(QStringLiteral("Inserted the source-checked xIPs call. Package versions remain pinned in this project.")); }
        else emit message(error);
    });
    watcher->setFuture(QtConcurrent::run([request, inspection, arguments] { return XipsHdlCallBuilder::build(request, inspection, arguments); }));
}
