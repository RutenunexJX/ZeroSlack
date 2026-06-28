#include "editorselection.h"

#include "editorsemanticcontextservice.h"
#include "mycodeeditor.h"

#include <algorithm>
#include <QColor>
#include <QFont>
#include <QMap>
#include <QPalette>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFormat>
#include <QTimer>
#include <QPointer>

namespace {
constexpr int kPrimarySelectionProperty = QTextFormat::UserProperty;
constexpr int kScopeBackgroundSelectionMarker = 997;
constexpr int kCurrentLineSelectionMarker = 998;
constexpr int kCommandSelectionProperty = kPrimarySelectionProperty;
constexpr int kCommandSelectionMarker = 999;
constexpr int kHoveredSymbolSelectionProperty = QTextFormat::UserProperty + 1;
constexpr int kHoveredSymbolSelectionMarker = 1001;
constexpr int kDiagnosticSelectionProperty = QTextFormat::UserProperty + 2;
constexpr int kDiagnosticSelectionMarker = 1002;
constexpr int kSemanticSelectionProperty = QTextFormat::UserProperty + 3;
constexpr int kSemanticSelectionMarker = 1003;
constexpr int kCurrentSymbolSelectionProperty = QTextFormat::UserProperty + 4;
constexpr int kCurrentSymbolSelectionMarker = 1004;
constexpr int kSearchSelectionProperty = QTextFormat::UserProperty + 5;
constexpr int kSearchSelectionMarker = 1005;
constexpr int kTemplateSlotSelectionProperty = QTextFormat::UserProperty + 7;
constexpr int kTemplateSlotSelectionMarker = 1007;
constexpr int kFlashSelectionProperty = QTextFormat::UserProperty + 6;
constexpr int kFlashSelectionMarker = 1006;
constexpr int kMaxPassiveMatchHighlights = 500;
constexpr int kMaxPassiveSymbolHighlightCharacters = 1024 * 1024;

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

bool editorUsesDarkPalette(const QPlainTextEdit* editor)
{
    const QColor base =
        editor->palette().color(QPalette::Base);
    return base.lightness() < 128;
}

QTextCharFormat semanticFormatForRole(
    SemanticDecorationRole role,
    bool dark)
{
    QTextCharFormat format;
    auto color = [dark](const char* darkColor, const char* lightColor) {
        return QColor(QString::fromLatin1(dark ? darkColor : lightColor));
    };

    switch (role) {
    case SemanticDecorationRole::ModuleInterface:
        format.setForeground(color("#61AFEF", "#005CC5"));
        format.setFontWeight(QFont::Bold);
        break;
    case SemanticDecorationRole::PackageClassType:
        format.setForeground(color("#56B6C2", "#007C89"));
        format.setFontWeight(QFont::Bold);
        break;
    case SemanticDecorationRole::InstanceName:
        format.setForeground(color("#E5C07B", "#8A5A00"));
        break;
    case SemanticDecorationRole::FormalPort:
        format.setForeground(color("#98C379", "#22863A"));
        break;
    case SemanticDecorationRole::ModulePort:
        format.setForeground(color("#9CDCFE", "#0366D6"));
        format.setFontWeight(QFont::DemiBold);
        break;
    case SemanticDecorationRole::ActualSignal:
        format.setForeground(color("#D19A66", "#B05A00"));
        break;
    case SemanticDecorationRole::Parameter:
        format.setForeground(color("#E06C75", "#D73A49"));
        break;
    case SemanticDecorationRole::EnumValue:
        format.setForeground(color("#DCDCAA", "#795E26"));
        format.setFontWeight(QFont::DemiBold);
        break;
    case SemanticDecorationRole::TypeAlias:
        format.setForeground(color("#4EC9B0", "#00796B"));
        format.setFontWeight(QFont::Bold);
        break;
    case SemanticDecorationRole::Macro:
        format.setForeground(color("#D7BA7D", "#735C0F"));
        break;
    case SemanticDecorationRole::SystemTask:
        format.setForeground(color("#56B6C2", "#007C89"));
        break;
    }

    format.setProperty(kSemanticSelectionProperty, kSemanticSelectionMarker);
    return format;
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
    QList<QTextEdit::ExtraSelection> selections = editor->extraSelections();
    const int before = selections.size();
    removeSelectionsByProperty(selections, property, value);
    if (selections.size() == before)
        return;
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

void EditorSelection::highlightDiagnostics(
    MyCodeEditor* editor,
    const QList<SemanticDiagnostic>& diagnostics)
{
    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kDiagnosticSelectionProperty,
            kDiagnosticSelectionMarker);

    QMap<int, SemanticDiagnostic::Severity> severityByLine;
    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.line <= 0)
            continue;
        if (diagnostic.severity != SemanticDiagnostic::Error
            && diagnostic.severity != SemanticDiagnostic::Warning) {
            continue;
        }

