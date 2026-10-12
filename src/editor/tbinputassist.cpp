#include "tbinputassist.h"
#include "effectivevalueservice.h"
#include "tsdocument.h"
#include "triviapositionmap.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
bool is(TSNode n, const char* type) { return !ts_node_is_null(n) && std::strcmp(ts_node_type(n), type) == 0; }
int start(TSNode n) { return int(ts_node_start_byte(n) / 2); }
int end(TSNode n) { return int(ts_node_end_byte(n) / 2); }
QString text(TSNode n, const QString& source) { return ts_node_is_null(n) ? QString() : source.mid(start(n), end(n) - start(n)); }
QList<TSNode> children(TSNode n) {
    QList<TSNode> result;
    for (uint32_t i = 0; i < ts_node_named_child_count(n); ++i) result << ts_node_named_child(n, i);
    return result;
}
TSNode field(TSNode n, const char* name) { return ts_node_child_by_field_name(n, name, uint32_t(std::strlen(name))); }
QList<TSNode> descendants(TSNode n, const char* type) {
    if (is(n, type)) return {n};
    QList<TSNode> result;
    for (auto c : children(n)) result += descendants(c, type);
    return result;
}
bool identifier(const QString& value) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_$]*$"));
    return pattern.match(value).hasMatch();
}
QString moduleName(TSNode module, const QString& source) {
    auto name = field(module, "name");
    if (ts_node_is_null(name)) for (auto c : children(module)) {
        name = field(c, "name");
        if (!ts_node_is_null(name)) break;
    }
    return text(name, source);
}
constexpr auto marker = "// @zeroslack-engineering ";
QString number(long double v) { return QString::number(double(v), 'g', 17); }
bool finiteNumber(const QString& s, long double* value) {
    static const QRegularExpression pattern(QStringLiteral("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
    if (!pattern.match(s.trimmed()).hasMatch()) return false;
    bool ok = false;
    *value = s.toDouble(&ok);
    return ok && std::isfinite(*value);
}
QString bounds(const TbInputAssist& input, qint64* low, qint64* high) {
    if (!input.current) return QStringLiteral("Semantic facts are pending for this document.");
    if (!input.enumValues.isEmpty()) return QStringLiteral("Enum inputs use their declared members, not numeric rules.");
    if (input.bitWidth == 0 || input.bitWidth > 53)
        return QStringLiteral("Engineering conversion supports fixed integral inputs of 1 to 53 bits.");
    *low = input.signedIntegral ? -(qint64(1) << (input.bitWidth - 1)) : 0;
    *high = input.signedIntegral ? (qint64(1) << (input.bitWidth - 1)) - 1 : (qint64(1) << input.bitWidth) - 1;
    return {};
}
QString literalValue(const TbInputAssist& input, qint64* value) {
    qint64 low, high;
    auto issue = bounds(input, &low, &high);
    if (!issue.isEmpty()) return issue;
    const QString expr = input.expression.trimmed();
    if (expr == QStringLiteral("'0") || expr == QStringLiteral("'1")) {
        *value = expr.endsWith(QLatin1Char('0')) ? 0 : input.signedIntegral ? -1 : high;
        return {};
    }
    static const QRegularExpression literal(QStringLiteral("^[+-]?(?:[0-9][0-9_]*|([0-9][0-9_]*)?'[sS]?[bBoOdDhH][0-9a-fA-F_xXzZ?]+)$"));
    if (!literal.match(expr).hasMatch()) return QStringLiteral("Only a single integer literal can be edited; keep complex expressions in source.");
    if (expr.contains(QRegularExpression(QStringLiteral("[xXzZ?]")))) return QStringLiteral("X/Z values have no engineering value.");
    // Reject a truncated source literal instead of displaying its wrapped value.
    const auto sized = QRegularExpression(QStringLiteral("^[+-]?([0-9_]+)'[sS]?([bBoOdDhH])([0-9a-fA-F_]+)$")).match(expr);
    if (sized.hasMatch()) {
        bool ok;
        const int width = QString(sized.captured(1)).remove(QLatin1Char('_')).toInt(&ok);
        if (!ok || width < 1 || width > 53) return QStringLiteral("Literal widths above 53 bits are not supported by this conversion.");
        const auto base = sized.captured(2).toLower();
        const auto magnitude = QString(sized.captured(3)).remove(QLatin1Char('_')).toULongLong(&ok,
            base == QStringLiteral("h") ? 16 : base == QStringLiteral("b") ? 2 : base == QStringLiteral("o") ? 8 : 10);
        if (!ok || magnitude >= (quint64(1) << width)) return QStringLiteral("The source literal overflows its declared width.");
    }
    const auto evaluated = EffectiveValueService::evaluateLiteral(expr);
    bool ok = false;
    for (const auto& radix : evaluated.radixRepresentations) if (radix.startsWith(QStringLiteral("decimal: ")))
        *value = QString(radix.mid(9)).remove(QLatin1Char('_')).toLongLong(&ok);
    if (!evaluated.available || !ok) return QStringLiteral("The source literal has no known integral value.");
    if (*value < low || *value > high) return QStringLiteral("The source value is outside the input's signedness or bit width.");
    return {};
}
QString renderLiteral(const TbInputAssist& input, qint64 value) {
    const auto expr = input.expression.trimmed();
    const auto match = QRegularExpression(QStringLiteral("^[+-]?([0-9_]*'([sS]?)([bBoOdDhH]))([0-9a-fA-F_]+)$")).match(expr);
    if (!match.hasMatch()) {
        if (expr == QStringLiteral("'0") && value == 0) return expr;
        if (expr == QStringLiteral("'1")) {
            qint64 low, high; bounds(input, &low, &high);
            if (value == (input.signedIntegral ? -1 : high)) return expr;
        }
        if (!expr.contains(QLatin1Char('\'')) && value >= -2147483648LL && value <= 2147483647LL)
            return QString::number(value);
        return QStringLiteral("%1%2'%3d%4").arg(value < 0 ? QStringLiteral("-") : QString())
            .arg(input.bitWidth).arg(input.signedIntegral ? QStringLiteral("s") : QString()).arg(std::abs(value));
    }
    auto prefix = match.captured(1);
    if (value < 0 && match.captured(2).isEmpty()) prefix.insert(prefix.indexOf(QLatin1Char('\'')) + 1, QLatin1Char('s'));
    const auto letter = match.captured(3).toLower();
    const int base = letter == QStringLiteral("h") ? 16 : letter == QStringLiteral("b") ? 2 : letter == QStringLiteral("o") ? 8 : 10;
    QString digits = QString::number(std::abs(value), base);
    if (match.captured(3).at(0).isUpper()) digits = digits.toUpper();
    const auto oldDigits = match.captured(4);
    if (base != 10) digits = digits.rightJustified(QString(oldDigits).remove(QLatin1Char('_')).size(), QLatin1Char('0'));
    // Retain the original right-to-left digit grouping when its size is uniform.
    if (oldDigits.contains(QLatin1Char('_'))) {
        const int group = oldDigits.size() - oldDigits.lastIndexOf(QLatin1Char('_')) - 1;
        if (group > 0) for (int p = digits.size() - group; p > 0; p -= group) digits.insert(p, QLatin1Char('_'));
    }
    return (value < 0 ? QStringLiteral("-") : QString()) + prefix + digits;
}
long double forward(const TbEngineeringRule& r, long double n, long double a, long double b) {
    return r.kind == QStringLiteral("linear") ? a * n + b : a / (n + b);
}
void readRule(const QString& source, TbInputAssist* input) {
    const int lineStart = source.lastIndexOf(QLatin1Char('\n'), input->statementStart - 1) + 1;
    if (lineStart <= 0) return;
    const int previousStart = source.lastIndexOf(QLatin1Char('\n'), lineStart - 2) + 1;
    const QString previous = source.mid(previousStart, lineStart - previousStart).trimmed();
    if (!previous.startsWith(QString::fromLatin1(marker))) return;
    input->ruleStart = previousStart;
    input->ruleLength = lineStart - previousStart;
    input->rule.bound = true;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(previous.mid(int(std::strlen(marker))).toUtf8(), &error);
    const auto object = doc.object();
    if (error.error != QJsonParseError::NoError || object.value(QStringLiteral("v")).toInt() != 1
        || object.value(QStringLiteral("signal")).toString() != input->signal
        || object.value(QStringLiteral("scope")).toString() != input->module) {
        input->rule.error = QStringLiteral("Invalid rule metadata or a different signal/scope; rebind explicitly."); return;
    }
    auto& r = input->rule;
    r.kind = object.value(QStringLiteral("kind")).toString();
    r.scale = object.value(QStringLiteral("a")).toString();
    r.offset = object.value(QStringLiteral("b")).toString();
    r.unit = object.value(QStringLiteral("unit")).toString();
    r.source = object.value(QStringLiteral("source")).toString();
    r.error = TbInputAssistService::validateRule(r);
}
QString typeShape(QString source, const QList<TbInputAssist>& inputs) {
    struct Span { int start, length; QString replacement; };
    QList<Span> spans;
    for (const auto& input : inputs) {
        spans << Span{input.start, input.length, QStringLiteral("__zeroslack_initializer")};
        if (input.ruleStart >= 0) spans << Span{input.ruleStart, input.ruleLength, {}};
    }
    std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) { return a.start > b.start; });
    for (const auto& span : spans) source.replace(span.start, span.length, span.replacement);
    return source;
}
void describe(TbInputAssist& item) {
    if (!item.enumValues.isEmpty()) {
        bool known = false, writable = false;
        QString underlying, failureReason;
        const QString expression = item.expression.trimmed();
        for (const auto& member : item.enumValues) {
            writable |= member.available && !member.expressionText.isEmpty();
            if (failureReason.isEmpty()) failureReason = member.expressionFailureReason;
            if (member.available && ((!member.expressionText.isEmpty() && expression == member.expressionText)
                || (member.unqualifiedReferenceAvailable && expression == member.name))) {
                known = true; underlying = member.displayValueText;
            }
        }
        if (!writable && item.status.isEmpty()) item.status = failureReason.isEmpty()
            ? QStringLiteral("No verified enum member is available for source editing.") : failureReason;
        if (!known && item.status.isEmpty()) item.status = QStringLiteral(
            "This initializer expression is not verified by the helper; source is preserved until you select a member.");
        item.annotation = item.status.isEmpty() ? QStringLiteral("enum · %1 ▾").arg(underlying) : QStringLiteral("Enum unavailable…");
    } else if (item.rule.bound) {
        QString issue;
        const auto value = TbInputAssistService::engineeringValue(item, &issue);
        item.annotation = issue.isEmpty() ? value + QLatin1Char(' ') + item.rule.unit + QStringLiteral(" ▾") : QStringLiteral("Value unavailable…");
    } else item.annotation = item.status.isEmpty() ? QStringLiteral("Bind rule…") : QStringLiteral("Input facts pending…");
}
}

