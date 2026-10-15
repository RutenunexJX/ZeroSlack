#include "duttemplatesession.h"
#include "templatecapabilities.h"
#include "globalcontrolpanel.h"
#include "commandlayerservice.h"
#include "mycodeeditor.h"
#include <QEvent>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

DutTemplateSession::DutTemplateSession(MyCodeEditor *e, SimulationCommandRequest r,
    std::function<QString()> valid, std::function<void(const QString&)> report, QObject *parent)
    : QObject(parent), editor(e), panel(new GlobalControlPanel(e->window())), request(std::move(r)),
      instances(e->templateModel()->instances()), current(std::move(valid)), message(std::move(report)) {
    panel->setObjectName("dutTemplateInputs");
    panel->setSimulationOnly(true);
    panel->setCategory(GlobalControlCategory::Templates);
    panel->setCloseOnActivation(false);
    panel->installEventFilter(this);
    panel->setQueryChangedHandler([this](const QString &query) { refresh(query); });
    panel->setItemActivatedHandler([this](const GlobalControlItem &item) { accept(item); });
    request.cancelled = std::make_shared<std::atomic_bool>(false);
    const auto config = request.item.parameters;
    options.replaceId = config.value("replaceId").toString();
    if (!options.replaceId.isEmpty()) {
        options.module = config.value("module").toString();
        options.moduleFile = config.value("moduleFile").toString();
        options.instance = config.value("instance").toString();
        const auto parameters = config.value("parameters").toMap(), connections = config.value("bindings").toMap();
        for (auto it = parameters.cbegin(); it != parameters.cend(); ++it) options.parameters[it.key()] = it.value().toString();
        for (auto it = connections.cbegin(); it != connections.cend(); ++it) {
            options.bindings[it.key()] = it.value().toString();
            options.reuse.insert(it.key());
        }
    }
}
DutTemplateSession::~DutTemplateSession() {
    request.cancelled->store(true);
    delete panel;
}
bool DutTemplateSession::eventFilter(QObject *object, QEvent *event) {
    if (object == panel && event->type() == QEvent::Hide)
        QTimer::singleShot(0, this, [this] { if (!done && !busy && !panel->isVisible()) close("DUT inputs cancelled; source unchanged."); });
    return false;
}
void DutTemplateSession::close(const QString &reason) {
    if (done) return;
    done = true;
    request.cancelled->store(true);
    panel->hide();
    if (editor) editor->setFocus();
    if (!reason.isEmpty()) message(reason);
    deleteLater();
}
void DutTemplateSession::start() {
    if (!options.replaceId.isEmpty()) show(Stage::Action);
    else load([this] { show(Stage::Module); });
}
void DutTemplateSession::load(std::function<void()> next) {
    const auto error = current();
    if (!error.isEmpty()) { close(error); return; }
    busy = true;
    panel->hide();
    auto *watcher = new QFutureWatcher<DutTemplateAnalysis>(this);
    connect(watcher, &QFutureWatcher<DutTemplateAnalysis>::finished, this, [this, watcher, next] {
        analysis = watcher->result(); watcher->deleteLater(); busy = false;
        auto error = current();
        if (error.isEmpty()) error = analysis.error;
        if (error.isEmpty() && !SimulationCommandPlanner::sourcesCurrent(analysis.captured)) error = "A DUT source changed; reopen its inputs.";
        if (!error.isEmpty()) { close(error); return; }
        next();
    });
    watcher->setFuture(QtConcurrent::run([r=request, items=instances, selected=options] {
        return DutTemplatePlanner::analyze(r, items, selected);
    }));
}
void DutTemplateSession::show(Stage next, const QString &value) {
    if (!editor) { close(); return; }
    stage = next;
    panel->resetQuery(value);
    refresh(value);
    if (!panel->isVisible()) panel->showAt(editor->window(), editor->viewport()->mapToGlobal(editor->cursorRect().bottomLeft()));
    panel->focusSearch(true);
}
void DutTemplateSession::refresh(const QString &query) {
    QList<GlobalControlItem> items;
    QString prompt;
    const auto add = [&](const QString &id, const QString &title, const QString &detail = QString()) {
        GlobalControlItem item; item.kind = GlobalControlItemKind::Template;
        item.id = id; item.title = title; item.subtitle = detail;
        items << item;
    };
    switch (stage) {
    case Stage::Action:
        prompt = "DUT · Enter selects · Esc cancels";
        add("edit", "Edit DUT inputs"); add("remove", "Remove DUT instance", "TB signals and handwritten declarations are retained."); break;
    case Stage::Module:
        prompt = "DUT module · type to filter · Enter selects · Esc cancels";
        {
            CommandLayerPickerQuery picker;
            picker.snapshot = std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(analysis.records));
            picker.filter = query;
            for (const auto &module : CommandLayerService().moduleItems(picker)) if (analysis.modules.contains(module.name)) {
                add(symbolStableKeyText(module.symbolRecord.stableKey), module.name,
                    module.displayPath + ":" + QString::number(module.line));
                items.last().parameters = {{"module", module.name}, {"file", module.filePath}};
            }
        }
        if (items.isEmpty()) prompt += "\nNo matching parsed modules in the current compilation inputs.";
        break;
    case Stage::Instance:
        prompt = "DUT instance name · Enter accepts";
        if (!query.trimmed().isEmpty()) add(query.trimmed(), query.trimmed()); break;
    case Stage::Parameter:
        prompt = "Parameter " + analysis.parameters.value(field) + " · integral constant · Enter accepts";
        if (!query.trimmed().isEmpty()) add(query.trimmed(), query.trimmed()); break;
    case Stage::Binding: {
        if (field >= analysis.ports.size()) break;
        const auto &port = analysis.ports[field];
        prompt = port.name + " · " + port.direction + " · " + port.type + port.suffix;
        const auto declaration = TemplateCapabilities::operation(request.item.parameters, "declareSignals");
        const auto generated = TemplateCapabilities::expandName(declaration.value("namePattern", "${instance}_${port}").toString(),
            {{"instance",options.instance},{"port",port.name},{"module",options.module}});
        const auto previous = options.bindings.value(port.name);
        const bool retain = declaration.value("reuse") != "never" && options.reuse.contains(port.name) && query.isEmpty() && port.compatibleSignals.contains(previous);
        if (retain) add("reuse:" + previous, "Reuse: " + previous, "Keep this existing explicit connection.");
        add("new:" + (query.trimmed().isEmpty() ? generated : query.trimmed()),
            "New signal: " + (query.trimmed().isEmpty() ? generated : query.trimmed()), "Added to TB definitions; collisions receive a numeric suffix.");
        for (const auto &name : port.compatibleSignals) if (declaration.value("reuse") != "never" && (!retain || name != previous) && name.contains(query, Qt::CaseInsensitive))
            add("reuse:" + name, "Reuse: " + name, "Explicit connection to this compatible TB signal.");
        break;
    }
    case Stage::Confirm:
        prompt = "DUT · " + options.module + " · " + options.instance + " · Enter applies · Esc cancels";
        add("apply", "Apply DUT and connections", "One undo step includes declarations, instance and template boundaries."); break;
    }
    panel->setAccessibleName(prompt);
    panel->setItems(items, prompt);
}
void DutTemplateSession::accept(const GlobalControlItem &item) {
    if (busy || done) return;
    const auto error = current();
    if (!error.isEmpty()) { close(error); return; }
    if (stage == Stage::Action) {
        if (item.id == "remove") { finish(true); return; }
        const auto module = options.module;
        options.module.clear();
        load([this, module] { options.module = module; show(Stage::Module, module); });
    } else if (stage == Stage::Module) {
        if (options.module != item.parameters.value("module").toString() || options.moduleFile != item.parameters.value("file").toString()) {
            options.parameters.clear(); options.bindings.clear(); options.reuse.clear();
        }
        options.module = item.parameters.value("module").toString();
        options.moduleFile = item.parameters.value("file").toString();
        load([this] { options.instance = analysis.instance; show(Stage::Instance, options.instance); });
    } else if (stage == Stage::Instance) {
        options.instance = item.id; field = 0;
        if (analysis.parameters.isEmpty()) bindings();
        else show(Stage::Parameter, options.parameters.value(analysis.parameters[field], analysis.defaults.value(analysis.parameters[field])));
    } else if (stage == Stage::Parameter) {
        options.parameters[analysis.parameters[field]] = item.id;
        if (++field == analysis.parameters.size()) bindings();
        else show(Stage::Parameter, options.parameters.value(analysis.parameters[field], analysis.defaults.value(analysis.parameters[field])));
    } else if (stage == Stage::Binding) {
        const auto &port = analysis.ports[field];
        const bool reuse = item.id.startsWith("reuse:");
        options.bindings[port.name] = item.id.mid(reuse ? 6 : 4);
        if (reuse) options.reuse.insert(port.name); else options.reuse.remove(port.name);
        if (++field == analysis.ports.size()) show(Stage::Confirm);
        else show(Stage::Binding);
    } else finish();
}
void DutTemplateSession::bindings() {
    load([this] {
        options.instance = analysis.instance; field = 0;
        if (analysis.ports.isEmpty()) show(Stage::Confirm);
        else show(Stage::Binding);
    });
}
void DutTemplateSession::finish(bool remove) {
    busy = true; panel->hide();
    if (remove) {
        TemplateDocumentEdit edit;
        edit.start = request.item.replacementStart; edit.length = request.item.replacementLength;
        edit.parentId = request.item.templateParentId; edit.parentSlot = request.item.templateParentSlot;
        edit.replaceId = options.replaceId;
        QString error = current();
        if (error.isEmpty() && editor) editor->applyTemplateEdits({edit}, &error);
        close(error.isEmpty() ? "Removed DUT; TB declarations retained." : error); return;
    }
    auto *watcher = new QFutureWatcher<DutTemplatePlan>(this);
    connect(watcher, &QFutureWatcher<DutTemplatePlan>::finished, this, [this, watcher] {
        auto plan = watcher->result(); watcher->deleteLater();
        QString error = current();
        if (error.isEmpty()) error = plan.error;
        if (error.isEmpty() && !SimulationCommandPlanner::sourcesCurrent(plan.captured)) error = "A source changed; no DUT changes were applied.";
        if (error.isEmpty() && editor) editor->applyTemplateEdits(plan.edits, &error);
        close(error.isEmpty() ? "DUT applied. Enter edits its inputs; Ctrl+Z undoes the complete change." : error);
    });
    watcher->setFuture(QtConcurrent::run([r=request, items=instances, selected=options] {
        return DutTemplatePlanner::prepare(r, items, selected);
    }));
}
