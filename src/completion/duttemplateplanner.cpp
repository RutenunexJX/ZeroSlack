#include "effectivevalueservice.h"
#include "duttemplateplanner.h"
#include "templatecapabilities.h"
#include "moduleinstantiationtemplate.h"
#include "slangmanager.h"
#include "editorfileidentity.h"
#include <QDir>
#include <QRegularExpression>
#include <algorithm>

namespace {
using Kind = SymbolTaxonomy::DeclarationKind;
bool identifier(const QString &value) {
    return QRegularExpression("^[A-Za-z_][A-Za-z0-9_$]*$").match(value).hasMatch();
}
const TemplateInstance *find(const QList<TemplateInstance> &items, const QString &id) {
    for (const auto &item : items) if (item.id == id) return &item;
    return nullptr;
}
CodeTemplateSlot region(const TemplateInstance &root, const QString &content) {
    for (const auto &slot : root.inputs)
        if (slot.input.value("kind") == "Code" && slot.input.value("content") == content) return slot;
    return {};
}
QString rootName(const TemplateInstance &root, const QString &text) {
    for (const auto &slot : root.inputs)
        if (slot.name == root.snapshot.value("nameSlot", "name").toString())
            return text.mid(slot.start, slot.length).trimmed();
    return {};
}
QString fileName(const SimulationCommandRequest &request) {
    return request.context.fileName.isEmpty() ? QDir::temp().filePath(
        QString("__zeroslack_untitled_%1.sv").arg(request.context.documentInstance)) : request.context.fileName;
}
SlangWorkspaceAnalysis compile(const SimulationCommandRequest &request, const QString &source,
                               const QString &top, std::shared_ptr<const SemanticAnalysisInput> *captured) {
    SemanticAnalysisRequest semantic;
    semantic.sourceOverrides = request.contents;
    semantic.sourceOverrides.remove(QString());
    semantic.sourceOverrides.insert(fileName(request), source);
    SemanticInputCapture capture(semantic);
    auto files = request.orderedFiles;
    files.removeAll(QString());
    if (!files.contains(fileName(request))) files << fileName(request);
    files.removeDuplicates();
    SlangManager slang;
    const auto cancelled = [flag = request.cancelled] { return flag && flag->load(); };
    auto result = slang.analyzeCapturedWorkspace(capture, files, request.includeDirectories,
        request.defines, top, cancelled, {true, true, false});
    if (result.cancelled || !capture.stillMatchesDisk(cancelled))
        result.error = "DUT preparation was cancelled or a source changed.";
    *captured = capture.seal();
    return result;
}
QString diagnostics(const SlangWorkspaceAnalysis &analysis) {
    if (!analysis.error.isEmpty()) return analysis.error;
    for (const auto &diagnostic : analysis.diagnostics)
        if (diagnostic.severity == SemanticDiagnostic::Error)
            return QString("Resolve the DUT/TB error: %1").arg(diagnostic.message);
    return {};
}
bool type(const SemanticDeclaredTypeFacts &declared, QString *base, QString *suffix) {
    if (!declared.complete || declared.externalDeclarationBaseText.isEmpty()) return false;
    *base = declared.externalDeclarationBaseText;
    suffix->clear();
    for (const auto &dimension : declared.dimensions) if (dimension.emitInDeclaration) {
        QString left, right;
        for (const auto &node : declared.constants.evaluationNodes) if (node.evaluated) {
            if (node.id == dimension.leftEvaluationNodeId) left = node.resultValueText;
            if (node.id == dimension.rightEvaluationNodeId) right = node.resultValueText;
        }
        if (left.isEmpty() || right.isEmpty()) return false;
        const auto range = QString(" [%1:%2]").arg(left, right);
        if (dimension.kind == SemanticTypeDimensionKind::Packed) *base += range;
        else *suffix += range;
    }
    return true;
}
QList<SemanticSymbolRecord> members(SemanticIndex &index, const SemanticSymbolRecord &module) {
    auto result = index.getDeclarationMemberRecords(module);
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.location.position < b.location.position;
    });
    return result;
}
SemanticSymbolRecord moduleRecord(SemanticIndex &index, const QString &name) {
    for (const auto &record : index.getSymbolRecordsByDeclarationKind(Kind::Module))
        if (record.name == name) return record;
    return {};
}
QHash<QString, QString> values(const DutTemplateOptions &options) {
    QHash<QString, QString> result{{"instance", options.instance}};
    for (auto it = options.parameters.cbegin(); it != options.parameters.cend(); ++it)
        result.insert("parameter:" + it.key(), it.value());
    for (auto it = options.bindings.cbegin(); it != options.bindings.cend(); ++it)
        result.insert("port:" + it.key(), it.value());
    return result;
}
}

