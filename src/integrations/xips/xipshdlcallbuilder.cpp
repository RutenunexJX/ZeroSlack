#include "xipshdlcallbuilder.h"
#include "editorfileidentity.h"
#include "semanticindexsnapshot.h"
#include "tsdocument.h"
#include <QFileInfo>
#include <QSet>

namespace {
bool kind(TSNode node, const char* name) { return !ts_node_is_null(node) && qstrcmp(ts_node_type(node), name) == 0; }
int start(TSNode node) { return int(ts_node_start_byte(node) / 2); }
int end(TSNode node) { return int(ts_node_end_byte(node) / 2); }
TSNode covering(TSNode node, int position) {
    if (ts_node_is_null(node) || start(node) > position || end(node) <= position) return {};
    for (uint32_t i = 0; i < ts_node_named_child_count(node); ++i) {
        auto child = covering(ts_node_named_child(node, i), position);
        if (!ts_node_is_null(child)) return child;
    }
    return node;
}
int insertionStart(const GlobalControlQueryContext& context) {
    return context.replacementStart >= 0 ? context.replacementStart : context.cursorPosition;
}
QString withProbe(const GlobalControlQueryContext& context) {
    auto text = context.documentText;
    text.replace(insertionStart(context), context.replacementLength, QStringLiteral("__xips_probe();"));
    return text;
}
int declarationPosition(const GlobalControlQueryContext& context) {
    TSDocument document; document.setText(withProbe(context));
    auto node = covering(document.rootNode(), insertionStart(context));
    auto child = node;
    for (; !ts_node_is_null(node); child = node, node = ts_node_parent(node)) {
        if (!kind(node, "module_declaration")) continue;
        const auto position = start(child);
        const auto line = context.documentText.lastIndexOf(QLatin1Char('\n'), qMax(0, position - 1)) + 1;
        // Start at the margin so the editor does not reindent the unchanged
        // text between the declaration and call as if it were a new template.
        return context.documentText.mid(line, position - line).trimmed().isEmpty() ? line : position;
    }
    return -1;
}
SlangWorkspaceAnalysis analyze(const XipsHdlCallRequest& request, const QString& text,
    std::shared_ptr<const SemanticAnalysisInput>* input = nullptr) {
    SemanticAnalysisRequest sourceRequest;
    sourceRequest.sourceOverrides = request.contents;
    sourceRequest.sourceOverrides.insert(request.context.fileName, text);
    SemanticInputCapture capture(sourceRequest);
    SlangManager slang;
    const auto cancelled = [flag = request.cancelled] { return flag && flag->load(); };
    auto result = slang.analyzeCapturedWorkspace(capture, request.orderedFiles, request.includeDirectories,
        request.defines, {}, cancelled, {true, true, false});
    if (!capture.stillMatchesDisk(cancelled)) result.error = QStringLiteral("A source or include changed during Slang analysis. Reopen the insertion palette.");
    if (input) *input = capture.seal();
    return result;
}
bool inputsCurrent(const XipsHdlCallInspection& inspection) {
    if (!inspection.input) return false;
    SemanticAnalysisRequest request;
    SemanticInputCapture capture(request);
    for (auto it = inspection.input->sources.cbegin(); it != inspection.input->sources.cend(); ++it)
        if (!it->overridden && capture.source(it.key()) != it.value()) return false;
    return true;
}
QString diagnosticKey(const SemanticDiagnostic& diagnostic) {
    return QFileInfo(diagnostic.fileName).absoluteFilePath().toCaseFolded()
        + QLatin1Char('|') + diagnostic.codeName + QLatin1Char('|') + diagnostic.message;
}
QString newError(const QList<SemanticDiagnostic>& before, const QList<SemanticDiagnostic>& after) {
    QHash<QString, int> existing;
    for (const auto& diagnostic : before)
        if (diagnostic.severity == SemanticDiagnostic::Error) ++existing[diagnosticKey(diagnostic)];
    for (const auto& diagnostic : after) {
        if (diagnostic.severity != SemanticDiagnostic::Error) continue;
        auto& count = existing[diagnosticKey(diagnostic)];
        if (count > 0) { --count; continue; }
        return QStringLiteral("%1:%2: %3").arg(QFileInfo(diagnostic.fileName).fileName()).arg(diagnostic.line).arg(diagnostic.message);
    }
    return {};
}
bool integralSignal(const SemanticSymbolRecord& record, bool scalar) {
    const auto& facts = record.presentation.defaultInfo;
    return record.presentation.directModuleVariable && facts.available && facts.integral && facts.fixedSize
        && facts.bitWidth > 0 && (!scalar || facts.bitWidth == 1) && !facts.unpackedArray
        && !facts.interfaceType && facts.enumValues.isEmpty() && !facts.structuredLeavesAvailable;
}
}

