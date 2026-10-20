#include "templateinputservice.h"
#include "editorfileidentity.h"
#include "effectivevalueservice.h"
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace
{
using Kind = SymbolTaxonomy::DeclarationKind;
QStringList stringList(const QVariant &value)
{
    QStringList result;
    for (const auto &entry : value.toList())
        result << entry.toString();
    if (result.isEmpty())
        result = value.toStringList();
    return result;
}
bool inRange(const QVariantMap &input, const SemanticElaboratedSymbolInfo &info)
{
    if (!info.available)
        return false;
    if (input.contains("minWidth") && info.bitWidth < input.value("minWidth").toULongLong())
        return false;
    if (input.contains("maxWidth") && info.bitWidth > input.value("maxWidth").toULongLong())
        return false;
    if (input.contains("packedRank") && info.packedRank != input.value("packedRank").toInt())
        return false;
    if (input.contains("unpackedRank") && info.unpackedRank != input.value("unpackedRank").toInt())
        return false;
    if (input.value("integral").toBool() && !info.integral)
        return false;
    if (input.value("access").toString().contains("write") && !info.writable)
        return false;
    if (input.value("access", "read").toString().contains("read") && !info.readable)
        return false;
    const auto direction = input.value("direction").toString();
    return direction.isEmpty() || direction == "any" || direction == info.portDirection;
}
QHash<QString, QString> constants(const TemplateInputContext &context)
{
    QHash<QString, QString> result;
    if (!context.snapshot)
        return result;
    SemanticIndex local;
    local.setSnapshot(context.snapshot);
    auto records =
        local.getCommandCompletionSymbolRecords(context.location, CompletionCommandKind::Parameter);
    records += local.getCommandCompletionSymbolRecords(context.location, CompletionCommandKind::Localparam);
    for (const auto &record : records)
    {
        const auto &info = record.presentation.defaultInfo;
        if (info.available && !info.valueText.isEmpty())
            result.insert(record.name, info.valueText);
    }
    return result;
}
} // namespace
QString TemplateInputService::roleKey(const SemanticSymbolRecord &record, const QString &objectPath)
{
    return EditorFileIdentity::lookupKey(record.location.fileName) + QLatin1Char('|') + objectPath;
}
QString TemplateInputService::specificationError(const QVariantMap &input)
{
    if (input.isEmpty())
        return {};
    const QStringList kinds{"Instance", "Port", "Internal", "Signal", "Parameter", "Choice", "Value", "Code"};
    if (!kinds.contains(input.value("kind").toString()))
        return QStringLiteral("Choose Instance, Port, Internal, Signal, Parameter, Choice, Value or Code.");
    if (input.value("kind").toString() == "Code" && !QStringList{"statements", "declarations", "instances", "behaviors"}.contains(input.value("content", "statements").toString()))
        return QStringLiteral("Unknown Code container content.");
    const auto role = input.value("role").toString();
    if (!role.isEmpty() && role != "Clock" && role != "Reset")
        return QStringLiteral("Role must be Clock or Reset; use explicit role assignments.");
    const auto scope = input.value("scope", "visible").toString();
    if (scope != "visible" && scope != "instance" && scope != "hierarchy" && scope != "package")
        return QStringLiteral("Unknown input scope.");
    for (const auto *name : {"minWidth", "maxWidth", "packedRank", "unpackedRank"})
    {
        if (!input.contains(name))
            continue;
        bool ok;
        const auto value = input.value(name).toLongLong(&ok);
        if (!ok || value < 0)
            return QStringLiteral("%1 must be a non-negative integer.").arg(QString::fromLatin1(name));
    }
    if (input.contains("minWidth") && input.contains("maxWidth") &&
        input.value("minWidth").toULongLong() > input.value("maxWidth").toULongLong())
        return QStringLiteral("Minimum width exceeds maximum width.");
    const auto direction = input.value("direction").toString();
    if (!direction.isEmpty() && !QStringList{"input", "output", "inout", "ref", "any"}.contains(direction))
        return QStringLiteral("Unknown port direction.");
    const auto access = input.value("access", "read").toString();
    if (!QStringList{"read", "write", "readwrite"}.contains(access))
        return QStringLiteral("Unknown access constraint.");
    const auto unit = input.value("unit").toString();
    if (!unit.isEmpty() && !QStringList{"s", "ms", "us", "ns", "ps", "fs"}.contains(unit))
        return QStringLiteral("Numeric time units must be s, ms, us, ns, ps or fs.");
    return {};
}
QString TemplateInputService::dependencyError(const CodeTemplateSlotList &placeholders)
{
    QHash<QString, QStringList> edges;
    QHash<QString, QVariantMap> specifications;
    for (const auto &slot : placeholders)
    {
        const auto issue = specificationError(slot.input);
        if (!issue.isEmpty())
            return slot.name + ": " + issue;
        if (specifications.contains(slot.name) && specifications.value(slot.name) != slot.input)
            return QStringLiteral("Linked inputs must use identical constraints.");
        specifications.insert(slot.name, slot.input);
        QStringList depends;
        for (const auto *key : {"instanceSlot", "enumOf"})
            if (!slot.input.value(key).toString().isEmpty())
                depends << slot.input.value(key).toString();
        if (edges.contains(slot.name) && edges.value(slot.name) != depends)
            return QStringLiteral("Linked placeholders must use the same dependencies.");
        edges.insert(slot.name, depends);
    }
    for (auto it = edges.cbegin(); it != edges.cend(); ++it)
    {
        QStringList pending = it.value();
        QSet<QString> visited;
        while (!pending.isEmpty())
        {
            const auto name = pending.takeLast();
            if (!edges.contains(name))
                return QStringLiteral("Unknown upstream slot: %1").arg(name);
            if (name == it.key())
                return QStringLiteral("Slot dependencies contain a cycle.");
            if (visited.contains(name))
                continue;
            visited.insert(name);
            pending += edges.value(name);
        }
    }
    return {};
}
TemplateInputCandidates TemplateInputService::candidates(const QVariantMap &input,
                                                         const TemplateInputContext &context,
                                                         const QHash<QString, QString> &bindings)
{
    if (input.value("closedChoice").toBool() && context.liveSnapshot) {
        auto live = context;
        live.snapshot = context.liveSnapshot();
        if (context.liveRoles) live.roles = context.liveRoles();
        live.liveSnapshot = {}; live.liveRoles = {};
        return candidates(input, live, bindings);
    }
    TemplateInputCandidates result;
    QMap<QString, QByteArray> currentHashes;
    result.reason = specificationError(input);
    if (!result.reason.isEmpty())
        return result;
    const auto kind = input.value("kind").toString();
    if (kind == "Value" || kind.isEmpty())
        return result;
    if (context.current && !context.current())
    {
        result.reason = QStringLiteral("Source or project context changed; reopen the inputs.");
        return result;
    }
    if (kind == "Choice" && input.value("enumOf").toString().isEmpty())
    {
        result.values = stringList(input.value("choices"));
        result.values.removeDuplicates();
        if (input.contains("minimum") || input.contains("maximum") || input.contains("minWidth") ||
            input.contains("maxWidth"))
        {
            auto numeric = input;
            numeric.insert("kind", "Value");
            result.values.erase(
                std::remove_if(result.values.begin(), result.values.end(), [&](const QString &value)
                               { return !validate(numeric, value, context, bindings).isEmpty(); }),
                result.values.end());
        }
        if (result.values.isEmpty())
            result.reason = QStringLiteral("This Choice has no configured values.");
        return result;
    }
    if (!context.snapshot)
    {
        result.reason = QStringLiteral("Current Slang facts are not available. Retry Up/Down after analysis.");
        return result;
    }
    SemanticIndex local;
    local.setSnapshot(context.snapshot);
    const auto all = context.snapshot->getSymbolRecords();
    QSet<QString> visibleParameters;
    for (auto category : {CompletionCommandKind::Parameter, CompletionCommandKind::Localparam})
        for (const auto &record : local.getCommandCompletionSymbolRecords(context.location, category))
            visibleParameters.insert(symbolStableKeyText(record.stableKey));
    const auto dependency = input.value("instanceSlot").toString();
    const auto instance = bindings.value(dependency).trimmed();
    if (!dependency.isEmpty() && instance.isEmpty())
    {
        result.reason = QStringLiteral("Fill %1 first.").arg(dependency);
        return result;
    }
    const auto enumExpression = bindings.value(input.value("enumOf").toString());
    const auto role = input.value("role").toString();
    const auto scope = input.value("scope", dependency.isEmpty() ? "visible" : "instance").toString();
    for (const auto &record : all)
    {
        if (input.value("variableOnly").toBool() && record.collectorKind == SymbolTaxonomy::CollectorKind::Wire) continue;
        if (input.value("moduleVariable").toBool() && !record.presentation.directModuleVariable)
            continue;
        if (kind == "Instance")
        {
            for (const auto &path : record.presentation.elaboratedInstancePaths)
                if (instance.isEmpty() || path.startsWith(instance + QLatin1Char('.')))
                    result.values << path;
            continue;
        }
        const bool port = record.declarationKind == Kind::Port;
        const bool signal = record.declarationKind == Kind::Signal ||
                            record.declarationKind == Kind::StructVariable ||
                            record.collectorKind == SymbolTaxonomy::CollectorKind::EnumVariable;
        const bool parameter =
            record.declarationKind == Kind::Parameter || record.declarationKind == Kind::Localparam;
        if ((kind == "Port" && !port) || (kind == "Internal" && !signal) ||
            (kind == "Signal" && !port && !signal) || (kind == "Parameter" && !parameter) ||
            (kind == "Choice" && !port && !signal))
            continue;
        const auto consider =
            [&](const QString &text, const QString &path, const SemanticElaboratedSymbolInfo &info)
        {
            if (!inRange(input, info))
                return;
            if (parameter && (input.contains("minimum") || input.contains("maximum")))
            {
                auto numeric = input;
                numeric.insert("kind", "Value");
                if (!validate(numeric, info.valueText, context, bindings).isEmpty())
                    return;
            }
            if (!role.isEmpty()) {
                const auto manual = context.roles.value(roleKey(record, path)).toString();
                if (input.value("closedChoice").toBool()) {
                    if (info.signalRoleConflict) {
                        result.reason = path + ": " + info.signalRoleIssues.join(" "); return;
                    }
                    QString automatic;
                    for (const auto &proof : info.signalRoles) {
                        bool fresh = true;
                        for (auto source = proof.sourceHashes.cbegin(); source != proof.sourceHashes.cend(); ++source) {
                            if (!currentHashes.contains(source.key())) {
                                const auto cached = context.snapshot->cachedFileSource(source.key());
                                currentHashes[source.key()] = cached.exists ? SemanticSignalRoleEvidence::sourceHash(cached.text) : QByteArray();
                            }
                            if (currentHashes[source.key()] != source.value()) { fresh = false; break; }
                        }
                        if (!fresh) { result.reason = "DUT role evidence is stale; wait for the current semantic analysis."; return; }
                        automatic = proof.role;
                        if (automatic == role) result.evidence[text] = QString("%1 (%2), %3:%4; %5; analysis %6, revision %7")
                            .arg(role, proof.rule, proof.sourceFile).arg(proof.sourceLine)
                            .arg(proof.mappingChain.join("; "), proof.analysisIdentity)
                            .arg(record.presentation.computationRevision)
                            + QString("; edge %1; active level %2; instance %3; parameters %4")
                                .arg(proof.edge, proof.activeLevel < 0 ? "unspecified" : QString::number(proof.activeLevel),
                                     proof.sourceInstance, [&] {
                                         QStringList values;
                                         for (auto it = proof.parameters.cbegin(); it != proof.parameters.cend(); ++it)
                                             values << it.key() + "=" + it.value();
                                         return values.join(", ");
                                     }());
                    }
                    if (!manual.isEmpty() && !automatic.isEmpty() && manual != automatic) {
                        result.reason = path + ": explicit annotation conflicts with Slang usage."; return;
                    }
                    if (automatic != role && manual != role) {
                        if (!info.signalRoleIssues.isEmpty()) result.reason = path + ": " + info.signalRoleIssues.join(" ");
                        return;
                    }
                    if (automatic.isEmpty()) result.evidence[text] = "Explicit user annotation: " + role;
                } else if (manual != role) return;
            }
            if (kind == "Choice")
            {
                if (text != enumExpression && path != enumExpression)
                    return;
                for (const auto &value : info.enumValues)
                {
                    const auto expression =
                        scope == "visible" ? value.expressionText : value.hierarchicalExpressionText;
                    if (value.available && !expression.isEmpty())
                        result.values << expression;
                }
            }
            else
                result.values << text;
        };
        if (scope == "visible")
        {
            const auto resolved = local.resolveVisibleValueRecord(record.name, context.location);
            if ((parameter && visibleParameters.contains(symbolStableKeyText(record.stableKey))) ||
                resolved.stableKey == record.stableKey)
            {
                const auto path = record.presentation.qualifiedScopePath + QLatin1Char('.') + record.name;
                const auto actual = record.presentation.objectInfoByPath.constFind(path);
                if (input.value("closedChoice").toBool() && actual == record.presentation.objectInfoByPath.cend()) continue;
                consider(record.name, path, actual == record.presentation.objectInfoByPath.cend()
                    ? record.presentation.defaultInfo : actual.value());
            }
        }
        else if (scope == "package")
        {
            if (record.owner.kind == SymbolTaxonomy::SymbolOwnerScope::Package)
                consider(record.owner.name + "::" + record.name, record.owner.name + "::" + record.name,
                         record.presentation.defaultInfo);
        }
        else
        {
            for (auto it = record.presentation.objectInfoByPath.cbegin();
                 it != record.presentation.objectInfoByPath.cend(); ++it)
            {
                if (!instance.isEmpty() && !it.key().startsWith(instance + QLatin1Char('.')))
                    continue;
                if (scope == "instance" && instance.isEmpty())
                    continue;
                if (!instance.isEmpty() && !input.value("recursive").toBool() &&
                    it.key().mid(instance.size() + 1).contains(QLatin1Char('.')))
                    continue;
                consider(it.key(), it.key(), it.value());
            }
        }
    }
    result.values.removeDuplicates();
    result.values.sort(Qt::CaseSensitive);
    if (result.values.isEmpty() && result.reason.isEmpty())
        result.reason =
            input.value("closedChoice").toBool()
                ? QStringLiteral("No current compatible %1 signal has a supported DUT usage and connection, or an explicit annotation. Unresolved signals remain unavailable.").arg(role)
            : !role.isEmpty()
                ? QStringLiteral("No matching %1 role is assigned. Use Signal roles in Templates.").arg(role)
                : QStringLiteral("No current Slang objects satisfy these input constraints.");
    return result;
}
QString TemplateInputService::validate(const QVariantMap &input, const QString &text,
                                       const TemplateInputContext &context,
                                       const QHash<QString, QString> &bindings)
{
    if (input.value("kind").toString() == "Code") return specificationError(input);
    if (input.isEmpty())
        return {};
    if (context.current && !context.current())
        return QStringLiteral("Source or project context changed; reopen the inputs.");
    const auto issue = specificationError(input);
    if (!issue.isEmpty())
        return issue;
    if (text.trimmed().isEmpty())
        return input.value("allowEmpty").toBool() ? QString()
                                                  : QStringLiteral("Required input is empty.");
    if (input.value("kind").toString() != "Value")
    {
        const auto result = candidates(input, context, bindings);
        return result.values.contains(text.trimmed()) ? QString()
               : result.reason.isEmpty()
                   ? QStringLiteral("The entered value does not satisfy this input's constraints.")
                   : result.reason;
    }
    const auto values = constants(context);
    const auto literal = EffectiveValueService::evaluateLiteral(text, false, values);
    if (!literal.available)
        return QStringLiteral("Slang could not evaluate this Value: %1").arg(literal.failureReason);
    if (input.value("finite").toBool()) {
        bool ok = false;
        const double number = literal.numericValueText.toDouble(&ok);
        if (!ok || !std::isfinite(number)) return QStringLiteral("Enter a finite numeric value.");
    }
    if ((input.value("integer").toBool() || input.value("integral").toBool()) && !literal.integral)
        return QStringLiteral("This input requires an integral value.");
    if (input.value("known", true).toBool() && literal.integral && literal.numericValueText.isEmpty())
        return QStringLiteral("This numeric input must not contain X or Z.");
    for (const auto *key : {"packedRank", "unpackedRank"})
    {
        const auto actual =
            QString::fromLatin1(key) == "packedRank" ? literal.packedRank : literal.unpackedRank;
        if (input.contains(key) && actual != input.value(key).toInt())
            return QStringLiteral("Value dimensions do not match the input constraints.");
    }
    if ((input.contains("minWidth") &&
         literal.bitWidthText.toULongLong() < input.value("minWidth").toULongLong()) ||
        (input.contains("maxWidth") &&
         literal.bitWidthText.toULongLong() > input.value("maxWidth").toULongLong()))
        return QStringLiteral("Value width does not match the input constraints.");
    for (const auto &bound : {QPair<QString, QString>{"minimum", ">="}, {"maximum", "<="}})
    {
        if (!input.contains(bound.first))
            continue;
        const auto unit = input.value("unit").toString();
        const auto measured = unit.isEmpty() ? text : QStringLiteral("(%1) / 1%2").arg(text, unit);
        const auto comparison = bound.first == "minimum" && input.value("exclusiveMinimum").toBool() ? QStringLiteral(">") : bound.second;
        const auto compared = EffectiveValueService::evaluateLiteral(
            QStringLiteral("((%1) %2 (%3))").arg(measured, comparison, input.value(bound.first).toString()),
            false, values);
        if (!compared.available || compared.numericValueText != "1")
            return QStringLiteral("Value must be %1 %2.")
                .arg(comparison, input.value(bound.first).toString());
    }
    return {};
}
