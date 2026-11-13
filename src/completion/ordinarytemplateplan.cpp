#include "ordinarytemplateplan.h"
#include "codetemplateservice.h"
#include "templateinputservice.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace {
struct Expansion {
    const QList<UserTemplateRecord>& catalog;
    QString selection;
    OrdinaryTemplatePlan plan;
    QSet<QString> visiting;
    QHash<QString, int> stops;
    int nodes = 0;

    void append(const UserTemplateRecord& record, int depth) {
        if (!plan.error.isEmpty()) return;
        if (++nodes > 128 || depth > 16 || visiting.contains(record.id)) {
            plan.error = "Template composition is cyclic or exceeds its limit."; return;
        }
        if (record.command.value("kind").toString() == "block") {
            plan.error = "A code template cannot include a block preset."; return;
        }
        visiting.insert(record.id);
        const auto inputs = record.command.value("inputs").toMap();
        auto addSlot = [&](QString name, QString value, QVariantMap input) {
            if (name == "body" && record.command.value("acceptsBody").toBool() && !selection.isEmpty()) {
                plan.text += selection; return;
            }
            if (!stops.contains(name)) stops.insert(name, stops.size() + 1);
            if (input.isEmpty()) input = {{"kind", "Text"}, {"allowEmpty",true}};
            plan.placeholders.append({name, int(plan.text.size()), int(value.size()), stops[name], true, input});
            plan.text += value;
        };
        if (!record.templateSlots.isEmpty()) {
            auto placeholders = record.templateSlots;
            std::stable_sort(placeholders.begin(), placeholders.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
            int previous = 0;
            for (const auto& slot : placeholders) {
                if (slot.start < previous || slot.length < 0 || slot.start + slot.length > record.insertText.size()) {
                    plan.error = "Template input ranges overlap or leave the body."; break;
                }
                plan.text += record.insertText.mid(previous, slot.start - previous);
                addSlot(slot.name, record.insertText.mid(slot.start, slot.length), slot.input);
                previous = slot.start + slot.length;
            }
            plan.text += record.insertText.mid(previous);
        } else {
            const QString& body = record.insertText;
            int position = 0;
            while (position < body.size() && plan.error.isEmpty()) {
                const int start = body.indexOf("${", position);
                if (start < 0) { plan.text += body.mid(position); break; }
                plan.text += body.mid(position, start - position);
                const int end = body.indexOf('}', start + 2);
                if (end < 0) { plan.error = "Unclosed template input."; break; }
                const auto name = body.mid(start + 2, end - start - 2);
                if (name == "cursor") {
                    plan.finalCursor = plan.text.size();
                } else if (name.startsWith("include:")) {
                    const auto id = name.mid(8);
                    const auto found = std::find_if(catalog.cbegin(), catalog.cend(), [&](const auto& r) { return r.id == id || r.commandToken == id; });
                    if (found == catalog.cend()) plan.error = "Missing template dependency: " + id;
                    else if (std::find_if(found + 1, catalog.cend(), [&](const auto& r) { return r.id == id || r.commandToken == id; }) != catalog.cend()) plan.error = "Ambiguous template dependency: " + id;
                    else append(*found, depth + 1);
                } else if (name == "body" && record.command.value("acceptsBody").toBool()) {
                    addSlot(name, inputs.value(name).toMap().value("default").toString(), {{"kind", "Code"}, {"allowEmpty", true}});
                } else if (!inputs.contains(name)) {
                    plan.error = "Configure template input: " + name;
                } else {
                    const auto input = inputs.value(name).toMap();
                    addSlot(name, input.value("default").toString(), input);
                }
                position = end + 1;
                if (plan.text.size() > 1024 * 1024) plan.error = "Expanded template exceeds 1 MiB.";
            }
        }
        if (plan.text.size() > 1024 * 1024) plan.error = "Expanded template exceeds 1 MiB.";
        visiting.remove(record.id);
    }
};
}

QList<UserTemplateRecord> OrdinaryTemplatePlanner::builtins() { return {}; }

QList<UserTemplateRecord> OrdinaryTemplatePlanner::catalog(UserTemplateService* service, const QString& file) {
    auto result = builtins();
    for (const auto& record : service->recordsForFile(file)) {
        if (record.commandToken.startsWith('@') || record.command.value("disabled").toBool()) continue;
        const auto extensions = record.command.value("extensions").toStringList();
        if (!extensions.isEmpty() && !extensions.contains(QFileInfo(file).suffix(), Qt::CaseInsensitive)) continue;
        const auto found = std::find_if(result.begin(), result.end(), [&](const auto& r) { return r.commandToken.compare(record.commandToken, Qt::CaseInsensitive) == 0; });
        if (found == result.end()) result.append(record); else *found = record;
    }
    result.erase(std::remove_if(result.begin(), result.end(), [&](const auto& record) {
        const auto extensions = record.command.value("extensions").toStringList();
        return !extensions.isEmpty() && !extensions.contains(QFileInfo(file).suffix(), Qt::CaseInsensitive);
    }), result.end());
    return result;
}

OrdinaryTemplatePlan OrdinaryTemplatePlanner::prepare(const UserTemplateRecord& resource,
                                                     const QList<UserTemplateRecord>& catalog,
                                                     const QString& selection) {
    Expansion expansion{catalog, selection};
    if (!selection.isEmpty() && !resource.command.value("acceptsBody").toBool()) {
        expansion.plan.error = "This template does not accept a selected body."; return expansion.plan;
    }
    expansion.append(resource, 0);
    if (expansion.plan.error.isEmpty()) expansion.plan.error = TemplateInputService::dependencyError(expansion.plan.placeholders);
    if (expansion.plan.finalCursor < 0) expansion.plan.finalCursor = expansion.plan.text.size();
    return expansion.plan;
}

QString OrdinaryTemplatePlanner::validate(const QList<UserTemplateRecord>& catalog) {
    QStringList contexts{QString()};
    for (const auto& record : catalog) { const auto file = record.command.value("file").toString(); if (!contexts.contains(file)) contexts << file; }
    for (const auto& file : contexts) {
    QList<UserTemplateRecord> effective;
    for (const auto& record : catalog) {
        const auto local = record.command.value("file").toString(); if (!local.isEmpty() && local.compare(file,Qt::CaseInsensitive) != 0) continue;
        auto found = std::find_if(effective.begin(),effective.end(),[&](const auto& item) { return item.commandToken.compare(record.commandToken,Qt::CaseInsensitive) == 0; });
        if (found == effective.end()) effective << record;
        else if (found->command.value("file").toString().isEmpty() || !local.isEmpty()) *found = record;
    }
    for (const auto& record : effective) {
        if (!record.commandToken.startsWith('/')) continue;
        if (!QStringList{"any","code","codeLine","module"}.contains(record.command.value("context","any").toString())) return "Unsupported template location.";
        if (record.command.value("kind").toString() == "block") {
            if (!QStringList{"text", "image", "reference", "code", "attachment", "wave"}.contains(record.command.value("blockType").toString()))
                return "Unsupported block preset type.";
            continue;
        }
        const auto plan = prepare(record, effective);
        if (!plan.error.isEmpty()) return record.commandToken + ": " + plan.error;
    }
    }
    return {};
}