bool XipsHdlCallBuilder::proceduralSite(const GlobalControlQueryContext& context, bool allowFunctions) {
    if (insertionStart(context) < 0 || insertionStart(context) > context.documentText.size()) return false;
    TSDocument document; document.setText(withProbe(context));
    bool procedure = false, statement = false;
    for (auto node = covering(document.rootNode(), insertionStart(context)); !ts_node_is_null(node); node = ts_node_parent(node)) {
        if (kind(node, "comment") || kind(node, "string_literal")) return false;
        if (kind(node, "function_declaration")) { if (!allowFunctions) return false; procedure = true; }
        if (kind(node, "subroutine_call_statement")) statement = true;
        if (kind(node, "initial_construct") || kind(node, "always_construct")
            || kind(node, "task_declaration")) procedure = true;
    }
    return procedure && statement;
}

XipsHdlCallInspection XipsHdlCallBuilder::inspect(const XipsHdlCallRequest& request) {
    XipsHdlCallInspection result;
    const auto& context = request.context;
    const auto symbol = request.entry.value(QStringLiteral("symbol")).toString();
    const bool task = request.entry.value(QStringLiteral("kind")).toString() == QStringLiteral("task");
    if (task && !proceduralSite(context)) {
        result.error = QStringLiteral("Insert a task at a procedural statement position, outside a function."); return result;
    }
    auto analysis = analyze(request, context.documentText, &result.input);
    if (analysis.cancelled || !analysis.error.isEmpty()) { result.error = analysis.error.isEmpty() ? QStringLiteral("Insertion cancelled.") : analysis.error; return result; }
    result.diagnostics = analysis.diagnostics;
    for (const auto& diagnostic : analysis.diagnostics) {
        if (diagnostic.severity == SemanticDiagnostic::Error
            && (!EditorFileIdentity::same(diagnostic.fileName, context.fileName)
                || qAbs(diagnostic.line - context.cursorLine) > 1)) {
            result.error = QStringLiteral("Resolve the existing Slang error before inserting: %1").arg(diagnostic.message);
            return result;
        }
    }
    result.callName = symbol;
    if (!request.entry.value(QStringLiteral("typeParameters")).toStringList().isEmpty()) {
        if (!task) {
            result.error = QStringLiteral("This generic command requires an explicitly bound signal driver."); return result;
        }
        if (request.target.isEmpty() || request.clock.isEmpty()) {
            result.needsSignalBinding = true;
            for (const auto& record : analysis.symbols)
                if (integralSignal(record, false) && record.owner.name == context.moduleName
                    && EditorFileIdentity::same(record.location.fileName, context.fileName))
                    result.bindingCandidates << record;
            if (result.bindingCandidates.isEmpty()) result.error = QStringLiteral("Slang found no supported module variables to bind.");
            return result;
        }
        SemanticIndex local;
        local.setSnapshot(std::make_shared<const SemanticIndexSnapshot>(SemanticIndexSnapshot::fromSymbolRecords(
            analysis.symbols, {}, {}, request.contents)));
        SemanticQueryContext query;
        query.fileName = context.fileName; query.moduleName = context.moduleName;
        query.cursorPosition = context.cursorPosition; query.cursorLine = context.cursorLine;
        const auto target = local.resolveVisibleValueRecord(request.target, query);
        const auto clock = local.resolveVisibleValueRecord(request.clock, query);
        const bool reset = symbol == QStringLiteral("tb_reset_package::reset_driver::run");
        if (!integralSignal(target, reset) || !integralSignal(clock, true) || target.stableKey == clock.stableKey) {
            result.error = QStringLiteral("Select distinct module variables: a one-bit clock and a supported packed integral target (one bit for reset)."); return result;
        }
        result.declarationPosition = declarationPosition(context);
        if (result.declarationPosition < 0 || result.declarationPosition > insertionStart(context)) {
            result.error = QStringLiteral("The signal adapter needs a module declaration scope."); return result;
        }
        int suffix = 1;
        do { result.bindingName = QStringLiteral("xips_driver_%1").arg(suffix++); }
        while (context.documentText.contains(result.bindingName));
        const auto type = result.bindingName + QStringLiteral("_value_t");
        result.declaration = QStringLiteral("typedef type(%1) %2;\n").arg(request.target, type) + QStringLiteral(
            "class %1 extends tb_signal_package::signal_driver#(%2);\n"
            "  virtual task wait_edge(input bit rising);\n"
            "    if (rising) @(posedge %3); else @(negedge %3);\n"
            "  endtask\n"
            "  virtual function void drive(input %2 value); %4 = value; endfunction\n"
            "endclass\n\n").arg(result.bindingName, type, request.clock, request.target);
        const auto separator = symbol.lastIndexOf(QStringLiteral("::"));
        result.callName = symbol.left(separator) + QStringLiteral("#(%1)").arg(type) + symbol.mid(separator);
        auto probe = context.documentText;
        probe.insert(result.declarationPosition, result.declaration + QStringLiteral("typedef %1 %2_callable;\n")
            .arg(result.callName.left(result.callName.lastIndexOf(QStringLiteral("::"))), result.bindingName));
        analysis = analyze(request, probe);
        result.error = analysis.error.isEmpty() ? newError(result.diagnostics, analysis.diagnostics) : analysis.error;
        if (!result.error.isEmpty()) return result;
    }
    QSet<QString> identities;
    for (const auto& record : analysis.symbols) {
        if (!record.presentation.callableSignatureAvailable
            || !EditorFileIdentity::same(record.location.fileName, request.sourceFile)
            || record.presentation.callableQualifiedName != symbol) continue;
        if ((record.declarationKind == SymbolTaxonomy::DeclarationKind::Task) != task) {
            result.error = QStringLiteral("The exported task/function kind differs from the command catalog."); return result;
        }
        identities.insert(symbolStableKeyText(record.stableKey));
        result.callable = record;
    }
    if (identities.size() != 1) result.error = QStringLiteral("Slang did not resolve one exported declaration for %1.").arg(symbol);
    else if (!task && result.callable.presentation.callableReturnsVoid && !proceduralSite(context, true))
        result.error = QStringLiteral("This function returns void; choose a procedural statement position.");
    return result;
}

