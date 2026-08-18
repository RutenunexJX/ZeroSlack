#include "editorselection.h"

#include "editorsemanticcontextservice.h"
#include "insightvisualstyle.h"
#include "mycodeeditor.h"
#include "tsdocument.h"

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

struct EditorOccurrenceNode {
    QString word;
    int position = 0;
    int length = 0;
    int lazyShift = 0;
    std::uint32_t priority = 0;
    EditorOccurrenceNode* left = nullptr;
    EditorOccurrenceNode* right = nullptr;
    EditorOccurrenceNode* parent = nullptr;
    bool active = true;
};

namespace {
QColor themeColorWithAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

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
constexpr int kSignalSelectionProperty = QTextFormat::UserProperty + 8;
constexpr int kSignalSelectionMarker = 1008;
constexpr int kKeywordPairSelectionProperty = QTextFormat::UserProperty + 9;
constexpr int kKeywordPairSelectionMarker = 1009;
constexpr int kFlashSelectionProperty = QTextFormat::UserProperty + 6;
constexpr int kFlashSelectionMarker = 1006;
constexpr int kMaxPassiveMatchHighlights = 500;

struct SmartSelectionSpan {
    int start = -1;
    int end = -1;

    bool isValid() const
    {
        return start >= 0 && end > start;
    }

    bool contains(const SmartSelectionSpan& other) const
    {
        return isValid() && other.isValid()
            && start <= other.start && end >= other.end;
    }

    int length() const
    {
        return isValid() ? end - start : 0;
    }

    bool operator==(const SmartSelectionSpan& other) const
    {
        return start == other.start && end == other.end;
    }
};

bool isSmartSelectionIdentifierStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_')
        || ch == QLatin1Char('$');
}

bool isSmartSelectionIdentifierPart(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_')
        || ch == QLatin1Char('$');
}

SmartSelectionSpan smartSymbolSpanAt(
    const QString& text,
    int position)
{
    if (text.isEmpty())
        return {};

    int pos = qBound(0, position, text.size());
    if (pos >= text.size()
        || !isSmartSelectionIdentifierPart(text.at(pos))) {
        if (pos > 0
            && isSmartSelectionIdentifierPart(text.at(pos - 1))) {
            --pos;
        } else {
            return {};
        }
    }

    int start = pos;
    while (start > 0
           && isSmartSelectionIdentifierPart(text.at(start - 1))) {
        --start;
    }
    if (start >= text.size()
        || !isSmartSelectionIdentifierStart(text.at(start))) {
        return {};
    }

    int end = pos + 1;
    while (end < text.size()
           && isSmartSelectionIdentifierPart(text.at(end))) {
        ++end;
    }
    return {start, end};
}

SmartSelectionSpan smartIdentifierSpanEndingAt(
    const QString& text,
    int end)
{
    int pos = end - 1;
    if (pos < 0 || pos >= text.size()
        || !isSmartSelectionIdentifierPart(text.at(pos))) {
        return {};
    }

    int start = pos;
    while (start > 0
           && isSmartSelectionIdentifierPart(text.at(start - 1))) {
        --start;
    }
    if (!isSmartSelectionIdentifierStart(text.at(start)))
        return {};
    return {start, end};
}

SmartSelectionSpan smartIdentifierSpanStartingAt(
    const QString& text,
    int start)
{
    if (start < 0 || start >= text.size()
        || !isSmartSelectionIdentifierStart(text.at(start))) {
        return {};
    }

    int end = start + 1;
    while (end < text.size()
           && isSmartSelectionIdentifierPart(text.at(end))) {
        ++end;
    }
    return {start, end};
}

SmartSelectionSpan smartHierarchySpan(
    const QString& text,
    SmartSelectionSpan span)
{
    if (!span.isValid())
        return {};

    SmartSelectionSpan result = span;
    while (result.start >= 2
           && text.at(result.start - 1) == QLatin1Char('.')) {
        const SmartSelectionSpan previous =
            smartIdentifierSpanEndingAt(
                text, result.start - 1);
        if (!previous.isValid())
            break;
        result.start = previous.start;
    }
    while (result.end + 1 < text.size()
           && text.at(result.end) == QLatin1Char('.')) {
        const SmartSelectionSpan next =
            smartIdentifierSpanStartingAt(
                text, result.end + 1);
        if (!next.isValid())
            break;
        result.end = next.end;
    }
    return result == span ? SmartSelectionSpan{} : result;
}

