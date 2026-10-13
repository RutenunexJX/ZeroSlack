#include "testbench.h"
#include <QRegularExpression>
#include "semanticindex.h"
#include "editorfileidentity.h"
#include "effectivevalueservice.h"
#include <algorithm>

namespace simdock {
bool completeTestbenchTypes(Module& module, SemanticIndex& index, const QString& file,
                           const QString& content, QString* error)
{
    error->clear();
    module.limitations = {QStringLiteral("Current Slang facts have not been validated for this DUT.")};
    if (index.getCachedFileContent(file) != content) {
        *error = QStringLiteral("Wait for semantic analysis of the current DUT document before creating its TB."); return false;
    }
    const auto definitions = index.getSymbolRecordsByDeclarationKind(SymbolTaxonomy::DeclarationKind::Module);
    QList<SemanticSymbolRecord> matches;
    for (const auto& record : definitions)
        if (record.name == module.name) matches << record;
    if (matches.size() != 1 || !EditorFileIdentity::same(matches.first().location.fileName, file)) {
        *error = QStringLiteral("No unambiguous current Slang module is available."); return false;
    }
    auto records = index.getSymbolRecordsByOwner(module.name);
    if (!matches.first().presentation.moduleDefinitionAvailable
        || !matches.first().presentation.tbGenerationIssue.isEmpty()) {
        *error = matches.first().presentation.tbGenerationIssue.isEmpty()
            ? QStringLiteral("The current Slang module definition is not ready.") : matches.first().presentation.tbGenerationIssue;
        return false;
    }
    std::stable_sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        return a.location.position < b.location.position;
    });
    EffectiveValueService localValues(&index);
    auto* values = &index == SemanticIndex::getInstance() ? EffectiveValueService::getInstance() : &localValues;
    QStringList issues;
    module.ports.clear(); module.parameters.clear(); module.imports.clear(); module.limitations.clear();
    // External type spellings are qualified by Slang; retain direct imports as source context.
    for (const auto& record : records)
        if (record.collectorKind == SymbolTaxonomy::CollectorKind::PackageImport)
            module.imports << QStringLiteral("import %1::%2;").arg(record.name, record.type.rawTypeText);
    module.imports.removeDuplicates();
    for (const auto& record : records) {
        const bool portRecord = record.declarationKind == SymbolTaxonomy::DeclarationKind::Port;
        const bool parameterRecord = record.declarationKind == SymbolTaxonomy::DeclarationKind::Parameter
            || record.declarationKind == SymbolTaxonomy::DeclarationKind::Localparam;
        if ((!portRecord && !parameterRecord) || record.usageRole != SymbolTaxonomy::SymbolUsageRole::Declaration) continue;
        const auto& facts = record.presentation.defaultInfo;
        EffectiveValueQuery query; query.symbol = record; query.documentText = content;
        if (!values->resolve(query).current()) {
            issues << QStringLiteral("%1 has pending or invalid semantic facts.").arg(record.name); continue;
        }
        if (!facts.available || !facts.fixedSize || !facts.integral || facts.unpackedArray || facts.interfaceType) {
            issues << QStringLiteral("%1 needs a manual declaration: %2").arg(record.name, facts.resolvedTypeText); continue;
        }
        if (parameterRecord) {
            if (!facts.enumValues.isEmpty()) { issues << QStringLiteral("Enum parameter %1 requires a manual TB.").arg(record.name); continue; }
            if (facts.valueText.isEmpty()) { issues << QStringLiteral("No constant default for %1.").arg(record.name); continue; }
            module.parameters.append({record.name, QStringLiteral("logic %1[%2:0]")
                .arg(facts.signedIntegral ? QStringLiteral("signed ") : QString()).arg(facts.bitWidth - 1),
                facts.valueText, record.declarationKind == SymbolTaxonomy::DeclarationKind::Localparam});
            continue;
        }
        Port port; port.name = record.name; port.bitWidth = facts.bitWidth;
        switch (record.collectorKind) {
        case SymbolTaxonomy::CollectorKind::PortInput: port.direction = QStringLiteral("input"); break;
        case SymbolTaxonomy::CollectorKind::PortOutput: port.direction = QStringLiteral("output"); break;
        case SymbolTaxonomy::CollectorKind::PortInout: port.direction = QStringLiteral("inout"); break;
        default: issues << QStringLiteral("Port %1 requires a manual direction/type contract.").arg(port.name); continue;
        }
        const auto& declared = record.presentation.defaultDeclaredTypeFacts;
        if (!declared.complete) {
            issues << QStringLiteral("Port %1 requires a manual aggregate or unresolved declaration.").arg(port.name); continue;
        }
        port.type = declared.externalDeclarationBaseText;
        port.namedType = !declared.typedefChain.isEmpty();
        if (port.type.isEmpty()) {
            issues << QStringLiteral("Port %1 uses a local type that is not visible in a new TB.").arg(port.name); continue;
        }
        for (const auto& dimension : declared.dimensions) if (dimension.emitInDeclaration) {
            QString left, right;
            for (const auto& node : declared.constants.evaluationNodes) if (node.evaluated) {
                if (node.id == dimension.leftEvaluationNodeId) left = node.resultValueText;
                if (node.id == dimension.rightEvaluationNodeId) right = node.resultValueText;
            }
            if (left.isEmpty() || right.isEmpty()) issues << QStringLiteral("Port %1 has an unresolved dimension.").arg(port.name);
            else port.type += QStringLiteral(" [%1:%2]").arg(left, right);
        }
        if (port.direction == QStringLiteral("input") && !facts.enumValues.isEmpty()) {
            for (const auto& value : facts.enumValues) if (value.available && !value.expressionText.isEmpty()) {
                port.initialExpression = value.expressionText; break;
            }
            if (port.initialExpression.isEmpty()) issues << QStringLiteral("No legal enum initializer is available for %1.").arg(port.name);
        }
        module.ports << port;
    }
    *error = issues.join(QLatin1Char('\n'));
    module.limitations = issues;
    return issues.isEmpty();
}
bool isTimingInput(const Port& port)
{
    return port.direction == QStringLiteral("input") && !port.namedType && port.bitWidth == 1;
}