        const auto existing = severityByLine.constFind(diagnostic.line);
        if (existing == severityByLine.constEnd()
            || diagnostic.severity == SemanticDiagnostic::Error) {
            severityByLine.insert(diagnostic.line, diagnostic.severity);
        }
    }

    for (auto it = severityByLine.constBegin(); it != severityByLine.constEnd(); ++it) {
        const QTextBlock block =
            editor->document()->findBlockByNumber(it.key() - 1);
        if (!block.isValid())
            continue;

        QTextEdit::ExtraSelection diagnosticSelection;
        diagnosticSelection.cursor = QTextCursor(block);
        diagnosticSelection.format.setProperty(
            QTextFormat::FullWidthSelection,
            true);
        diagnosticSelection.format.setProperty(
            kDiagnosticSelectionProperty,
            kDiagnosticSelectionMarker);
        diagnosticSelection.format.setBackground(
            it.value() == SemanticDiagnostic::Error
                ? QColor(180, 40, 40, 55)
                : QColor(200, 160, 35, 55));
        selections.append(diagnosticSelection);
    }

    for (const SemanticDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.line <= 0)
            continue;
        if (diagnostic.severity != SemanticDiagnostic::Error
            && diagnostic.severity != SemanticDiagnostic::Warning) {
            continue;
        }

        const QTextBlock block =
            editor->document()->findBlockByNumber(diagnostic.line - 1);
        if (!block.isValid())
            continue;

        const int column = qBound(1, diagnostic.column, block.length());
        QTextCursor underlineCursor(block);
        underlineCursor.setPosition(block.position() + column - 1);
        underlineCursor.setPosition(
            qMin(block.position() + block.length() - 1,
                 block.position() + column),
            QTextCursor::KeepAnchor);

        QTextEdit::ExtraSelection underlineSelection;
        underlineSelection.cursor = underlineCursor;
        underlineSelection.format.setUnderlineStyle(
            QTextCharFormat::WaveUnderline);
        underlineSelection.format.setUnderlineColor(
            diagnostic.severity == SemanticDiagnostic::Error
                ? QColor("#EF4444")
                : QColor("#FBBF24"));
        underlineSelection.format.setProperty(
            kDiagnosticSelectionProperty,
            kDiagnosticSelectionMarker);
        selections.append(underlineSelection);
    }

    clampSelectionsToDocument(editor->document(), selections);
    editor->setExtraSelections(selections);
}

void EditorSelection::highlightSemanticDecorations(
    MyCodeEditor* editor,
    const QList<SemanticDecoration>& decorations)
{
    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kSemanticSelectionProperty,
            kSemanticSelectionMarker);

    const bool dark = editorUsesDarkPalette(editor);
    for (const SemanticDecoration& decoration : decorations) {
        if (!decoration.isValid())
            continue;

        QTextEdit::ExtraSelection selection;
        selection.cursor = editor->textCursor();
        selection.cursor.setPosition(decoration.startPosition);
        selection.cursor.setPosition(
            decoration.startPosition + decoration.length,
            QTextCursor::KeepAnchor);
        selection.format = semanticFormatForRole(decoration.role, dark);
        selections.append(selection);
    }

    clampSelectionsToDocument(editor->document(), selections);
    editor->setExtraSelections(selections);
}

void EditorSelection::highlightCurrentSymbolReferences(MyCodeEditor* editor)
{
    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kCurrentSymbolSelectionProperty,
            kCurrentSymbolSelectionMarker);

    if (editor->document()->characterCount()
        > kMaxPassiveSymbolHighlightCharacters) {
        if (selections.size() != editor->extraSelections().size())
            editor->setExtraSelections(selections);
        return;
    }

    QTextCursor wordCursor = editor->textCursor();
    wordCursor.select(QTextCursor::WordUnderCursor);
    const QString word = wordCursor.selectedText().trimmed();
    if (word.size() < 2
        || (!word.at(0).isLetter() && word.at(0) != QLatin1Char('_'))) {
        editor->setExtraSelections(selections);
        return;
    }

    QTextCursor cursor(editor->document());
    int matchCount = 0;
    while (!cursor.isNull() && matchCount < kMaxPassiveMatchHighlights) {
        cursor = editor->document()->find(word, cursor, QTextDocument::FindWholeWords);
        if (cursor.isNull())
            break;

        QTextEdit::ExtraSelection match;
        match.cursor = cursor;
        match.format.setBackground(QColor(59, 130, 246, 28));
        match.format.setProperty(
            kCurrentSymbolSelectionProperty,
            kCurrentSymbolSelectionMarker);
        selections.append(match);
        ++matchCount;
    }

    editor->setExtraSelections(selections);
}