SmartSelectionSpan smartParenthesizedContentSpan(
    const QString& text,
    SmartSelectionSpan currentSpan)
{
    if (text.isEmpty())
        return {};

    const int targetStart = currentSpan.isValid()
        ? currentSpan.start
        : qBound(0, currentSpan.start, text.size());
    const int targetEnd = currentSpan.isValid()
        ? currentSpan.end
        : targetStart;

    QList<SmartSelectionSpan> stack;
    SmartSelectionSpan best;
    for (int index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);
        if (ch == QLatin1Char('(')) {
            stack.append({index, index + 1});
            continue;
        }
        if (ch != QLatin1Char(')') || stack.isEmpty())
            continue;

        const SmartSelectionSpan opening = stack.takeLast();
        const SmartSelectionSpan content{
            opening.start + 1, index};
        if (!content.isValid()
            || content.start > targetStart
            || content.end < targetEnd
            || (currentSpan.isValid()
                && content == currentSpan)) {
            continue;
        }
        if (!best.isValid() || content.length() < best.length())
            best = content;
    }
    return best;
}

void applySmartSelectionSpan(
    MyCodeEditor* editor,
    SmartSelectionSpan span)
{
    if (!editor || !span.isValid())
        return;
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(span.start);
    cursor.setPosition(
        span.end, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
}

bool isStandaloneSmartIdentifier(const QString& text)
{
    if (text.isEmpty()
        || !isSmartSelectionIdentifierStart(text.at(0))) {
        return false;
    }
    for (int index = 1; index < text.size(); ++index) {
        if (!isSmartSelectionIdentifierPart(text.at(index)))
            return false;
    }
    return true;
}

QList<SmartSelectionSpan> smartIdentifierOccurrences(
    const QString& text,
    const QString& symbol)
{
    QList<SmartSelectionSpan> occurrences;
    if (!isStandaloneSmartIdentifier(symbol))
        return occurrences;

    int position = 0;
    while (position >= 0 && position < text.size()) {
        position = text.indexOf(
            symbol, position, Qt::CaseSensitive);
        if (position < 0)
            break;
        const int end = position + symbol.size();
        const bool leftBoundary =
            position == 0
            || !isSmartSelectionIdentifierPart(
                text.at(position - 1));
        const bool rightBoundary =
            end >= text.size()
            || !isSmartSelectionIdentifierPart(text.at(end));
        if (leftBoundary && rightBoundary)
            occurrences.append({position, end});
        position = end;
    }
    return occurrences;
}

bool isOccurrenceIdentifierStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_');
}

bool isOccurrenceIdentifierPart(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_')
        || ch == QLatin1Char('$');
}

int lineStartAt(const QString& text, int position)
{
    const int bounded = qBound(0, position, text.size());
    if (bounded == 0)
        return 0;
    const int newline = text.lastIndexOf(QLatin1Char('\n'), bounded - 1);
    return newline < 0 ? 0 : newline + 1;
}

int lineEndAfter(const QString& text, int position)
{
    const int bounded = qBound(0, position, text.size());
    const int newline = text.indexOf(QLatin1Char('\n'), bounded);
    return newline < 0 ? text.size() : newline;
}

void shiftOccurrenceTree(EditorOccurrenceNode* node, int delta)
{
    if (!node || delta == 0)
        return;
    node->position += delta;
    node->lazyShift += delta;
}

void pushOccurrenceShift(EditorOccurrenceNode* node)
{
    if (!node || node->lazyShift == 0)
        return;
    shiftOccurrenceTree(node->left, node->lazyShift);
    shiftOccurrenceTree(node->right, node->lazyShift);
    node->lazyShift = 0;
}

EditorOccurrenceNode* mergeOccurrenceTrees(EditorOccurrenceNode* left,
                                           EditorOccurrenceNode* right)
{
    if (!left) {
        if (right)
            right->parent = nullptr;
        return right;
    }
    if (!right) {
        left->parent = nullptr;
        return left;
    }

    if (left->priority < right->priority) {
        pushOccurrenceShift(left);
        left->right = mergeOccurrenceTrees(left->right, right);
        if (left->right)
            left->right->parent = left;
        left->parent = nullptr;
        return left;
    }

    pushOccurrenceShift(right);
    right->left = mergeOccurrenceTrees(left, right->left);
    if (right->left)
        right->left->parent = right;
    right->parent = nullptr;
    return right;
}

void splitOccurrenceTree(EditorOccurrenceNode* root,
                         int position,
                         EditorOccurrenceNode** left,
                         EditorOccurrenceNode** right)
{
    if (!root) {
        *left = nullptr;
        *right = nullptr;
        return;
    }

    pushOccurrenceShift(root);
    if (root->position < position) {
        splitOccurrenceTree(root->right,
                            position,
                            &root->right,
                            right);
        if (root->right)
            root->right->parent = root;
        root->parent = nullptr;
        *left = root;
        if (*right)
            (*right)->parent = nullptr;
        return;
    }

    splitOccurrenceTree(root->left,
                        position,
                        left,
                        &root->left);
    if (root->left)
        root->left->parent = root;
    root->parent = nullptr;
    *right = root;
    if (*left)
        (*left)->parent = nullptr;
}

