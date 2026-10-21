#include "automationservice.h"
#include "mainwindow.h"
#include "mycodeeditor.h"
#include "tabmanager.h"
#include "shareddocument.h"
#include "documenttemplatemodel.h"
#include "usertemplateservice.h"
#include "simulationcommandplanner.h"
#include "simulationcommandservice.h"
#include "duttemplateplanner.h"
#include "templatecapabilities.h"
#include "formatterservice.h"
#include "formattercursoranchor.h"
#include "workspacesessionstateservice.h"
#include "workspacemanager.h"
#include "workspaceconfigurationservice.h"
#include "slangmanager.h"
#include "editorfileidentity.h"
#include "../simulation/simdock/core/workspace.h"
#include "../simulation/simdock/core/ownedpaths.h"
#include "../simulation/simdock/core/questasession.h"
#include "../simulation/simdock/ui/workbench.h"
#include <zeroslack/documents/documentfileread.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextDocument>
#include <QTextBlock>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QUuid>
#include <algorithm>

namespace {
QString absolute(const QString& root, const QString& path) {
    return QDir::cleanPath(QDir(root).absoluteFilePath(path));
}
QJsonArray strings(const QStringList& values) { return QJsonArray::fromStringList(values); }
QStringList stringList(const QJsonValue& value) {
    QStringList result;
    for (const auto& v : value.toArray()) result << v.toString();
    return result;
}
QString projectRevision(const simdock::Project& project) {
    QJsonObject hashes;
    for (auto it = project.storageRevisions.cbegin(); it != project.storageRevisions.cend(); ++it)
        hashes.insert(it.key(), QString::fromLatin1(it.value().toHex()));
    return ZeroSlackAutomationService::digest(QJsonDocument(hashes).toJson(QJsonDocument::Compact));
}
QJsonObject projectObject(const simdock::Project& project) {
    auto data = simdock::projectJson(project);
    data.insert("revision", projectRevision(project));
    return data;
}
QString reportError(const UserTemplateLoadReport& report) {
    QStringList messages;
    if (!report.failureReason.isEmpty()) messages << report.failureReason;
    for (const auto& issue : report.issues) messages << issue.id + "/" + issue.field + ": " + issue.reason;
    return messages.join('\n');
}
QJsonObject instances(DocumentTemplateModel* model, const QString& text) {
    QJsonArray items;
    for (const auto& item : model->instances()) {
        QJsonArray inputs, fragments;
        for (const auto& input : item.inputs)
            inputs << QJsonObject{{"name", input.name}, {"start", input.start}, {"length", input.length},
                {"value", text.mid(input.start, input.length)}, {"constraints", QJsonObject::fromVariantMap(input.input)}};
        for (const auto& f : item.fragments) fragments << QJsonObject{{"start", f.start}, {"length", f.length}, {"auxiliary", f.auxiliary}};
        items << QJsonObject{{"id", item.id}, {"parentId", item.parentId}, {"parentSlot", item.parentSlot},
            {"label", item.label}, {"inputs", inputs}, {"fragments", fragments}, {"snapshot", QJsonObject::fromVariantMap(item.snapshot)}};
    }
    return {{"instances", items}, {"metadataIssue", model->issue()}};
}
QString prepareRequest(const QJsonObject& json, const QString& root, const QString& file,
    const QString& text, MainWindow* window, SimulationCommandRequest* result) {
    result->context.fileName = file;
    result->context.documentText = text;
    result->context.editorAvailable = true;
    result->context.cursorPosition = json.value("start").toInt(0);
    result->context.replacementStart = result->context.cursorPosition;
    result->context.replacementLength = json.value("length").toInt(0);
    result->context.cursorLine = text.left(result->context.cursorPosition).count('\n') + 1;
    const auto configuration = WorkspaceConfigurationService().loadWithResult(root);
    if (!configuration.usable()) return configuration.message;
    result->includeDirectories = configuration.configuration.includeDirs;
    result->defines = configuration.configuration.defines;
    const auto projectId = json.value("projectId").toString();
    if (!projectId.isEmpty()) {
        bool found = false;
        for (const auto& project : simdock::loadProjects(root, nullptr, false)) if (project.id == projectId) {
            found = true;
            for (const auto& source : project.sources) result->orderedFiles << absolute(root, source);
            if (!project.tbFile.isEmpty()) result->orderedFiles << absolute(root, project.tbFile);
            for (const auto& dir : project.includeDirectories) result->includeDirectories << absolute(root, dir);
        }
        if (!found) return "Unknown projectId.";
    } else {
        if (!json.value("sources").isUndefined() && !json.value("sources").isArray()) return "sources must be an array.";
        for (const auto& source : json.value("sources").toArray()) {
            if (!source.isString() || source.toString().isEmpty()) return "sources must contain nonempty paths.";
            const auto path = absolute(root, source.toString());
            if (!simdock::insideWorkspace(root, path) || !QFileInfo(path).isFile()) return "A source is missing or outside the workspace.";
            result->orderedFiles << path;
        }
    }
    if (result->orderedFiles.size() > 512) return "At most 512 compilation sources are supported per request.";
    if (!result->orderedFiles.contains(file)) result->orderedFiles << file;
    result->orderedFiles.removeDuplicates();
    result->contents.insert(file, text);
    if (window && window->tabManager) for (auto* editor : window->tabManager->openEditors()) {
        const auto snapshot = window->tabManager->getDocumentForEditor(editor);
        if (result->orderedFiles.contains(snapshot.fileName)) result->contents.insert(snapshot.fileName, editor->toPlainText());
    }
    UserTemplateService catalog;
    catalog.setWorkspaceRoot(root);
    result->roles = catalog.signalRoles();
    return {};
}
std::shared_ptr<TemplateInputContext> analyze(SimulationCommandRequest* request, QString* error) {
    SemanticAnalysisRequest semantic;
    semantic.sourceOverrides = request->contents;
    SemanticInputCapture capture(semantic);
    SlangManager slang;
    const auto analysis = slang.analyzeCapturedWorkspace(capture, request->orderedFiles, request->includeDirectories,
        request->defines, {}, {}, {true, true, false});
    if (!analysis.error.isEmpty() || analysis.cancelled || !capture.stillMatchesDisk({})) {
        *error = analysis.error.isEmpty() ? "Sources changed during semantic analysis." : analysis.error; return {};
    }
    auto result = std::make_shared<TemplateInputContext>();
    result->snapshot = std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(
        analysis.symbols, {}, {}, capture.contents()));
    SemanticIndex index; index.setSnapshot(result->snapshot);
    request->context.moduleName = index.currentModuleAt(request->context.fileName, request->context.cursorPosition);
    result->location.fileName = request->context.fileName;
    result->location.moduleName = request->context.moduleName;
    result->location.cursorPosition = request->context.cursorPosition;
    result->location.cursorLine = request->context.cursorLine;
    result->roles = request->roles;
    const auto captured = capture.seal();
    result->current = [captured] { return SimulationCommandPlanner::sourcesCurrent(captured); };
    return result;
}
}