TbOptions suggestedTbOptions(const Module& module)
{
    TbOptions o;
    o.name = QStringLiteral("tb_") + module.name;
    for (const auto& port : module.ports) {
        if (!isTimingInput(port)) continue;
        const auto name = port.name.toLower();
        if (o.clock.isEmpty() && QRegularExpression(QStringLiteral("(^|_)(clk|clock)(_|$)")).match(name).hasMatch()) o.clock = port.name;
        if (o.reset.isEmpty() && QRegularExpression(QStringLiteral("(^|_)(rst|reset|rstn|resetn)(_|$)")).match(name).hasMatch()) {
            o.reset = port.name;
            o.resetActiveLow = name.endsWith(QLatin1Char('n')) || name.endsWith(QStringLiteral("_b")) || name.contains(QStringLiteral("_n_"));
        }
    }
    return o;
}

QString generateTestbench(const Module& m, const TbOptions& o, qint64 durationNs, QString* error)
{
    error->clear();
    if (!m.limitations.isEmpty()) { *error = m.limitations.join(QLatin1Char('\n')); return {}; }
    if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*$")).match(o.name).hasMatch()
        || o.name == m.name) { *error = QStringLiteral("The TB name must be a valid identifier and differ from the DUT name."); return {}; }
    if (o.clockPeriodNs < 2 || durationNs < 1 || o.resetCycles < 1
        || (!o.clock.isEmpty() && o.clock == o.reset)) { *error = QStringLiteral("Invalid clock, reset, or duration settings."); return {}; }
    for (const auto& name : {o.clock, o.reset}) {
        if (name.isEmpty()) continue;
        bool valid = false;
        for (const auto& p : m.ports)
            if (p.name == name && isTimingInput(p)) valid = true;
        if (!valid) { *error = QStringLiteral("Clock and reset signals must be scalar input ports."); return {}; }
    }
    QString s = QStringLiteral("// Editable testbench. Add application-specific stimulus and checks.\n");
    s += QString(QChar(96)) + QStringLiteral("timescale 1ns/1ps\n\nmodule %1;\n").arg(o.name);
    for (const auto& declaration : m.imports) s += QStringLiteral("    %1\n").arg(declaration);
    if (!m.imports.isEmpty()) s += QLatin1Char('\n');
    for (const auto& p : m.parameters)
        s += QStringLiteral("    localparam %1%2 = %3;\n").arg(p.type.isEmpty() ? QString() : p.type + QLatin1Char(' '), p.name, p.value);
    if (!m.parameters.isEmpty()) s += QLatin1Char('\n');
    for (const auto& p : m.ports) {
        QString type = p.type;
        if (p.direction == QStringLiteral("inout")) type.prepend(QStringLiteral("wire "));
        s += QStringLiteral("    %1 %2;\n").arg(type, p.name);
    }
    s += QStringLiteral("\n    %1").arg(m.name);
    QStringList overrides;
    for (const auto& p : m.parameters) if (!p.local) overrides << QStringLiteral("        .%1(%1)").arg(p.name);
    if (!overrides.isEmpty()) s += QStringLiteral(" #(\n%1\n    )").arg(overrides.join(QStringLiteral(",\n")));
    QStringList connections;
    for (const auto& p : m.ports) connections << QStringLiteral("        .%1(%1)").arg(p.name);
    s += QStringLiteral(" dut (\n%1\n    );\n\n").arg(connections.join(QStringLiteral(",\n")));
    {
        s += QStringLiteral("    initial begin\n");
        for (const auto& p : m.ports) {
            if (p.direction != QStringLiteral("input")) continue;
            const QString value = p.name == o.reset ? (o.resetActiveLow ? QStringLiteral("1'b0") : QStringLiteral("1'b1"))
                : !p.initialExpression.isEmpty() ? p.initialExpression
                : p.namedType ? QStringLiteral("%1'('0)").arg(p.type) : QStringLiteral("'0");
            s += QStringLiteral("        %1 = %2;\n").arg(p.name, value);
        }
        s += QStringLiteral("    end\n");
        if (!o.clock.isEmpty()) s += QStringLiteral("\n    always #%1 %2 = ~%2;\n").arg(o.clockPeriodNs / 2.0, 0, 'f', 1).arg(o.clock);
        if (!o.reset.isEmpty()) {
            s += QStringLiteral("\n    initial begin\n        #%1;\n").arg(o.clockPeriodNs * o.resetCycles);
            if (!o.clock.isEmpty()) s += QStringLiteral("        @(negedge %1);\n").arg(o.clock);
            s += QStringLiteral("        %1 = %2;\n    end\n").arg(o.reset, o.resetActiveLow ? QStringLiteral("1'b1") : QStringLiteral("1'b0"));
        }
        s += QStringLiteral("\n    // Add application-specific stimulus and checks here.\n");
    }
    for (const auto& p : m.ports)
        if (p.direction == QStringLiteral("inout")) s += QStringLiteral("    // TB releases %1; replace with an explicit drive-enable when needed.\n    assign %1 = 'z;\n").arg(p.name);
    s += QStringLiteral("    longint unsigned __simdock_duration_ns;\n    initial begin\n        if (!$value$plusargs(\"SIMDOCK_DURATION_NS=%d\", __simdock_duration_ns))\n            __simdock_duration_ns = %1;\n        #(__simdock_duration_ns);\n").arg(durationNs);
    s += QStringLiteral("        $display(\"SimDock demo completed; no functional assertions.\");\n");
    s += QStringLiteral("        $finish;\n    end\nendmodule\n");
    return s;
}
}