EditorOccurrenceNode* insertOccurrenceNode(EditorOccurrenceNode* root,
                                           EditorOccurrenceNode* node)
{
    EditorOccurrenceNode* left = nullptr;
    EditorOccurrenceNode* right = nullptr;
    splitOccurrenceTree(root, node->position, &left, &right);
    return mergeOccurrenceTrees(
        mergeOccurrenceTrees(left, node), right);
}

void deactivateOccurrenceTree(
    EditorOccurrenceNode* node,
    QHash<QString, QSet<EditorOccurrenceNode*>>* index,
    std::vector<EditorOccurrenceNode*>* freeNodes,
    qsizetype* activeCount)
{
    if (!node)
        return;
    deactivateOccurrenceTree(node->left, index, freeNodes, activeCount);
    deactivateOccurrenceTree(node->right, index, freeNodes, activeCount);
    if (index) {
        auto wordIt = index->find(node->word);
        if (wordIt != index->end()) {
            wordIt.value().remove(node);
            if (wordIt.value().isEmpty())
                index->erase(wordIt);
        }
    }
    node->active = false;
    node->left = nullptr;
    node->right = nullptr;
    node->parent = nullptr;
    node->lazyShift = 0;
    if (activeCount && *activeCount > 0)
        --(*activeCount);
    if (freeNodes)
        freeNodes->push_back(node);
}

int currentOccurrencePosition(const EditorOccurrenceNode* node)
{
    if (!node)
        return -1;
    int position = node->position;
    for (const EditorOccurrenceNode* parent = node->parent;
         parent;
         parent = parent->parent) {
        position += parent->lazyShift;
    }
    return position;
}

std::uint32_t nextOccurrencePriority(std::uint32_t* state)
{
    std::uint32_t value = (*state += 0x9e3779b9u);
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

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
    const InsightEditorSemanticTokens& semantic =
        InsightVisualStyle::theme(
            dark ? ThemeMode::Dark : ThemeMode::Light)
            .editorSemantic;

    switch (role) {
    case SemanticDecorationRole::ModuleInterface:
        format.setForeground(semantic.moduleInterface);
        format.setFontWeight(QFont::Bold);
        break;
    case SemanticDecorationRole::PackageClassType:
        format.setForeground(semantic.packageClassType);
        format.setFontWeight(QFont::Bold);
        break;
    case SemanticDecorationRole::InstanceName:
        format.setForeground(semantic.instanceName);
        break;
    case SemanticDecorationRole::FormalPort:
        format.setForeground(semantic.formalPort);
        break;
    case SemanticDecorationRole::ModulePort:
        format.setForeground(semantic.modulePort);
        format.setFontWeight(QFont::DemiBold);
        break;
    case SemanticDecorationRole::ActualSignal:
        format.setForeground(semantic.actualSignal);
        break;
    case SemanticDecorationRole::Parameter:
        format.setForeground(semantic.parameter);
        break;
    case SemanticDecorationRole::EnumValue:
        format.setForeground(semantic.enumValue);
        format.setFontWeight(QFont::DemiBold);
        break;
    case SemanticDecorationRole::TypeAlias:
        format.setForeground(semantic.typeAlias);
        format.setFontWeight(QFont::Bold);
        break;
    case SemanticDecorationRole::Macro:
        format.setForeground(semantic.macro);
        break;
    case SemanticDecorationRole::SystemTask:
        format.setForeground(semantic.systemTask);
        break;
    case SemanticDecorationRole::InactivePreprocessorBranch:
        format.setForeground(semantic.inactiveText);
        format.setBackground(semantic.inactiveBackground);
        format.setProperty(QTextFormat::FullWidthSelection, true);
        break;
    }

    format.setProperty(kSemanticSelectionProperty, kSemanticSelectionMarker);
    return format;
}
}

EditorSelection::EditorSelection() = default;
EditorSelection::~EditorSelection() = default;