XipsHdlCallPlan XipsHdlCallBuilder::build(const XipsHdlCallRequest& request,
    const XipsHdlCallInspection& inspection, const QHash<QString, QString>& arguments) {
    XipsHdlCallPlan result;
    if (!inspection.error.isEmpty()) { result.error = inspection.error; return result; }
    if (inspection.needsSignalBinding) { result.error = QStringLiteral("Choose the clock and target before inserting this call."); return result; }
    if (!inputsCurrent(inspection)) { result.error = QStringLiteral("A source or include changed after the call was prepared. Reopen the insertion palette."); return result; }
    const bool task = inspection.callable.declarationKind == SymbolTaxonomy::DeclarationKind::Task;
    QString call;
    if (!inspection.bindingName.isEmpty()) call = QStringLiteral("begin\n  %1 driver;\n  tb_signal_package::status_t status;\n  driver = new;\n  ").arg(inspection.bindingName);
    call += inspection.callName + QLatin1Char('(');
    bool first = true;
    for (const auto& argument : inspection.callable.presentation.callableArguments) {
        const auto value = arguments.value(argument.name).trimmed();
        if (value.isEmpty() && !argument.hasDefault) { result.error = QStringLiteral("Supply a value for %1.").arg(argument.name); return result; }
        if (!first) call += QStringLiteral(", ");
        first = false;
        call += QLatin1Char('.') + argument.name + QLatin1Char('(');
        if (inspection.bindingName.isEmpty() || (argument.name != QStringLiteral("target") && argument.name != QStringLiteral("status")))
            result.placeholders.append({argument.name, int(call.size()), int(value.size()), int(result.placeholders.size()) + 1, true});
        call += value + QLatin1Char(')');
    }
    call += (task || inspection.callable.presentation.callableReturnsVoid) ? QStringLiteral(");") : QStringLiteral(")");
    if (!inspection.bindingName.isEmpty()) call += QStringLiteral("\nend");
    result.start = insertionStart(request.context);
    result.end = result.start + request.context.replacementLength;
    result.text = call;
    if (!inspection.declaration.isEmpty()) {
        const auto prefix = inspection.declaration + request.context.documentText.mid(inspection.declarationPosition,
            result.start - inspection.declarationPosition);
        for (auto& placeholder : result.placeholders) placeholder.start += prefix.size();
        result.text.prepend(prefix);
        result.start = inspection.declarationPosition;
    }
    auto proposed = request.context.documentText;
    proposed.replace(result.start, result.end - result.start, result.text);
    const auto analysis = analyze(request, proposed);
    if (analysis.cancelled) result.error = QStringLiteral("Insertion cancelled.");
    else if (!analysis.error.isEmpty()) result.error = analysis.error;
    else result.error = newError(inspection.diagnostics, analysis.diagnostics);
    return result;
}
