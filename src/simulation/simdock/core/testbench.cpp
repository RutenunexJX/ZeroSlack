#include "testbench.h"
#include <QRegularExpression>
#include "semanticindex.h"
#include "editorfileidentity.h"
#include "effectivevalueservice.h"

namespace simdock {
bool completeTestbenchTypes(Module& module, SemanticIndex& index, const QString& file,
                           const QString& content, QString* error)
{
    error->clear();
    if (index.getCachedFileContent(file) != content) {
        *error = QStringLiteral("Wait for semantic analysis of the current DUT document before creating its TB."); return false;
    }
    const auto records = index.getSymbolRecords(file);
    EffectiveValueService localValues(&index);
    auto* values = &index == SemanticIndex::getInstance() ? EffectiveValueService::getInstance() : &localValues;
    QStringList issues;
    for (auto& port : module.ports) {
        QList<SemanticSymbolRecord> matches;
        for (const auto& record : records)
            if (record.declarationKind == SymbolTaxonomy::DeclarationKind::Port
                && record.owner.name == module.name && record.name == port.name) matches << record;
        if (matches.size() != 1) { issues << QStringLiteral("Port %1 has no unambiguous Slang type.").arg(port.name); continue; }
        const auto& facts = matches.first().presentation.defaultInfo;
        EffectiveValueQuery query; query.symbol = matches.first(); query.documentText = content;
        if (!values->resolve(query).current()) {
            issues << QStringLiteral("Port %1 has pending or invalid semantic facts.").arg(port.name); continue;
        }
        if (!facts.available || !facts.fixedSize || !facts.integral || facts.unpackedArray || facts.interfaceType) {
            issues << QStringLiteral("Port %1 needs a manual declaration: %2").arg(port.name, facts.resolvedTypeText); continue;
        }
        if (port.direction != QStringLiteral("input")) continue;
        if (!facts.enumValues.isEmpty()) {
            for (const auto& value : facts.enumValues) if (value.available && !value.expressionText.isEmpty()) {
                port.initialExpression = value.expressionText; break;
            }
            if (port.initialExpression.isEmpty()) issues << QStringLiteral("No legal enum initializer is available for %1.").arg(port.name);
        } else if (facts.structuredLeavesAvailable) {
            issues << QStringLiteral("Port %1 is an aggregate; specify its field initializers manually.").arg(port.name);
        }
    }
    *error = issues.join(QLatin1Char('\n'));
    return issues.isEmpty();
}
bool isTimingInput(const Port& port)
{
    return port.direction == QStringLiteral("input") && !port.namedType
        && !port.type.contains(QLatin1Char('['));
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
        if (p.direction == QStringLiteral("inout")) type.replace(QStringLiteral("logic"), QStringLiteral("wire"));
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