bool EditorSelection::expandSmartSelection(
    MyCodeEditor* editor,
    QString* message)
{
    if (message)
        message->clear();
    if (!editor || !editor->document()) {
        if (message) {
            *message = QStringLiteral(
                "No editor is available.");
        }
        return false;
    }

    const QString text = editor->toPlainText();
    const QTextCursor cursor = editor->textCursor();
    const SmartSelectionSpan current = cursor.hasSelection()
        ? SmartSelectionSpan{
              cursor.selectionStart(),
              cursor.selectionEnd()}
        : SmartSelectionSpan{
              cursor.position(),
              cursor.position()};

    SmartSelectionSpan target;
    if (!cursor.hasSelection()) {
        target = smartSymbolSpanAt(
            text, cursor.position());
    } else {
        const SmartSelectionSpan symbol =
            smartSymbolSpanAt(text, current.start);
        const SmartSelectionSpan hierarchy =
            symbol.isValid() && current == symbol
            ? smartHierarchySpan(text, symbol)
            : smartHierarchySpan(text, current);
        if (hierarchy.isValid()
            && hierarchy.contains(current)) {
            target = hierarchy;
        }
    }
    if (!target.isValid()) {
        target = smartParenthesizedContentSpan(
            text, current);
    }
    if (!target.isValid()) {
        if (message) {
            *message = QStringLiteral(
                "No larger structural selection is available.");
        }
        return false;
    }

    applySmartSelectionSpan(editor, target);
    if (message) {
        *message = QStringLiteral(
            "Expanded structural selection");
    }
    return true;
}

bool EditorSelection::navigateSelectedSymbolOccurrence(
    MyCodeEditor* editor,
    bool previous,
    QString* message)
{
    if (message)
        message->clear();
    if (!editor || !editor->document()) {
        if (message) {
            *message = QStringLiteral(
                "No editor is available.");
        }
        return false;
    }

    const QTextCursor cursor = editor->textCursor();
    const QString symbol = cursor.selectedText();
    if (!cursor.hasSelection()
        || !isStandaloneSmartIdentifier(symbol)) {
        if (message) {
            *message = QStringLiteral(
                "Select one SystemVerilog identifier.");
        }
        return false;
    }

    const QList<SmartSelectionSpan> occurrences =
        smartIdentifierOccurrences(
            editor->toPlainText(), symbol);
    if (occurrences.isEmpty()) {
        if (message) {
            *message = QStringLiteral(
                "No matching symbol occurrence is available.");
        }
        return false;
    }

    const int currentStart = cursor.selectionStart();
    SmartSelectionSpan target = previous
        ? occurrences.constLast()
        : occurrences.constFirst();
    if (previous) {
        for (int index = occurrences.size() - 1;
             index >= 0;
             --index) {
            if (occurrences.at(index).start < currentStart) {
                target = occurrences.at(index);
                break;
            }
        }
    } else {
        for (const SmartSelectionSpan& occurrence : occurrences) {
            if (occurrence.start > currentStart) {
                target = occurrence;
                break;
            }
        }
    }

    applySmartSelectionSpan(editor, target);
    editor->centerCursor();
    if (message) {
        *message = previous
            ? QStringLiteral("Previous occurrence of %1")
                  .arg(symbol)
            : QStringLiteral("Next occurrence of %1")
                  .arg(symbol);
    }
    return true;
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
    currentLine.format.setBackground(
        themeColorWithAlpha(
            InsightVisualStyle::theme().hover,
            24));
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
    commandSelection.format.setBackground(
        themeColorWithAlpha(
            InsightVisualStyle::theme().accent,
            205));
    commandSelection.format.setForeground(
        InsightVisualStyle::theme().button.textChecked);
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
    if (!editor
        || !editor->document()
        || target.text.isEmpty()
        || target.startPos < 0
        || target.endPos <= target.startPos) {
        return;
    }

    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    const int start = qBound(0, target.startPos, documentEnd);
    const int end = qBound(start, target.endPos, documentEnd);
    if (end <= start)
        return;

    QTextEdit::ExtraSelection highlight;
    highlight.cursor = QTextCursor(editor->document());
    highlight.cursor.setPosition(start);
    highlight.cursor.setPosition(
        end,
        QTextCursor::KeepAnchor);
    highlight.format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    highlight.format.setUnderlineColor(
        InsightVisualStyle::theme().accent);
    highlight.format.setForeground(
        InsightVisualStyle::theme().accent);
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
    const QList<SemanticDiagnostic>& diagnostics,
    int firstVisiblePosition,
    int endVisiblePosition)
{
    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kDiagnosticSelectionProperty,
            kDiagnosticSelectionMarker);

    QList<SemanticDiagnostic> ordered = diagnostics;
    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const SemanticDiagnostic& left,
           const SemanticDiagnostic& right) {
            // Later extra selections win when exact warning/error ranges
            // overlap, so errors are deliberately appended last.
            return left.severity < right.severity;
        });

    QSet<QString> seenRanges;
    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    for (const SemanticDiagnostic& diagnostic : std::as_const(ordered)) {
        if (diagnostic.severity != SemanticDiagnostic::Error
            && diagnostic.severity != SemanticDiagnostic::Warning) {
            continue;
        }

        for (const SemanticSourceRange& range : diagnostic.ranges) {
            if (range.position < 0 || range.length <= 0)
                continue;
            int start = qBound(0, range.position, documentEnd);
            int end = qBound(
                start,
                range.position + range.length,
                documentEnd);
            start = qMax(start, firstVisiblePosition);
            end = qMin(end, endVisiblePosition);
            if (end <= start)
                continue;

            const QString key = QStringLiteral("%1:%2:%3")
                                    .arg(start)
                                    .arg(end)
                                    .arg(static_cast<int>(
                                        diagnostic.severity));
            if (seenRanges.contains(key))
                continue;
            seenRanges.insert(key);

            QTextCursor underlineCursor(editor->document());
            underlineCursor.setPosition(start);
            underlineCursor.setPosition(end, QTextCursor::KeepAnchor);

            QTextEdit::ExtraSelection underlineSelection;
            underlineSelection.cursor = underlineCursor;
            underlineSelection.format.setUnderlineStyle(
                QTextCharFormat::WaveUnderline);
            underlineSelection.format.setUnderlineColor(
                diagnostic.severity == SemanticDiagnostic::Error
                    ? InsightVisualStyle::theme()
                          .syntax.errorUnderline
                    : InsightVisualStyle::theme()
                          .syntax.warningUnderline);
            underlineSelection.format.setProperty(
                kDiagnosticSelectionProperty,
                kDiagnosticSelectionMarker);
            selections.append(underlineSelection);
        }
    }

    clampSelectionsToDocument(editor->document(), selections);
    editor->setExtraSelections(selections);
}