void EditorSelection::highlightSearchMatches(
    MyCodeEditor* editor,
    const QString& text,
    bool caseSensitive)
{
    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kSearchSelectionProperty,
            kSearchSelectionMarker);
    if (text.isEmpty()) {
        editor->setExtraSelections(selections);
        return;
    }

    QTextDocument::FindFlags flags;
    if (caseSensitive)
        flags |= QTextDocument::FindCaseSensitively;

    QTextCursor cursor(editor->document());
    int matchCount = 0;
    while (!cursor.isNull() && matchCount < kMaxPassiveMatchHighlights) {
        cursor = editor->document()->find(text, cursor, flags);
        if (cursor.isNull())
            break;

        QTextEdit::ExtraSelection match;
        match.cursor = cursor;
        match.format.setBackground(QColor(245, 158, 11, 45));
        match.format.setProperty(
            kSearchSelectionProperty,
            kSearchSelectionMarker);
        selections.append(match);
        ++matchCount;
    }

    editor->setExtraSelections(selections);
}

void EditorSelection::clearSearchMatches(QPlainTextEdit* editor)
{
    removeByProperty(editor, kSearchSelectionProperty, kSearchSelectionMarker);
}

void EditorSelection::highlightTemplateSlots(
    MyCodeEditor* editor,
    const QList<QPair<int, int>>& ranges,
    int activeIndex,
    bool pulseOn)
{
    if (!editor || !editor->document())
        return;

    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kTemplateSlotSelectionProperty,
            kTemplateSlotSelectionMarker);

    const int docEnd = qMax(0, editor->document()->characterCount() - 1);
    for (int i = 0; i < ranges.size(); ++i) {
        const int start = ranges.at(i).first;
        const int requestedLength = ranges.at(i).second;
        if (start < 0 || requestedLength < 0 || start > docEnd)
            continue;

        int visibleStart = qBound(0, start, docEnd);
        int visibleEnd = qBound(0, start + requestedLength, docEnd);
        if (visibleEnd <= visibleStart) {
            if (visibleStart < docEnd) {
                visibleEnd = visibleStart + 1;
            } else if (visibleStart > 0) {
                --visibleStart;
                visibleEnd = visibleStart + 1;
            } else {
                continue;
            }
        }

        QTextCursor cursor(editor->document());
        cursor.setPosition(visibleStart);
        cursor.setPosition(visibleEnd, QTextCursor::KeepAnchor);

        QTextEdit::ExtraSelection slotSelection;
        slotSelection.cursor = cursor;
        const bool active = i == activeIndex;
        const int weakAlpha = pulseOn ? 58 : 26;
        const int activeAlpha = pulseOn ? 118 : 82;
        slotSelection.format.setBackground(
            active
                ? QColor(34, 197, 94, activeAlpha)
                : QColor(59, 130, 246, weakAlpha));
        slotSelection.format.setUnderlineStyle(
            active ? QTextCharFormat::DashUnderline
                   : QTextCharFormat::SingleUnderline);
        slotSelection.format.setUnderlineColor(
            active ? QColor("#22C55E") : QColor("#60A5FA"));
        slotSelection.format.setProperty(
            kTemplateSlotSelectionProperty,
            kTemplateSlotSelectionMarker);
        selections.append(slotSelection);
    }

    editor->setExtraSelections(selections);
}

void EditorSelection::clearTemplateSlots(QPlainTextEdit* editor)
{
    removeByProperty(
        editor,
        kTemplateSlotSelectionProperty,
        kTemplateSlotSelectionMarker);
}

void EditorSelection::flashLine(MyCodeEditor* editor)
{
    if (!editor)
        return;
    flashLine(editor, editor->textCursor().blockNumber() + 1);
}

void EditorSelection::flashLine(MyCodeEditor* editor, int lineNumber)
{
    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kFlashSelectionProperty,
            kFlashSelectionMarker);

    QTextBlock block = editor->document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid())
        return;

    QTextEdit::ExtraSelection flash;
    flash.cursor = QTextCursor(block);
    flash.format.setBackground(QColor(97, 175, 239, 70));
    flash.format.setProperty(QTextFormat::FullWidthSelection, true);
    flash.format.setProperty(kFlashSelectionProperty, kFlashSelectionMarker);
    selections.append(flash);
    editor->setExtraSelections(selections);

    const QPointer<QPlainTextEdit> guardedEditor(editor);
    QTimer::singleShot(650, editor, [this, guardedEditor]() {
        if (guardedEditor)
            removeByProperty(guardedEditor, kFlashSelectionProperty, kFlashSelectionMarker);
    });
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