QString TbInputAssistService::validateRule(const TbEngineeringRule& r) {
    long double a, b;
    if (r.kind != QStringLiteral("linear") && r.kind != QStringLiteral("reciprocal"))
        return QStringLiteral("Supported rules: y = a*N + b and y = a/(N+b).");
    if (!finiteNumber(r.scale, &a) || !finiteNumber(r.offset, &b)) return QStringLiteral("a and b must be finite numeric constants.");
    if (a == 0) return QStringLiteral("a must be nonzero so the rule has a unique inverse.");
    if (r.unit.trimmed().isEmpty() || r.source.trimmed().isEmpty()) return QStringLiteral("Specify the unit and rule source explicitly.");
    if (r.unit.size() > 40 || r.source.size() > 200) return QStringLiteral("Unit or source description is too long.");
    return {};
}
QString TbInputAssistService::ruleComment(const TbInputAssist& input, const TbEngineeringRule& rule) {
    const QJsonObject data{{QStringLiteral("v"), 1}, {QStringLiteral("signal"), input.signal}, {QStringLiteral("scope"), input.module},
        {QStringLiteral("kind"), rule.kind}, {QStringLiteral("a"), rule.scale}, {QStringLiteral("b"), rule.offset},
        {QStringLiteral("unit"), rule.unit}, {QStringLiteral("source"), rule.source}};
    return QString::fromLatin1(marker) + QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
}
QString TbInputAssistService::engineeringValue(const TbInputAssist& input, QString* error) {
    error->clear();
    if (!input.rule.bound) { *error = QStringLiteral("No engineering rule is bound."); return {}; }
    *error = input.rule.error.isEmpty() ? validateRule(input.rule) : input.rule.error;
    if (!error->isEmpty()) return {};
    qint64 n;
    *error = literalValue(input, &n);
    if (!error->isEmpty()) return {};
    long double a, b; finiteNumber(input.rule.scale, &a); finiteNumber(input.rule.offset, &b);
    if (input.rule.kind == QStringLiteral("reciprocal") && n + b == 0) { *error = QStringLiteral("The source value causes division by zero."); return {}; }
    const auto actual = forward(input.rule, n, a, b);
    if (!std::isfinite(double(actual))) { *error = QStringLiteral("The engineering value is outside the numeric range."); return {}; }
    return number(actual);
}
TbEngineeringPreview TbInputAssistService::preview(const TbInputAssist& input, const QString& target) {
    TbEngineeringPreview result;
    engineeringValue(input, &result.error);
    if (!result.error.isEmpty()) return result;
    long double y, a, b;
    if (!finiteNumber(target, &y)) { result.error = QStringLiteral("Enter a finite numeric target."); return result; }
    finiteNumber(input.rule.scale, &a); finiteNumber(input.rule.offset, &b);
    if (input.rule.kind == QStringLiteral("reciprocal") && y == 0) { result.error = QStringLiteral("Zero has no finite inverse for this rule."); return result; }
    const auto exact = input.rule.kind == QStringLiteral("linear") ? (y - b) / a : a / y - b;
    qint64 low, high; result.error = bounds(input, &low, &high);
    if (!std::isfinite(exact) || exact < low || exact > high) { result.error = QStringLiteral("The requested value is outside the input's representable configuration range."); return result; }
    const auto n = qint64(std::round(exact));
    if (input.rule.kind == QStringLiteral("reciprocal") && n + b == 0) { result.error = QStringLiteral("The quantized configuration causes division by zero."); return result; }
    result.expression = renderLiteral(input, n);
    TbInputAssist proposal = input; proposal.expression = result.expression;
    qint64 checked;
    result.error = literalValue(proposal, &checked);
    if (result.error.isEmpty() && checked != n) result.error = QStringLiteral("The existing literal format cannot represent this value without truncation.");
    if (!result.error.isEmpty()) return result;
    const auto actual = forward(input.rule, n, a, b);
    if (!std::isfinite(double(actual))) { result.error = QStringLiteral("The engineering result is outside the numeric range."); return result; }
    result.actual = number(actual);
    result.quantized = std::abs(actual - y) > 1e-12L * std::max(1.0L, std::abs(y));
    result.difference = number(actual - y) + QLatin1Char(' ') + input.rule.unit;
    if (y != 0) result.difference += QStringLiteral(" (%1%)").arg(number((actual - y) / std::abs(y) * 100));
    result.valid = true;
    return result;
}