void EditorSelection::highlightSemanticDecorations(
    MyCodeEditor* editor,
    const QList<SemanticDecoration>& decorations)
{
    if (!editor || !editor->document())
        return;

    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kSemanticSelectionProperty,
            kSemanticSelectionMarker);

    const bool dark = editorUsesDarkPalette(editor);
    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    for (const SemanticDecoration& decoration : decorations) {
        if (!decoration.isValid())
            continue;

        const int start = qBound(
            0, decoration.startPosition, documentEnd);
        const qint64 requestedEnd =
            static_cast<qint64>(decoration.startPosition)
            + static_cast<qint64>(decoration.length);
        const int end = requestedEnd >= documentEnd
            ? documentEnd
            : qBound(start,
                     static_cast<int>(requestedEnd),
                     documentEnd);
        if (end <= start)
            continue;

        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(editor->document());
        selection.cursor.setPosition(start);
        selection.cursor.setPosition(
            end,
            QTextCursor::KeepAnchor);
        selection.format = semanticFormatForRole(decoration.role, dark);
        selections.append(selection);
    }

    clampSelectionsToDocument(editor->document(), selections);
    editor->setExtraSelections(selections);
}

void EditorSelection::activateCurrentSymbolReferences(MyCodeEditor* editor)
{
    if (!editor)
        return;
    QTextCursor wordCursor = editor->textCursor();
    if (!wordCursor.hasSelection())
        wordCursor.select(QTextCursor::WordUnderCursor);
    const QString word = wordCursor.selectedText().trimmed();
    currentSymbolReferencesActive = word.size() >= 2
        && (word.at(0).isLetter() || word.at(0) == QLatin1Char('_'));
    currentSymbolReferenceWord = currentSymbolReferencesActive
        ? word
        : QString();
    highlightCurrentSymbolReferences(editor);
}

void EditorSelection::clearCurrentSymbolReferences(MyCodeEditor* editor)
{
    currentSymbolReferencesActive = false;
    currentSymbolReferenceWord.clear();
    highlightCurrentSymbolReferences(editor);
}

void EditorSelection::highlightCurrentSymbolReferences(MyCodeEditor* editor)
{
    if (!editor)
        return;
    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kCurrentSymbolSelectionProperty,
            kCurrentSymbolSelectionMarker);

    if (!currentSymbolReferencesActive
        || currentSymbolReferenceWord.isEmpty()) {
        editor->setExtraSelections(selections);
        return;
    }
    const QString word = currentSymbolReferenceWord;

    auto occurrenceIt = occurrenceIndex.find(word);
    if (!occurrenceIndexInitialized
        || occurrenceIt == occurrenceIndex.end()) {
        editor->setExtraSelections(selections);
        return;
    }

    const QSet<EditorOccurrenceNode*>& occurrences = occurrenceIt.value();
    int matchCount = 0;
    for (EditorOccurrenceNode* occurrence : occurrences) {
        if (matchCount >= kMaxPassiveMatchHighlights)
            break;
        const int position = currentOccurrencePosition(occurrence);
        const bool valid = occurrence && occurrence->active
            && occurrence->word == word
            && occurrence->length == word.size()
            && position >= 0
            && editor->cachedDocumentSlice(
                   position, occurrence->length) == word;
        Q_ASSERT_X(valid,
                   "EditorSelection::highlightCurrentSymbolReferences",
                   "occurrence index contains a stale node");
        if (!valid)
            continue;

        QTextEdit::ExtraSelection match;
        match.cursor = QTextCursor(editor->document());
        match.cursor.setPosition(position);
        match.cursor.setPosition(position + occurrence->length,
                                 QTextCursor::KeepAnchor);
        match.format.setBackground(
            themeColorWithAlpha(
                InsightVisualStyle::theme().accent,
                34));
        match.format.setProperty(
            kCurrentSymbolSelectionProperty,
            kCurrentSymbolSelectionMarker);
        selections.append(match);
        ++matchCount;
    }

    editor->setExtraSelections(selections);
}

