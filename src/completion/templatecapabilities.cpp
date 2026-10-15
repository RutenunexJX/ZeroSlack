#include "templatecapabilities.h"
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace {
bool identifier(const QString& value) {
    return QRegularExpression("^[A-Za-z_][A-Za-z0-9_$]*$").match(value).hasMatch();
}
bool numeric(const QVariant& value) {
    return value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong ||
           value.typeId() == QMetaType::Double;
}
struct ExpressionRenderer {
    QVariantMap expressions, bindings;
    QSet<QString> visiting;
    QString error;
    int nodes = 0;
    QString named(const QString& name) {
        if (!expressions.contains(name) || visiting.contains(name)) {
            error = "Unknown or cyclic derived expression: " + name; return {};
        }
        visiting.insert(name); const auto result = expression(expressions.value(name), 0); visiting.remove(name); return result;
    }
    QString expression(const QVariant& value, int depth) {
        if (++nodes > 256 || depth > 32) { error = "Template expression exceeds the supported complexity."; return {}; }
        if (value.typeId() != QMetaType::QVariantMap) { error = "An expression node must be an object."; return {}; }
        const auto node = value.toMap();
        if (node.size() == 1 && node.contains("number")) {
            const auto text = node.value("number").toString(); bool ok = false;
            const auto number = text.toDouble(&ok);
            if (!ok || !std::isfinite(number) || !QRegularExpression("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$").match(text).hasMatch())
                error = "Derived expression requires a finite decimal literal.";
            return text;
        }
        if (node.size() == 1 && node.contains("variable")) {
            const auto name = bindings.value(node.value("variable").toString()).toString();
            if (!identifier(name)) error = "Unknown or invalid expression variable binding.";
            return name;
        }
        if (node.size() == 1 && node.contains("expression")) return named(node.value("expression").toString());
        if (node.size() == 2 && node.contains("unit") && node.contains("value")) {
            const auto unit = node.value("unit").toString();
            if (!QStringList{"s","ms","us","ns","ps","fs"}.contains(unit)) error = "Unsupported expression time unit.";
            return "(" + expression(node.value("value"), depth + 1) + " * 1" + unit + ")";
        }
        if (node.size() != 2 || !node.contains("op") || !node.contains("args")) {
            error = "Unknown template expression node."; return {};
        }
        if (node.value("args").typeId() != QMetaType::QVariantList) { error = "Expression operands must be an array."; return {}; }
        const auto op = node.value("op").toString(); const auto args = node.value("args").toList();
        const int count = op == "if" ? 3 : op == "!" ? 1 : 2;
        if (!QStringList{"+","-","*","/",">",">=","<","<=","==","!=","&&","||","!","if"}.contains(op) || args.size() != count) {
            error = "Unsupported expression operator or operand count."; return {};
        }
        QStringList parts; for (const auto& arg : args) parts << expression(arg, depth + 1);
        if (op == "if") return "(" + parts[0] + " ? " + parts[1] + " : " + parts[2] + ")";
        if (op == "!") return "(!" + parts[0] + ")";
        return "(" + parts[0] + " " + op + " " + parts[1] + ")";
    }
};
}
bool TemplateCapabilities::root(const QVariantMap& config) {
    return config.value("capability") == "moduleRoot" || (!config.contains("capability") && config.value("role") == "tbRoot");
}
bool TemplateCapabilities::module(const QVariantMap& config) {
    return config.value("capability") == "moduleInstance" || (!config.contains("capability") && config.value("kind") == "dut");
}
QString TemplateCapabilities::region(const QVariantMap& config, const QString& content) {
    return config.value("regions").toMap().value(content).toString();
}
QVariantMap TemplateCapabilities::operation(const QVariantMap& config, const QString& op) {
    for (const auto& value : config.value("operations").toList()) {
        const auto item = value.toMap(); if (item.value("op") == op) return item;
    }
    return {};
}
QString TemplateCapabilities::expandName(QString pattern, const QHash<QString, QString>& values) {
    for (auto it = values.cbegin(); it != values.cend(); ++it) pattern.replace("${" + it.key() + "}", it.value());
    return pattern;
}
QString TemplateCapabilities::activationQuery(const QVariantMap& config) {
    const auto activation = config.value("activation").toMap();
    if (activation.value("mode") == "exactLockedFileGap") return activation.value("query").toString();
    return config.value("directQuery").toString();
}
QString TemplateCapabilities::validate(const QVariantMap& config) {
    if (!config.contains("capability")) return {};
    if (!numeric(config.value("capabilityVersion")) || config.value("capabilityVersion").toDouble() != 1.0)
        return "Unsupported template capability version.";
    const auto capability = config.value("capability").toString();
    if (!QStringList{"moduleRoot","moduleInstance","moduleProcess"}.contains(capability)) return "Unknown template capability.";
    if (capability == "moduleInstance") {
        if (config.value("kind") != "operations") return "Module instantiation requires the operations executor.";
        const auto inputs = config.value("semanticInputs").toMap();
        if (inputs.value("moduleSource") != "compilation" || inputs.value("parameters") != "selectedModule" ||
            inputs.value("ports") != "elaboratedPorts") return "Unsupported semantic input source.";
        const auto operations = config.value("operations").toList();
        if (operations.size() != 2 || operations[0].toMap().value("op") != "declareSignals" ||
            operations[1].toMap().value("op") != "instantiateModule") return "Expected declareSignals followed by instantiateModule.";
        const auto declaration = operations[0].toMap(), instance = operations[1].toMap();
        if (!identifier(declaration.value("target").toString()) || !identifier(instance.value("target").toString()) ||
            declaration.value("target") == instance.value("target")) return "Operations require distinct named target regions.";
        if (!QStringList{"explicit", "never"}.contains(declaration.value("reuse").toString()) || instance.value("bindings") != "named")
            return "Only explicit signal reuse and named port bindings are supported.";
        auto pattern = declaration.value("namePattern").toString();
        pattern = expandName(pattern, {{"instance","unit"},{"port","value"},{"module","module"}});
        if (!identifier(pattern)) return "Invalid signal naming pattern.";
    } else if (config.value("kind") != "snippet") return "This capability requires a snippet body.";
    if (capability == "moduleRoot") {
        const auto regions = config.value("regions").toMap(); QSet<QString> names;
        for (const auto& content : {"declarations", "instances", "behaviors"}) {
            const auto name = regions.value(content).toString();
            if (!identifier(name) || names.contains(name)) return "Root regions require three distinct slot names.";
            names.insert(name);
        }
        if (!identifier(config.value("nameSlot").toString())) return "Root module name requires a named input slot.";
    }
    if (config.contains("activation")) {
        const auto activation = config.value("activation").toMap();
        const auto query = activation.value("query").toString();
        if (!root(config) || activation.value("mode") != "exactLockedFileGap" || !identifier(query) || query.size() < 2)
            return "Automatic activation is restricted to an exact root-template query in a locked file gap.";
    }
    const auto render = config.value("render").toMap();
    if (config.contains("render") && config.value("render").typeId() != QMetaType::QVariantMap)
        return "Template render must be an object.";
    for (const auto& key : {"expressions", "bindings"})
        if (render.contains(key) && render.value(key).typeId() != QMetaType::QVariantMap)
            return "Template render expressions and bindings must be objects.";
    ExpressionRenderer renderer{render.value("expressions").toMap(), render.value("bindings").toMap()};
    for (auto it = renderer.expressions.cbegin(); it != renderer.expressions.cend(); ++it) {
        renderer.nodes = 0; renderer.named(it.key()); if (!renderer.error.isEmpty()) return renderer.error;
    }
    return {};
}
TemplateBodyPlan TemplateCapabilities::render(const QVariantMap& config, const QString& body, const CodeTemplateSlotList& placeholders) {
    TemplateBodyPlan result{body, placeholders, validate(config)}; if (!result.error.isEmpty()) return result;
    if (!config.contains("capability")) return result;
    for (const auto& slot : placeholders) if (slot.input.value("role") == "Clock" || slot.input.value("role") == "Reset") {
        if (slot.input.value("kind") != "Signal" || slot.input.value("scope") != "visible" ||
            !slot.input.value("closedChoice").toBool() || !slot.input.value("moduleVariable").toBool() ||
            !slot.input.value("variableOnly").toBool() || slot.input.value("access") != "write" ||
            slot.input.value("minWidth").toInt() != 1 || slot.input.value("maxWidth").toInt() != 1 ||
            slot.input.value("unpackedRank", -1).toInt() != 0) {
            result.error = "Clock/Reset capability inputs require closed, writable, scalar module-variable candidates."; return result;
        }
    }
    const auto render = config.value("render").toMap();
    ExpressionRenderer renderer{render.value("expressions").toMap(), render.value("bindings").toMap()};
    auto matches = QRegularExpression("\\{\\{=([A-Za-z_][A-Za-z0-9_]*)\\}\\}").globalMatch(body);
    QList<QRegularExpressionMatch> tokens; while (matches.hasNext()) tokens.prepend(matches.next());
    for (const auto& token : tokens) {
        renderer.nodes = 0; const auto expression = renderer.named(token.captured(1));
        if (!renderer.error.isEmpty()) { result.error = renderer.error; return result; }
        const int start = token.capturedStart(), length = token.capturedLength(), delta = expression.size() - length;
        for (auto& slot : result.placeholders) {
            if (slot.start < start + length && slot.start + slot.length > start) {
                result.error = "A derived expression must remain outside editable placeholders."; return result;
            }
            if (slot.start >= start + length) slot.start += delta;
        }
        result.text.replace(start, length, expression);
    }
    if (result.text.contains("{{=")) result.error = "Malformed derived expression token.";
    return result;
}
