#include "editorselection.h"

#include "editorsemanticcontextservice.h"
#include "mycodeeditor.h"

#include <algorithm>
#include <QColor>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFormat>
#include <QTimer>

namespace {
constexpr int kPrimarySelectionProperty = QTextFormat::UserProperty;
constexpr int kScopeBackgroundSelectionMarker = 997;
constexpr int kCurrentLineSelectionMarker = 998;
constexpr int kCommandSelectionProperty = kPrimarySelectionProperty;
constexpr int kCommandSelectionMarker = 999;
constexpr int kHoveredSymbolSelectionProperty = QTextFormat::UserProperty + 1;
constexpr int kHoveredSymbolSelectionMarker = 1001;

void removeSelectionsByProperty(
    QList<QTextEdit::ExtraSelection>& selections,
    int property,
    int value)
{
    selections.erase(
        std::remove_if(selections.begin(), selections.end(),
            [property, value](const QTextEdit::ExtraSelection& selection) {
                return selection.format.property(property).toInt() == value;
            }),
        selections.end());
}

QList<QTextEdit::ExtraSelection> editorSelectionsWithout(
    QPlainTextEdit* editor,
    int property,
    int value)
{
    QList<QTextEdit::ExtraSelection> selections = editor->extraSelections();
    removeSelectionsByProperty(selections, property, value);
    return selections;
}

void clampSelectionsToDocument(
    QTextDocument* document,
    QList<QTextEdit::ExtraSelection>& selections)
{
    const int docLen = document->characterCount();
    const int docEnd = (docLen > 0) ? docLen - 1 : 0;
    for (auto& selection : selections) {
        QTextCursor& cursor = selection.cursor;
        const int pos = qBound(0, cursor.position(), docEnd);
        const int anchor = qBound(0, cursor.anchor(), docEnd);
        cursor.setPosition(anchor);
        cursor.setPosition(pos, QTextCursor::KeepAnchor);
    }
}
}

void EditorSelection::highlightCurrentLine(MyCodeEditor* editor)
{
    QList<QTextEdit::ExtraSelection> selections = editor->extraSelections();
    selections.erase(
        std::remove_if(selections.begin(), selections.end(),
            [](const QTextEdit::ExtraSelection& selection) {
                const int property = selection.format
                    .property(kPrimarySelectionProperty)
                    .toInt();
                return property == kScopeBackgroundSelectionMarker
                    || property == kCurrentLineSelectionMarker;
            }),
        selections.end());

    QTextEdit::ExtraSelection currentLine;
    currentLine.format.setBackground(QColor(0, 100, 100, 20));
    currentLine.format.setProperty(
        QTextFormat::FullWidthSelection,
        true);
    currentLine.format.setProperty(
        kPrimarySelectionProperty,
        kCurrentLineSelectionMarker);
    currentLine.cursor = editor->textCursor();
    selections.append(currentLine);

    clampSelectionsToDocument(editor->document(), selections);
    editor->setExtraSelections(selections);
}

void EditorSelection::removeByProperty(
    QPlainTextEdit* editor,
    int property,
    int value)
{
    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(editor, property, value);
    editor->setExtraSelections(selections);
}

void EditorSelection::highlightCommand(MyCodeEditor* editor, int prefixPosition)
{
    if (prefixPosition < 0)
        return;

    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kCommandSelectionProperty,
            kCommandSelectionMarker);

    QTextEdit::ExtraSelection commandSelection;
    commandSelection.format.setBackground(QColor(60, 60, 60, 180));
    commandSelection.format.setForeground(QColor(255, 255, 255));
    commandSelection.format.setProperty(
        kCommandSelectionProperty,
        kCommandSelectionMarker);

    QTextCursor commandCursor = editor->textCursor();
    const int commandStartPosition =
        commandCursor.block().position() + prefixPosition;
    commandCursor.setPosition(commandStartPosition);
    commandCursor.setPosition(
        editor->textCursor().position(),
        QTextCursor::KeepAnchor);
    commandSelection.cursor = commandCursor;

    selections.append(commandSelection);
    editor->setExtraSelections(selections);
}

void EditorSelection::clearCommand(QPlainTextEdit* editor)
{
    removeByProperty(
        editor,
        kCommandSelectionProperty,
        kCommandSelectionMarker);
}

void EditorSelection::highlightHoveredSymbol(
    MyCodeEditor* editor,
    const EditorSourceNavigationTarget& target)
{
    if (target.text.isEmpty()
        || target.startPos < 0
        || target.endPos <= target.startPos) {
        return;
    }

    QTextEdit::ExtraSelection highlight;
    highlight.cursor = editor->textCursor();
    highlight.cursor.setPosition(target.startPos);
    highlight.cursor.setPosition(
        target.endPos,
        QTextCursor::KeepAnchor);
    highlight.format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    highlight.format.setUnderlineColor(QColor(0, 100, 200));
    highlight.format.setForeground(QColor(0, 100, 200));
    highlight.format.setProperty(
        kHoveredSymbolSelectionProperty,
        kHoveredSymbolSelectionMarker);

    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kHoveredSymbolSelectionProperty,
            kHoveredSymbolSelectionMarker);
    selections.append(highlight);
    editor->setExtraSelections(selections);
}

void EditorSelection::clearHoveredSymbol(QPlainTextEdit* editor)
{
    removeByProperty(
        editor,
        kHoveredSymbolSelectionProperty,
        kHoveredSymbolSelectionMarker);
}

void EditorHighlightRefresh::attachToEditor(
    MyCodeEditor* editor,
    const std::function<void()>& refresh)
{
    timer = new QTimer(editor);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, editor, refresh);

    auto scheduleHighlightRefresh = [this]() { schedule(); };
    QObject::connect(
        editor,
        &QPlainTextEdit::cursorPositionChanged,
        editor,
        scheduleHighlightRefresh);
    QObject::connect(
        editor,
        &QPlainTextEdit::textChanged,
        editor,
        scheduleHighlightRefresh);
}

void EditorHighlightRefresh::schedule() const
{
    timer->start(0);
}