void EditorSelection::rebuildOccurrenceIndex(MyCodeEditor* editor,
                                              const QString& text)
{
    Q_UNUSED(editor)
    occurrenceIndex.clear();
    freeOccurrenceNodes.clear();
    occurrenceNodes.clear();
    occurrenceRoot = nullptr;
    occurrencePrioritySeed = 0x9e3779b9u;
    activeOccurrenceCount = 0;
    occurrenceIndexInitialized = true;
    appendOccurrenceRange(text, 0, text.size());
}

void EditorSelection::resetDocumentText(MyCodeEditor* editor,
                                        const QString& text)
{
    currentSymbolReferencesActive = false;
    currentSymbolReferenceWord.clear();
    rebuildOccurrenceIndex(editor, text);
    highlightCurrentSymbolReferences(editor);
}

void EditorSelection::appendOccurrenceRange(const QString& text,
                                            int start,
                                            int end,
                                            int absoluteOffset)
{
    int position = qBound(0, start, text.size());
    const int limit = qBound(position, end, text.size());
    while (position < limit) {
        if (!isOccurrenceIdentifierStart(text.at(position))) {
            ++position;
            continue;
        }
        const int wordStart = position++;
        while (position < limit
               && isOccurrenceIdentifierPart(text.at(position))) {
            ++position;
        }
        if (position - wordStart < 2)
            continue;

        EditorOccurrenceNode* occurrencePtr = nullptr;
        if (!freeOccurrenceNodes.empty()) {
            occurrencePtr = freeOccurrenceNodes.back();
            freeOccurrenceNodes.pop_back();
        } else {
            auto occurrence = std::make_unique<EditorOccurrenceNode>();
            occurrencePtr = occurrence.get();
            occurrenceNodes.push_back(std::move(occurrence));
        }
        occurrencePtr->word = text.mid(wordStart, position - wordStart);
        occurrencePtr->position = absoluteOffset + wordStart;
        occurrencePtr->length = position - wordStart;
        occurrencePtr->lazyShift = 0;
        occurrencePtr->priority = nextOccurrencePriority(
            &occurrencePrioritySeed);
        occurrencePtr->left = nullptr;
        occurrencePtr->right = nullptr;
        occurrencePtr->parent = nullptr;
        occurrencePtr->active = true;
        occurrenceIndex[occurrencePtr->word].insert(occurrencePtr);
        ++activeOccurrenceCount;
        occurrenceRoot = insertOccurrenceNode(occurrenceRoot,
                                              occurrencePtr);
    }
}

OccurrenceChangeContext EditorSelection::prepareDocumentChange(
    const DocumentChange& change,
    const QString& oldText) const
{
    OccurrenceChangeContext context;
    context.rebuild = !occurrenceIndexInitialized
        || (change.position == 0
            && change.removedLength == change.oldLength);
    if (context.rebuild)
        return context;

    context.oldStart = lineStartAt(oldText, change.position);
    context.oldEnd = lineEndAfter(oldText, change.oldEnd());
    return context;
}

OccurrenceChangeContext EditorSelection::prepareDocumentLineChange(
    const DocumentChange& change,
    int oldLineStart,
    int oldLineEnd) const
{
    OccurrenceChangeContext context;
    context.rebuild = !occurrenceIndexInitialized
        || (change.position == 0
            && change.removedLength == change.oldLength);
    if (context.rebuild)
        return context;

    context.oldStart = qMax(0, oldLineStart);
    context.oldEnd = qMax(context.oldStart, oldLineEnd);
    return context;
}