QString DutTemplatePlanner::rootError(const TemplateInstance &root) {
    if (!TemplateCapabilities::root(root.snapshot)) return "Module insertion requires a module-root template.";
    const auto error = TemplateCapabilities::validate(root.snapshot); if (!error.isEmpty()) return error;
    int nameInputs = 0;
    for (const auto& slot : root.inputs) if (slot.name == root.snapshot.value("nameSlot", "name").toString() && slot.input.value("kind") != "Code") ++nameInputs;
    if (nameInputs == 0) return "A module root requires a module-name input.";
    for (const auto &content : {"declarations", "instances", "behaviors"}) {
        int count = 0;
        for (const auto &slot : root.inputs)
            if (slot.input.value("kind") == "Code" && slot.input.value("content") == content) ++count;
        if (count != 1) return "A TB root requires exactly one definitions, instances and behaviors region.";
        const auto configured = TemplateCapabilities::region(root.snapshot, content);
        if (!configured.isEmpty() && region(root, content).name != configured) return "A configured root region does not match its Code slot.";
    }
    if (region(root, "declarations").start >= region(root, "instances").start ||
        region(root, "instances").start >= region(root, "behaviors").start)
        return "TB regions must be ordered: definitions, instances, behaviors.";
    return {};
}
QString DutTemplatePlanner::availableName(const QString &base, const QSet<QString> &names) {
    auto name = base;
    for (int i = 2; names.contains(name); ++i) name = base + "_" + QString::number(i);
    return name;
}
DutTemplateAnalysis DutTemplatePlanner::analyze(const SimulationCommandRequest &request,
    const QList<TemplateInstance> &instances, const DutTemplateOptions &options) {
    DutTemplateAnalysis result;
    if (request.context.readOnly) { result.error = "This document is read-only."; return result; }
    const auto *root = find(instances, request.item.templateParentId);
    if (!root) { result.error = "The TB root is no longer available."; return result; }
    result.error = rootError(*root);
    if (!result.error.isEmpty()) return result;
    result.error = TemplateCapabilities::validate(request.item.parameters);
    if (!result.error.isEmpty()) return result;
    const auto definition = region(*root, "declarations");
    const auto instanceRegion = region(*root, "instances");
    const auto declarationOp = TemplateCapabilities::operation(request.item.parameters, "declareSignals");
    const auto instanceOp = TemplateCapabilities::operation(request.item.parameters, "instantiateModule");
    if ((!declarationOp.isEmpty() && declarationOp.value("target") != definition.name) ||
        (!instanceOp.isEmpty() && instanceOp.value("target") != instanceRegion.name)) {
        result.error = "The configured operation targets do not belong to this root's declaration and instance regions."; return result;
    }
    const auto top = rootName(*root, request.context.documentText);
    if (!identifier(top) || request.item.templateParentSlot != instanceRegion.name) {
        result.error = "Select the TB instance region and enter a valid TB name."; return result;
    }
    auto source = request.context.documentText;
    if (const auto *old = find(instances, options.replaceId))
        for (const auto &f : old->fragments) source.replace(f.start, f.length, QString(f.length, ' '));
    auto analysis = compile(request, source, top, &result.captured);
    result.error = options.module.isEmpty() ? analysis.error : diagnostics(analysis);
    if (!result.error.isEmpty()) return result;
    SemanticIndex index;
    index.setSnapshot(std::make_shared<const SemanticIndexSnapshot>(
        SemanticIndexSnapshot::fromSymbolRecords(analysis.symbols)));
    QHash<QString, int> counts;
    for (const auto &record : index.getSymbolRecordsByDeclarationKind(Kind::Module)) ++counts[record.name];
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.key() != top && identifier(it.key()) && it.key().startsWith(
            request.item.parameters.value("semanticInputs").toMap().value("namePrefix").toString(), Qt::CaseInsensitive)) result.modules << it.key();
    result.modules.sort();
    for (const auto &record : analysis.symbols)
        if (record.owner.name == top) result.names.insert(record.name);
    result.records = analysis.symbols;
    if (options.module.isEmpty()) return result;
    if (!result.modules.contains(options.module)) { result.error = "Select an unambiguous current DUT module."; return result; }
    const auto module = moduleRecord(index, options.module);
    if (counts.value(options.module) != 1 || (!options.moduleFile.isEmpty() &&
        !EditorFileIdentity::same(module.location.fileName, options.moduleFile))) {
        result.error = "The selected module source is ambiguous in the active compilation inputs. Resolve duplicate definitions before generating its instance.";
        return result;
    }
    if (!module.presentation.moduleDefinitionAvailable || !module.presentation.tbGenerationIssue.isEmpty()) {
        result.error = module.presentation.tbGenerationIssue.isEmpty() ? "DUT semantic facts are unavailable." : module.presentation.tbGenerationIssue;
        return result;
    }
    for (const auto &record : members(index, module)) if (record.declarationKind == Kind::Parameter) {
        const auto value = record.presentation.defaultInfo.valueText;
        if (value.isEmpty()) { result.error = "Unresolved parameter: " + record.name; return result; }
        result.parameters << record.name;
        result.defaults[record.name] = value;
    }
    auto selected = options;
    selected.instance = availableName(options.instance.isEmpty() ? "u_" + moduleInstanceStem(options.module) : options.instance, result.names);
    if (!identifier(selected.instance)) { result.error = "The DUT instance name must be an SV identifier."; return result; }
    result.instance = selected.instance;
    for (const auto &name : result.parameters) {
        if (!selected.parameters.contains(name)) selected.parameters[name] = result.defaults[name];
        const auto literal = EffectiveValueService::evaluateLiteral(selected.parameters[name]);
        if (!literal.available || !literal.integral || literal.numericValueText.isEmpty()) {
            result.error = "Use a resolved integral constant for parameter " + name; return result;
        }
    }
    auto probeValues = values(selected);
    for (const auto &record : members(index, module))
        if (record.declarationKind == Kind::Port) probeValues["port:" + record.name] = {};
    const auto probe = moduleInstantiationTemplateForRecord(&index, module, probeValues).text;
    const int start = request.item.replacementStart;
    if (start < instanceRegion.start || start > instanceRegion.start + instanceRegion.length) {
        result.error = "The DUT insertion position left the instance region."; return result;
    }
    source.insert(start, probe);
    analysis = compile(request, source, top, &result.captured);
    result.error = diagnostics(analysis);
    if (!result.error.isEmpty()) return result;
    index.setSnapshot(std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(analysis.symbols)));
    const auto path = top + "." + selected.instance;
    for (const auto &record : members(index, moduleRecord(index, options.module))) if (record.declarationKind == Kind::Port) {
        DutTemplatePort port; port.name = record.name;
        const auto facts = record.presentation.instanceInfoByPath.value(path);
        const auto declared = record.presentation.declaredTypeFactsByPath.value(path);
        port.direction = facts.portDirection;
        if (!identifier(port.name) || !facts.available || !facts.fixedSize || facts.interfaceType ||
            !QStringList{"input", "output", "inout"}.contains(port.direction) || !type(declared, &port.type, &port.suffix)) {
            result.error = QString("Port %1 has an unsupported or unresolved direction/type/interface. No source was changed.").arg(port.name);
            return result;
        }
        for (const auto &signal : analysis.symbols) {
            if (signal.owner.name != top || !EditorFileIdentity::same(signal.location.fileName, fileName(request)) ||
                signal.location.position < definition.start || signal.location.position >= definition.start + definition.length ||
                (signal.declarationKind != Kind::Signal && signal.declarationKind != Kind::StructVariable &&
                 signal.collectorKind != SymbolTaxonomy::CollectorKind::EnumVariable)) continue;
            QString base, suffix;
            const auto info = signal.presentation.instanceInfoByPath.value(top, signal.presentation.defaultInfo);
            const auto shape = signal.presentation.declaredTypeFactsByPath.value(top, signal.presentation.defaultDeclaredTypeFacts);
            if (!type(shape, &base, &suffix) || base != port.type || suffix != port.suffix) continue;
            if (port.direction == "inout" && signal.collectorKind != SymbolTaxonomy::CollectorKind::Wire) continue;
            if (port.direction == "output" && (info.driverPresence != SemanticDriverPresenceState::ProvenZero)) continue;
            port.compatibleSignals << signal.name;
        }
        port.compatibleSignals.removeDuplicates(); port.compatibleSignals.sort();
        result.ports << port;
    }
    result.records = analysis.symbols;
    return result;
}
DutTemplatePlan DutTemplatePlanner::prepare(const SimulationCommandRequest &request,
    const QList<TemplateInstance> &instances, const DutTemplateOptions &options) {
    DutTemplatePlan result;
    const auto analysis = analyze(request, instances, options);
    result.error = analysis.error; result.captured = analysis.captured;
    if (!result.error.isEmpty()) return result;
    if (options.module.isEmpty()) { result.error = "Choose a DUT module."; return result; }
    const auto *root = find(instances, request.item.templateParentId);
    const auto definitions = region(*root, "declarations");
    auto selected = options;
    selected.instance = analysis.instance;
    auto names = analysis.names; names.insert(selected.instance);
    QString declarations;
    for (const auto &port : analysis.ports) {
        auto name = selected.bindings.value(port.name);
        if (selected.reuse.contains(port.name)) {
            if (TemplateCapabilities::operation(request.item.parameters, "declareSignals").value("reuse") == "never") {
                result.error = "This template requires a new signal for each port."; return result;
            }
            if (!port.compatibleSignals.contains(name)) { result.error = "The explicit signal binding is incompatible: " + port.name; return result; }
        } else {
            if (name.isEmpty()) name = TemplateCapabilities::expandName(
                TemplateCapabilities::operation(request.item.parameters, "declareSignals").value("namePattern", "${instance}_${port}").toString(),
                {{"instance",selected.instance},{"port",port.name},{"module",selected.module}});
            if (!identifier(name)) { result.error = "Invalid signal name for " + port.name; return result; }
            name = availableName(name, names);
            declarations += QString("  %1%2 %3%4;\n").arg(port.direction == "inout" ? "wire " : "", port.type, name, port.suffix);
            names.insert(name);
        }
        selected.bindings[port.name] = name;

    }
    for (const auto &name : analysis.parameters)
        if (!selected.parameters.contains(name)) selected.parameters[name] = analysis.defaults[name];
    SemanticIndex index;
    index.setSnapshot(std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(analysis.records)));
    TemplateDocumentEdit main;
    main.start = request.item.replacementStart; main.length = request.item.replacementLength;
    main.parentId = root->id; main.parentSlot = request.item.templateParentSlot; main.replaceId = options.replaceId;
    main.text = moduleInstantiationTemplateForRecord(&index, moduleRecord(index, options.module), values(selected)).text + "\n";
    main.instance.label = request.item.title;
    auto snapshot = request.item.parameters;
    QVariantMap parameters, bindings;
    for (auto it = selected.parameters.cbegin(); it != selected.parameters.cend(); ++it) parameters[it.key()] = it.value();
    for (auto it = selected.bindings.cbegin(); it != selected.bindings.cend(); ++it) bindings[it.key()] = it.value();
    snapshot.insert("module", selected.module); snapshot.insert("instance", selected.instance);
    snapshot.insert("moduleFile", selected.moduleFile);
    snapshot.insert("parameters", parameters); snapshot.insert("bindings", bindings);
    main.instance.snapshot = snapshot;
    result.edits << main;
    if (!declarations.isEmpty()) {
        TemplateDocumentEdit extra;
        extra.start = definitions.start; extra.parentId = root->id; extra.parentSlot = definitions.name;
        extra.text = declarations; extra.instance.label = TemplateCapabilities::operation(request.item.parameters, "declareSignals").value("label", "DUT signals").toString();
        extra.instance.snapshot = {{"kind", "dutSignals"}, {"ownership", "tb"}};
        result.edits << extra;
    }
    auto source = request.context.documentText;
    auto edits = result.edits;
    std::sort(edits.begin(), edits.end(), [](const auto &a, const auto &b) { return a.start > b.start; });
    for (const auto &edit : edits) source.replace(edit.start, edit.length, edit.text);
    const auto verified = compile(request, source, rootName(*root, request.context.documentText), &result.captured);
    result.error = diagnostics(verified);
    if (!result.error.isEmpty()) result.edits.clear();
    return result;
}