QString ZeroSlackAutomationService::digest(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString ZeroSlackAutomationService::diskRevision(const QString& path) {
    QFile file(path);
    if (!file.exists()) return "missing";
    if (!file.open(QIODevice::ReadOnly)) return "unreadable";
    return digest(file.readAll());
}
QJsonObject ZeroSlackAutomationService::success(const QJsonObject& data) {
    return {{"schema", "zeroslack.automation/v1"}, {"ok", true}, {"data", data}};
}
QJsonObject ZeroSlackAutomationService::failure(const QString& code, const QString& message, const QJsonObject& data) {
    return {{"schema", "zeroslack.automation/v1"}, {"ok", false}, {"error", QJsonObject{{"code", code}, {"message", message}}}, {"data", data}};
}
QJsonObject ZeroSlackAutomationService::capabilities() {
    return success({{"appVersion", QCoreApplication::applicationVersion()}, {"commands", QJsonArray{
        "capabilities", "session.describe", "support.install", "templates.examples", "templates.list", "templates.get", "templates.validate", "templates.put", "templates.delete",
        "document.inspect", "document.open", "document.insert", "document.input", "document.candidates", "document.format",
        "document.save", "document.undo", "document.redo", "dut.inspect",
        "simulation.projects", "simulation.project", "simulation.open", "simulation.put", "simulation.validate",
        "simulation.run", "simulation.status", "simulation.events", "simulation.cancel", "simulation.clear"}},
        {"coordinates", "UTF-16 offsets; start inclusive, length in code units; all paths relative to explicit workspace"},
        {"writeContract", "expectedRevision required; dryRun previews. Online edits adopt memory only; document.save requires expectedDiskRevision. Active template editing returns conflict."},
        {"sourceContract", "sources is an ordered array, or projectId selects saved Build inputs; open document contents are used in an explicit GUI session"},
        {"limits", QJsonObject{{"requestBytes", 1024*1024}, {"documentBytes", 8*1024*1024}, {"sources", 512}, {"retainedEvents", 10000}}}});
}
QJsonObject ZeroSlackAutomationService::execute(const QJsonObject& request) {
    const auto command = request.value("command").toString();
    const QSet<QString> stringFields{"command", "workspace", "file", "scope", "id", "templateId", "parentId", "parentSlot", "instanceId", "slot", "value", "module", "instance", "replaceId", "projectId", "runId", "simulator", "expectedRevision", "expectedDiskRevision", "expectedOutputRevision", "output", "kind"};
    const QSet<QString> integerFields{"start", "length", "offset", "limit", "timeoutMs"};
    const QSet<QString> arrayFields{"sources", "reuse"};
    const QSet<QString> objectFields{"record", "parameters", "bindings", "project"};
    for (auto it = request.begin(); it != request.end(); ++it) {
        bool valid = false;
        if (stringFields.contains(it.key())) valid = it.value().isString();
        else if (integerFields.contains(it.key())) valid = it.value().isDouble() && it.value().toDouble() == it.value().toInt(-1) && it.value().toDouble() >= 0;
        else if (arrayFields.contains(it.key())) {
            valid = it.value().isArray();
            for (const auto& v : it.value().toArray()) valid = valid && v.isString() && !v.toString().isEmpty();
        } else if (objectFields.contains(it.key())) valid = it.value().isObject();
        else if (it.key() == "dryRun") valid = it.value().isBool();
        if (!valid) return failure("invalid_arguments", "Unknown field or wrong value type: " + it.key());
    }
    for (const auto& key : {"parameters", "bindings"}) {
        const auto values = request.value(key).toObject();
        for (auto it = values.begin(); it != values.end(); ++it)
            if (!it.value().isString()) return failure("invalid_arguments", QString(key) + " values must be expression strings.");
    }
    if (command == "capabilities") return capabilities();
    if (command == "templates.examples") {
        QFile examples(QStringLiteral(":/templates/tb-dut.json"));
        if (!examples.open(QIODevice::ReadOnly)) return failure("examples_unavailable", "Bundled template examples are unavailable.");
        return success(QJsonDocument::fromJson(examples.readAll()).object());
    }
    if (command == "session.describe") {
        QJsonArray documents;
        if (window && window->tabManager) for (auto* editor : window->tabManager->openEditors()) {
            const auto snapshot = window->tabManager->getDocumentForEditor(editor);
            documents << QJsonObject{{"file", snapshot.fileName}, {"dirty", snapshot.dirty},
                {"viewId", editor->property("editorViewId").toString()}, {"editing", editor->templateInputEditing()}};
        }
        return success({{"kind", window ? "gui" : "headless"}, {"documents", documents},
            {"workspace", window && window->workspaceManager ? window->workspaceManager->getWorkspacePath() : QString()},
            {"simulationWorkspace", window && window->simulationWorkbench() ? window->simulationWorkbench()->workspace() : QString()}});
    }
    const auto root = QFileInfo(request.value("workspace").toString()).canonicalFilePath();
    if (!request.value("workspace").isString() || request.value("workspace").toString().isEmpty() || !QFileInfo(root).isDir())
        return failure("invalid_workspace", "An existing explicit workspace directory is required.");
    if (command.startsWith("templates.")) return templates(request, root);
    if (command == "support.install") {
        const auto kind = request.value("kind").toString();
        if (kind != "print" && kind != "behaviors") return failure("invalid_kind", "kind must be print or behaviors.");
        if (request.value("dryRun").toBool()) return success({{"kind", kind}, {"written", false}, {"preview", true}});
        QStringList files;
        const auto error = SimulationCommandService::materializeSupport(root, kind, &files);
        if (!error.isEmpty()) return failure("support_error", error);
        return success({{"files", strings(files)}, {"available", true}, {"buildInputsAdopted", false}});
    }
    if (command.startsWith("document.") || command == "dut.inspect") return document(request, root);
    if (command.startsWith("simulation.")) return simulation(request, root);
    return failure("unknown_command", "Unknown automation command; use capabilities.");
}

QJsonObject ZeroSlackAutomationService::templates(const QJsonObject& request, const QString& root) {
    UserTemplateService service;
    service.setWorkspaceRoot(root);
    const auto loaded = service.reload();
    if (!loaded.valid) return failure("invalid_catalog", reportError(loaded));
    const auto command = request.value("command").toString();
    const auto scope = request.value("scope").toString("workspace");
    if (scope != "workspace" && scope != "global") return failure("invalid_scope", "scope must be workspace or global; file scope uses simulation.file.");
    const bool local = scope == "workspace";
    QJsonObject data{{"revision", service.storageRevision(local)}, {"scope", scope},
        {"storage", local ? service.workspaceTemplateLocation() : service.globalTemplateLocation()}};
    const auto file = request.value("file").toString().isEmpty() ? QString() : absolute(root, request.value("file").toString());
    const auto records = service.recordsForFile(file);
    if (command == "templates.list" || command == "templates.get") {
        QJsonArray items;
        for (const auto& record : records) {
            if (command == "templates.get" && record.id != request.value("id").toString()) continue;
            auto item = UserTemplateService::toJson(record);
            item.insert("storageSource", record.storageSource);
            item.insert("builtin", record.storageSource == "builtin");
            items << item;
        }
        if (command == "templates.get" && items.isEmpty()) return failure("not_found", "Unknown or shadowed template id.");
        data.insert("templates", items); return success(data);
    }
    if (command != "templates.validate" && command != "templates.put" && command != "templates.delete")
        return failure("unknown_command", "Unknown template operation.");
    const bool preview = request.value("dryRun").toBool() || command == "templates.validate";
    if (!preview && request.value("expectedRevision").toString() != data.value("revision").toString())
        return failure("revision_conflict", "Read the selected template storage revision before writing.", data);
    UserTemplateLoadReport report;
    if (command == "templates.delete") {
        bool found = false;
        for (const auto& record : loaded.records) if (record.id == request.value("id").toString() && record.storageSource == data.value("storage").toString()) found = true;
        if (!found) return failure("not_found", "The selected storage does not contain that user template id.");
        if (!preview) report = service.deleteRecord(request.value("id").toString(), local, 0, data.value("revision").toString());
        else report.valid = true;
    } else {
        if (!request.value("record").isObject()) return failure("invalid_record", "record must be a template object.");
        auto record = request.value("record").toObject();
        auto configuration = record.value("simulation").toObject();
        for (const auto& key : {"file", "sourceFile"}) if (!configuration.value(key).toString().isEmpty())
            configuration.insert(key, absolute(root, configuration.value(key).toString()));
        if (!configuration.isEmpty()) record.insert("simulation", configuration);
        report = UserTemplateService::fromJson(record);
        if (report.valid && !preview) report = service.saveRecord(report.records.first(), local, 0, data.value("revision").toString());
    }
    if (!report.valid) return failure("validation_or_conflict", reportError(report), data);
    data.insert("written", !preview);
    data.insert("revision", service.storageRevision(local));
    return success(data);
}

QJsonObject ZeroSlackAutomationService::document(const QJsonObject& request, const QString& root) {
    const auto command = request.value("command").toString();
    const auto file = absolute(root, request.value("file").toString());
    if (request.value("file").toString().isEmpty() || !simdock::plainPathWithin(root, file))
        return failure("invalid_file", "file must identify a plain path within the workspace.");
    if (QFileInfo(file).size() > 8*1024*1024) return failure("input_limit", "The document exceeds 8 MiB.");
    MyCodeEditor* editor = nullptr;
    if (window && window->tabManager) {
        if (request.contains("output")) return failure("invalid_output", "Online edits target the selected document. Use normal save for its path; output is offline only.");
        for (auto* candidate : window->tabManager->openEditors())
            if (EditorFileIdentity::same(window->tabManager->getDocumentForEditor(candidate).fileName, file)) { editor = candidate; break; }
        if (command == "document.open") {
            if (!QFileInfo(file).isFile() || !window->tabManager->openFileInTab(file)) return failure("open_failed", "Cannot open the selected document.");
            auto query = request; query.insert("command", "document.inspect");
            auto result = document(query, root); auto data = result.value("data").toObject();
            data.insert("viewLocated", true); result.insert("data", data); return result;
        }
        if (!editor) return failure("document_not_open", "Open this file in the selected session first; no disk fallback was used.");
    } else if (command == "document.open" || command == "document.save" || command == "document.undo" || command == "document.redo")
        return failure("session_required", "This operation requires an explicit GUI session.");
    const bool exists = QFileInfo(file).exists();
    const auto disk = readDocumentFile(file);
    if (exists && (!disk.available || disk.text.contains(QChar::Null) || encodeDocumentText(disk.text, disk.format) != disk.rawBytes))
        return failure("read_error", "Source is unreadable or does not round-trip losslessly.");
    const auto before = editor ? editor->toPlainText() : exists ? disk.text : QString();
    QString error;
    const auto metadata = editor ? editor->templateModel()->serialize(file) : WorkspaceSessionStateService().loadDocumentTemplates(file, &error);
    if (!error.isEmpty()) return failure("metadata_error", error);
    QTextDocument staged; staged.setPlainText(before);
    auto* model = DocumentTemplateModel::get(&staged);
    if (!metadata.isEmpty() && !model->restore(metadata, file)) return failure("metadata_conflict", model->issue());
    const auto version = digest(before.toUtf8() + QJsonDocument(metadata).toJson(QJsonDocument::Compact)
        + (editor ? QByteArray::number(window->tabManager->sharedDocumentForEditor(editor)->textRevision()) : QByteArray()));
    auto data = instances(model, before);
    data.insert("file", file); data.insert("revision", version); data.insert("diskRevision", exists ? digest(disk.rawBytes) : "missing");
    data.insert("text", before); data.insert("adopted", bool(editor)); data.insert("written", false);
    data.insert("dirty", editor && editor->document()->isModified()); data.insert("viewLocated", false);
    if (command == "document.inspect") return success(data);
    const bool readOnly = command == "document.candidates" || command == "dut.inspect";
    const bool preview = request.value("dryRun").toBool();
    if (!readOnly && request.value("expectedRevision").toString() != version)
        return failure("revision_conflict", "Read the current document revision before applying a change.", data);
    if (!readOnly && editor) {
        for (auto* view : window->tabManager->sharedDocumentForEditor(editor)->views())
            if (view->templateInputEditing()) return failure("editing_conflict", "A view is actively editing a template input. Finish it before automation applies changes.", data);
        if (editor->documentReadOnly()) return failure("read_only", "This document is read-only.", data);
        const auto* shared = window->tabManager->sharedDocumentForEditor(editor);
        if (shared && shared->externalState() == SharedDocumentExternalState::Conflict)
            return failure("external_conflict", "Resolve the existing external document conflict before automation changes this document.", data);
    }
    if (command == "document.save") {
        if (request.value("expectedDiskRevision").toString() != data.value("diskRevision").toString())
            return failure("disk_conflict", "The disk generation differs from the explicitly adopted save baseline.", data);
        if (preview) return success(data);
        const auto* shared = window->tabManager->sharedDocumentForEditor(editor);
        if (shared && QString::fromLatin1(shared->savedBaselineSha256().toHex()) != data.value("diskRevision").toString())
            return failure("disk_conflict", "The disk source differs from this editor's saved baseline; explicit conflict resolution is required.", data);
        if (!window->tabManager->saveEditorView(editor)) return failure("save_failed", "The normal document save service rejected the save; inspect document and disk state.", data);
        auto query = request; query.insert("command", "document.inspect");
        auto result = document(query, root); auto saved = result.value("data").toObject(); saved.insert("written", true); result.insert("data", saved); return result;
    }
    if (command == "document.undo" || command == "document.redo") {
        if (!preview) { if (command == "document.undo") editor->undo(); else editor->redo(); }
        auto query = request; query.insert("command", "document.inspect"); return document(query, root);
    }
    QList<TemplateDocumentEdit> edits;
    QString selected, inputOwner; int inputIndex = -1;
    std::shared_ptr<TemplateInputContext> inputs;
    FormatterReport formatting;
    if (command == "document.format") {
        formatting = FormatterService::getInstance()->formatDocument(before);
        if (!formatting.accepted()) return failure("format_rejected", formatting.diagnostic, data);
        FormatterTriviaPositionMapper mapper(before, formatting.formattedText);
        if (!model->applyFormatting(formatting.formattedText, mapper, &error)) return failure("format_rejected", error, data);
        data.insert("diagnostic", formatting.diagnostic); data.insert("formattedLines", formatting.formattedLines);
    } else if (command == "document.insert" || command == "dut.inspect" || command == "document.input" || command == "document.candidates") {
        SimulationCommandRequest operation;
        auto prepared = request;
        inputOwner = request.value("instanceId").toString();
        if (command == "document.input" || command == "document.candidates") {
            const auto* item = model->instance(inputOwner);
            if (!item) return failure("not_found", "Unknown template instanceId.", data);
            for (int i = 0; i < item->inputs.size(); ++i) if (item->inputs[i].name == request.value("slot").toString()) { inputIndex = i; break; }
            if (inputIndex < 0) return failure("not_found", "Unknown template slot.", data);
            prepared.insert("start", item->inputs[inputIndex].start);
        }
        error = prepareRequest(prepared, root, file, before, window, &operation);
        if (!error.isEmpty()) return failure("invalid_inputs", error, data);
        const int start = operation.context.cursorPosition;
        if (start < 0 || start > before.size() || operation.context.replacementLength < 0 || operation.context.replacementLength > before.size() - start)
            return failure("invalid_range", "The UTF-16 range is outside the document.", data);
        inputs = analyze(&operation, &error);
        if (!inputs) return failure("analysis_failed", error, data);
        if (inputIndex >= 0) {
            const auto item = *model->instance(inputOwner);
            const auto slot = item.inputs[inputIndex];
            QHash<QString, QString> bindings;
            for (const auto& s : item.inputs) bindings[s.name] = before.mid(s.start, s.length);
            if (command == "document.candidates") {
                const auto choices = TemplateInputService::candidates(slot.input, *inputs, bindings);
                data.insert("values", strings(choices.values)); data.insert("reason", choices.reason);
                QJsonObject evidence; for (auto it = choices.evidence.cbegin(); it != choices.evidence.cend(); ++it) evidence.insert(it.key(), it.value());
                data.insert("evidence", evidence); return success(data);
            }
            if (!request.value("value").isString()) return failure("invalid_value", "value must be a string.");
            error = TemplateInputService::validate(slot.input, request.value("value").toString(), *inputs, bindings);
            if (!error.isEmpty()) return failure("invalid_value", error, data);
            model->setInputContext(inputOwner, inputs);
            QTextCursor cursor(&staged); cursor.setPosition(slot.start); cursor.setPosition(slot.start + slot.length, QTextCursor::KeepAnchor);
            if (!model->replaceInput(inputOwner, inputIndex, cursor, request.value("value").toString(), nullptr, true))
                return failure("protected_range", "The replacement intersects template ownership.", data);
        } else {
            if (!request.contains("start")) return failure("invalid_range", "An explicit UTF-16 start offset is required.", data);
            UserTemplateService catalog; catalog.setWorkspaceRoot(root);
            bool found = false;
            for (const auto& record : catalog.recordsForFile(file)) {
                if (record.id != request.value("templateId").toString()) continue;
                found = true; operation.item.id = record.id; operation.item.title = record.label;
                operation.item.parameters = record.command; operation.item.insertionText = record.insertText; operation.item.templateSlots = record.templateSlots;
            }
            if (!found) return failure("not_found", "Unknown or shadowed templateId. Use templates.list for this file.", data);
            operation.item.replacementStart = start; operation.item.replacementLength = operation.context.replacementLength;
            operation.item.templateParentId = request.value("parentId").toString(); operation.item.templateParentSlot = request.value("parentSlot").toString();
            operation.context.templateParentId = operation.item.templateParentId; operation.context.templateParentSlot = operation.item.templateParentSlot;
            if (!operation.item.templateParentId.isEmpty()) {
                const auto* parent = model->instance(operation.item.templateParentId);
                if (!parent) return failure("not_found", "Unknown parentId.", data);
                for (const auto& slot : parent->inputs) if (slot.name == operation.item.templateParentSlot)
                    operation.item.templateContent = operation.context.templateContent = slot.input.value("content").toString();
            }
            if (TemplateCapabilities::module(operation.item.parameters)) {
                DutTemplateOptions options; options.module = request.value("module").toString(); options.instance = request.value("instance").toString(); options.replaceId = request.value("replaceId").toString();
                const auto parameters = request.value("parameters").toObject(), bindings = request.value("bindings").toObject();
                for (auto it = parameters.begin(); it != parameters.end(); ++it) options.parameters[it.key()] = it.value().toString();
                for (auto it = bindings.begin(); it != bindings.end(); ++it) options.bindings[it.key()] = it.value().toString();
                for (const auto& name : request.value("reuse").toArray()) options.reuse.insert(name.toString());
                if (command == "dut.inspect") {
                    const auto analysis = DutTemplatePlanner::analyze(operation, model->instances(), options);
                    if (!analysis.error.isEmpty()) return failure("analysis_failed", analysis.error, data);
                    QJsonArray ports; QJsonObject defaults;
                    for (const auto& port : analysis.ports) ports << QJsonObject{{"name", port.name}, {"direction", port.direction}, {"type", port.type}, {"suffix", port.suffix}, {"compatibleSignals", strings(port.compatibleSignals)}};
                    for (auto it = analysis.defaults.cbegin(); it != analysis.defaults.cend(); ++it) defaults.insert(it.key(), it.value());
                    data.insert("modules", strings(analysis.modules)); data.insert("parameters", defaults); data.insert("ports", ports); data.insert("instance", analysis.instance); return success(data);
                }
                const auto plan = DutTemplatePlanner::prepare(operation, model->instances(), options);
                if (!plan.error.isEmpty()) return failure("plan_rejected", plan.error, data);
                edits = plan.edits;
            } else {
                if (command == "dut.inspect") return failure("invalid_template", "dut.inspect requires a module-generator template.");
                const auto plan = SimulationCommandPlanner::prepare(operation);
                if (!plan.error.isEmpty()) return failure("plan_rejected", plan.error, data);
                edits << TemplateDocumentEdit{plan.start, plan.length, plan.text, operation.item.templateParentId,
                    operation.item.templateParentSlot, {}, plan.instance, plan.placeholders};
            }
            if (!model->applyEdits(edits, &selected, &error)) return failure("plan_rejected", error, data);
        }
        if (inputs->current && !inputs->current()) return failure("source_conflict", "A dependency changed during preparation.", data);
    } else return failure("unknown_command", "Unknown document operation.");
    const auto after = staged.toPlainText();
    data.insert("changed", before != after); data.insert("text", after);
    const auto described = instances(model, after);
    for (auto it = described.begin(); it != described.end(); ++it) data.insert(it.key(), it.value());
    data.insert("metadata", model->serialize(file)); data.insert("selectedId", selected);
    if (preview) { data.insert("preview", true); data.insert("adopted", false); return success(data); }
    if (editor) {
        if (editor->toPlainText() != before) return failure("revision_conflict", "The open document changed during preparation.");
        if (command == "document.format") {
            const auto result = editor->formatDocument(); if (!result.accepted()) return failure("format_rejected", result.diagnostic);
        } else if (inputIndex >= 0) {
            auto transaction = editor->beginSynchronousEditTransaction();
            const auto slot = editor->templateModel()->instance(inputOwner)->inputs[inputIndex];
            editor->templateModel()->setInputContext(inputOwner, inputs);
            QTextCursor cursor(editor->document()); cursor.setPosition(slot.start); cursor.setPosition(slot.start + slot.length, QTextCursor::KeepAnchor);
            if (!editor->templateModel()->replaceInput(inputOwner, inputIndex, cursor, request.value("value").toString(), nullptr, true)) return failure("apply_failed", "Input ownership changed.");
        } else if (!editor->applyTemplateEdits(edits, &error)) return failure("apply_failed", error);
        auto query = request; query.insert("command", "document.inspect"); return document(query, root);
    }
    const auto output = request.value("output").toString().isEmpty() ? file : absolute(root, request.value("output").toString());
    if (!simdock::plainPathWithin(root, output)) return failure("invalid_output", "Output must be a plain path within the workspace.");
    const auto expected = output == file ? data.value("diskRevision").toString() : request.value("expectedOutputRevision").toString();
    if (output != file && expected.isEmpty()) return failure("revision_required", "expectedOutputRevision is required; use missing only for a new output.");
    if (diskRevision(file) != data.value("diskRevision").toString()) return failure("disk_conflict", "The source changed during preparation.");
    const auto bytes = exists ? encodeDocumentText(after, disk.format) : after.toUtf8();
    if (!WorkspaceSessionStateService().saveDocumentWithTemplates(output, bytes, expected, model->serialize(output), &error))
        return failure("write_conflict", error, data);
    auto query = request; query.insert("command", "document.inspect"); query.insert("file", output);
    auto result = document(query, root); auto written = result.value("data").toObject(); written.insert("written", true); written.insert("changed", before != after); result.insert("data", written); return result;
}

struct ZeroSlackAutomationService::Run {
    QString id, workspace, projectId, state = "preparing", message, log;
    bool done = false, completed = false, cancelled = false;
    quint64 rejected = 0, dropped = 0;
    QJsonArray events;
    std::shared_ptr<std::atomic_bool> cancellation = std::make_shared<std::atomic_bool>(false);
    std::unique_ptr<simdock::QuestaSession> session;
    QJsonObject json() const {
        return {{"id", id}, {"workspace", workspace}, {"projectId", projectId}, {"state", state},
            {"done", done}, {"completed", completed}, {"cancelled", cancelled}, {"message", message},
            {"log", log}, {"eventCount", events.size()}, {"rejectedRecords", double(rejected)},
            {"droppedEvents", double(dropped)}, {"processAlive", session && session->alive()}};
    }
};
ZeroSlackAutomationService::ZeroSlackAutomationService(MainWindow* owner, QObject* parent) : QObject(parent), window(owner) {}
ZeroSlackAutomationService::~ZeroSlackAutomationService() { if (run) run->cancellation->store(true); }

QJsonObject ZeroSlackAutomationService::simulation(const QJsonObject& request, const QString& root) {
    const auto command = request.value("command").toString();
    if (command == "simulation.status" || command == "simulation.events" || command == "simulation.cancel" || command == "simulation.clear") {
        if (!run || run->workspace != root || request.value("runId").toString() != run->id)
            return failure("not_found", "Unknown runId in this session and workspace.");
        if (command == "simulation.clear") {
            if (!run->done || (run->session && run->session->alive())) return failure("busy", "Wait for process cleanup before clearing the completed run.");
            run.reset(); return success({{"cleared", true}});
        }
        if (command == "simulation.cancel" && !run->done) {
            run->cancelled = true; run->cancellation->store(true); run->state = "cancelling";
            if (run->session) run->session->stop();
        }
        auto data = run->json();
        if (command == "simulation.events") {
            const int offset = request.value("offset").toInt(0), limit = request.value("limit").toInt(100);
            if (offset < 0 || limit < 1 || limit > 1000) return failure("invalid_range", "offset >= 0 and 1 <= limit <= 1000 required.");
            QJsonArray events; int next = offset;
            for (; next < run->events.size() && events.size() < limit; ++next) events << run->events[next];
            data.insert("events", events); data.insert("nextOffset", next);
        }
        return success(data);
    }
    QStringList errors;
    const auto projects = simdock::loadProjects(root, &errors, false);
    if (command == "simulation.projects") {
        QJsonArray items; for (const auto& project : projects) items << projectObject(project);
        return success({{"projects", items}, {"diagnostics", strings(errors)}});
    }
    const auto id = request.value("projectId").toString();
    simdock::Project project;
    bool found = false;
    for (const auto& p : projects) if (p.id == id) { project = p; found = true; break; }
    if (command == "simulation.put") {
        const auto revision = found ? projectRevision(project) : QStringLiteral("missing");
        if ((!found && !id.isEmpty()) || request.value("expectedRevision").toString() != revision)
            return failure("revision_conflict", "Select an existing projectId and revision, or omit projectId and use missing to create.");
        if (!request.value("project").isObject()) return failure("invalid_project", "project must be an object.");
        const auto original = project;
        const auto patch = request.value("project").toObject();
        const QSet<QString> fields{"name", "sources", "includeDirectories", "dutFile", "dutName", "tbFile", "tbName", "durationNs"};
        for (auto it = patch.begin(); it != patch.end(); ++it) if (!fields.contains(it.key())) return failure("invalid_project", "Unknown or retired project field: " + it.key());
        for (auto it = patch.begin(); it != patch.end(); ++it) {
            if (it.key() == "sources" || it.key() == "includeDirectories") {
                if (!it.value().isArray()) return failure("invalid_project", "Build inputs must be arrays of paths.");
                for (const auto& value : it.value().toArray()) if (!value.isString() || value.toString().isEmpty()) return failure("invalid_project", "Build input paths must be nonempty strings.");
            } else if (it.key() == "durationNs") {
                if (!it.value().isDouble() || it.value().toDouble() != double(it.value().toInteger(-1))) return failure("invalid_project", "durationNs must be an integer.");
            } else if (!it.value().isString()) return failure("invalid_project", "Project text fields must be strings.");
        }
        if (!found) project = simdock::newProject(patch.value("name").toString());
        if (patch.contains("name")) project.name = patch.value("name").toString();
        if (patch.contains("sources")) project.sources = stringList(patch.value("sources"));
        if (patch.contains("includeDirectories")) project.includeDirectories = stringList(patch.value("includeDirectories"));
        if (patch.contains("dutFile")) project.dutFile = patch.value("dutFile").toString();
        if (patch.contains("dutName")) project.dutName = patch.value("dutName").toString();
        if (patch.contains("tbFile")) project.tbFile = patch.value("tbFile").toString();
        if (patch.contains("tbName")) project.tbName = patch.value("tbName").toString();
        if (patch.contains("durationNs")) project.durationNs = patch.value("durationNs").toInteger(-1);
        const auto relative = [&](const QString& path) {
            return QDir::isAbsolutePath(path) ? QDir(root).relativeFilePath(path) : QDir::fromNativeSeparators(path);
        };
        for (auto& path : project.sources) path = relative(path);
        for (auto& path : project.includeDirectories) path = relative(path);
        project.dutFile = relative(project.dutFile); project.tbFile = relative(project.tbFile);
        QString issue = simdock::validateProject(root, project);
        if (issue.isEmpty()) issue = simdock::validateStorageName(project.name);
        auto* workbench = window ? window->simulationWorkbench() : nullptr;
        const bool adopt = workbench && EditorFileIdentity::same(workbench->workspace(), root);
        if (issue.isEmpty() && adopt) issue = workbench->automationProjectWriteIssue(found ? &original : nullptr);
        if (!issue.isEmpty()) return failure("project_conflict", issue);
        if (request.value("dryRun").toBool()) return success({{"project", simdock::projectJson(project)}, {"preview", true}, {"written", false}, {"adopted", false}});
        if (!simdock::saveProject(root, project, &issue)) return failure("write_conflict", issue);
        if (adopt) workbench->adoptAutomationProject(project);
        return success({{"project", projectObject(project)}, {"written", true}, {"adopted", adopt}, {"diagnostic", issue}, {"viewLocated", false}});
    }
    if (!found) return failure("not_found", "Unknown projectId; no implicit active-project selection is performed.");
    if (command == "simulation.open") {
        if (!window) return failure("session_required", "A GUI session is required for workbench adoption and view positioning.");
        if (window->tabManager && window->tabManager->hasUnsavedChanges()) return failure("dirty_documents", "Save or resolve open documents before changing the workbench workspace.");
        QString error;
        if (!window->openSimDockTarget(root, id, &error)) return failure("open_failed", error);
        return success({{"project", projectObject(project)}, {"adopted", true}, {"viewLocated", true}, {"written", false}});
    }
    if (command == "simulation.project") {
        auto* workbench = window ? window->simulationWorkbench() : nullptr;
        const auto memory = workbench && EditorFileIdentity::same(workbench->workspace(), root)
            ? workbench->automationProjectState(project) : QJsonObject{{"present", false}, {"matchesDisk", false}};
        return success({{"project", projectObject(project)}, {"origin", "disk"}, {"workbench", memory},
            {"adopted", memory.value("matchesDisk").toBool()}, {"viewLocated", false}});
    }
    if (command == "simulation.validate") {
        const auto issue = simdock::validateInputs(root, project);
        return issue.isEmpty() ? success({{"project", projectObject(project)}, {"valid", true}}) : failure("invalid_inputs", issue);
    }
    if (command != "simulation.run") return failure("unknown_command", "Unknown simulation operation.");
    if (request.value("expectedRevision").toString() != projectRevision(project)) return failure("revision_conflict", "Read the project revision before starting a run.");
    if (run) return failure("run_retained", "Query or cancel the current run, then clear it before starting another.", run->json());
    if (window && window->tabManager && window->tabManager->workspaceHasUnsavedChanges(root))
        return failure("dirty_inputs", "Save or resolve the selected workspace's open documents before compiling disk inputs.");
    const auto executable = request.value("simulator").toString();
    const auto issue = simdock::simulatorError(executable);
    if (!issue.isEmpty()) return failure("invalid_simulator", issue);
    const int timeout = request.value("timeoutMs").toInt(120000);
    if (timeout < 100 || timeout > 3600000) return failure("invalid_timeout", "timeoutMs must be between 100 and 3600000.");
    run = std::make_unique<Run>();
    run->id = QUuid::createUuid().toString(QUuid::WithoutBraces); run->workspace = root; run->projectId = id;
    const auto runId = run->id;
    const auto cancellation = run->cancellation;
    auto* watcher = new QFutureWatcher<simdock::PreparedRun>(this);
    connect(watcher, &QFutureWatcher<simdock::PreparedRun>::finished, this, [this, watcher, runId, timeout] {
        const auto prepared = watcher->result(); watcher->deleteLater();
        if (!run || run->id != runId) return;
        if (run->cancellation->load() || !prepared.error.isEmpty()) {
            run->done = true; run->state = run->cancelled ? "cancelled" : "failed"; run->message = prepared.error; return;
        }
        run->session = std::make_unique<simdock::QuestaSession>();
        auto* session = run->session.get(); session->setInteractive(false); session->setWallTimeout(timeout);
        connect(session, &simdock::QuestaSession::stateChanged, this, [this](const QString& state) { if (run && !run->done) run->state = state; });
        connect(session, &simdock::QuestaSession::logText, this, [this](const QString& log) { if (run) { run->log += log; if (run->log.size() > 1024*1024) run->log = run->log.right(1024*1024); } });
        connect(session, &simdock::QuestaSession::eventRecordsRejected, this, [this](quint64 count) { if (run) run->rejected = count; });
        connect(session, &simdock::QuestaSession::eventsReady, this, [this](const QList<simdock::SimulationEvent>& events) {
            if (!run) return;
            for (const auto& event : events) {
                if (run->events.size() >= 10000) { ++run->dropped; continue; }
                QJsonArray values; for (const auto& value : event.values) values << QJsonObject::fromVariantMap(value);
                run->events << QJsonObject{{"runId", run->id}, {"simulatorRunId", event.runId}, {"timePs", event.timePs},
                    {"level", event.level}, {"tag", event.tag}, {"instance", event.instance}, {"sourceFile", event.sourceFile},
                    {"sourceLine", event.sourceLine}, {"message", event.message}, {"values", values}};
            }
        });
        connect(session, &simdock::QuestaSession::finished, this, [this](bool completed, const QString& message) {
            if (!run || run->done) return;
            run->done = true; run->completed = completed && !run->cancelled; run->message = message;
            run->state = run->cancelled ? "cancelled" : completed ? "completed" : "failed";
            run->session->stop();
        });
        QString error;
        if (!session->start(prepared, &error)) { run->done = true; run->state = "failed"; run->message = error; }
    });
    watcher->setFuture(QtConcurrent::run([root, project, executable, cancellation] { return simdock::prepareRun(executable, root, project, cancellation.get()); }));
    return success(run->json());
}