void EditorSelection::removeOccurrenceRangeAndShift(
    const OccurrenceChangeContext& context,
    int characterDelta)
{
    EditorOccurrenceNode* before = nullptr;
    EditorOccurrenceNode* affectedAndAfter = nullptr;
    splitOccurrenceTree(occurrenceRoot,
                        context.oldStart,
                        &before,
                        &affectedAndAfter);
    EditorOccurrenceNode* affected = nullptr;
    EditorOccurrenceNode* after = nullptr;
    splitOccurrenceTree(affectedAndAfter,
                        context.oldEnd,
                        &affected,
                        &after);
    deactivateOccurrenceTree(affected,
                             &occurrenceIndex,
                             &freeOccurrenceNodes,
                             &activeOccurrenceCount);
    shiftOccurrenceTree(after, characterDelta);
    occurrenceRoot = mergeOccurrenceTrees(before, after);
}

OccurrenceIndexUpdate EditorSelection::applyDocumentChange(
    MyCodeEditor* editor,
    const DocumentChange& change,
    const OccurrenceChangeContext& context,
    const QString& newText)
{
    if (context.rebuild) {
        rebuildOccurrenceIndex(editor, newText);
        return OccurrenceIndexUpdate::Full;
    }

    removeOccurrenceRangeAndShift(context,
                                  change.characterDelta());

    const int newStart = lineStartAt(newText, change.position);
    const int newEnd = lineEndAfter(newText, change.newEnd());
    appendOccurrenceRange(newText, newStart, newEnd);
    return OccurrenceIndexUpdate::Incremental;
}

OccurrenceIndexUpdate EditorSelection::applyDocumentLineChange(
    MyCodeEditor* editor,
    const DocumentChange& change,
    const OccurrenceChangeContext& context,
    int newLineStart,
    const QString& newLineText)
{
    if (context.rebuild || !editor || newLineStart < 0
        || newLineStart + newLineText.size() > change.newLength) {
        if (!editor)
            return OccurrenceIndexUpdate::None;
        rebuildOccurrenceIndex(editor, editor->cachedDocumentText());
        return OccurrenceIndexUpdate::Full;
    }

    removeOccurrenceRangeAndShift(context,
                                  change.characterDelta());
    appendOccurrenceRange(newLineText,
                          0,
                          newLineText.size(),
                          newLineStart);
    return OccurrenceIndexUpdate::Incremental;
}

EditorOccurrenceIndexStats EditorSelection::occurrenceIndexStatsForTest() const
{
    EditorOccurrenceIndexStats stats;
    for (auto it = occurrenceIndex.cbegin(); it != occurrenceIndex.cend(); ++it)
        stats.handleCount += it.value().size();
    stats.allocatedNodeCount =
        static_cast<qsizetype>(occurrenceNodes.size());
    stats.activeNodeCount = activeOccurrenceCount;
    stats.freeNodeCount =
        static_cast<qsizetype>(freeOccurrenceNodes.size());
    return stats;
}

QList<int> EditorSelection::occurrencePositionsForTest(
    const QString& word) const
{
    QList<int> positions;
    const auto found = occurrenceIndex.constFind(word);
    if (found == occurrenceIndex.constEnd())
        return positions;

    positions.reserve(found.value().size());
    for (const EditorOccurrenceNode* occurrence : found.value()) {
        if (occurrence && occurrence->active)
            positions.append(currentOccurrencePosition(occurrence));
    }
    std::sort(positions.begin(), positions.end());
    return positions;
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
        match.format.setBackground(
            themeColorWithAlpha(
                InsightVisualStyle::theme().warning,
                52));
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

void EditorSelection::highlightKeywordPair(
    MyCodeEditor* editor,
    const TSKeywordPairTarget& target)
{
    if (!editor || !editor->document())
        return;
    if (!target.ok()) {
        clearKeywordPair(editor);
        return;
    }

    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kKeywordPairSelectionProperty,
            kKeywordPairSelectionMarker);
    const QList<QPair<int, int>> ranges = {
        {target.openingStartChar, target.openingEndChar},
        {target.closingStartChar, target.closingEndChar},
    };
    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    for (const QPair<int, int>& range : ranges) {
        const int start = qBound(0, range.first, documentEnd);
        const int end = qBound(start, range.second, documentEnd);
        if (end <= start)
            continue;

        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(editor->document());
        selection.cursor.setPosition(start);
        selection.cursor.setPosition(end, QTextCursor::KeepAnchor);
        selection.format.setBackground(
            themeColorWithAlpha(
                InsightVisualStyle::theme().syntax.structuralPair,
                58));
        selection.format.setUnderlineStyle(
            QTextCharFormat::SingleUnderline);
        selection.format.setUnderlineColor(
            InsightVisualStyle::theme().syntax.structuralPair);
        selection.format.setFontWeight(QFont::DemiBold);
        selection.format.setProperty(
            kKeywordPairSelectionProperty,
            kKeywordPairSelectionMarker);
        selections.append(selection);
    }
    editor->setExtraSelections(selections);
}

