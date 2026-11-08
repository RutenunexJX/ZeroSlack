#pragma once

#include "mycodeeditor.h"
#include "simulationcommandplanner.h"
#include "usertemplateservice.h"
#include <QtTest>

// Set up retained template/slot data without the removed Ctrl+Space entry.
inline void insertTemplateFixture(MyCodeEditor* editor, SimulationCommandRequest request,
                                  const QString& token)
{
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    const int count = editor->templateModel()->instances().size();
    const auto command = token.startsWith('@') ? token : "@" + token;
    bool found = false;
    for (const auto& item : UserTemplateService::getInstance()->catalogForFile(
             editor->documentFileName(), true)) {
        if (item.commandToken != command) continue;
        found = true;
        request.item.id = item.commandToken;
        request.item.title = item.label;
        request.item.parameters = item.command;
        request.item.insertionText = item.insertText;
        request.item.templateSlots = item.templateSlots;
        break;
    }
    QVERIFY2(found, qPrintable(token));
    request.item.replacementStart = editor->textCursor().selectionStart();
    request.item.replacementLength = editor->textCursor().selectionEnd() - request.item.replacementStart;
    editor->templateInsertionContext(&request.item.templateParentId, &request.item.templateParentSlot);
    request.item.templateContent = editor->templateContainerContent();
    request.context.templateParentId = request.item.templateParentId;
    request.context.templateParentSlot = request.item.templateParentSlot;
    request.context.templateContent = request.item.templateContent;
    const auto plan = SimulationCommandPlanner::prepare(request);
    QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
    plan.inputs->current = [captured = plan.captured] {
        return SimulationCommandPlanner::sourcesCurrent(captured);
    };
    editor->configureTemplateInputContext(plan.inputs);
    QTextCursor cursor(editor->document());
    cursor.setPosition(plan.start);
    cursor.setPosition(plan.start + plan.length, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    QString error;
    QVERIFY2(editor->insertSimulationTemplate(plan.text, plan.placeholders, plan.instance,
        request.item.templateParentId, request.item.templateParentSlot, &error), qPrintable(error));
    editor->configureTemplateInputContext(plan.inputs, editor->selectedTemplateId(), true);
    QCOMPARE(editor->templateModel()->instances().size(), count + 1);
    QTRY_VERIFY(editor->hasFocus());
}