QList<TbInputAssist> TbInputAssistService::analyze(const QString& file, const QString& source,
    SemanticIndex& index, const std::function<bool()>& cancelled,
    const EffectiveValueService* values, quint64 documentRevision) {
    QList<TbInputAssist> result;
    if (source.size() > 4 * 1024 * 1024 || (cancelled && cancelled())) return result;
    TSDocument syntax;
    if (!syntax.setText(source, cancelled)) return result;
    const bool current = index.getCachedFileContent(file) == source;
    const auto records = index.getSymbolRecords(file);
    for (const auto module : descendants(syntax.rootNode(), "module_declaration")) {
        if (cancelled && cancelled()) return {};
        const QString scope = moduleName(module, source);
        QSet<QString> inputs, driven;
        for (auto instance : descendants(module, "module_instantiation")) {
            const QString type = text(field(instance, "instance_type"), source);
            QHash<QString, SymbolTaxonomy::CollectorKind> ports;
            for (const auto& port : index.getSymbolRecordsByOwner(type)) {
                if (port.declarationKind != SymbolTaxonomy::DeclarationKind::Port) continue;
                // Duplicate declarations are ambiguous, even when their names match.
                if (ports.contains(port.name)) ports[port.name] = SymbolTaxonomy::CollectorKind::User;
                else ports.insert(port.name, port.collectorKind);
            }
            for (auto connection : descendants(instance, "named_port_connection")) {
                const auto formal = text(field(connection, "port_name"), source);
                const auto actual = text(field(connection, "connection"), source).trimmed();
                if (!identifier(actual) || !ports.contains(formal)) continue;
                if (ports[formal] == SymbolTaxonomy::CollectorKind::PortInput) inputs.insert(actual);
                else driven.insert(actual);
            }
        }
        inputs.subtract(driven);
        if (inputs.isEmpty()) continue;
        QList<TbInputAssist> candidates;
        auto capture = [&](TSNode node, bool declaration) {
            if (ts_node_has_error(node)) return;
            TSNode lhs{}, rhs{};
            bool afterEquals = false;
            for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
                const auto c = ts_node_child(node, i);
                if (is(c, "=") || (is(c, "assignment_operator") && text(c, source) == QStringLiteral("="))) { afterEquals = true; continue; }
                if (!ts_node_is_named(c) || is(c, "comment")) continue;
                if (!afterEquals && ts_node_is_null(lhs)) lhs = c;
                if (afterEquals) rhs = c;
            }
            const auto name = text(lhs, source).trimmed();
            if (!inputs.contains(name) || ts_node_is_null(rhs)) return;
            TbInputAssist item;
            item.signal = name; item.module = scope;
            item.start = start(rhs); item.length = end(rhs) - item.start;
            item.line = int(ts_node_start_point(rhs).row);
            item.expression = text(rhs, source);
            item.statementStart = start(node);
            if (declaration) {
                TSNode parent = ts_node_parent(node);
                while (!ts_node_is_null(parent) && !is(parent, "data_declaration")) parent = ts_node_parent(parent);
                if (!ts_node_is_null(parent)) item.statementStart = start(parent);
            }
            QList<SemanticSymbolRecord> matches;
            for (const auto& record : records) if (record.name == name && record.owner.name == scope
                && record.usageRole == SymbolTaxonomy::SymbolUsageRole::Declaration) matches << record;
            if (matches.size() == 1) {
                const auto& facts = matches.first().presentation.defaultInfo;
                item.current = current && facts.available && facts.fixedSize && facts.integral && !facts.unpackedArray;
                if (values) {
                    EffectiveValueQuery query;
                    query.symbol = matches.first(); query.documentText = source; query.documentRevision = documentRevision;
                    const auto resolved = values->resolve(query);
                    item.current = item.current && resolved.current();
                    if (!resolved.current()) item.status = resolved.failureReason;
                }
                item.bitWidth = facts.bitWidth; item.signedIntegral = facts.signedIntegral;
                item.enumValues = facts.enumValues;
            }
            readRule(source, &item);
            if (!item.current && item.status.isEmpty()) item.status = current ? QStringLiteral("Input type is unavailable or unsupported.") : QStringLiteral("Semantic facts are pending for this document.");
            candidates << item;
        };
        // Declaration initializers at module scope, not locals inside procedures.
        for (auto declaration : descendants(module, "variable_decl_assignment")) {
            bool local = false;
            for (auto p = ts_node_parent(declaration); !ts_node_is_null(p) && !is(p, "module_declaration"); p = ts_node_parent(p))
                if (is(p, "initial_construct") || is(p, "function_declaration") || is(p, "task_declaration") || is(p, "always_construct")) local = true;
            if (!local) capture(declaration, true);
        }
        // Only straight-line, time-zero assignments. Stop at timing, branches,
        // loops and calls; later stimulus must not be mistaken for initialization.
        for (auto initial : descendants(module, "initial_construct")) {
            bool stopped = false;
            std::function<void(TSNode)> visit = [&](TSNode n) {
                if (stopped) return;
                if (is(n, "operator_assignment") || is(n, "variable_assignment")) { capture(n, false); return; }
                if (is(n, "comment") || is(n, "one_line_comment") || is(n, "block_comment")) return;
                if (is(n, "initial_construct") || is(n, "statement_or_null") || is(n, "statement")
                    || is(n, "statement_item") || is(n, "seq_block") || is(n, "blocking_assignment")) {
                    for (auto c : children(n)) visit(c);
                } else stopped = true;
            };
            visit(initial);
        }
        QHash<QString, int> counts;
        for (const auto& item : candidates) ++counts[item.signal];
        for (auto& item : candidates) {
            if (counts[item.signal] != 1) {
                item.current = false;
                item.status = QStringLiteral("Multiple time-zero initializers target this signal.");
            }
            result << item;
        }
    }
    // A source-only initializer change does not change its declared type or
    // DUT connection. Prove that restriction using the existing token-map
    // contract before reusing type facts; never evaluate the old value.
    const auto baseline = index.getCachedFileContent(file);
    if ((!current || values) && !baseline.isEmpty() && !result.isEmpty()
        && std::any_of(result.cbegin(), result.cend(), [](const auto& i) { return !i.current; })) {
        const auto original = analyze(file, baseline, index, cancelled);
        bool sameInputs = original.size() == result.size();
        for (int i = 0; sameInputs && i < original.size(); ++i)
            sameInputs = original[i].signal == result[i].signal && original[i].module == result[i].module;
        if (sameInputs && TriviaPositionMap(typeShape(baseline, original), typeShape(source, result),
                                           TriviaPositionLeafPolicy::IncludeComments).isCompatible()) {
            for (int i = 0; i < result.size(); ++i) {
                result[i].current = original[i].current;
                result[i].bitWidth = original[i].bitWidth; result[i].signedIntegral = original[i].signedIntegral;
                result[i].enumValues = original[i].enumValues; result[i].status = original[i].current ? QString() : original[i].status;
            }
        }
    }
    for (auto& item : result) describe(item);
    return result;
}