void EditorSelection::clearKeywordPair(QPlainTextEdit* editor)
{
    removeByProperty(
        editor,
        kKeywordPairSelectionProperty,
        kKeywordPairSelectionMarker);
}

void EditorSelection::highlightSignalSelections(
    MyCodeEditor* editor,
    const QList<QPair<int, int>>& ranges)
{
    if (!editor || !editor->document())
        return;
    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kSignalSelectionProperty,
            kSignalSelectionMarker);
    const QColor accent =
        editor->palette().color(QPalette::Highlight);
    QColor background = accent;
    background.setAlpha(46);
    QColor underline = accent;
    underline.setAlpha(150);
    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    for (const QPair<int, int>& range : ranges) {
        if (range.first < 0 || range.second <= 0)
            continue;
        const int start = qBound(0, range.first, documentEnd);
        const qint64 requestedEnd =
            static_cast<qint64>(range.first)
            + static_cast<qint64>(range.second);
        const int end = requestedEnd >= documentEnd
            ? documentEnd
            : qBound(start,
                     static_cast<int>(requestedEnd),
                     documentEnd);
        if (end <= start)
            continue;
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(editor->document());
        selection.cursor.setPosition(start);
        selection.cursor.setPosition(
            end,
            QTextCursor::KeepAnchor);
        selection.format.setBackground(background);
        selection.format.setUnderlineStyle(
            QTextCharFormat::DotLine);
        selection.format.setUnderlineColor(underline);
        selection.format.setProperty(
            kSignalSelectionProperty,
            kSignalSelectionMarker);
        selections.append(selection);
    }
    clampSelectionsToDocument(editor->document(), selections);
    editor->setExtraSelections(selections);
}

void EditorSelection::clearSignalSelections(
    QPlainTextEdit* editor)
{
    removeByProperty(editor,
                     kSignalSelectionProperty,
                     kSignalSelectionMarker);
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
    flash.format.setBackground(
        themeColorWithAlpha(
            InsightVisualStyle::theme().accent,
            72));
    flash.format.setProperty(QTextFormat::FullWidthSelection, true);
    flash.format.setProperty(kFlashSelectionProperty, kFlashSelectionMarker);
    selections.append(flash);
    editor->setExtraSelections(selections);

    const QPointer<QPlainTextEdit> guardedEditor(editor);
    QTimer::singleShot(650, editor, [guardedEditor]() {
        if (guardedEditor)
            EditorSelection::removeByProperty(
                guardedEditor,
                kFlashSelectionProperty,
                kFlashSelectionMarker);
    });
}

void EditorSelection::flashRange(MyCodeEditor* editor,
                                 int startChar,
                                 int endChar)
{
    if (!editor || !editor->document())
        return;
    const int documentEnd = qMax(
        0, editor->document()->characterCount() - 1);
    const int start = qBound(0, startChar, documentEnd);
    const int end = qBound(start, endChar, documentEnd);
    if (end <= start)
        return;

    QList<QTextEdit::ExtraSelection> selections =
        editorSelectionsWithout(
            editor,
            kFlashSelectionProperty,
            kFlashSelectionMarker);
    QTextEdit::ExtraSelection flash;
    flash.cursor = QTextCursor(editor->document());
    flash.cursor.setPosition(start);
    flash.cursor.setPosition(end, QTextCursor::KeepAnchor);
    flash.format.setBackground(
        themeColorWithAlpha(
            InsightVisualStyle::theme().accent,
            72));
    flash.format.setProperty(
        kFlashSelectionProperty,
        kFlashSelectionMarker);
    selections.append(flash);
    editor->setExtraSelections(selections);

    const QPointer<QPlainTextEdit> guardedEditor(editor);
    QTimer::singleShot(450, editor, [guardedEditor]() {
        if (guardedEditor) {
            EditorSelection::removeByProperty(
                guardedEditor,
                kFlashSelectionProperty,
                kFlashSelectionMarker);
        }
    });
}

void EditorHighlightRefresh::attachToEditor(
    MyCodeEditor* editor,
    const std::function<void()>& refresh)
{
    detach();
    refreshHandler = refresh;
    if (!editor)
        return;
    cursorConnection = QObject::connect(
        editor,
        &QPlainTextEdit::cursorPositionChanged,
        editor,
        [this]() { schedule(); });
}

void EditorHighlightRefresh::detach()
{
    QObject::disconnect(cursorConnection);
    cursorConnection = {};
    refreshHandler = {};
}

void EditorHighlightRefresh::schedule() const
{
    if (refreshHandler)
        refreshHandler();
}
