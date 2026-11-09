#include "automationservice.h"
#include "mainwindow.h"
#include "mycodeeditor.h"
#include "tabmanager.h"
#include "webdocumentpage.h"
#include "pdfdocumentpage.h"
#include "shareddocument.h"
#include "documenttemplatemodel.h"
#include "usertemplateservice.h"
#include "simulationcommandplanner.h"
#include "simulationcommandservice.h"
#include "formatterservice.h"
#include "formattercursoranchor.h"
#include "workspacesessionstateservice.h"
#include "workspacemanager.h"
#include "workspaceconfigurationservice.h"
#include "slangmanager.h"
#include "editorfileidentity.h"
#include "../simulation/simdock/core/workspace.h"
#include "../simulation/simdock/core/dependencies.h"
#include "../simulation/simdock/core/ownedpaths.h"
#include "../simulation/simdock/core/questasession.h"
#include "../simulation/simdock/ui/workbench.h"
#include "../simulation/simdock/ui/simulationruncoordinator.h"
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
        "document.save", "document.undo", "document.redo",
        "simulation.projects", "simulation.project", "simulation.open", "simulation.validate",
        "simulation.run", "simulation.status", "simulation.events", "simulation.cancel", "simulation.clear"}},
        {"coordinates", "UTF-16 offsets; start inclusive, length in code units; all paths relative to explicit workspace"},
        {"writeContract", "expectedRevision required; dryRun previews. Online edits adopt memory only; document.save requires expectedDiskRevision. Active template editing returns conflict."},
        {"sourceContract", "Template sources are ordered. Simulation uses explicit tb + top and derives inputs; legacy projectId maps a saved TB only."},
        {"documentContract", QJsonObject{{"version", 1}, {"kind", "text/code buffers and template metadata"},
            {"unsupported", QJsonArray{"markdown", "blocks", "pdf"}},
            {"viewRegions", QJsonArray{"main", "left", "right", "bottom"}}}},
        {"simulationContract", QJsonObject{{"version", 2}, {"termination", "TB-controlled; preparation timeout only"},
            {"retired", QJsonArray{"simulation.put", "sources", "durationNs", "timeoutMs"}},
            {"savePolicy", "GUI sessions save only participating modified inputs using normal conflict checks; headless sessions use disk."}}},
        {"limits", QJsonObject{{"requestBytes", 1024*1024}, {"documentBytes", 8*1024*1024}, {"sources", 512}, {"retainedEvents", 10000}}}});
}
QJsonObject ZeroSlackAutomationService::execute(const QJsonObject& request) {
    const auto command = request.value("command").toString();
    if (command.startsWith("simulation.") && (request.contains("timeoutMs") || request.contains("durationNs") || request.contains("sources")))
        return failure("retired_parameter", "Simulation derives its inputs from tb + top and ends under TB control. sources, durationNs and timeoutMs are retired; client waiting does not stop a run.");
    const QSet<QString> stringFields{"command", "workspace", "file", "tb", "top", "scope", "id", "templateId", "parentId", "parentSlot", "instanceId", "slot", "value", "module", "instance", "replaceId", "projectId", "runId", "simulator", "expectedRevision", "expectedDiskRevision", "expectedOutputRevision", "output", "kind"};
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
    if (command.startsWith("document.")) return document(request, root);
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
    if (WebDocumentPage::supportsFile(file) || PdfDocumentPage::supportsFile(file))
        return failure("unsupported_document_kind", "document.* operates on text/code buffers. Markdown, block and PDF pages use their GUI document sessions.");
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
    const bool readOnly = command == "document.candidates";
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
    } else if (command == "document.insert" || command == "document.input" || command == "document.candidates") {
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
            const auto plan = SimulationCommandPlanner::prepare(operation);
            if (!plan.error.isEmpty()) return failure("plan_rejected", plan.error, data);
            edits << TemplateDocumentEdit{plan.start, plan.length, plan.text, operation.item.templateParentId,
                operation.item.templateParentSlot, {}, plan.instance, plan.placeholders};
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

namespace {
QJsonObject simulationPlanObject(const simdock::SimulationBuildPlan& plan)
{
    QJsonObject compilation, elaboration;
    for (auto it = plan.compilation.dependencies.cbegin(); it != plan.compilation.dependencies.cend(); ++it)
        compilation.insert(it.key(), strings(it.value()));
    for (auto it = plan.elaborationDependencies.cbegin(); it != plan.elaborationDependencies.cend(); ++it)
        elaboration.insert(it.key(), it.value());
    return {{"target", QJsonObject{{"file", plan.target.file}, {"top", plan.target.top}}},
        {"valid", plan.valid()}, {"compilationOrder", strings(plan.compilation.files)},
        {"includeDirectories", strings(plan.includeDirectories)}, {"includeOnly", strings(plan.compilation.headers)},
        {"participatingFiles", strings(plan.participatingFiles)}, {"dependencies", compilation},
        {"elaborationDependencies", elaboration}, {"diagnostics", strings(plan.compilation.diagnostics)}};
}
}
struct ZeroSlackAutomationService::Run {
    QString id, workspace, projectId, nativeRunDirectory, state = "preparing", message, log, outcome;
    bool done = false, completed = false, cancelled = false, started = false;
    quint64 rejected = 0, dropped = 0;
    QJsonArray events;
    QJsonObject plan, target;
    std::unique_ptr<QObject> observer = std::make_unique<QObject>();
    std::unique_ptr<simdock::WorkspaceSimulationController> ownedController;
    QPointer<simdock::WorkspaceSimulationController> controller;
    ~Run() { observer.reset(); if (ownedController) ownedController->shutdown(); }
    QJsonObject json() const {
        return {{"id", id}, {"workspace", workspace}, {"projectId", projectId}, {"target", target}, {"plan", plan},
            {"state", state}, {"done", done}, {"completed", completed}, {"cancelled", cancelled}, {"message", message},
            {"outcome", outcome}, {"log", log}, {"eventCount", events.size()}, {"rejectedRecords", double(rejected)},
            {"droppedEvents", double(dropped)}, {"processAlive", controller && !nativeRunDirectory.isEmpty()
                && controller->session()->lastRunDirectory() == nativeRunDirectory && controller->session()->alive()}};
    }
};
ZeroSlackAutomationService::ZeroSlackAutomationService(MainWindow* owner, QObject* parent) : QObject(parent), window(owner) {}
ZeroSlackAutomationService::~ZeroSlackAutomationService() = default;

QJsonObject ZeroSlackAutomationService::simulation(const QJsonObject& request, const QString& root)
{
    const auto command = request.value("command").toString();
    if (command == "simulation.put") return failure("retired_operation",
        "Simulation projects and manual source lists are retired. Use tb + top; saved projects remain readable.");
    if (command == "simulation.status" || command == "simulation.events" || command == "simulation.cancel" || command == "simulation.clear") {
        if (!run || run->workspace != root || request.value("runId").toString() != run->id)
            return failure("not_found", "Unknown runId in this session and workspace.");
        if (command == "simulation.clear") {
            if (!run->done) return failure("busy", "Finish or cancel the retained run before clearing its result.");
            run.reset(); return success({{"cleared", true}});
        }
        if (command == "simulation.cancel" && !run->done) {
            run->cancelled = true; run->state = "cancelling";
            if (run->controller) run->controller->stop();
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
    QStringList errors; QSet<QString> ambiguous;
    const auto projects = simdock::loadProjects(root, &errors, false, &ambiguous);
    if (command == "simulation.projects") {
        QJsonArray items; for (const auto& project : projects) items << projectObject(project);
        return success({{"projects", items}, {"diagnostics", strings(errors)}, {"readOnlyCompatibility", true}});
    }
    const auto id = request.value("projectId").toString();
    simdock::Project project; bool found = false;
    if (ambiguous.contains(id)) return failure("ambiguous_project", "The legacy project identity resolves to multiple records.");
    for (const auto& p : projects) if (!id.isEmpty() && p.id == id) { project = p; found = true; break; }
    if (!id.isEmpty() && !found) return failure("not_found", "Unknown legacy projectId.");
    if (command == "simulation.project") {
        if (!found) return failure("not_found", "An explicit legacy projectId is required.");
        return success({{"project", projectObject(project)}, {"origin", "disk"}, {"readOnlyCompatibility", true}});
    }
    if (command != "simulation.open" && command != "simulation.validate" && command != "simulation.run")
        return failure("unknown_command", "Unknown simulation operation.");
    if (!id.isEmpty() && (request.contains("tb") || request.contains("top")))
        return failure("invalid_target", "Use either tb + top or legacy projectId.");
    const simdock::SimulationTarget target = found ? simdock::SimulationTarget{project.tbFile, project.tbName}
        : simdock::SimulationTarget{request.value("tb").toString(), request.value("top").toString()};
    if (target.isEmpty() || target.file.isEmpty() || target.top.isEmpty())
        return failure("invalid_target", "Explicit tb and top are required; the filename is not used to guess a top.");
    auto* workbench = window ? window->simulationWorkbench() : nullptr;
    auto* coordinator = window ? window->simulationRunCoordinator() : nullptr;
    if (command == "simulation.open") {
        if (!window) return failure("session_required", "A GUI session is required to select and reveal the TB.");
        QString error;
        if (!window->openSimDockTarget(root, {}, &error)) return failure("open_failed", error);
        auto captured = coordinator->capture(target, &error, true);
        if (!error.isEmpty()) return failure("invalid_inputs", error);
        QMap<QString, QByteArray> overrides;
        for (auto it = captured.overrides.cbegin(); it != captured.overrides.cend(); ++it) overrides.insert(it.key(), it.value().toUtf8());
        if (found) error = workbench->controller()->setSettings(project.includeDirectories, workbench->controller()->definitionSources());
        if (error.isEmpty()) error = workbench->controller()->selectTarget(target, id, overrides);
        if (!error.isEmpty()) return failure("invalid_target", error);
        return success({{"target", QJsonObject{{"file", target.file}, {"top", target.top}}}, {"adopted", true}, {"viewLocated", true}});
    }
    QString error;
    simdock::SimulationBuildRequest inputs;
    if (coordinator) {
        if (!workbench || !EditorFileIdentity::same(workbench->workspace(), root))
            return failure("workspace_mismatch", "Activate the requested workspace in this GUI session first.");
        inputs = coordinator->capture(target, &error, command == "simulation.run");
    } else {
        inputs = simdock::SimulationBuildPlanner::workspaceRequest(root, target, &error);
        simdock::WorkspaceStateFile settings;
        if (error.isEmpty() && !settings.open(root, "simulation", "zeroslack.simulation/v1", &error))
            return failure("invalid_settings", error);
        if (error.isEmpty() && settings.exists()) {
            const auto record = settings.value();
            if (!record.value("includeDirectories").isArray()
                || (record.contains("definitionSources") && !record.value("definitionSources").isObject()))
                return failure("invalid_settings", "Invalid saved simulation compilation settings.");
            for (const auto& item : record.value("includeDirectories").toArray()) {
                if (!item.isString() || item.toString().isEmpty()) return failure("invalid_settings", "Invalid include path.");
                inputs.includeDirectories << item.toString();
            }
            const auto mappings = record.value("definitionSources").toObject();
            for (auto it = mappings.begin(); it != mappings.end(); ++it) {
                if (!it.value().isString() || it.key().trimmed().isEmpty() || it.value().toString().isEmpty())
                    return failure("invalid_settings", "Invalid definition source mapping.");
                inputs.definitionSources.insert(it.key(), it.value().toString());
            }
        }
    }
    if (!error.isEmpty()) return failure("invalid_inputs", error);
    if (found) inputs.includeDirectories = project.includeDirectories;
    if (command == "simulation.validate") {
        const auto plan = simdock::SimulationBuildPlanner::plan(inputs);
        const auto data = simulationPlanObject(*plan);
        return plan->valid() ? success(data) : failure("invalid_inputs", plan->error(), data);
    }
    if (found && request.value("expectedRevision").toString() != projectRevision(project))
        return failure("revision_conflict", "Read the legacy project revision before using its target.");
    if (run) return failure("run_retained", "Query or cancel the current run, then clear its result before starting another.", run->json());
    auto* controller = workbench ? workbench->controller() : nullptr;
    if (controller && controller->busy()) return failure("busy", "A simulation is already active in this workspace.");
    const auto executable = request.value("simulator").toString(workbench ? workbench->simulator() : QString());
    error = simdock::simulatorError(executable);
    if (!error.isEmpty()) return failure("invalid_simulator", error);
    auto pending = std::make_unique<Run>();
    if (!controller) {
        pending->ownedController = std::make_unique<simdock::WorkspaceSimulationController>();
        controller = pending->ownedController.get();
        error = controller->setWorkspace(root);
        controller->session()->setInteractive(false);
    }
    QMap<QString, QByteArray> overrides;
    for (auto it = inputs.overrides.cbegin(); it != inputs.overrides.cend(); ++it) overrides.insert(it.key(), it.value().toUtf8());
    if (error.isEmpty() && found) error = controller->setSettings(inputs.includeDirectories, inputs.definitionSources);
    if (error.isEmpty()) error = controller->selectTarget(target, id, overrides);
    if (!error.isEmpty()) return failure("invalid_target", error);
    pending->controller = controller;
    pending->id = QUuid::createUuid().toString(QUuid::WithoutBraces); pending->workspace = root; pending->projectId = id;
    pending->target = {{"file", target.file}, {"top", target.top}};
    run = std::move(pending);
    auto* observer = run->observer.get();
    auto* session = controller->session();
    connect(controller, &simdock::WorkspaceSimulationController::stateChanged, observer, [this] {
        if (run && !run->done && run->controller) run->state = run->controller->state();
    });
    connect(controller, &simdock::WorkspaceSimulationController::planReady, observer,
        [this](std::shared_ptr<const simdock::SimulationBuildPlan> plan) { if (run && !run->done && plan) run->plan = simulationPlanObject(*plan); });
    connect(session, &simdock::QuestaSession::eventRunStarted, observer, [this] {
        if (run && !run->done) { run->started = true; run->nativeRunDirectory = run->controller->session()->lastRunDirectory(); }
    });
    connect(session, &simdock::QuestaSession::logText, observer, [this](const QString& log) {
        if (run && !run->done) { run->log += log; if (run->log.size() > 1024*1024) run->log = run->log.right(1024*1024); }
    });
    connect(session, &simdock::QuestaSession::eventRecordsRejected, observer, [this](quint64 count) { if (run && !run->done) run->rejected = count; });
    connect(session, &simdock::QuestaSession::eventsReady, observer, [this](const QList<simdock::SimulationEvent>& events) {
        if (!run || run->done) return;
        for (const auto& event : events) {
            if (run->events.size() >= 10000) { ++run->dropped; continue; }
            QJsonArray values; for (const auto& value : event.values) values << QJsonObject::fromVariantMap(value);
            run->events << QJsonObject{{"runId", run->id}, {"simulatorRunId", event.runId}, {"timePs", event.timePs},
                {"level", event.level}, {"tag", event.tag}, {"instance", event.instance}, {"sourceFile", event.sourceFile},
                {"sourceLine", event.sourceLine}, {"message", event.message}, {"values", values}};
        }
    });
    connect(controller, &simdock::WorkspaceSimulationController::finished, observer, [this](bool completed, const QString& message) {
        if (!run || run->done) return;
        run->done = true; run->completed = completed && !run->cancelled; run->message = message;
        run->outcome = run->cancelled ? "cancelled" : run->started ? run->controller->session()->outcome() : QStringLiteral("failed");
        run->state = run->outcome;
        if (run->ownedController) run->ownedController->stop();
    });
    controller->prepare(executable, std::move(inputs), workbench ? workbench->waveTheme() : simdock::WaveTheme::QuestaDefault,
        coordinator ? coordinator->saveInputs() : simdock::WorkspaceSimulationController::SaveInputs(),
        coordinator ? coordinator->checkCurrent() : simdock::WorkspaceSimulationController::CheckCurrent(),
        coordinator ? coordinator->awaitInputs() : simdock::WorkspaceSimulationController::AwaitInputs());
    return success(run->json());
}
