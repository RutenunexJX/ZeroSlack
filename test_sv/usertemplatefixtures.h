#pragma once
#include "usertemplateservice.h"
#include <QJsonDocument>
#include <QJsonArray>
// Explicit test inputs for the retained user template engine; never part of the product catalog.
inline QList<UserTemplateRecord> simulationTemplateFixtures()
{
    QList<UserTemplateRecord> result;
    const QStringList tokens{QStringLiteral("reset"), QStringLiteral("ramp"), QStringLiteral("random"), QStringLiteral("random_window")};
    const QStringList symbols{QStringLiteral("tb_reset_package::reset_driver::run"), QStringLiteral("tb_sequence_package::ramp_driver::run"),
        QStringLiteral("tb_random_package::random_driver::run"), QStringLiteral("tb_random_package::random_driver::window")};
    const QStringList descriptions{QStringLiteral("Assert and release a reset on selected clock edges"), QStringLiteral("Drive an incrementing sequence"),
        QStringLiteral("Drive a bounded random sequence"), QStringLiteral("Drive random values within an absolute time window")};
    for (int i = 0; i < tokens.size(); ++i) {
        UserTemplateRecord record;
        record.id = QStringLiteral("fixture:") + tokens[i]; record.commandToken = QLatin1Char('@') + tokens[i]; record.label = record.commandToken;
        record.description = descriptions[i]; record.storageSource = QStringLiteral("fixture");
        record.command = {{QStringLiteral("kind"), QStringLiteral("driver")}, {QStringLiteral("symbol"), symbols[i]},
            {QStringLiteral("builtinAsset"), QStringLiteral("tb-behaviors")},
            {QStringLiteral("summary"), i == 0 ? QStringLiteral("reset · cycles") : i == 1 ? QStringLiteral("signal · range · step") : i == 2 ? QStringLiteral("signal · range · cycles") : QStringLiteral("signal · range · time")}};
        result << record;
    }
    UserTemplateRecord print;
    print.id = QStringLiteral("builtin:print"); print.commandToken = print.label = QStringLiteral("@print"); print.storageSource = QStringLiteral("fixture");
    print.description = QStringLiteral("Emit a structured simulation event with runtime values");
    print.command = {{QStringLiteral("kind"), QStringLiteral("print")}, {QStringLiteral("summary"), QStringLiteral("message · values · condition")}};
    result << print;
    const auto container = [&](const QString& token, const QString& description, const QString& text, const QString& content, const QString& placement, int start, int length) {
        UserTemplateRecord record;
        record.id = QStringLiteral("fixture:") + token; record.commandToken = record.label = QLatin1Char('@') + token;
        record.description = description; record.storageSource = QStringLiteral("fixture"); record.insertText = text;
        record.command = {{"kind", "snippet"}, {"content", content}, {"placement", placement}, {"summary", "body · mixed code and child templates"}};
        record.templateSlots << CodeTemplateSlot{"body", start, length, 1, true, {{"kind", "Code"}, {"content", content == "declarations" && token == "definitions" ? "declarations" : "statements"}, {"allowEmpty", true}}};
        result << record;
    };
    container("definitions", "Module declaration region", "\n  \n", "declarations", "module", 1, 2);
    result.last().command.insert("containerRole", "definitions");
    container("process", "Simulation process with a code container", "initial begin\n  \nend\n", "process", "module", 14, 2);
    container("repeat", "Repeat a simulation statement sequence", "repeat (3) begin\n  \nend\n", "statements", "statement", 17, 2);
    result.last().templateSlots.first().tabStop = 2;
    result.last().templateSlots.prepend(CodeTemplateSlot{"count", 8, 1, 1, true, {{"kind", "Value"}, {"integer", true}, {"minimum", "0"}}});
    return result;
}


inline QList<UserTemplateRecord> ordinaryTemplateFixtures() {
    QList<UserTemplateRecord> result;
    const auto rows=QJsonDocument::fromJson(R"FIXTURE([{"id": "code:process", "command": "/process", "label": "Clocked process", "body": "always_ff @(posedge ${clock}) begin\n    ${body}\nend${cursor}", "editor": {"kind": "snippet", "context": "module", "extensions": ["sv", "svh", "v", "vh"], "inputs": {"clock": {"kind": "Signal", "scope": "visible", "default": "clk"}, "body": {"kind": "Code", "allowEmpty": true, "default": ""}}, "aliases": ["sequential", "clock"]}}, {"id": "code:wrap", "command": "/wrap", "label": "Begin / end around selection", "body": "begin\n    ${body}\nend${cursor}", "editor": {"kind": "snippet", "context": "code", "acceptsBody": true, "inputs": {"body": {"kind": "Code", "allowEmpty": true, "default": ""}}}}])FIXTURE").array();
    for(const auto& row:rows) result.append(UserTemplateService::fromJson(row.toObject()).records);
    return result;
}
