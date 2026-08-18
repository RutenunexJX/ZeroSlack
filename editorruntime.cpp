#include "editorruntime.h"

#include "mycodeeditor.h"

#include "actionregistry.h"
#include "editorcontextmenumodel.h"
#include "editorhoverpopup.h"
#include "editorlexicalboundary.h"
#include "formattercursoranchor.h"
#include "insightvisualstyle.h"
#include "rtlbatcheditservice.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QHash>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPolygon>
#include <QPointer>
#include <QPushButton>
#include <QRect>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>
#include <QTimer>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "formatterservice.h"
#include "tsdocument.h"

namespace {
constexpr int kManualIndentWidth = 4;
constexpr int kDiagnosticOverviewBucketCount = 1024;
constexpr const char* kDiagnosticsEmptyProperty =
    "zeroslackDiagnosticsSelectionsEmpty";
constexpr const char* kSemanticDecorationsEmptyProperty =
    "zeroslackSemanticDecorationsEmpty";

int lifecycleDiagnosticRankFromEnvironment()
{
    const QString profile = qEnvironmentVariable(
        "ZEROSLACK_EDITOR_LIFECYCLE_PROFILE");
    static const QStringList profiles = {
        QStringLiteral("bare"),
        QStringLiteral("semantic"),
        QStringLiteral("syntax-state"),
        QStringLiteral("gutter-core"),
        QStringLiteral("modes"),
        QStringLiteral("connections"),
        QStringLiteral("appearance"),
        QStringLiteral("highlighter"),
        QStringLiteral("workflow"),
        QStringLiteral("completion"),
        QStringLiteral("current-line"),
        QStringLiteral("folding"),
        QStringLiteral("derived-state"),
        QStringLiteral("full")
    };
    return profiles.indexOf(profile);
}

int changeDiagnosticRankFromEnvironment()
{
    const QString profile = qEnvironmentVariable(
        "ZEROSLACK_EDITOR_CHANGE_PROFILE");
    static const QStringList profiles = {
        QStringLiteral("prepare"),
        QStringLiteral("syntax"),
        QStringLiteral("folding"),
        QStringLiteral("decorations"),
        QStringLiteral("occurrences"),
        QStringLiteral("presentation"),
        QStringLiteral("full")
    };
    return profiles.indexOf(profile);
}

void lifecycleTrace(const char* marker)
{
    if (!qEnvironmentVariableIsSet(
            "ZEROSLACK_EDITOR_LIFECYCLE_TRACE")) {
        return;
    }
    std::fprintf(stderr, "lifecycle.%s\n", marker);
    std::fflush(stderr);
}

QString declareSignalClassText(
    DeclareSignalProposalClass classification)
{
    switch (classification) {
    case DeclareSignalProposalClass::Exact:
        return QStringLiteral("Exact");
    case DeclareSignalProposalClass::Inferred:
        return QStringLiteral("Inferred");
    case DeclareSignalProposalClass::Uncertain:
        return QStringLiteral("Uncertain");
    case DeclareSignalProposalClass::Conflict:
        return QStringLiteral("Conflict");
    }
    return QStringLiteral("Uncertain");
}

QString declareSignalCandidateKindText(
    const DeclareSignalCandidate& candidate)
{
    const QString scope =
        candidate.scopeKind
                == DeclareSignalScopeKind::BlockLocal
            ? QStringLiteral("block-local")
            : QStringLiteral("module");
    const QString object =
        candidate.objectKind
                == DeclareSignalObjectKind::Net
            ? QStringLiteral("net")
            : QStringLiteral("variable");
    return QStringLiteral("%1 %2").arg(scope, object);
}

QString firstDeclareSignalIssue(
    const DeclareSignalFactCollectionResult& facts,
    const DeclareSignalProposal& proposal)
{
    for (const DeclareSignalFactCollectionIssue& issue :
         facts.issues) {
        if (!issue.message.isEmpty())
            return issue.message;
    }
    for (const DeclareSignalIssue& issue : proposal.issues) {
        if (!issue.message.isEmpty())
            return issue.message;
    }
    return QString();
}

bool sameSemanticSnapshotToken(
    const SemanticSnapshotToken& left,
    const SemanticSnapshotToken& right)
{
    return left.isValid()
        && right.isValid()
        && left.revision == right.revision
        && left.snapshot == right.snapshot;
}

class InsertionPositionMapper final
    : public FormatterPositionMapper
{
public:
    InsertionPositionMapper(int insertionPosition,
                            int insertedLength)
        : insertionPosition(qMax(0, insertionPosition))
        , insertedLength(qMax(0, insertedLength))
    {
    }

    FormatterLogicalPosition capturePosition(
        int oldPosition,
        FormatterPositionAffinity affinity) const override
    {
        FormatterLogicalPosition result;
        result.absoluteFallback = qMax(0, oldPosition);
        result.affinity = affinity;
        return result;
    }

    int restorePosition(
        const FormatterLogicalPosition& position,
        int newDocumentLength) const override
    {
        if (position.absoluteFallback < 0)
            return -1;
        const int mapped =
            position.absoluteFallback >= insertionPosition
            ? position.absoluteFallback + insertedLength
            : position.absoluteFallback;
        return qBound(0, mapped, qMax(0, newDocumentLength));
    }

private:
    int insertionPosition = 0;
    int insertedLength = 0;
};

bool hasDeclareSignalIssue(
    const DeclareSignalProposal& proposal,
    DeclareSignalIssueCode code)
{
    for (const DeclareSignalIssue& issue : proposal.issues) {
        if (issue.code == code)
            return true;
    }
    return false;
}

QColor diagnosticSeverityColor(SemanticDiagnostic::Severity severity)
{
    return severity == SemanticDiagnostic::Error
        ? InsightVisualStyle::theme().syntax.errorUnderline
        : InsightVisualStyle::theme().warning;
}

SemanticDiagnostic::Severity annotationDiagnosticSeverity(
    const EditorAnnotation& annotation)
{
    bool valid = false;
    const int value = annotation.detail.toInt(&valid);
    if (!valid
        || value < static_cast<int>(SemanticDiagnostic::Info)
        || value > static_cast<int>(SemanticDiagnostic::Error)) {
        return SemanticDiagnostic::Info;
    }
    return static_cast<SemanticDiagnostic::Severity>(value);
}

QString insertedDocumentText(QTextDocument* document,
                             int position,
                             int length)
{
    if (!document || length <= 0)
        return QString();
    const int documentEnd = qMax(0, document->characterCount() - 1);
    const int start = qBound(0, position, documentEnd);
    const int end = qBound(start, position + length, documentEnd);
    QTextCursor cursor(document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

bool samePackageAvailability(const EditorPackageToolAvailability& left,
                             const EditorPackageToolAvailability& right)
{
    return left.available == right.available
        && left.packageName == right.packageName
        && left.failureMessage == right.failureMessage;
}

QString wavePreviewScopeKey(const MyCodeEditorState& state,
                            const MyCodeEditor* editor)
{
    const EditorAlwaysScopeTarget always =
        state.currentAlwaysScopeTarget(editor);
    if (always.ok()) {
        return QStringLiteral("always:%1:%2:%3")
            .arg(always.startPosition)
            .arg(always.endPosition)
            .arg(always.label);
    }
    const EditorModuleScopeTarget module =
        state.currentModuleScopeTarget(editor);
    if (module.ok()) {
        return QStringLiteral("module:%1:%2:%3")
            .arg(module.startPosition)
            .arg(module.endPosition)
            .arg(module.label);
    }
    return QStringLiteral("unavailable:%1").arg(module.failureMessage);
}

struct TextSpan {
    int start = -1;
    int end = -1;

    bool isValid() const { return start >= 0 && end > start; }
    int length() const { return end - start; }
    bool contains(const TextSpan& other) const
    {
        return isValid() && other.isValid()
            && start <= other.start && end >= other.end;
    }
    bool operator==(const TextSpan& other) const
    {
        return start == other.start && end == other.end;
    }
};

bool hasCommandModifier(QKeyEvent* event)
{
    if (!event)
        return false;
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    return modifiers.testFlag(Qt::ControlModifier)
        || modifiers.testFlag(Qt::AltModifier)
        || modifiers.testFlag(Qt::MetaModifier);
}

bool isUnsignedIntegerText(const QString& text)
{
    if (text.isEmpty())
        return false;
    for (const QChar ch : text) {
        if (!ch.isDigit())
            return false;
    }
    return true;
}

bool isSmartIdentifierStart(QChar ch)
{
    return ch.isLetter() || ch == QLatin1Char('_')
        || ch == QLatin1Char('$');
}

bool isSmartIdentifierPart(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_')
        || ch == QLatin1Char('$');
}

TextSpan symbolSpanAt(const QString& text, int position)
{
    if (text.isEmpty())
        return {};

    int pos = qBound(0, position, text.size());
    if (pos >= text.size() || !isSmartIdentifierPart(text.at(pos))) {
        if (pos > 0 && isSmartIdentifierPart(text.at(pos - 1)))
            --pos;
        else
            return {};
    }

    int start = pos;
    while (start > 0 && isSmartIdentifierPart(text.at(start - 1)))
        --start;
    if (start >= text.size() || !isSmartIdentifierStart(text.at(start)))
        return {};

    int end = pos + 1;
    while (end < text.size() && isSmartIdentifierPart(text.at(end)))
        ++end;
    return {start, end};
}

bool isStandaloneIdentifierText(const QString& text)
{
    if (text.isEmpty() || !isSmartIdentifierStart(text.at(0)))
        return false;
    for (int i = 1; i < text.size(); ++i) {
        if (!isSmartIdentifierPart(text.at(i)))
            return false;
    }
    return true;
}

bool matchesRegisteredShortcut(
    const QKeyEvent* event,
    const QString& actionId);

bool handleSafeRename(MyCodeEditorState* state,
                      MyCodeEditor* editor,
                      QKeyEvent* event)
{
    if (!state || !editor || !event
        || !matchesRegisteredShortcut(
            event,
            QString::fromLatin1(ActionIds::RtlRename))) {
        return false;
    }

    QString failureReason;
    if (!state->beginSemanticRenameEditor(
            editor, &failureReason)) {
        if (!failureReason.isEmpty()) {
            emit editor->editorStatusMessageRequested(
                failureReason);
        }
        return false;
    }
    event->accept();
    return true;
}

QString rangeFromBracketInnerText(const QString& innerText)
{
    const QString trimmed = innerText.trimmed();
    if (trimmed.isEmpty())
        return QString();

    if (trimmed.contains(QLatin1Char(':')))
        return QStringLiteral("[%1]").arg(trimmed);

    if (isUnsignedIntegerText(trimmed)) {
        const int width = trimmed.toInt();
        if (width <= 0)
            return QString();
        return QStringLiteral("[%1:0]").arg(width - 1);
    }

    return QStringLiteral("[%1 - 1:0]").arg(trimmed);
}

bool bracketRangeAroundCursor(const QTextCursor& cursor,
                              int* rangeStart,
                              int* rangeEnd)
{
    if (!cursor.block().isValid())
        return false;

    const QString line = cursor.block().text();
    const int column = cursor.position() - cursor.block().position();

    int left = -1;
    for (int i = qMin(column - 1, line.size() - 1); i >= 0; --i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char(']'))
            return false;
        if (ch == QLatin1Char('[')) {
            left = i;
            break;
        }
    }
    if (left < 0)
        return false;

    int right = -1;
    for (int i = qMax(column, left + 1); i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('['))
            return false;
        if (ch == QLatin1Char(']')) {
            right = i;
            break;
        }
    }
    if (right <= left + 1)
        return false;

    if (rangeStart)
        *rangeStart = cursor.block().position() + left;
    if (rangeEnd)
        *rangeEnd = cursor.block().position() + right + 1;
    return true;
}

bool matchesRegisteredShortcut(
    const QKeyEvent* event,
    const QString& actionId)
{
    if (!event)
        return false;
    const QString shortcutText =
        effectiveActionShortcut(actionId);
    if (shortcutText.isEmpty())
        return false;
    const QKeySequence shortcut =
        QKeySequence::fromString(
            shortcutText,
            QKeySequence::PortableText);
    const QKeySequence pressed(
        event->keyCombination());
    return shortcut.matches(pressed)
        == QKeySequence::ExactMatch;
}

bool requestRegisteredEditorAction(
    MyCodeEditor* editor,
    const QString& actionId,
    const QVariantMap& parameters = {})
{
    if (!editor)
        return false;
    bool handled = false;
    emit editor->registeredActionRequested(
        actionId, parameters, &handled);
    return handled;
}

bool selectedFullLineRange(MyCodeEditor* editor, int* rangeStart, int* rangeEnd)
{
    if (!editor)
        return false;

    const QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection() || cursor.selectionEnd() <= cursor.selectionStart())
        return false;

    QTextDocument* document = editor->document();
    if (!document)
        return false;

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    const QTextBlock startBlock = document->findBlock(selectionStart);
    const QTextBlock endBlock =
        document->findBlock(std::max(selectionStart, selectionEnd - 1));
    if (!startBlock.isValid() || !endBlock.isValid())
        return false;

    const int start = startBlock.position();
    const int documentEnd = std::max(0, document->characterCount() - 1);
    const int end = std::min(endBlock.position() + endBlock.length(),
                             documentEnd);
    if (end <= start)
        return false;

    if (rangeStart)
        *rangeStart = start;
    if (rangeEnd)
        *rangeEnd = end;
    return true;
}

int lineStartOffset(const QString& text, int lineNumber)
{
    if (lineNumber <= 0)
        return 0;
    int offset = 0;
    for (int line = 0; line < lineNumber; ++line) {
        const int newline = text.indexOf(QLatin1Char('\n'), offset);
        if (newline < 0)
            return text.size();
        offset = newline + 1;
    }
    return offset;
}

QString formattedLineSlice(const QString& formattedText,
                           int firstLine,
                           int lastLine,
                           bool includeTrailingNewline)
{
    const int start = lineStartOffset(formattedText, firstLine);
    const int lastStart = lineStartOffset(formattedText, lastLine);
    const int newline = formattedText.indexOf(
        QLatin1Char('\n'), lastStart);
    const int end = newline < 0
        ? formattedText.size()
        : newline + (includeTrailingNewline ? 1 : 0);
    return formattedText.mid(start, qMax(0, end - start));
}

struct TouchedLineRange {
    int firstLine = -1;
    int lastLine = -1;
    int currentLine = -1;
    int currentColumn = 0;
    bool hadSelection = false;
};

TouchedLineRange touchedLineRange(MyCodeEditor* editor)
{
    TouchedLineRange range;
    if (!editor || !editor->document())
        return range;

    const QTextCursor cursor = editor->textCursor();
    range.currentLine = cursor.block().blockNumber();
    range.currentColumn =
        cursor.block().isValid()
            ? qMax(0, cursor.position() - cursor.block().position())
            : 0;
    range.hadSelection =
        cursor.hasSelection() && cursor.selectionEnd() > cursor.selectionStart();

    if (!range.hadSelection) {
        range.firstLine = range.currentLine;
        range.lastLine = range.currentLine;
        return range;
    }

    const int selectionStart = cursor.selectionStart();
    int adjustedEnd = cursor.selectionEnd();
    const QTextBlock endAtBlock =
        editor->document()->findBlock(adjustedEnd);
    if (endAtBlock.isValid()
        && adjustedEnd == endAtBlock.position()
        && adjustedEnd > selectionStart) {
        --adjustedEnd;
    } else {
        --adjustedEnd;
    }

    const QTextBlock firstBlock =
        editor->document()->findBlock(selectionStart);
    const QTextBlock lastBlock =
        editor->document()->findBlock(qMax(selectionStart, adjustedEnd));
    if (!firstBlock.isValid() || !lastBlock.isValid())
        return {};

    range.firstLine = firstBlock.blockNumber();
    range.lastLine = lastBlock.blockNumber();
    return range;
}

int leadingWhitespaceCount(const QString& text)
{
    int count = 0;
    while (count < text.size() && text.at(count).isSpace())
        ++count;
    return count;
}

int lineEndPosition(QTextDocument* document, int line)
{
    if (!document)
        return -1;
    const QTextBlock block = document->findBlockByNumber(line);
    if (!block.isValid())
        return -1;
    return block.position() + block.text().size();
}

void restoreLineActionCursor(MyCodeEditor* editor,
                             const TouchedLineRange& range,
                             const QList<int>& removedByLine,
                             bool commentAction)
{
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return;

    QTextDocument* document = editor->document();
    if (!document)
        return;

    if (range.hadSelection) {
        const QTextBlock firstBlock =
            document->findBlockByNumber(range.firstLine);
        if (!firstBlock.isValid())
            return;
        const int end = lineEndPosition(document, range.lastLine);
        if (end < firstBlock.position())
            return;
        QTextCursor cursor(document);
        cursor.setPosition(firstBlock.position());
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        return;
    }

    const QTextBlock currentBlock =
        document->findBlockByNumber(range.currentLine);
    if (!currentBlock.isValid())
        return;

    int nextColumn = range.currentColumn;
    if (commentAction) {
        const int indent = leadingWhitespaceCount(currentBlock.text());
        if (nextColumn >= indent)
            nextColumn += 3;
    } else if (range.currentLine >= range.firstLine
               && range.currentLine <= range.lastLine) {
        const int removed =
            removedByLine.value(range.currentLine - range.firstLine);
        if (removed > 0) {
            const int indent = leadingWhitespaceCount(currentBlock.text());
            if (nextColumn >= indent + removed)
                nextColumn -= removed;
            else if (nextColumn > indent)
                nextColumn = indent;
        }
    }

    QTextCursor cursor(document);
    cursor.setPosition(currentBlock.position()
                       + qMin(nextColumn, currentBlock.text().size()));
    editor->setTextCursor(cursor);
}

bool applyLineComment(MyCodeEditor* editor)
{
    const TouchedLineRange range = touchedLineRange(editor);
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return false;

    QTextDocument* document = editor->document();
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (int line = range.lastLine; line >= range.firstLine; --line) {
        const QTextBlock block = document->findBlockByNumber(line);
        if (!block.isValid())
            continue;
        const int insertPos =
            block.position() + leadingWhitespaceCount(block.text());
        cursor.setPosition(insertPos);
        cursor.insertText(QStringLiteral("// "));
    }
    cursor.endEditBlock();

    restoreLineActionCursor(editor, range, {}, true);
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Commented %1 line%2")
            .arg(range.lastLine - range.firstLine + 1)
            .arg(range.lastLine == range.firstLine ? QString()
                                                   : QStringLiteral("s")));
    return true;
}

bool applyLineUncomment(MyCodeEditor* editor)
{
    const TouchedLineRange range = touchedLineRange(editor);
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return false;

    QTextDocument* document = editor->document();
    QTextCursor cursor(document);
    QList<int> removedByLine;
    removedByLine.reserve(range.lastLine - range.firstLine + 1);

    int changedLines = 0;
    cursor.beginEditBlock();
    for (int line = range.lastLine; line >= range.firstLine; --line) {
        const QTextBlock block = document->findBlockByNumber(line);
        if (!block.isValid()) {
            removedByLine.prepend(0);
            continue;
        }

        const QString text = block.text();
        const int indent = leadingWhitespaceCount(text);
        int removed = 0;
        if (text.mid(indent).startsWith(QStringLiteral("//"))) {
            removed = 2;
            if (indent + removed < text.size()
                && text.at(indent + removed) == QLatin1Char(' ')) {
                ++removed;
            }
            cursor.setPosition(block.position() + indent);
            cursor.setPosition(block.position() + indent + removed,
                               QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            ++changedLines;
        }
        removedByLine.prepend(removed);
    }
    cursor.endEditBlock();

    restoreLineActionCursor(editor, range, removedByLine, false);
    emit editor->editorStatusMessageRequested(
        changedLines > 0
            ? QStringLiteral("Uncommented %1 line%2")
                  .arg(changedLines)
                  .arg(changedLines == 1 ? QString()
                                         : QStringLiteral("s"))
            : QStringLiteral("No line comments to uncomment"));
    return changedLines > 0;
}

int leadingUnindentCount(const QString& text)
{
    if (text.startsWith(QLatin1Char('\t')))
        return 1;

    int count = 0;
    while (count < text.size()
           && count < kManualIndentWidth
           && text.at(count) == QLatin1Char(' ')) {
        ++count;
    }
    return count;
}

void restoreLinePrefixActionCursor(MyCodeEditor* editor,
                                   const TouchedLineRange& range,
                                   const QList<int>& insertedByLine,
                                   const QList<int>& removedByLine)
{
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return;

    QTextDocument* document = editor->document();
    if (!document)
        return;

    if (range.hadSelection) {
        const QTextBlock firstBlock =
            document->findBlockByNumber(range.firstLine);
        if (!firstBlock.isValid())
            return;
        const int end = lineEndPosition(document, range.lastLine);
        if (end < firstBlock.position())
            return;
        QTextCursor cursor(document);
        cursor.setPosition(firstBlock.position());
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        return;
    }

    const QTextBlock currentBlock =
        document->findBlockByNumber(range.currentLine);
    if (!currentBlock.isValid())
        return;

    const int index = range.currentLine - range.firstLine;
    const int inserted = insertedByLine.value(index);
    const int removed = removedByLine.value(index);
    int nextColumn = range.currentColumn + inserted;
    if (removed > 0)
        nextColumn = range.currentColumn >= removed
            ? range.currentColumn - removed
            : 0;

    QTextCursor cursor(document);
    cursor.setPosition(currentBlock.position()
                       + qMin(nextColumn, currentBlock.text().size()));
    editor->setTextCursor(cursor);
}

bool applyLineIndent(MyCodeEditor* editor)
{
    const TouchedLineRange range = touchedLineRange(editor);
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return false;

    QTextDocument* document = editor->document();
    QTextCursor cursor(document);
    QList<int> insertedByLine;
    insertedByLine.reserve(range.lastLine - range.firstLine + 1);

    int changedLines = 0;
    cursor.beginEditBlock();
    for (int line = range.lastLine; line >= range.firstLine; --line) {
        const QTextBlock block = document->findBlockByNumber(line);
        if (!block.isValid()) {
            insertedByLine.prepend(0);
            continue;
        }
        cursor.setPosition(block.position());
        cursor.insertText(QString(kManualIndentWidth, QLatin1Char(' ')));
        insertedByLine.prepend(kManualIndentWidth);
        ++changedLines;
    }
    cursor.endEditBlock();

    restoreLinePrefixActionCursor(editor, range, insertedByLine, {});
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Indented %1 line%2")
            .arg(changedLines)
            .arg(changedLines == 1 ? QString() : QStringLiteral("s")));
    return changedLines > 0;
}

bool applyLineUnindent(MyCodeEditor* editor)
{
    const TouchedLineRange range = touchedLineRange(editor);
    if (!editor || range.firstLine < 0 || range.lastLine < range.firstLine)
        return false;

    QTextDocument* document = editor->document();
    QTextCursor cursor(document);
    QList<int> removedByLine;
    removedByLine.reserve(range.lastLine - range.firstLine + 1);

    int changedLines = 0;
    cursor.beginEditBlock();
    for (int line = range.lastLine; line >= range.firstLine; --line) {
        const QTextBlock block = document->findBlockByNumber(line);
        if (!block.isValid()) {
            removedByLine.prepend(0);
            continue;
        }

        const int removed = leadingUnindentCount(block.text());
        if (removed > 0) {
            cursor.setPosition(block.position());
            cursor.setPosition(block.position() + removed,
                               QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            ++changedLines;
        }
        removedByLine.prepend(removed);
    }
    cursor.endEditBlock();

    restoreLinePrefixActionCursor(editor, range, {}, removedByLine);
    emit editor->editorStatusMessageRequested(
        changedLines > 0
            ? QStringLiteral("Unindented %1 line%2")
                  .arg(changedLines)
                  .arg(changedLines == 1 ? QString()
                                         : QStringLiteral("s"))
            : QStringLiteral("No indentation to remove"));
    return changedLines > 0;
}

bool handleLineIndentShortcut(MyCodeEditor* editor, QKeyEvent* event)
{
    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("format.indentLines"))) {
        if (!requestRegisteredEditorAction(
                editor,
                QStringLiteral("format.indentLines"))) {
            applyLineIndent(editor);
        }
        event->accept();
        return true;
    }
    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("format.unindentLines"))) {
        if (!requestRegisteredEditorAction(
                editor,
                QStringLiteral("format.unindentLines"))) {
            applyLineUnindent(editor);
        }
        event->accept();
        return true;
    }
    return false;
}

bool handleLineCommentShortcut(MyCodeEditor* editor, QKeyEvent* event)
{
    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("format.commentLines"))) {
        if (!requestRegisteredEditorAction(
                editor,
                QStringLiteral("format.commentLines"))) {
            applyLineComment(editor);
        }
        event->accept();
        return true;
    }
    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("format.uncommentLines"))) {
        if (!requestRegisteredEditorAction(
                editor,
                QStringLiteral("format.uncommentLines"))) {
            applyLineUncomment(editor);
        }
        event->accept();
        return true;
    }
    return false;
}

bool handleBracketRangeTab(MyCodeEditor* editor, QKeyEvent* event)
{
    if (!editor || !event
        || event->key() != Qt::Key_Tab)
        return false;
    const Qt::KeyboardModifiers modifiers =
        event->modifiers()
        & (Qt::ShiftModifier
           | Qt::ControlModifier
           | Qt::AltModifier
           | Qt::MetaModifier);
    if (modifiers != Qt::NoModifier)
        return false;

    QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection())
        return false;

    int rangeStart = -1;
    int rangeEnd = -1;
    if (!bracketRangeAroundCursor(cursor, &rangeStart, &rangeEnd))
        return false;

    const QString rangeText =
        editor->cachedDocumentSlice(rangeStart, rangeEnd - rangeStart);
    const QString expanded =
        rangeFromBracketInnerText(rangeText.mid(1, rangeText.size() - 2));
    if (expanded.isEmpty())
        return false;

    cursor.beginEditBlock();
    cursor.setPosition(rangeStart);
    cursor.setPosition(rangeEnd, QTextCursor::KeepAnchor);
    cursor.insertText(expanded);
    cursor.endEditBlock();
    cursor.setPosition(rangeStart + expanded.size());
    editor->setTextCursor(cursor);
    event->accept();
    return true;
}

bool handleBracketRangeAltClick(MyCodeEditor* editor, QMouseEvent* event)
{
    if (!editor || !event
        || event->button() != Qt::LeftButton
        || !event->modifiers().testFlag(Qt::AltModifier)
        || event->modifiers().testFlag(Qt::ControlModifier))
        return false;

    QTextCursor cursor = editor->cursorForPosition(
        event->position().toPoint());
    int rangeStart = -1;
    int rangeEnd = -1;
    if (!bracketRangeAroundCursor(cursor, &rangeStart, &rangeEnd))
        return false;

    QTextCursor selection = editor->textCursor();
    selection.setPosition(rangeStart + 1);
    selection.setPosition(rangeEnd - 1, QTextCursor::KeepAnchor);
    editor->setTextCursor(selection);
    event->accept();
    return true;
}

bool selectedRangeBoundSpan(const QString& selected,
                            bool rightBound,
                            int* boundStart,
                            int* boundEnd)
{
    int colonIndex = -1;
    for (int i = 0; i < selected.size(); ++i) {
        if (selected.at(i) == QLatin1Char(':')) {
            colonIndex = i;
            break;
        }
    }
    if (colonIndex < 0)
        return false;

    int start = 0;
    int stop = selected.size();
    if (rightBound) {
        start = colonIndex + 1;
    } else {
        stop = colonIndex;
    }

    while (start < stop && selected.at(start).isSpace())
        ++start;

    int end = stop;
    while (end > start && selected.at(end - 1).isSpace())
        --end;

    if (end == start)
        return false;

    if (boundStart)
        *boundStart = start;
    if (boundEnd)
        *boundEnd = end;
    return true;
}

bool parseNonNegativeInteger(const QString& text, int* value)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    for (const QChar ch : trimmed) {
        if (!ch.isDigit())
            return false;
    }

    bool ok = false;
    const int parsed = trimmed.toInt(&ok);
    if (!ok)
        return false;

    if (value)
        *value = parsed;
    return true;
}

bool simpleRangeExpression(const QString& expression)
{
    const QString trimmed = expression.trimmed();
    if (trimmed.isEmpty())
        return false;

    for (const QChar ch : trimmed) {
        if (!(ch.isLetterOrNumber()
              || ch == QLatin1Char('_')
              || ch == QLatin1Char('$'))) {
            return false;
        }
    }
    return true;
}

bool wrappedByOuterParentheses(const QString& expression)
{
    const QString trimmed = expression.trimmed();
    if (!trimmed.startsWith(QLatin1Char('('))
        || !trimmed.endsWith(QLatin1Char(')'))) {
        return false;
    }

    int depth = 0;
    for (int i = 0; i < trimmed.size(); ++i) {
        const QChar ch = trimmed.at(i);
        if (ch == QLatin1Char('('))
            ++depth;
        else if (ch == QLatin1Char(')'))
            --depth;
        if (depth == 0 && i < trimmed.size() - 1)
            return false;
        if (depth < 0)
            return false;
    }
    return depth == 0;
}

QString adjustedExpressionBound(const QString& expression, bool increment)
{
    const QString trimmed = expression.trimmed();
    const QString base =
        simpleRangeExpression(trimmed) || wrappedByOuterParentheses(trimmed)
            ? trimmed
            : QStringLiteral("(%1)").arg(trimmed);
    return base + (increment ? QStringLiteral("+1") : QStringLiteral("-1"));
}

bool adjustSelectedRangeBound(MyCodeEditor* editor,
                              QKeyEvent* event)
{
    if (!editor || !event || hasCommandModifier(event))
        return false;
    const bool increment = event->key() == Qt::Key_Up;
    const bool decrement = event->key() == Qt::Key_Down;
    if (!increment && !decrement)
        return false;

    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection())
        return false;

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = cursor.selectionEnd();
    if (selectionStart <= 0
        || selectionEnd >= editor->document()->characterCount())
        return false;
    if (editor->document()->characterAt(selectionStart - 1)
            != QLatin1Char('[')
        || editor->document()->characterAt(selectionEnd)
            != QLatin1Char(']')) {
        return false;
    }

    const QString selected = cursor.selectedText();
    const bool rightBound =
        event->modifiers().testFlag(Qt::ShiftModifier);
    int boundStart = -1;
    int boundEnd = -1;
    if (!selectedRangeBoundSpan(selected, rightBound, &boundStart, &boundEnd))
        return false;

    const QString currentText =
        selected.mid(boundStart, boundEnd - boundStart);
    int current = 0;
    QString nextText;
    if (parseNonNegativeInteger(currentText, &current)) {
        int lowerBound = 0;
        if (!rightBound) {
            int rightValue = 0;
            int rightStart = -1;
            int rightEnd = -1;
            if (selectedRangeBoundSpan(selected, true, &rightStart, &rightEnd)
                && parseNonNegativeInteger(
                    selected.mid(rightStart, rightEnd - rightStart),
                    &rightValue)) {
                lowerBound = rightValue;
            }
        }
        const int next = increment
            ? current + 1
            : qMax(lowerBound, current - 1);
        nextText = QString::number(next);
    } else {
        nextText = adjustedExpressionBound(currentText, increment);
    }

    const QString replacement =
        selected.left(boundStart)
        + nextText
        + selected.mid(boundEnd);

    cursor.insertText(replacement);
    QTextCursor reselection = editor->textCursor();
    reselection.setPosition(selectionStart);
    reselection.setPosition(selectionStart + replacement.size(),
                            QTextCursor::KeepAnchor);
    editor->setTextCursor(reselection);
    event->accept();
    return true;
}

} // namespace

void MyCodeEditorState::initializeCore(MyCodeEditor* editor)
{
    semantic.init();
    syntax.init();
    gutter.init(editor);
    identity.set(QString());
    semanticRevisionText.setText(QString());
    inlineFilterTextOverlayActive = false;
    inlineFilterTextOverlayStart = -1;
    inlineFilterTextOverlayOriginalLength = 0;
    inlineFilterTextOverlayOriginalText.clear();
    inlineFilterTextOverlayCurrentText.clear();
    qRegisterMetaType<DocumentChange>("DocumentChange");
    qRegisterMetaType<EditorModeSnapshot>("EditorModeSnapshot");
    editor->setProperty(kDiagnosticsEmptyProperty, true);
    editor->setProperty(kSemanticDecorationsEmptyProperty, true);
    editor->setMouseTracking(true);
    editor->setAcceptDrops(true);
}

void MyCodeEditorState::shutdown(MyCodeEditor* editor)
{
    folding.resetForDocumentChange(editor);
    highlightRefresh.detach();
    completion.detach();
    if (editor) {
        QObject::disconnect(editor, nullptr, editor, nullptr);
        // Extra selections own QTextCursor instances registered with the
        // current QTextDocument. Release them while that document is still
        // alive; QPlainTextEdit's base destructor runs after this state.
        editor->setExtraSelections({});
    }
    QObject::disconnect(documentContentsChangeConnection);
    documentContentsChangeConnection = {};

    if (lifecycleDiagnosticRank >= 0) {
        if (lifecycleDiagnosticRank >= 4) {
            modes.exitAll(EditorModeExitReason::DocumentClosed);
            templateSlots.shutdown(nullptr);
            keywordGhost.shutdown(nullptr);
            multiCursor.shutdown(nullptr);
            signalSelection.shutdown(nullptr);
            columnMode.shutdown(nullptr);
            sourceNavigation.shutdown();
        }
        shutdownGhostQueries(editor);
        if (lifecycleDiagnosticRank >= 7)
            syntax.detachHighlighter();
        cancelSignalDefinitionEditor();
        cancelSemanticRenameEditor();
        annotationLayer.clear();
        if (lifecycleDiagnosticRank >= 3)
            gutter.destroy();
        return;
    }

    modes.exitAll(EditorModeExitReason::DocumentClosed);
    templateSlots.shutdown(nullptr);
    keywordGhost.shutdown(nullptr);
    multiCursor.shutdown(nullptr);
    shutdownGhostQueries(editor);
    syntax.detachHighlighter();
    cancelSignalDefinitionEditor();
    cancelSemanticRenameEditor();
    signalSelection.shutdown(nullptr);
    columnMode.shutdown(nullptr);
    sourceNavigation.shutdown();
    annotationLayer.clear();
    gutter.destroy();
}

void MyCodeEditorState::bindEditorModes(MyCodeEditor* editor)
{
    const QPointer<MyCodeEditor> target(editor);
    modes.setChangeHandler(
        [target](const EditorModeSnapshot& snapshot) {
            if (target)
                emit target->editorModeStateChanged(snapshot);
        });
    templateSlots.bind(&modes, &annotationLayer, editor);
    keywordGhost.bind(&modes, &annotationLayer, editor);
    multiCursor.bind(&modes, editor);
    signalSelection.bind(
        &modes,
        &selections,
        editor,
        [this, target](int cursorPosition) {
            return resolveSignalSelectionCandidate(
                target,
                cursorPosition);
        });
    columnMode.bind(&modes, &annotationLayer, editor);
    sourceNavigation.bindModeController(
        &modes,
        editor,
        &selections);
    folding.bindModeController(&modes, editor);
}

EditorModeSnapshot MyCodeEditorState::modeSnapshot() const
{
    return modes.snapshot();
}

void MyCodeEditorState::exitInteractionModes(
    EditorModeExitReason reason)
{
    modes.exitAll(reason);
}


void MyCodeEditorState::attachEditorConnections(MyCodeEditor* editor)
{
    highlightRefresh.attachToEditor(editor, [this, editor]() {
        lifecycleTrace("cursor.highlight-enter");
        if (suppressNextCursorPresentation)
            return;
        refreshScopeAndCurrentLineHighlight(editor);
        editorPresentationPending = false;
        lifecycleTrace("cursor.highlight-exit");
    });
    QObject::connect(
        editor,
        &QPlainTextEdit::blockCountChanged,
        editor,
        [this, editor]() {
            gutter.updateViewportMargins(editor);
        });
    QObject::connect(
        editor,
        &QPlainTextEdit::updateRequest,
        editor,
        [this, editor](const QRect& rect, int dy) {
            gutter.handleUpdateRequest(editor, rect, dy);
            if (dy != 0) {
                refreshVisibleRegionPresentation(editor);
                sourceNavigation.handleEditorScrolled(editor, selections);
                sourceNavigation.syncMode();
            }
        });
    attachDocumentConnection(editor);
    QObject::connect(
        editor,
        &QPlainTextEdit::cursorPositionChanged,
        editor,
        [this, editor]() {
            lifecycleTrace("cursor.state-enter");
            handleVirtualCursorChanged(editor);
            lifecycleTrace("cursor.virtual");
            if (suppressNextCursorPresentation) {
                suppressNextCursorPresentation = false;
                lifecycleTrace("cursor.suppressed");
                return;
            }
            completionWorkflow.handleCursorPositionChanged();
            lifecycleTrace("cursor.completion");
            handleTemplateSlotCursorChanged(editor);
            lifecycleTrace("cursor.templates");
            refreshDerivedEditorState(editor, true);
            lifecycleTrace("cursor.state-exit");
        });
}

void MyCodeEditorState::attachDocumentConnection(
    MyCodeEditor* editor)
{
    QObject::disconnect(documentContentsChangeConnection);
    documentContentsChangeConnection = {};
    if (!editor || !editor->document())
        return;
    documentContentsChangeConnection = QObject::connect(
        editor->document(),
        &QTextDocument::contentsChange,
        editor,
        [this, editor](int position, int charsRemoved, int charsAdded) {
            if (rebindingDocument)
                return;
            handleDocumentContentsChange(editor,
                                         position,
                                         charsRemoved,
                                         charsAdded);
        });
}

void MyCodeEditorState::attachToEditor(MyCodeEditor* editor)
{
    lifecycleDiagnosticRank =
        lifecycleDiagnosticRankFromEnvironment();
    if (lifecycleDiagnosticRank >= 0) {
        if (lifecycleDiagnosticRank >= 1)
            semantic.init();
        if (lifecycleDiagnosticRank >= 2)
            syntax.init();
        if (lifecycleDiagnosticRank >= 3) {
            gutter.init(editor);
            identity.set(QString());
            semanticRevisionText.setText(QString());
            inlineFilterTextOverlayActive = false;
            inlineFilterTextOverlayStart = -1;
            inlineFilterTextOverlayOriginalLength = 0;
            inlineFilterTextOverlayOriginalText.clear();
            inlineFilterTextOverlayCurrentText.clear();
            qRegisterMetaType<DocumentChange>("DocumentChange");
            qRegisterMetaType<EditorModeSnapshot>("EditorModeSnapshot");
            editor->setProperty(kDiagnosticsEmptyProperty, true);
            editor->setProperty(kSemanticDecorationsEmptyProperty, true);
            editor->setMouseTracking(true);
            editor->setAcceptDrops(true);
        }
        if (lifecycleDiagnosticRank >= 4)
            bindEditorModes(editor);
        if (lifecycleDiagnosticRank >= 5)
            attachEditorConnections(editor);
        if (lifecycleDiagnosticRank >= 6)
            appearance.apply(editor);
        if (lifecycleDiagnosticRank >= 7)
            syntax.attachToEditor(editor);
        if (lifecycleDiagnosticRank >= 8) {
            completionWorkflow.bind(
                editor,
                &completion,
                &modes,
                &selections,
                [this, editor](int cursorPosition,
                               bool includeDocumentText) {
                    return semanticContextForPosition(
                        editor,
                        cursorPosition,
                        includeDocumentText);
                },
                [this](int charPos) {
                    return currentModuleNameAt(charPos);
                },
                [this]() { return semanticService(); });
        }
        if (lifecycleDiagnosticRank >= 9) {
            completion.attachToEditor(
                editor,
                [this, editor](const QModelIndex& index) {
                    auto edit =
                        editor->beginSynchronousEditTransaction();
                    completionWorkflow.handleCompletionActivated(index);
                });
        }
        if (lifecycleDiagnosticRank >= 10)
            selections.highlightCurrentLine(editor);
        if (lifecycleDiagnosticRank >= 11) {
            folding.refresh(editor, syntax.tsDocument());
            ++hotPathMetrics.fullFoldingRebuilds;
        }
        if (lifecycleDiagnosticRank >= 12)
            refreshDerivedEditorState(editor, false);
        if (lifecycleDiagnosticRank >= 13)
            gutter.updateViewportMargins(editor);
        return;
    }

    initializeCore(editor);
    bindEditorModes(editor);
    attachEditorConnections(editor);
    appearance.apply(editor);
    syntax.attachToEditor(editor);
    completionWorkflow.bind(
        editor,
        &completion,
        &modes,
        &selections,
        [this, editor](int cursorPosition, bool includeDocumentText) {
            return semanticContextForPosition(
                editor,
                cursorPosition,
                includeDocumentText);
        },
        [this](int charPos) {
            return currentModuleNameAt(charPos);
        },
        [this]() {
            return semanticService();
        });
    completion.attachToEditor(
        editor,
        [this, editor](const QModelIndex& index) {
            auto edit = editor->beginSynchronousEditTransaction();
            completionWorkflow.handleCompletionActivated(index);
        });
    selections.highlightCurrentLine(editor);
    folding.refresh(editor, syntax.tsDocument());
    ++hotPathMetrics.fullFoldingRebuilds;
    refreshDerivedEditorState(editor, false);
    gutter.updateViewportMargins(editor);
}

void MyCodeEditorState::rebindDocument(
    MyCodeEditor* editor,
    QTextDocument* document,
    std::uint64_t textRevision)
{
    if (!editor || !document)
        return;
    if (editor->document() == document) {
        semanticTextRevision = textRevision;
        return;
    }

    modes.exitAll(EditorModeExitReason::DocumentChanged);
    columnMode.clearPendingColumnAnchor();
    folding.resetForDocumentChange(editor);
    cancelSignalDefinitionEditor();
    cancelSemanticRenameEditor();
    finishInlineFilterTextOverlay();
    ++ghostQueryGeneration;
    if (ghostQueryCancellation)
        ghostQueryCancellation->store(true);

    QObject::disconnect(documentContentsChangeConnection);
    documentContentsChangeConnection = {};
    syntax.detachHighlighter();
    editor->setExtraSelections({});
    rebindingDocument = true;
    editor->QPlainTextEdit::setDocument(document);
    rebindingDocument = false;
    multiCursor.resetToEditorCursor();

    const QString text = document->toPlainText();
    semanticRevisionText.setText(text);
    semanticTextRevision = textRevision;
    inlineFilterTextOverlayActive = false;
    inlineFilterTextOverlayStart = -1;
    inlineFilterTextOverlayOriginalLength = 0;
    inlineFilterTextOverlayOriginalText.clear();
    inlineFilterTextOverlayCurrentText.clear();
    diagnostics.clear();
    diagnosticIndexesByLine.clear();
    diagnosticSeverityByLine.clear();
    diagnosticOverviewSeverityByBucket.clear();
    diagnosticComputationRevision = 0;
    ghostAnnotations.clear();
    semanticDecorations.clear();
    annotationLayer.clear();
    editor->setProperty(kDiagnosticsEmptyProperty, true);
    editor->setProperty(kSemanticDecorationsEmptyProperty, true);

    syntax.syncText(text);
    // QTextDocument invokes direct connections in registration order. Keep
    // the syntax model current before the highlighter reads it after a shared
    // document edit.
    attachDocumentConnection(editor);
    syntax.createHighlighter(document);
    selections.resetDocumentText(editor, text);
    selections.highlightDiagnostics(editor, {});
    selections.highlightSemanticDecorations(editor, {});
    selections.highlightCurrentLine(editor);
    folding.refresh(editor, syntax.tsDocument());
    ++hotPathMetrics.fullFoldingRebuilds;
    lastWavePreviewScopeKey.clear();
    suppressNextCursorPresentation = false;
    editorPresentationPending = false;
    ghostPresentationPending = false;
    refreshDerivedEditorState(editor, false);
    gutter.updateViewportMargins(editor);
    handleResize(editor);
    editor->viewport()->update();
    emit editor->wavePreviewScopeChanged();
}

void MyCodeEditorState::handleDocumentContentsChange(
    MyCodeEditor* editor,
    int position,
    int charsRemoved,
    int charsAdded)
{
    lifecycleTrace("change.enter");
    const int changeDiagnosticRank =
        changeDiagnosticRankFromEnvironment();
    if (!editor || !editor->document())
        return;
    QElapsedTimer documentChangeCoreTimer;
    QElapsedTimer documentChangePhaseTimer;
    if (hotPathTimingEnabled) {
        documentChangeCoreTimer.start();
        documentChangePhaseTimer.start();
    }
    const auto finishDocumentChangePhase =
        [this, &documentChangePhaseTimer](std::uint64_t& destination) {
        if (!hotPathTimingEnabled)
            return;
        destination += static_cast<std::uint64_t>(
            documentChangePhaseTimer.nsecsElapsed());
        documentChangePhaseTimer.restart();
    };

    if (signalDefinitionPeekActive
        || signalDefinitionEditor)
        cancelSignalDefinitionEditor();
    if (semanticRenamePeek || semanticRenameEditor)
        cancelSemanticRenameEditor();
    if (modes.isActive(EditorModeId::VirtualCursor))
        clearVirtualCursor(editor);
    else
        columnMode.clearPendingColumnAnchor();
    if (signalSelection.active()
        || signalSelection.hasSelection()) {
        cancelSignalSelectionMode(editor);
    }
    if (multiCursor.active()
        && !multiCursor.applyingDocumentEdit()) {
        modes.exit(
            EditorModeId::MultiCursor,
            EditorModeExitReason::DocumentChanged);
    }
    ++ghostQueryGeneration;
    if (ghostQueryCancellation)
        ghostQueryCancellation->store(true);

    const int oldLength = cachedDocumentLength();
    const int newLength = qMax(0, editor->document()->characterCount() - 1);
    const int boundedPosition = qBound(0, position, oldLength);
    const int removedLength = qBound(0,
                                     charsRemoved,
                                     oldLength - boundedPosition);
    int insertedLength = newLength - (oldLength - removedLength);
    if (insertedLength < 0
        || boundedPosition + insertedLength > newLength) {
        insertedLength = qBound(0,
                                charsAdded,
                                newLength - qMin(boundedPosition,
                                                 newLength));
    }

    DocumentChange change;
    change.position = boundedPosition;
    change.removedLength = removedLength;
    change.removedText = cachedDocumentSlice(
        boundedPosition, removedLength);
    change.insertedText = insertedDocumentText(editor->document(),
                                               boundedPosition,
                                               insertedLength);
    change.oldLength = oldLength;
    change.newLength = newLength;
    if (!change.changesText() && oldLength == newLength)
        return;

    const QTextBlock startBlock = editor->document()->findBlock(
        qBound(0, boundedPosition, newLength));
    change.startLine = startBlock.isValid() ? startBlock.blockNumber() : 0;
    change.startColumn = startBlock.isValid()
        ? boundedPosition - startBlock.position()
        : boundedPosition;
    change.oldEndLine = change.startLine
        + change.removedText.count(QLatin1Char('\n'));
    change.newEndLine = change.startLine
        + change.insertedText.count(QLatin1Char('\n'));
    change.lineDelta = change.newEndLine - change.oldEndLine;

    const QTextBlock changedBlock = editor->document()->findBlock(
        qBound(0, change.position, newLength));
    int occurrenceNewLineStart = changedBlock.isValid()
        ? changedBlock.position()
        : -1;
    const int occurrenceNewLineEnd = changedBlock.isValid()
        ? changedBlock.position() + changedBlock.text().size()
        : -1;
    const bool localLineChange = changedBlock.isValid()
        && !change.removedText.contains(QLatin1Char('\n'))
        && !change.insertedText.contains(QLatin1Char('\n'))
        && change.position >= occurrenceNewLineStart
        && change.newEnd() <= occurrenceNewLineEnd;

    OccurrenceChangeContext occurrenceContext;
    bool useLocalOccurrenceLine = false;
    QString occurrenceNewLineText;
    bool appliedToInlineOverlay = false;
    if (inlineFilterTextOverlayActive) {
        const int overlayEnd =
            inlineFilterTextOverlayStart
            + inlineFilterTextOverlayCurrentText.size();
        if (change.position >= inlineFilterTextOverlayStart
            && change.oldEnd() <= overlayEnd
            && localLineChange) {
            QString nextOverlay =
                inlineFilterTextOverlayCurrentText;
            nextOverlay.replace(
                change.position - inlineFilterTextOverlayStart,
                change.removedLength,
                change.insertedText);
            const int nextLength =
                semanticRevisionText.size()
                - inlineFilterTextOverlayOriginalLength
                + nextOverlay.size();
            if (nextLength == change.newLength) {
                inlineFilterTextOverlayCurrentText =
                    std::move(nextOverlay);
                appliedToInlineOverlay = true;
                ++hotPathMetrics.inlineFilterOverlayEdits;
                occurrenceNewLineText = changedBlock.text();
                occurrenceContext =
                    selections.prepareDocumentLineChange(
                        change,
                        occurrenceNewLineStart,
                        occurrenceNewLineEnd
                            - change.characterDelta());
                useLocalOccurrenceLine = true;
            }
        }
    }
    if (!appliedToInlineOverlay) {
        finishInlineFilterTextOverlay();
        if (localLineChange) {
            occurrenceNewLineText = changedBlock.text();
            occurrenceContext =
                selections.prepareDocumentLineChange(
                    change,
                    occurrenceNewLineStart,
                    occurrenceNewLineEnd
                        - change.characterDelta());
            useLocalOccurrenceLine = true;
        } else {
            const int oldLineStart = change.position <= 0
                ? 0
                : semanticRevisionText.lastIndexOf(
                      QLatin1Char('\n'), change.position - 1) + 1;
            const int oldLineBreak = semanticRevisionText.indexOf(
                QStringLiteral("\n"), change.oldEnd());
            const int oldLineEnd = oldLineBreak < 0
                ? semanticRevisionText.size()
                : oldLineBreak;
            occurrenceContext = selections.prepareDocumentLineChange(
                change, oldLineStart, oldLineEnd);
        }
        semanticRevisionText.replace(change.position,
                                     change.removedLength,
                                     change.insertedText);
        if (!localLineChange && !occurrenceContext.rebuild) {
            occurrenceNewLineStart = change.position <= 0
                ? 0
                : semanticRevisionText.lastIndexOf(
                      QLatin1Char('\n'), change.position - 1) + 1;
            const int newLineBreak = semanticRevisionText.indexOf(
                QStringLiteral("\n"), change.newEnd());
            const int newLineEnd = newLineBreak < 0
                ? semanticRevisionText.size()
                : newLineBreak;
            occurrenceNewLineText = semanticRevisionText.mid(
                occurrenceNewLineStart,
                newLineEnd - occurrenceNewLineStart);
            useLocalOccurrenceLine = true;
        }
    }
    ++semanticTextRevision;
    change.revision = semanticTextRevision;
    if (!diagnostics.isEmpty())
        clearDiagnosticHighlights(editor);
    ++hotPathMetrics.documentChanges;
    suppressNextCursorPresentation = true;
    editorPresentationPending = true;
    finishDocumentChangePhase(
        hotPathMetrics.documentChangePrepareNanoseconds);
    lifecycleTrace("change.prepared");
    if (changeDiagnosticRank == 0)
        return;

    const QList<TSChangedRange> changedRanges =
        syntax.applyDocumentChange(change,
                                   semanticRevisionText,
                                   appliedToInlineOverlay);
    finishDocumentChangePhase(
        hotPathMetrics.documentChangeSyntaxNanoseconds);
    lifecycleTrace("change.syntax");
    if (changeDiagnosticRank == 1)
        return;
    if (const TSDocument* syntaxDocument = syntax.tsDocument()) {
        const bool fullFoldRebuild = folding.applyDocumentChange(
            editor, syntaxDocument, change, changedRanges);
        if (fullFoldRebuild)
            ++hotPathMetrics.fullFoldingRebuilds;
        else
            ++hotPathMetrics.incrementalFoldingUpdates;
    }
    finishDocumentChangePhase(
        hotPathMetrics.documentChangeFoldingNanoseconds);
    lifecycleTrace("change.folding");
    if (changeDiagnosticRank == 2)
        return;
    remapSemanticDecorations(editor, change);
    finishDocumentChangePhase(
        hotPathMetrics.documentChangeDecorationNanoseconds);
    lifecycleTrace("change.decorations");
    if (changeDiagnosticRank == 3)
        return;

    const OccurrenceIndexUpdate occurrenceUpdate =
        useLocalOccurrenceLine
        ? selections.applyDocumentLineChange(
              editor,
              change,
              occurrenceContext,
              occurrenceNewLineStart,
              occurrenceNewLineText)
        : selections.applyDocumentChange(editor,
                                         change,
                                         occurrenceContext,
                                         cachedDocumentText());
    if (occurrenceUpdate == OccurrenceIndexUpdate::Full)
        ++hotPathMetrics.occurrenceFullBuilds;
    else if (occurrenceUpdate == OccurrenceIndexUpdate::Incremental)
        ++hotPathMetrics.occurrenceIncrementalUpdates;
    finishDocumentChangePhase(
        hotPathMetrics.documentChangeOccurrenceNanoseconds);
    lifecycleTrace("change.occurrences");
    if (changeDiagnosticRank == 4)
        return;

    templateSlots.markPresentationPending();
    handleTemplateSlotContentsChange(editor,
                                     change.position,
                                     change.removedLength,
                                     change.insertedText.size(),
                                     false);
    remapGhostAnnotations(editor, change);
    finishDocumentChangePhase(
        hotPathMetrics.documentChangePresentationNanoseconds);
    lifecycleTrace("change.presentation");
    if (changeDiagnosticRank == 5)
        return;
    finishDocumentChangePhase(
        hotPathMetrics.documentChangeDerivedStateNanoseconds);
    lifecycleTrace("change.derived-deferred");
    if (hotPathTimingEnabled) {
        hotPathMetrics.documentChangeCoreNanoseconds +=
            static_cast<std::uint64_t>(
                documentChangeCoreTimer.nsecsElapsed());
    }
    QElapsedTimer dispatchTimer;
    if (hotPathTimingEnabled)
        dispatchTimer.start();
    emit editor->documentChangeApplied(change);
    lifecycleTrace("change.emitted");
    if (hotPathTimingEnabled) {
        hotPathMetrics.documentChangeDispatchNanoseconds +=
            static_cast<std::uint64_t>(dispatchTimer.nsecsElapsed());
    }
    if (synchronousEditTransactionDepth == 0) {
        const QPointer<MyCodeEditor> target(editor);
        QTimer::singleShot(0, editor, [this, target]() {
            if (target && synchronousEditTransactionDepth == 0)
                finishEditorInput(target);
        });
    }
    lifecycleTrace("change.exit");
}

void MyCodeEditorState::remapGhostAnnotations(
    MyCodeEditor* editor,
    const DocumentChange& change)
{
    ++hotPathMetrics.ghostRemaps;
    const EditorAnchoredRangeIndexStats beforeGhostStats =
        ghostAnnotations.stats();
    const EditorAnchoredRangeIndexStats beforeLayerStats =
        annotationLayer.ghostIndexStats();
    const EditorAnchoredRangeRemapReport ghostReport =
        ghostAnnotations.remap(change);
    const EditorAnchoredRangeRemapReport layerReport =
        annotationLayer.remapGhostSource(
            change, ghostQueryGeneration);
    const EditorAnchoredRangeIndexStats afterGhostStats =
        ghostAnnotations.stats();
    const EditorAnchoredRangeIndexStats afterLayerStats =
        annotationLayer.ghostIndexStats();

    if (editor) {
        editor->setProperty(
            "zeroslackGhostAnnotationRemapVisitedCount",
            static_cast<qlonglong>(
                ghostReport.visitedNodes
                + layerReport.visitedNodes));
        editor->setProperty(
            "zeroslackGhostAnnotationRemapShiftedCount",
            static_cast<qlonglong>(ghostReport.shiftedItems));
        editor->setProperty(
            "zeroslackGhostAnnotationRemapRemovedCount",
            static_cast<qlonglong>(ghostReport.removedItems));
        editor->setProperty(
            "zeroslackGhostAnnotationRemapMaterializationCount",
            static_cast<qlonglong>(
                afterGhostStats.materializationCount
                - beforeGhostStats.materializationCount
                + afterLayerStats.materializationCount
                - beforeLayerStats.materializationCount));
    }
    if (ghostReport.changed() || layerReport.changed()) {
        ghostPresentationPending = true;
    }
}

void MyCodeEditorState::remapSemanticDecorations(
    MyCodeEditor* editor,
    const DocumentChange& change)
{
    const EditorAnchoredRangeIndexStats beforeStats =
        semanticDecorations.stats();
    const EditorAnchoredRangeRemapReport report =
        semanticDecorations.remap(change);
    const EditorAnchoredRangeIndexStats afterStats =
        semanticDecorations.stats();
    if (editor) {
        editor->setProperty(
            "zeroslackSemanticDecorationRemapVisitedCount",
            static_cast<qlonglong>(report.visitedNodes));
        editor->setProperty(
            "zeroslackSemanticDecorationRemapShiftedCount",
            static_cast<qlonglong>(report.shiftedItems));
        editor->setProperty(
            "zeroslackSemanticDecorationRemapRemovedCount",
            static_cast<qlonglong>(report.removedItems));
        editor->setProperty(
            "zeroslackSemanticDecorationRemapMaterializationCount",
            static_cast<qlonglong>(
                afterStats.materializationCount
                - beforeStats.materializationCount));
    }
}

void MyCodeEditorState::rebuildSemanticDecorationPositionIndex()
{
    QList<SemanticDecoration> sorted =
        semanticDecorations.toList();
    sorted.erase(
        std::remove_if(
            sorted.begin(),
            sorted.end(),
            [](const SemanticDecoration& decoration) {
                return !decoration.isValid();
            }),
        sorted.end());
    std::stable_sort(
        sorted.begin(),
        sorted.end(),
        [](const SemanticDecoration& left,
           const SemanticDecoration& right) {
            if (left.startPosition != right.startPosition) {
                return left.startPosition
                    < right.startPosition;
            }
            return left.length < right.length;
        });
    semanticDecorations = sorted;
}

EditorVisibleDocumentRange
MyCodeEditorState::visibleDocumentRange(
    const MyCodeEditor* editor) const
{
    EditorVisibleDocumentRange range;
    if (!editor || !editor->document())
        return range;

    const QTextBlock first = editor->firstVisibleBlock();
    if (!first.isValid())
        return range;
    QTextBlock last = editor->cursorForPosition(
        QPoint(0, qMax(0, editor->viewport()->height() - 1))).block();
    if (!last.isValid() || last.blockNumber() < first.blockNumber())
        last = first;

    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    range.firstLine = first.blockNumber();
    range.lastLine = last.blockNumber();
    range.startPosition = qBound(
        0, first.position(), documentEnd);
    range.endPosition = qBound(
        range.startPosition,
        last.position() + qMax(0, last.length() - 1),
        documentEnd);
    return range;
}

void MyCodeEditorState::refreshVisibleRegionPresentation(
    MyCodeEditor* editor)
{
    if (!editor)
        return;
    ++hotPathMetrics.visiblePresentationRefreshes;
    const EditorVisibleDocumentRange range =
        visibleDocumentRange(editor);
    if (range.valid()) {
        templateSlots.publishVisibleAnnotations(
            editor,
            range.firstLine,
            range.lastLine);
        columnMode.publishVisibleAnnotations(
            editor,
            range.firstLine,
            range.lastLine);
    }
    refreshDiagnosticPresentation(editor);
    refreshSemanticDecorationPresentation(editor);
}

void MyCodeEditorState::refreshSemanticDecorationPresentation(
    MyCodeEditor* editor)
{
    if (!editor)
        return;

    const EditorVisibleDocumentRange range =
        visibleDocumentRange(editor);
    QList<SemanticDecoration> visible;
    if (!range.valid() || semanticDecorations.isEmpty()) {
        selections.highlightSemanticDecorations(editor, visible);
        editor->setProperty(
            kSemanticDecorationsEmptyProperty, true);
        return;
    }

    qsizetype visitedNodes = 0;
    const QList<SemanticDecoration> candidates =
        semanticDecorations.overlapping(
            range.startPosition,
            range.endPosition,
            &visitedNodes);
    editor->setProperty(
        "zeroslackSemanticDecorationVisibleQueryVisitedCount",
        static_cast<qlonglong>(visitedNodes));
    visible.reserve(candidates.size());
    const TSDocument* document = syntax.tsDocument();
    for (const SemanticDecoration& decoration : candidates) {
        ++hotPathMetrics.semanticDecorationCandidatesExamined;
        if (!decoration.isValid())
            continue;
        if (document
            && (document->isCommentAt(decoration.startPosition)
                || document->isCommentAt(
                    decoration.startPosition + decoration.length - 1))) {
            continue;
        }
        visible.append(decoration);
    }
    hotPathMetrics.semanticDecorationSelectionsBuilt +=
        static_cast<std::uint64_t>(visible.size());
    selections.highlightSemanticDecorations(editor, visible);
    editor->setProperty(kSemanticDecorationsEmptyProperty,
                        visible.isEmpty());
}

void MyCodeEditorState::refreshDerivedEditorState(
    MyCodeEditor* editor,
    bool allowWavePreviewSignal)
{
    if (!editor)
        return;

    QElapsedTimer derivedTimer;
    derivedTimer.start();
    keywordGhost.refresh(editor, syntax);
    hotPathMetrics.editorDerivedKeywordGhostNanoseconds +=
        static_cast<std::uint64_t>(derivedTimer.nsecsElapsed());
    derivedTimer.restart();
    selections.highlightKeywordPair(
        editor,
        syntax.matchingKeywordPairAt(
            editor->textCursor().position()));
    hotPathMetrics.editorDerivedKeywordPairNanoseconds +=
        static_cast<std::uint64_t>(derivedTimer.nsecsElapsed());

    derivedTimer.restart();
    const EditorPackageToolAvailability availability =
        currentPackageToolAvailability(editor);
    if (!packageToolAvailabilityInitialized
        || !samePackageAvailability(availability,
                                    lastPackageToolAvailability)) {
        lastPackageToolAvailability = availability;
        packageToolAvailabilityInitialized = true;
        emit editor->packageToolAvailabilityChanged(availability);
    }
    hotPathMetrics.editorDerivedPackageToolNanoseconds +=
        static_cast<std::uint64_t>(derivedTimer.nsecsElapsed());

    derivedTimer.restart();
    const QString scopeKey = wavePreviewScopeKey(*this, editor);
    hotPathMetrics.editorDerivedWaveScopeNanoseconds +=
        static_cast<std::uint64_t>(derivedTimer.nsecsElapsed());
    if (!allowWavePreviewSignal) {
        lastWavePreviewScopeKey = scopeKey;
        return;
    }

    if (lastWavePreviewScopeKey.isEmpty()) {
        lastWavePreviewScopeKey = scopeKey;
        emit editor->wavePreviewScopeChanged();
    } else if (scopeKey != lastWavePreviewScopeKey) {
        lastWavePreviewScopeKey = scopeKey;
        emit editor->wavePreviewScopeChanged();
    }
}

bool MyCodeEditorState::handleKeyPress(MyCodeEditor* editor, QKeyEvent* event)
{
    if (!editor || !event)
        return false;

    if (event->key() == Qt::Key_Escape) {
        const EditorModeId escapeMode = modes.escapeTarget();
        switch (escapeMode) {
        case EditorModeId::CommandMode:
        case EditorModeId::MultiCursor:
        case EditorModeId::KeywordGhost:
            if (escapeMode == EditorModeId::MultiCursor) {
                multiLineBoundarySelectionAnchors.clear();
                multiLineBoundarySelectionDirection = 0;
            }
            modes.exit(escapeMode,
                       EditorModeExitReason::Canceled);
            break;
        case EditorModeId::SignalSelection:
            cancelSignalSelectionMode(editor);
            emit editor->editorStatusMessageRequested(
                QStringLiteral("Signal selection canceled"));
            break;
        case EditorModeId::FoldRegion:
            folding.cancelFoldRegionMarkMode(editor);
            break;
        case EditorModeId::FoldShelf:
            folding.cancelFoldShelfMode(editor);
            break;
        case EditorModeId::TemplateSlots:
            handleTemplateSlotKeyPress(editor, event);
            return true;
        case EditorModeId::VirtualCursor:
            clearVirtualCursor(editor);
            break;
        case EditorModeId::InlineCandidates:
        case EditorModeId::CompletionCandidates:
            if (!completionWorkflow.handleCompletionPopupKey(event)) {
                modes.exit(escapeMode,
                           EditorModeExitReason::Canceled);
            }
            break;
        case EditorModeId::ColumnSelection:
            columnMode.clearSelection(editor);
            break;
        case EditorModeId::SourceNavigation:
            sourceNavigation.handleEscape(editor, selections);
            modes.exit(EditorModeId::SourceNavigation,
                       EditorModeExitReason::Canceled);
            break;
        case EditorModeId::None:
            break;
        }
        if (escapeMode != EditorModeId::None) {
            event->accept();
            return true;
        }
    }

    // Slot navigation owns Tab and Backtab whenever a slot session exists.
    // Do not depend on the mode stack's current primary presentation: a
    // completion or source-navigation overlay can otherwise consume the same
    // physical Shift+Tab event and make it advance instead of moving back.
    if (templateSlots.active()
        && handleTemplateSlotKeyPress(editor, event)) {
        return true;
    }

    const bool nextOccurrenceShortcut =
        matchesRegisteredShortcut(
            event,
            QStringLiteral(
                "select.nextSymbolOccurrence"));
    const bool allOccurrencesShortcut =
        matchesRegisteredShortcut(
            event,
            QStringLiteral(
                "select.allSymbolOccurrences"));
    if ((nextOccurrenceShortcut || allOccurrencesShortcut)
        && modes.primaryMode()
               == EditorModeId::SourceNavigation) {
        // A modifier press can enter transient Ctrl-hover navigation before
        // the rest of the registered chord arrives.  The explicit occurrence
        // action owns the chord and must close that transient mode first.
        modes.exit(EditorModeId::SourceNavigation,
                   EditorModeExitReason::Conflict);
    }

    const EditorModeId primaryMode = modes.primaryMode();
    const bool occurrenceMode =
        primaryMode == EditorModeId::None
        || primaryMode == EditorModeId::MultiCursor
        || primaryMode == EditorModeId::KeywordGhost;
    if (occurrenceMode
        && nextOccurrenceShortcut) {
        keywordGhost.clear(editor);
        if (requestRegisteredEditorAction(
                editor,
                QStringLiteral(
                    "select.nextSymbolOccurrence"))) {
            event->accept();
            return true;
        }
        QString failure;
        selectSymbolOccurrences(
            editor, false, &failure);
        if (!failure.isEmpty()) {
            emit editor->editorStatusMessageRequested(
                failure);
        }
        event->accept();
        return true;
    }
    if (occurrenceMode
        && allOccurrencesShortcut) {
        keywordGhost.clear(editor);
        if (requestRegisteredEditorAction(
                editor,
                QStringLiteral(
                    "select.allSymbolOccurrences"))) {
            event->accept();
            return true;
        }
        QString failure;
        selectSymbolOccurrences(
            editor, true, &failure);
        if (!failure.isEmpty()) {
            emit editor->editorStatusMessageRequested(
                failure);
        }
        event->accept();
        return true;
    }

    if (primaryMode == EditorModeId::KeywordGhost
        && keywordGhost.handleKeyPress(editor, event)) {
        return true;
    }
    if (primaryMode == EditorModeId::SignalSelection) {
        event->accept();
        return true;
    }
    if (primaryMode == EditorModeId::FoldRegion) {
        event->accept();
        return true;
    }
    if (primaryMode == EditorModeId::FoldShelf
        && !event->text().isEmpty()
        && !event->modifiers().testFlag(Qt::ControlModifier)) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("Fold Shelf: drag custom fold blocks"));
        event->accept();
        return true;
    }

    if (primaryMode == EditorModeId::MultiCursor) {
        if (handleMultiCursorLineBoundarySelection(
                editor, event)) {
            return true;
        }
        if (event->key() != Qt::Key_Alt) {
            multiLineBoundarySelectionAnchors.clear();
            multiLineBoundarySelectionDirection = 0;
        }

        const bool undoShortcut =
            matchesRegisteredShortcut(
                event,
                QStringLiteral("edit.undo"));
        const bool redoShortcut =
            matchesRegisteredShortcut(
                event,
                QStringLiteral("edit.redo"));
        if (undoShortcut || redoShortcut) {
            multiCursor.escapeToSingleCursor();
            const QString actionId = undoShortcut
                ? QStringLiteral("edit.undo")
                : QStringLiteral("edit.redo");
            if (!requestRegisteredEditorAction(
                    editor, actionId)) {
                if (undoShortcut)
                    editor->undo();
                else
                    editor->redo();
            }
            event->accept();
            return true;
        }
        if (matchesRegisteredShortcut(
                event,
                QStringLiteral("edit.copy"))) {
            if (!requestRegisteredEditorAction(
                    editor,
                    QStringLiteral("edit.copy"))) {
                editor->copy();
            }
            event->accept();
            return true;
        }
        if (matchesRegisteredShortcut(
                event,
                QStringLiteral("edit.cut"))) {
            if (!requestRegisteredEditorAction(
                    editor,
                    QStringLiteral("edit.cut"))) {
                editor->cut();
            }
            event->accept();
            return true;
        }
        if (matchesRegisteredShortcut(
                event,
                QStringLiteral("edit.paste"))) {
            if (!requestRegisteredEditorAction(
                    editor,
                    QStringLiteral("edit.paste"))) {
                editor->paste();
            }
            event->accept();
            return true;
        }
        if (event->key() == Qt::Key_Backspace) {
            multiCursor.backspace();
            event->accept();
            return true;
        }
        if (event->key() == Qt::Key_Delete) {
            multiCursor.deleteForward();
            event->accept();
            return true;
        }
        if (event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter) {
            const Qt::KeyboardModifiers commandModifiers =
                event->modifiers()
                & (Qt::ControlModifier
                   | Qt::AltModifier
                   | Qt::MetaModifier);
            if (commandModifiers == Qt::NoModifier) {
                multiCursor.insertStructuralNewline(
                    syntax.tsDocument(),
                    kManualIndentWidth);
            } else {
                multiCursor.insertNewline();
            }
            event->accept();
            return true;
        }
        if (event->key() == Qt::Key_Backtab
            || (event->key() == Qt::Key_Tab
                && event->modifiers()
                       == Qt::ShiftModifier)) {
            multiCursor.unindent(4);
            event->accept();
            return true;
        }
        if (event->key() == Qt::Key_Tab
            && event->modifiers()
                   == Qt::NoModifier) {
            multiCursor.insertText(
                QStringLiteral("    "));
            event->accept();
            return true;
        }

        EditorMultiCursorMove move =
            EditorMultiCursorMove::Left;
        bool navigation = true;
        switch (event->key()) {
        case Qt::Key_Left:
            move = EditorMultiCursorMove::Left;
            break;
        case Qt::Key_Right:
            move = EditorMultiCursorMove::Right;
            break;
        case Qt::Key_Up:
            move = EditorMultiCursorMove::Up;
            break;
        case Qt::Key_Down:
            move = EditorMultiCursorMove::Down;
            break;
        case Qt::Key_Home:
            move = EditorMultiCursorMove::LineStart;
            break;
        case Qt::Key_End:
            move = EditorMultiCursorMove::LineEnd;
            break;
        default:
            navigation = false;
            break;
        }
        const Qt::KeyboardModifiers navigationModifiers =
            event->modifiers()
            & (Qt::ShiftModifier
               | Qt::ControlModifier
               | Qt::AltModifier
               | Qt::MetaModifier);
        if (navigation
            && (navigationModifiers
                    == Qt::NoModifier
                || navigationModifiers
                       == Qt::ShiftModifier)) {
            multiCursor.moveCarets(
                move,
                navigationModifiers
                    == Qt::ShiftModifier);
            event->accept();
            return true;
        }

        const Qt::KeyboardModifiers commandModifiers =
            event->modifiers()
            & (Qt::ControlModifier
               | Qt::AltModifier
               | Qt::MetaModifier);
        if (commandModifiers == Qt::NoModifier
            && !event->text().isEmpty()) {
            multiCursor.insertTextStructurally(
                event->text(),
                syntax.tsDocument());
            event->accept();
            return true;
        }
    }

    if ((primaryMode == EditorModeId::VirtualCursor
         || primaryMode == EditorModeId::None)
        && handleVirtualCursorKeyPress(editor, event)) {
        return true;
    }

    if (event->key() == Qt::Key_Escape
        && sourceNavigation.handleEscape(editor, selections)) {
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Escape && editor->textCursor().hasSelection()) {
        QTextCursor cursor = editor->textCursor();
        cursor.clearSelection();
        editor->setTextCursor(cursor);
        lineBoundarySelectionAnchor = -1;
        lineBoundarySelectionDirection = 0;
        lineBoundarySelectionAtPhysicalEdge = false;
        event->accept();
        return true;
    }

    if (primaryMode == EditorModeId::None
        && handleStructuralNavigation(editor, event)) {
        return true;
    }

    if (primaryMode == EditorModeId::None
        && handleLineBoundarySelection(editor, event)) {
        return true;
    }
    if (event->key() != Qt::Key_Alt) {
        lineBoundarySelectionAnchor = -1;
        lineBoundarySelectionDirection = 0;
        lineBoundarySelectionAtPhysicalEdge = false;
    }

    QString selectionActionId;
    if (matchesRegisteredShortcut(
            event,
            QString::fromLatin1(
                ActionIds::SelectExpandSmart))) {
        selectionActionId = QString::fromLatin1(
            ActionIds::SelectExpandSmart);
    } else if (matchesRegisteredShortcut(
                   event,
                   QString::fromLatin1(
                       ActionIds::
                           NavigationNextSelectedSymbolOccurrence))) {
        selectionActionId = QString::fromLatin1(
            ActionIds::NavigationNextSelectedSymbolOccurrence);
    } else if (matchesRegisteredShortcut(
                   event,
                   QString::fromLatin1(
                       ActionIds::
                           NavigationPreviousSelectedSymbolOccurrence))) {
        selectionActionId = QString::fromLatin1(
            ActionIds::NavigationPreviousSelectedSymbolOccurrence);
    }
    if (!selectionActionId.isEmpty()) {
        bool executed = requestRegisteredEditorAction(
            editor, selectionActionId);
        if (!executed) {
            QString message;
            if (selectionActionId
                == QString::fromLatin1(
                    ActionIds::SelectExpandSmart)) {
                executed = expandSmartSelection(
                    editor, &message);
            } else {
                executed = navigateSelectedSymbolOccurrence(
                    editor,
                    selectionActionId
                        == QString::fromLatin1(
                            ActionIds::
                                NavigationPreviousSelectedSymbolOccurrence),
                    &message);
            }
        }
        if (executed) {
            event->accept();
            return true;
        }
    }

    if (handleLexicalNavigationOrDeletion(editor, event))
        return true;

    const bool undoShortcut =
        matchesRegisteredShortcut(
            event, QStringLiteral("edit.undo"));
    const bool redoShortcut =
        matchesRegisteredShortcut(
            event, QStringLiteral("edit.redo"));
    if (undoShortcut || redoShortcut) {
        const QString actionId = undoShortcut
            ? QStringLiteral("edit.undo")
            : QStringLiteral("edit.redo");
        if (!requestRegisteredEditorAction(
                editor, actionId)) {
            if (undoShortcut)
                editor->undo();
            else
                editor->redo();
        }
        event->accept();
        return true;
    }

    if (matchesRegisteredShortcut(
            event, QStringLiteral("select.all"))) {
        if (!requestRegisteredEditorAction(
                editor,
                QStringLiteral("select.all"))) {
            editor->selectAll();
        }
        event->accept();
        return true;
    }

    if (matchesRegisteredShortcut(
            event, QStringLiteral("edit.find"))) {
        if (!requestRegisteredEditorAction(
                editor,
                QStringLiteral("edit.find"))) {
            editor->showFindDialog();
        }
        event->accept();
        return true;
    }

    if (matchesRegisteredShortcut(
            event, QStringLiteral("edit.replace"))) {
        if (!requestRegisteredEditorAction(
                editor,
                QStringLiteral("edit.replace"))) {
            editor->showReplaceDialog();
        }
        event->accept();
        return true;
    }

    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("navigation.goLine"))) {
        if (!requestRegisteredEditorAction(
                editor,
                QStringLiteral("navigation.goLine"))) {
            editor->showGotoLineDialog();
        }
        event->accept();
        return true;
    }
    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("format.document"))) {
        if (!requestRegisteredEditorAction(
                editor,
                QStringLiteral("format.document"))) {
            editor->formatDocument();
        }
        event->accept();
        return true;
    }
    if (handleSafeRename(this, editor, event))
        return true;

    QString moveLinesActionId;
    if (matchesRegisteredShortcut(
            event,
            QString::fromLatin1(
                ActionIds::EditMoveLinesUp))) {
        moveLinesActionId = QString::fromLatin1(
            ActionIds::EditMoveLinesUp);
    } else if (matchesRegisteredShortcut(
                   event,
                   QString::fromLatin1(
                       ActionIds::EditMoveLinesDown))) {
        moveLinesActionId = QString::fromLatin1(
            ActionIds::EditMoveLinesDown);
    }
    if (!moveLinesActionId.isEmpty()) {
        if (event->isAutoRepeat()) {
            event->accept();
            return true;
        }
        if (!requestRegisteredEditorAction(
                editor, moveLinesActionId)) {
            executeLineOperation(
                editor,
                moveLinesActionId
                        == QString::fromLatin1(
                            ActionIds::EditMoveLinesUp)
                    ? EditorLineOperation::MoveLinesUp
                    : EditorLineOperation::MoveLinesDown);
        }
        event->accept();
        return true;
    }

    handleControlKeyPress(editor, event);

    if (handleLineCommentShortcut(editor, event))
        return true;

    if (handleLineIndentShortcut(editor, event))
        return true;

    if (sourceNavigation.handleSourceSymbolShortcut(
            editor,
            event,
            semanticService(),
            sourceContextProvider(editor))) {
        sourceNavigation.syncMode();
        return true;
    }

    if (event->key() == Qt::Key_Shift) {
        event->ignore();
        return true;
    }

    if (columnMode.selectionActive()) {
        QString columnClipboardAction;
        if (matchesRegisteredShortcut(
                event,
                QStringLiteral("edit.copy"))) {
            columnClipboardAction =
                QStringLiteral("edit.copy");
        } else if (matchesRegisteredShortcut(
                       event,
                       QStringLiteral("edit.cut"))) {
            columnClipboardAction =
                QStringLiteral("edit.cut");
        } else if (matchesRegisteredShortcut(
                       event,
                       QStringLiteral("edit.paste"))) {
            columnClipboardAction =
                QStringLiteral("edit.paste");
        }
        if (!columnClipboardAction.isEmpty()) {
            if (!requestRegisteredEditorAction(
                    editor,
                    columnClipboardAction)) {
                if (columnClipboardAction
                    == QStringLiteral("edit.copy")) {
                    editor->copy();
                } else if (columnClipboardAction
                           == QStringLiteral("edit.cut")) {
                    editor->cut();
                } else {
                    editor->paste();
                }
            }
            event->accept();
            return true;
        }
    }

    if (columnMode.handleSelectionNavigation(
            editor,
            event)) {
        return true;
    }

    if (columnMode.handleSelectionKeyInput(
            editor,
            event)) {
        return true;
    }

    if (matchesRegisteredShortcut(
            event,
            QString::fromLatin1(
                ActionIds::EditDuplicateLines))) {
        if (!requestRegisteredEditorAction(
                editor,
                QString::fromLatin1(
                    ActionIds::EditDuplicateLines))) {
            executeLineOperation(
                editor,
                EditorLineOperation::DuplicateLines);
        }
        event->accept();
        return true;
    }

    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("edit.copy"))) {
        if (requestRegisteredEditorAction(
                editor,
                QStringLiteral("edit.copy"))) {
            event->accept();
            return true;
        }
        const EditorLineOperationResult result =
            executeLineOperation(
                editor,
                EditorLineOperation::Copy);
        if (result.handled) {
            event->accept();
            return true;
        }
    }

    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("edit.cut"))) {
        if (requestRegisteredEditorAction(
                editor,
                QStringLiteral("edit.cut"))) {
            event->accept();
            return true;
        }
        const EditorLineOperationResult result =
            executeLineOperation(
                editor,
                EditorLineOperation::Cut);
        if (result.handled) {
            event->accept();
            return true;
        }
    }

    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("edit.paste"))) {
        if (!requestRegisteredEditorAction(
                editor,
                QStringLiteral("edit.paste"))) {
            editor->paste();
        }
        event->accept();
        return true;
    }

    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("edit.deleteLines"))) {
        if (requestRegisteredEditorAction(
                editor,
                QStringLiteral("edit.deleteLines"))) {
            event->accept();
            return true;
        }
        const EditorLineOperationResult result =
            executeLineOperation(
                editor,
                EditorLineOperation::DeleteLines);
        if (result.handled) {
            event->accept();
            return true;
        }
    }

    if (matchesRegisteredShortcut(
            event,
            QStringLiteral("edit.joinLines"))) {
        if (requestRegisteredEditorAction(
                editor,
                QStringLiteral("edit.joinLines"))) {
            event->accept();
            return true;
        }
        const EditorLineOperationResult result =
            executeLineOperation(
                editor,
                EditorLineOperation::JoinWithNextLine);
        if (result.handled) {
            event->accept();
            return true;
        }
    }

    if (adjustSelectedRangeBound(editor, event))
        return true;

    if (completionWorkflow.handleCompletionPopupKey(event))
        return true;

    if (handleBracketRangeTab(editor, event))
        return true;

    if (structuralInput.handleKeyPress(editor, event, syntax))
        return true;

    if (completionWorkflow.handleCompletionPopupKey(event))
        return true;

    if (event->key() == Qt::Key_Backtab
        || (event->key() == Qt::Key_Tab
            && event->modifiers() == Qt::ShiftModifier)) {
        applyLineUnindent(editor);
        event->accept();
        return true;
    }

    if (event->key() == Qt::Key_Tab
        && event->modifiers() == Qt::NoModifier) {
        QTextCursor cursor = editor->textCursor();
        cursor.insertText(QStringLiteral("    "));
        editor->setTextCursor(cursor);
        event->accept();
        return true;
    }

    return false;
}

namespace {
EditorLogicalCursorState captureLogicalCursorState(
    MyCodeEditor* editor,
    const EditorColumnModeController& columnMode,
    const EditorMultiCursorController& multiCursor)
{
    EditorLogicalCursorState state;
    if (!editor)
        return state;
    const QTextCursor cursor = editor->textCursor();
    state.anchor = cursor.anchor();
    state.position = cursor.position();
    state.column = columnMode.snapshotForTest();
    state.multiCursor = multiCursor.snapshot();
    return state;
}
}

void MyCodeEditorState::beginSynchronousEditTransaction(
    MyCodeEditor* editor)
{
    if (synchronousEditTransactionDepth == 0) {
        synchronousEditStartRevision = editor && editor->document()
            ? editor->document()->revision()
            : -1;
        synchronousEditStartUndoSteps = editor && editor->document()
            ? editor->document()->availableUndoSteps()
            : 0;
        synchronousEditStartCursor = captureLogicalCursorState(
            editor, columnMode, multiCursor);
        synchronousEditIsUndoRedo = false;
        undoRedoViewportCaptured = false;
    }
    ++synchronousEditTransactionDepth;
}

void MyCodeEditorState::endSynchronousEditTransaction(MyCodeEditor* editor)
{
    lifecycleTrace("transaction.end-enter");
    if (synchronousEditTransactionDepth <= 0)
        return;
    --synchronousEditTransactionDepth;
    if (synchronousEditTransactionDepth != 0)
        return;
    ++completedSynchronousEditTransactions;
    const int finalRevision = editor && editor->document()
        ? editor->document()->revision()
        : -1;
    const int finalUndoSteps = editor && editor->document()
        ? editor->document()->availableUndoSteps()
        : 0;
    if (!synchronousEditIsUndoRedo
        && finalRevision != synchronousEditStartRevision) {
        if (finalUndoSteps == 0) {
            undoCursorEntries.clear();
        } else if (finalUndoSteps >= synchronousEditStartUndoSteps) {
            while (!undoCursorEntries.isEmpty()
                   && undoCursorEntries.constLast().afterUndoSteps
                          > synchronousEditStartUndoSteps) {
                undoCursorEntries.removeLast();
            }
            const EditorLogicalCursorState finalCursor =
                captureLogicalCursorState(editor,
                                          columnMode,
                                          multiCursor);
            if (!undoCursorEntries.isEmpty()
                && undoCursorEntries.constLast().afterUndoSteps
                       == finalUndoSteps) {
                undoCursorEntries.last().after = finalCursor;
            } else {
                undoCursorEntries.append({
                    synchronousEditStartUndoSteps,
                    finalUndoSteps,
                    synchronousEditStartCursor,
                    finalCursor,
                });
            }
        }
    }
    lifecycleTrace("transaction.before-finish");
    QElapsedTimer finishTimer;
    finishTimer.start();
    finishEditorInput(editor);
    hotPathMetrics.editorInputFinishNanoseconds +=
        static_cast<std::uint64_t>(finishTimer.nsecsElapsed());
    lifecycleTrace("transaction.after-finish");
    if (synchronousEditIsUndoRedo
        && undoRedoViewportCaptured
        && editor) {
        QScrollBar* vertical = editor->verticalScrollBar();
        QScrollBar* horizontal = editor->horizontalScrollBar();
        if (vertical) {
            vertical->setValue(
                qBound(vertical->minimum(),
                       undoRedoVerticalScroll,
                       vertical->maximum()));
        }
        if (horizontal) {
            horizontal->setValue(
                qBound(horizontal->minimum(),
                       undoRedoHorizontalScroll,
                       horizontal->maximum()));
        }
        editor->viewport()->update();
    }
    synchronousEditIsUndoRedo = false;
    undoRedoViewportCaptured = false;
}

void MyCodeEditorState::beginUndoRedo(MyCodeEditor* editor)
{
    synchronousEditIsUndoRedo = true;
    if (!editor || undoRedoViewportCaptured)
        return;
    if (QScrollBar* vertical = editor->verticalScrollBar())
        undoRedoVerticalScroll = vertical->value();
    if (QScrollBar* horizontal = editor->horizontalScrollBar())
        undoRedoHorizontalScroll = horizontal->value();
    undoRedoViewportCaptured = true;
}

void MyCodeEditorState::restoreCursorAfterUndoRedo(
    MyCodeEditor* editor,
    bool redo,
    int beforeUndoSteps,
    int afterUndoSteps)
{
    if (!editor || !editor->document())
        return;

    const EditorUndoCursorEntry* match = nullptr;
    for (auto it = undoCursorEntries.crbegin();
         it != undoCursorEntries.crend();
         ++it) {
        const bool matches = redo
            ? it->beforeUndoSteps == beforeUndoSteps
                  && it->afterUndoSteps == afterUndoSteps
            : it->afterUndoSteps == beforeUndoSteps
                  && it->beforeUndoSteps == afterUndoSteps;
        if (matches) {
            match = &*it;
            break;
        }
    }
    if (!match) {
        columnMode.clearSelection(editor);
        clearVirtualCursor(editor);
        return;
    }

    const EditorLogicalCursorState& logical =
        redo ? match->after : match->before;
    const int documentEnd = qMax(
        0, editor->document()->characterCount() - 1);
    QTextCursor cursor(editor->document());
    cursor.setPosition(qBound(0, logical.anchor, documentEnd));
    cursor.setPosition(qBound(0, logical.position, documentEnd),
                       QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    columnMode.restoreSnapshot(editor, logical.column);
    if (logical.multiCursor.active
        && !logical.multiCursor.carets.isEmpty()) {
        multiCursor.setCarets(logical.multiCursor.carets,
                              logical.multiCursor.primaryIndex);
    } else {
        multiCursor.resetToEditorCursor();
    }
}

EditorSynchronousEditState
MyCodeEditorState::synchronousEditStateForTest() const
{
    EditorSynchronousEditState editState;
    editState.transactionDepth = synchronousEditTransactionDepth;
    editState.presentationPending = editorPresentationPending;
    editState.cursorPresentationSuppressed = suppressNextCursorPresentation;
    editState.completedTransactionCount = completedSynchronousEditTransactions;
    return editState;
}

void MyCodeEditorState::finishEditorInput(MyCodeEditor* editor)
{
    lifecycleTrace("finish.enter");
    suppressNextCursorPresentation = false;
    if (!editorPresentationPending)
        return;
    QElapsedTimer phaseTimer;
    phaseTimer.start();
    sourceNavigation.handleEditorContentChanged(editor, selections);
    lifecycleTrace("finish.navigation-content");
    sourceNavigation.syncMode();
    hotPathMetrics.editorInputNavigationNanoseconds +=
        static_cast<std::uint64_t>(phaseTimer.nsecsElapsed());
    lifecycleTrace("finish.navigation-mode");
    phaseTimer.restart();
    templateSlots.flushPendingPresentation(editor);
    lifecycleTrace("finish.templates");
    if (ghostPresentationPending) {
        ghostPresentationPending = false;
        if (editor) {
            gutter.updateViewportMargins(editor);
            handleResize(editor);
            gutter.handleUpdateRequest(editor,
                                       editor->viewport()->rect(),
                                       0);
            editor->viewport()->update();
        }
    }
    hotPathMetrics.editorInputTemplateNanoseconds +=
        static_cast<std::uint64_t>(phaseTimer.nsecsElapsed());
    phaseTimer.restart();
    refreshDerivedEditorState(editor, false);
    hotPathMetrics.editorInputDerivedStateNanoseconds +=
        static_cast<std::uint64_t>(phaseTimer.nsecsElapsed());
    lifecycleTrace("finish.derived");
    // Extra selections own QTextCursor instances. Rebuild their presentation
    // only after QTextDocument has finished adjusting its registered cursors
    // for the current edit.
    phaseTimer.restart();
    refreshSemanticDecorationPresentation(editor);
    hotPathMetrics.editorInputSemanticDecorationNanoseconds +=
        static_cast<std::uint64_t>(phaseTimer.nsecsElapsed());
    lifecycleTrace("finish.semantic-decorations");
    phaseTimer.restart();
    refreshScopeAndCurrentLineHighlight(editor);
    hotPathMetrics.editorInputHighlightNanoseconds +=
        static_cast<std::uint64_t>(phaseTimer.nsecsElapsed());
    lifecycleTrace("finish.highlights");
    editorPresentationPending = false;
}

bool MyCodeEditorState::handleDragEnter(
    MyCodeEditor* editor,
    QDragEnterEvent* event)
{
    return folding.handleFoldShelfDragEnter(editor, event);
}

bool MyCodeEditorState::handleDragMove(
    MyCodeEditor* editor,
    QDragMoveEvent* event)
{
    return folding.handleFoldShelfDragMove(editor, event);
}

bool MyCodeEditorState::handleDrop(MyCodeEditor* editor, QDropEvent* event)
{
    return folding.handleFoldShelfDrop(editor, event);
}

void MyCodeEditorState::handleResize(MyCodeEditor* editor)
{
    gutter.updateViewportMargins(editor);
    gutter.resizeTo(editor, editor->contentsRect());
    refreshVisibleRegionPresentation(editor);
}

bool MyCodeEditorState::handleGutterMousePress(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!editor || !event)
        return false;

    columnMode.clearPendingColumnAnchor();

    const int y = static_cast<int>(event->position().y());
    if (y < 0 || y >= editor->viewport()->height())
        return false;

    ++hotPathMetrics.gutterBlockProbes;
    const QTextBlock block = editor->cursorForPosition(QPoint(0, y)).block();
    if (!block.isValid()
        || !editor->sourceLineVisible(block.blockNumber()))
        return false;
    const EditorBlockGeometry geometry = editor->blockGeometry(
        block.blockNumber());
    if (y < geometry.top || y > geometry.top + geometry.height)
        return false;

    if (folding.foldRegionMarkModeActive())
        return folding.handleFoldRegionGutterLine(editor,
                                                  block.blockNumber());
    if (event->position().x() > 14)
        return false;
    return folding.toggleFoldAtLine(editor, block.blockNumber());
}

bool MyCodeEditorState::handleGutterMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    const auto closeDiagnosticPeek = [this]() {
        EditorHoverPopup* peek = sourceNavigation.currentPeek();
        if (peek
            && peek->isVisible()
            && peek->contentModel().kind
                   == PeekContentKind::DiagnosticDetail) {
            sourceNavigation.closeExternalPeek();
        }
    };
    if (!editor || !event)
        return false;

    const int y = static_cast<int>(event->position().y());
    if (y < 0 || y >= editor->viewport()->height()) {
        closeDiagnosticPeek();
        return false;
    }

    ++hotPathMetrics.gutterBlockProbes;
    const QTextBlock block = editor->cursorForPosition(QPoint(0, y)).block();
    if (!block.isValid()
        || !editor->sourceLineVisible(block.blockNumber())) {
        closeDiagnosticPeek();
        return false;
    }
    const EditorBlockGeometry geometry = editor->blockGeometry(
        block.blockNumber());
    if (y < geometry.top || y > geometry.top + geometry.height) {
        closeDiagnosticPeek();
        return false;
    }

    const qreal x = event->position().x();
    if (annotationDisplayOptions.enabled
        && x >= 14.0 && x <= 28.0
        && diagnosticSeverityByLine.contains(block.blockNumber())) {
        const QString tooltip =
            diagnosticTooltipForLine(block.blockNumber());
        if (!tooltip.isEmpty()) {
            QStringList rows = tooltip.split(
                QLatin1Char('\n'),
                Qt::SkipEmptyParts);
            const QString title =
                QStringLiteral("Diagnostics - line %1")
                    .arg(block.blockNumber() + 1);
            EditorHoverPopup* peek =
                sourceNavigation.beginExternalPeek(
                    editor, false);
            QStringList visibleRows;
            if (peek
                && peek->contentModel().kind
                       == PeekContentKind::DiagnosticDetail) {
                for (const PeekContentRow& row :
                     peek->contentModel().rows) {
                    visibleRows.append(row.text);
                }
            }
            if (peek
                && (!peek->isVisible()
                    || peek->contentModel().kind
                           != PeekContentKind::DiagnosticDetail
                    || peek->contentModel().title != title
                    || visibleRows != rows)) {
                const QString message =
                    rows.isEmpty() ? QString() : rows.takeFirst();
                QPoint anchorPoint =
                    event->globalPosition().toPoint();
                anchorPoint.rx() -= 7;
                anchorPoint.ry() +=
                    static_cast<int>(geometry.top) - y;
                peek->showDiagnosticDetail(
                    title,
                    message,
                    rows,
                    QRect(anchorPoint,
                          QSize(14,
                                qMax(1,
                                     static_cast<int>(
                                         geometry.height)))),
                    editor->font());
            }
        }
        return true;
    }
    closeDiagnosticPeek();

    const bool handled =
        folding.handleFoldRegionHoverLine(editor, block.blockNumber());
    if (handled)
        gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);
    return handled;
}

void MyCodeEditorState::paintGutterDecorations(
    MyCodeEditor* editor,
    QPainter& painter,
    const QRect& rect) const
{
    folding.paintGutter(editor, painter, rect);
    if (!editor
        || !annotationDisplayOptions.enabled
        || diagnosticSeverityByLine.isEmpty()) {
        return;
    }

    QTextBlock block = editor->firstVisibleBlock();
    AnnotationLayerQuery query;
    query.firstVisibleLine = qMax(0, block.blockNumber());
    query.lastVisibleLine = qMax(
        query.firstVisibleLine,
        editor->cursorForPosition(
            QPoint(0, qMax(0, rect.bottom()))).blockNumber());
    query.maxAnnotationsPerLine =
        annotationDisplayOptions.maxAnnotationsPerLine;
    query.maxLanes = 1;
    QHash<int, SemanticDiagnostic::Severity> visibleDiagnostics;
    const AnnotationLayerReport report = annotationLayer.resolve(query);
    for (const ResolvedEditorAnnotation& resolved :
         report.annotations) {
        const EditorAnnotation& annotation = resolved.annotation;
        if (annotation.kind != EditorAnnotationKind::Diagnostic
            || annotation.placement
                   != EditorAnnotationPlacement::Gutter) {
            continue;
        }
        visibleDiagnostics.insert(
            annotation.range.firstLine,
            annotationDiagnosticSeverity(annotation));
    }

    int top = static_cast<int>(
        editor->blockBoundingGeometry(block)
            .translated(editor->contentOffset())
            .top());
    int bottom =
        top + static_cast<int>(editor->blockBoundingRect(block).height());
    while (block.isValid() && top <= rect.bottom()) {
        const auto severity =
            visibleDiagnostics.constFind(block.blockNumber());
        if (severity != visibleDiagnostics.constEnd()) {
            const int middle = top + (bottom - top) / 2;
            const QPolygon triangle{
                QPoint(21, middle - 6),
                QPoint(15, middle + 5),
                QPoint(27, middle + 5)
            };
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(diagnosticSeverityColor(severity.value()));
            painter.drawPolygon(triangle);
            painter.setPen(
                InsightVisualStyle::theme().button.textChecked);
            QFont iconFont = painter.font();
            iconFont.setBold(true);
            iconFont.setPixelSize(9);
            painter.setFont(iconFont);
            painter.drawText(
                QRect(15, middle - 5, 12, 10),
                Qt::AlignCenter,
                QStringLiteral("!"));
            painter.restore();
        }

        block = editor->nextVisibleBlock(block);
        top = bottom;
        bottom =
            top + static_cast<int>(editor->blockBoundingRect(block).height());
    }
}

void MyCodeEditorState::paintDiagnosticOverview(
    MyCodeEditor* editor,
    QPaintEvent* event)
{
    if (!editor
        || !event
        || !annotationDisplayOptions.enabled
        || diagnosticOverviewSeverityByBucket.isEmpty()
        || editor->blockCount() <= 0) {
        return;
    }

    QPainter painter(editor->viewport());
    const int markerWidth = 5;
    const int markerHeight = 4;
    const int x = qMax(0, editor->viewport()->width() - markerWidth);
    const int availableHeight =
        qMax(1, editor->viewport()->height() - markerHeight);
    for (auto markerIt =
             diagnosticOverviewSeverityByBucket.cbegin();
         markerIt
             != diagnosticOverviewSeverityByBucket.cend();
         ++markerIt) {
        ++hotPathMetrics.diagnosticOverviewCandidatesPainted;
        const int y = qRound(
            static_cast<qreal>(markerIt.key())
            / (kDiagnosticOverviewBucketCount - 1)
            * availableHeight);
        const QRect marker(x, y, markerWidth, markerHeight);
        if (!event->rect().intersects(marker))
            continue;
        painter.fillRect(marker,
                         diagnosticSeverityColor(
                             markerIt.value()));
    }
}

void MyCodeEditorState::paintFoldPlaceholders(
    MyCodeEditor* editor,
    QPaintEvent* event) const
{
    Q_UNUSED(event)
    if (!editor || !folding.hasPaintOverlay())
        return;
    QPainter painter(editor->viewport());
    folding.paintPlaceholders(editor, painter);
}

void MyCodeEditorState::paintColumnSelection(
    MyCodeEditor* editor,
    QPaintEvent* event) const
{
    // The existing paint hook remains for MyCodeEditor compatibility.
    // Column carets are rendered by the unified annotation pass.
    Q_UNUSED(editor)
    Q_UNUSED(event)
}

void MyCodeEditorState::paintMultiCursor(
    MyCodeEditor* editor,
    QPaintEvent* event) const
{
    multiCursor.paint(editor, event);
}

void MyCodeEditorState::handleContextMenu(
    MyCodeEditor* editor,
    QContextMenuEvent* event)
{
    if (signalSelection.active()
        && editor && event) {
        signalSelection.completeForContextMenu();
        const QTextCursor contextCursor =
            editor->cursorForPosition(event->pos());
        QMenu menu(editor);
        EditorContextMenuRequest request;
        request.actionContext.workspacePath =
            hierarchyInstance.workspacePath;
        request.actionContext.fileName = identity.current();
        request.actionContext.moduleName =
            currentModuleName(editor);
        request.actionContext.semanticState =
            !semanticRevisionText.isEmpty()
                && cachedDocumentText() == editor->toPlainText()
                ? EditorActionSemanticState::Current
                : EditorActionSemanticState::Stale;
        request.actionContext.resolvedHierarchy =
            hierarchyInstance;
        EditorContextMenuCapability capability;
        capability.actionId =
            QStringLiteral("refactor.createAssignmentQueue");
        capability.executable =
            signalSelection.hasSelection();
        capability.unavailableReason =
            QStringLiteral("Select at least one signal.");
        request.capabilities.append(capability);
        const EditorContextMenuModel model =
            buildEditorContextMenuModel(request);
        QMenu* refactorMenu =
            menu.addMenu(QStringLiteral("Refactor"));
        refactorMenu->setObjectName(
            QStringLiteral("editorContextMenu.refactor"));
        const EditorContextMenuItem item =
            !model.sections.isEmpty()
                && !model.sections.first().items.isEmpty()
                ? model.sections.first().items.first()
                : EditorContextMenuItem();
        QAction* createQueue =
            refactorMenu->addAction(item.text);
        createQueue->setObjectName(
            QStringLiteral("refactor.createAssignmentQueue"));
        createQueue->setProperty(
            "actionId",
            QStringLiteral(
                "refactor.createAssignmentQueue"));
        if (const ActionDescriptor* descriptor =
                findActionById(QStringLiteral(
                    "refactor.createAssignmentQueue"))) {
            createQueue->setProperty(
                "executionRoute",
                descriptor->executionRoute);
        }
        createQueue->setEnabled(item.enabled);
        createQueue->setProperty(
            "visibleReason", item.visibleReason);
        if (!item.visibleReason.isEmpty())
            createQueue->setStatusTip(item.visibleReason);
        bool actionTriggered = false;
        QObject::connect(
            createQueue,
            &QAction::triggered,
            editor,
            [this,
             editor,
             position = contextCursor.position(),
             &actionTriggered]() {
                actionTriggered = true;
                QVariantMap parameters;
                parameters.insert(
                    QStringLiteral("cursorPosition"),
                    position);
                bool handled = false;
                emit editor->registeredActionRequested(
                    QStringLiteral(
                        "refactor.createAssignmentQueue"),
                    parameters,
                    &handled);
                if (handled)
                    return;
                QString message;
                if (!createAssignmentQueueAt(
                        editor, position, &message)
                    && !message.isEmpty()) {
                    emit editor->editorStatusMessageRequested(message);
                }
            });
        menu.exec(event->globalPos());
        if (!actionTriggered)
            cancelSignalSelectionMode(editor);
        event->accept();
        return;
    }
    sourceNavigation.handleContextMenu(
        editor,
        event,
        sourceContextProvider(editor));
}

bool MyCodeEditorState::editInstanceSlotsAt(
    MyCodeEditor* editor,
    int cursorPosition,
    QString* message)
{
    if (!editor || !editor->document()) {
        if (message)
            *message = QStringLiteral("No editor document");
        return false;
    }

    const TSInstantiationTarget target =
        syntax.instantiationAt(cursorPosition);
    if (!target.ok()) {
        if (message)
            *message = QStringLiteral("No complete module instantiation");
        return false;
    }

    CodeTemplateSlotList metadata;
    const auto appendSlots =
        [&metadata, &target](const QList<TSExpressionSlot>& source,
                             const QString& prefix) {
            for (int index = 0; index < source.size(); ++index) {
                const TSExpressionSlot& expression = source.at(index);
                if (!expression.ok())
                    continue;
                CodeTemplateSlot slot;
                slot.name = expression.name.isEmpty()
                    ? QStringLiteral("%1 %2")
                          .arg(prefix)
                          .arg(index + 1)
                    : expression.name;
                slot.start = expression.startChar - target.startChar;
                slot.length =
                    expression.endChar - expression.startChar;
                metadata.append(slot);
            }
        };
    appendSlots(target.parameterActuals,
                QStringLiteral("parameter"));
    appendSlots(target.portActuals,
                QStringLiteral("port"));
    if (metadata.isEmpty()) {
        if (message)
            *message = QStringLiteral("Instance has no editable actuals");
        return false;
    }

    startTemplateSlotMode(editor,
                          target.startChar,
                          target.endChar - target.startChar,
                          metadata);
    if (!templateSlotModeActive()) {
        if (message)
            *message = QStringLiteral("Instance slot ranges are invalid");
        return false;
    }
    if (message)
        *message = QStringLiteral("Editing instance slots");
    return true;
}

DeclareSignalFactCollectionResult
MyCodeEditorState::signalDefinitionFactsAt(
    const MyCodeEditor* editor,
    int cursorPosition,
    SemanticSnapshotToken* semanticToken,
    std::uint64_t expectedSemanticGeneration,
    std::uint64_t expectedDocumentRevision) const
{
    if (semanticToken)
        *semanticToken = {};

    DeclareSignalFactCollectionQuery query;
    query.document = syntax.tsDocument();
    query.fileName = identity.current();
    query.cursorPosition = cursorPosition;
    query.documentRevision = semanticDocumentRevision();
    query.expectedDocumentRevision =
        expectedDocumentRevision != 0
        ? expectedDocumentRevision
        : query.documentRevision;
    if (hierarchyInstance.isBound()) {
        query.hierarchyInstancePath =
            hierarchyInstance.instancePath;
    }

    SemanticIndex* index = SemanticIndex::getInstance();
    if (index) {
        query.semanticSnapshot = index->snapshotToken();
        query.expectedSemanticGeneration =
            expectedSemanticGeneration != 0
            ? expectedSemanticGeneration
            : query.semanticSnapshot.revision;
    }
    if (semanticToken)
        *semanticToken = query.semanticSnapshot;

    if (!editor || !editor->document())
        query.document = nullptr;
    return DeclareSignalFactCollector::collect(query);
}

DeclareSignalProposal
MyCodeEditorState::signalDefinitionProposalAt(
    const MyCodeEditor* editor,
    int cursorPosition,
    DeclareSignalFactCollectionResult* facts,
    SemanticSnapshotToken* semanticToken,
    std::uint64_t expectedSemanticGeneration,
    std::uint64_t expectedDocumentRevision) const
{
    const DeclareSignalFactCollectionResult collected =
        signalDefinitionFactsAt(
            editor,
            cursorPosition,
            semanticToken,
            expectedSemanticGeneration,
            expectedDocumentRevision);
    if (facts)
        *facts = collected;
    if (!collected.acceptedForProposal())
        return {};
    return DeclareSignalService::propose(
        collected.request);
}

QString MyCodeEditorState::signalDefinitionCandidateAt(
    const MyCodeEditor* editor,
    int cursorPosition,
    QString* failureReason) const
{
    const auto fail = [failureReason](const QString& reason) {
        if (failureReason)
            *failureReason = reason;
        return QString();
    };

    DeclareSignalFactCollectionResult facts;
    const DeclareSignalProposal proposal =
        signalDefinitionProposalAt(
            editor, cursorPosition, &facts);
    if (!facts.acceptedForProposal()) {
        const QString reason =
            firstDeclareSignalIssue(facts, proposal);
        return fail(
            reason.isEmpty()
                ? QStringLiteral(
                      "No current structured semantic facts are available")
                : reason);
    }
    if (proposal.classification
        == DeclareSignalProposalClass::Conflict) {
        const QString reason =
            firstDeclareSignalIssue(facts, proposal);
        return fail(
            reason.isEmpty()
                ? QStringLiteral(
                      "Declare Signal proposal has a conflict")
                : QStringLiteral("Conflict: %1").arg(reason));
    }

    const DeclareSignalCandidate* candidate =
        proposal.primaryCandidate();
    if (!candidate) {
        const QString reason =
            firstDeclareSignalIssue(facts, proposal);
        return fail(
            reason.isEmpty()
                ? QStringLiteral(
                      "No safe declaration candidate is available")
                : QStringLiteral("%1: %2")
                      .arg(
                          declareSignalClassText(
                              proposal.classification),
                          reason));
    }

    const QString declaration =
        candidate->declarationText(proposal.identifier);
    if (declaration.isEmpty()) {
        return fail(QStringLiteral(
            "The structured declaration candidate is incomplete"));
    }
    if (failureReason)
        failureReason->clear();
    return declaration;
}

void MyCodeEditorState::clearSignalDefinitionEditorState()
{
    ++signalDefinitionSessionGeneration;
    QObject::disconnect(
        signalDefinitionPeekClosedConnection);
    signalDefinitionPeekClosedConnection = {};
    QObject::disconnect(
        signalDefinitionReturnConnection);
    signalDefinitionReturnConnection = {};
    signalDefinitionPeek.clear();
    signalDefinitionPeekActive = false;
    signalDefinitionEditor.clear();
    signalDefinitionIdentifierStart = -1;
    signalDefinitionIdentifierEnd = -1;
    signalDefinitionIdentifier.clear();
    signalDefinitionDocument.clear();
    signalDefinitionDocumentRevision = 0;
    signalDefinitionFileName.clear();
    signalDefinitionFileIdentityKey.clear();
    signalDefinitionHierarchyInstance = {};
    signalDefinitionSemanticToken = {};
    signalDefinitionCandidateId.clear();
    signalDefinitionScopeKind =
        DeclareSignalScopeKind::Module;
    signalDefinitionBlockScopeId.clear();
}

void MyCodeEditorState::cancelSignalDefinitionEditor()
{
    const QPointer<EditorHoverPopup> pendingPeek =
        signalDefinitionPeek;
    const bool closePendingPeek =
        signalDefinitionPeekActive
        && pendingPeek
        && sourceNavigation.currentPeek()
               == pendingPeek.data();
    clearSignalDefinitionEditorState();
    if (closePendingPeek)
        sourceNavigation.closeExternalPeek();
}

bool MyCodeEditorState::beginSignalDefinitionEditor(
    MyCodeEditor* editor,
    int cursorPosition,
    QString* failureReason)
{
    if (!editor || !editor->document()) {
        if (failureReason)
            *failureReason = QStringLiteral("No editor document");
        return false;
    }

    const QPointer<QTextDocument> capturedDocument(
        editor->document());
    const HierarchyInstanceContext capturedHierarchyInstance =
        hierarchyInstance;
    DeclareSignalFactCollectionResult facts;
    SemanticSnapshotToken semanticToken;
    const DeclareSignalProposal proposal =
        signalDefinitionProposalAt(
            editor,
            cursorPosition,
            &facts,
            &semanticToken);
    if (!facts.acceptedForProposal()
        || !facts.request.identifierAcceptedBySyntax
        || facts.request.uses.isEmpty()) {
        if (failureReason) {
            const QString reason =
                firstDeclareSignalIssue(facts, proposal);
            *failureReason =
                reason.isEmpty()
                ? QStringLiteral(
                      "No structured Declare Signal context is available")
                : reason;
        }
        return false;
    }

    const TSIdentifierTarget selected =
        syntax.identifierAt(cursorPosition);
    if (!selected.ok()
        || selected.text != proposal.identifier) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Declare Signal identifier context is stale");
        }
        return false;
    }
    const std::uint64_t capturedDocumentRevision =
        facts.request.documentRevision;
    const QString capturedFileName = identity.current();
    const QString capturedFileIdentityKey =
        EditorFileIdentity::lookupKey(capturedFileName);

    const DeclareSignalCandidate* primary =
        proposal.primaryCandidate();
    const QString primaryDeclaration =
        primary
        ? primary->declarationText(proposal.identifier)
        : QString();
    cancelSignalDefinitionEditor();
    EditorHoverPopup* peek =
        sourceNavigation.beginExternalPeek(editor, true);
    if (!peek) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Unable to open the declaration preview");
        }
        return false;
    }

    QTextCursor anchor(editor->document());
    anchor.setPosition(selected.startChar);
    const QRect anchorRect = editor->cursorRect(anchor);
    const QRect globalAnchorRect(
        editor->viewport()->mapToGlobal(anchorRect.topLeft()),
        anchorRect.size());
    const int availableWidth =
        qMax(260, editor->viewport()->width() - 12);
    int widestText =
        editor->fontMetrics().horizontalAdvance(
            QStringLiteral("Classification: Conflict"));
    for (const DeclareSignalCandidate& candidate :
         proposal.candidates) {
        widestText = qMax(
            widestText,
            editor->fontMetrics().horizontalAdvance(
                candidate.declarationText(
                    proposal.identifier)));
    }
    const int minimumWidth = qMin(300, availableWidth);
    const int desiredWidth =
        qBound(minimumWidth,
               widestText + 120,
               availableWidth);

    PeekContentModel content;
    content.kind = PeekContentKind::DeclarationPreview;
    content.title =
        QStringLiteral("Declare signal - %1")
            .arg(declareSignalClassText(
                proposal.classification));
    content.rows.append(
        {QStringLiteral("Classification: %1")
             .arg(declareSignalClassText(
                 proposal.classification)),
         proposal.classification
                 == DeclareSignalProposalClass::Conflict
             ? PeekContentRowRole::Warning
             : PeekContentRowRole::Body,
         false});
    for (int index = 0;
         index < proposal.candidates.size();
         ++index) {
        const DeclareSignalCandidate& candidate =
            proposal.candidates.at(index);
        content.rows.append(
            {QStringLiteral("Candidate %1 (%2): %3")
                 .arg(index + 1)
                 .arg(declareSignalCandidateKindText(candidate))
                 .arg(candidate.declarationText(
                     proposal.identifier)),
             PeekContentRowRole::Code,
             true});
    }

    int issueCount = 0;
    for (const DeclareSignalFactCollectionIssue& issue :
         facts.issues) {
        if (issue.message.isEmpty())
            continue;
        content.rows.append(
            {issue.message,
             PeekContentRowRole::Muted,
             true});
        if (++issueCount >= 6)
            break;
    }
    if (issueCount < 6) {
        for (const DeclareSignalIssue& issue :
             proposal.issues) {
            if (issue.message.isEmpty())
                continue;
            content.rows.append(
                {issue.message,
                 issue.disposition
                         == DeclareSignalIssueDisposition::Conflict
                     ? PeekContentRowRole::Warning
                     : PeekContentRowRole::Muted,
                 true});
            if (++issueCount >= 6)
                break;
        }
    }

    const bool editable =
        proposal.actionable()
        && primary
        && !primaryDeclaration.isEmpty();
    if (editable) {
        content.rows.append(
            {QStringLiteral(
                 "Edit the selected candidate. Enter confirms; Esc cancels."),
             PeekContentRowRole::Muted,
             true});
        content.editor.enabled = true;
        content.editor.text = primaryDeclaration;
        content.editor.placeholderText =
            QStringLiteral("SystemVerilog declaration");
        content.editor.objectName =
            QStringLiteral("signalDefinitionInlineEditor");
        content.editor.minimumWidth =
            qMax(240, desiredWidth - 24);
    } else if (proposal.classification
               == DeclareSignalProposalClass::Conflict) {
        content.rows.append(
            {QStringLiteral(
                 "Conflict proposals cannot be applied."),
             PeekContentRowRole::Warning,
             true});
    } else {
        content.rows.append(
            {QStringLiteral(
                 "No complete structured candidate is available."),
             PeekContentRowRole::Muted,
             true});
    }
    content.maximumSize = QSize(
        qMin(availableWidth, desiredWidth + 24),
        qMin(420,
             120 + content.rows.size()
                 * editor->fontMetrics().height() * 2));
    peek->showContent(
        content,
        globalAnchorRect,
        editor->font());
    SemanticIndex* semanticIndex =
        SemanticIndex::getInstance();
    const SemanticSnapshotToken shownToken =
        semanticIndex
        ? semanticIndex->snapshotToken()
        : SemanticSnapshotToken{};
    if (semanticDocumentRevision()
                != capturedDocumentRevision
        || editor->document()
               != capturedDocument.data()
        || !(hierarchyInstance
             == capturedHierarchyInstance)
        || !sameSemanticSnapshotToken(
            semanticToken, shownToken)
        || capturedFileIdentityKey.isEmpty()
        || capturedFileIdentityKey
               != EditorFileIdentity::lookupKey(
                   identity.current())
        || !EditorFileIdentity::same(
            capturedFileName, identity.current())) {
        sourceNavigation.closeExternalPeek();
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Declare Signal context changed while opening the preview");
        }
        return false;
    }
    const std::uint64_t sessionGeneration =
        ++signalDefinitionSessionGeneration;
    signalDefinitionPeek = peek;
    signalDefinitionPeekActive = true;
    const QPointer<EditorHoverPopup> pendingPeek(peek);
    signalDefinitionPeekClosedConnection =
        QObject::connect(
            peek,
            &EditorHoverPopup::closed,
            editor,
            [this, pendingPeek, sessionGeneration]() {
                if (signalDefinitionSessionGeneration
                        != sessionGeneration
                    || signalDefinitionPeek != pendingPeek) {
                    return;
                }
                clearSignalDefinitionEditorState();
            });

    if (!editable) {
        if (failureReason)
            failureReason->clear();
        return true;
    }

    QLineEdit* lineEdit = peek->editableLineEdit();
    if (!lineEdit) {
        cancelSignalDefinitionEditor();
        if (failureReason) {
            *failureReason = QStringLiteral(
                "Unable to create declaration editor");
        }
        return false;
    }
    signalDefinitionEditor = lineEdit;
    signalDefinitionIdentifierStart =
        selected.startChar;
    signalDefinitionIdentifierEnd =
        selected.endChar;
    signalDefinitionIdentifier =
        selected.text;
    signalDefinitionDocument =
        capturedDocument;
    signalDefinitionDocumentRevision =
        capturedDocumentRevision;
    signalDefinitionFileName = capturedFileName;
    signalDefinitionFileIdentityKey =
        capturedFileIdentityKey;
    signalDefinitionHierarchyInstance =
        capturedHierarchyInstance;
    signalDefinitionSemanticToken = semanticToken;
    signalDefinitionCandidateId =
        primary->candidateId;
    signalDefinitionScopeKind =
        primary->scopeKind;
    signalDefinitionBlockScopeId =
        primary->blockScopeId;

    const QPointer<QLineEdit> pendingEditor(lineEdit);
    signalDefinitionReturnConnection =
        QObject::connect(
            lineEdit,
            &QLineEdit::returnPressed,
            editor,
            [this,
             editor,
             pendingEditor,
             sessionGeneration]() {
                if (!pendingEditor
                    || signalDefinitionSessionGeneration
                           != sessionGeneration
                    || signalDefinitionEditor
                           != pendingEditor) {
                    return;
                }
                const QString declarationText =
                    pendingEditor->text();
                auto edit =
                    editor
                        ->beginSynchronousEditTransaction();
                QString reason;
                if (!confirmSignalDefinition(
                        editor,
                        declarationText,
                        &reason)
                    && !reason.isEmpty()) {
                    emit editor
                        ->editorStatusMessageRequested(
                            reason);
                }
            });
    lineEdit->setFocus(Qt::PopupFocusReason);
    lineEdit->selectAll();
    if (failureReason)
        failureReason->clear();
    return true;
}

bool MyCodeEditorState::confirmSignalDefinition(
    MyCodeEditor* editor,
    const QString& declaration,
    QString* failureReason)
{
    const auto fail = [failureReason](const QString& reason) {
        if (failureReason)
            *failureReason = reason;
        return false;
    };
    if (!editor || !editor->document())
        return fail(QStringLiteral("No editor document"));
    if (!signalDefinitionPeekActive
        || signalDefinitionPeek.isNull()
        || signalDefinitionEditor.isNull()
        || signalDefinitionIdentifierStart < 0
        || signalDefinitionIdentifierEnd
               <= signalDefinitionIdentifierStart
        || signalDefinitionIdentifier.isEmpty()
        || signalDefinitionDocument.isNull()
        || editor->document()
               != signalDefinitionDocument.data()) {
        return fail(QStringLiteral(
            "No pending signal definition"));
    }
    SemanticIndex* semanticIndex =
        SemanticIndex::getInstance();
    const SemanticSnapshotToken currentToken =
        semanticIndex
        ? semanticIndex->snapshotToken()
        : SemanticSnapshotToken{};
    const QString currentFileIdentityKey =
        EditorFileIdentity::lookupKey(
            identity.current());
    const bool currentFileIdentityMatches =
        !signalDefinitionFileIdentityKey.isEmpty()
        && signalDefinitionFileIdentityKey
               == currentFileIdentityKey;
    if (signalDefinitionDocumentRevision
                != semanticDocumentRevision()
        || !(hierarchyInstance
             == signalDefinitionHierarchyInstance)
        || !sameSemanticSnapshotToken(
            signalDefinitionSemanticToken,
            currentToken)
        || !currentFileIdentityMatches
        || !EditorFileIdentity::same(
            signalDefinitionFileName,
            identity.current())) {
        cancelSignalDefinitionEditor();
        return fail(QStringLiteral(
            "Signal definition context is stale"));
    }

    const TSIdentifierTarget selected =
        syntax.identifierAt(
            signalDefinitionIdentifierStart);
    if (!selected.ok()
        || selected.startChar
               != signalDefinitionIdentifierStart
        || selected.endChar
               != signalDefinitionIdentifierEnd
        || selected.text
               != signalDefinitionIdentifier) {
        cancelSignalDefinitionEditor();
        return fail(QStringLiteral(
            "Signal definition context is stale"));
    }

    DeclareSignalFactCollectionResult facts;
    SemanticSnapshotToken checkedToken;
    const DeclareSignalProposal proposal =
        signalDefinitionProposalAt(
            editor,
            signalDefinitionIdentifierStart,
            &facts,
            &checkedToken,
            signalDefinitionSemanticToken.revision,
            signalDefinitionDocumentRevision);
    const SemanticSnapshotToken postCollectionToken =
        semanticIndex
        ? semanticIndex->snapshotToken()
        : SemanticSnapshotToken{};
    const QString postCollectionFileIdentityKey =
        EditorFileIdentity::lookupKey(
            identity.current());
    if (!sameSemanticSnapshotToken(
            signalDefinitionSemanticToken,
            checkedToken)
        || !sameSemanticSnapshotToken(
            signalDefinitionSemanticToken,
            postCollectionToken)
        || semanticDocumentRevision()
               != signalDefinitionDocumentRevision
        || editor->document()
               != signalDefinitionDocument.data()
        || !(hierarchyInstance
             == signalDefinitionHierarchyInstance)
        || postCollectionFileIdentityKey
               != signalDefinitionFileIdentityKey
        || !facts.acceptedForProposal()
        || facts.request.documentRevision
               != signalDefinitionDocumentRevision
        || proposal.identifier
               != signalDefinitionIdentifier
        || proposal.classification
               == DeclareSignalProposalClass::Conflict) {
        cancelSignalDefinitionEditor();
        return fail(QStringLiteral(
            "Declare Signal proposal is stale or conflicting"));
    }

    const DeclareSignalCandidate* checkedCandidate = nullptr;
    for (const DeclareSignalCandidate& candidate :
         proposal.candidates) {
        if (candidate.candidateId
            == signalDefinitionCandidateId) {
            checkedCandidate = &candidate;
            break;
        }
    }
    if (!checkedCandidate
        || checkedCandidate->scopeKind
               != signalDefinitionScopeKind
        || checkedCandidate->blockScopeId
               != signalDefinitionBlockScopeId) {
        cancelSignalDefinitionEditor();
        return fail(QStringLiteral(
            "The selected structured candidate is no longer valid"));
    }

    const QString declarationText = declaration.trimmed();
    if (declarationText.isEmpty())
        return fail(QStringLiteral("Signal declaration is empty"));
    const bool blockLocal =
        checkedCandidate->scopeKind
        == DeclareSignalScopeKind::BlockLocal;
    if (!TSDocument::isSingleSignalDeclaration(
            declarationText,
            proposal.identifier,
            blockLocal)) {
        return fail(QStringLiteral(
            "The edited text is not one structured declaration "
            "for the selected signal"));
    }

    const TSSignalInsertTarget target =
        blockLocal
        ? syntax.blockSignalInsertTargetAt(
              signalDefinitionIdentifierStart)
        : syntax.signalInsertTargetAt(
            signalDefinitionIdentifierStart);
    if (!target.ok()) {
        cancelSignalDefinitionEditor();
        return fail(
            blockLocal
                ? QStringLiteral(
                      "No clear block-local declaration section")
                : QStringLiteral(
                      "No clear module signal declaration section"));
    }

    QString insertionText;
    if (target.insertText.startsWith(
            QLatin1Char('\n'))) {
        insertionText =
            target.insertText + declarationText;
    } else if (target.insertText.endsWith(
                   QLatin1Char('\n'))) {
        insertionText =
            target.insertText.left(
                target.insertText.size() - 1)
            + declarationText
            + QLatin1Char('\n');
    } else {
        cancelSignalDefinitionEditor();
        return fail(QStringLiteral(
            "Invalid signal declaration anchor"));
    }

    const int insertPosition = target.insertChar;
    const int declarationPosition =
        target.caretCharAfterEdit;
    const InsertionPositionMapper positionMapper(
        insertPosition,
        insertionText.size());
    FormatterCursorAnchor cursorAnchor;
    cursorAnchor.capture(editor, positionMapper);
    cancelSignalDefinitionEditor();

    QTextCursor insertion(editor->document());
    insertion.beginEditBlock();
    insertion.setPosition(insertPosition);
    insertion.insertText(insertionText);
    insertion.endEditBlock();

    cursorAnchor.restore(editor, positionMapper);

    const QTextBlock insertedBlock =
        editor->document()->findBlock(
            qBound(
                0,
                declarationPosition,
                qMax(
                    0,
                    editor->document()->characterCount()
                        - 1)));
    if (insertedBlock.isValid()) {
        editor->flashLine(
            insertedBlock.blockNumber() + 1);
    }

    if (failureReason)
        failureReason->clear();
    emit editor->editorStatusMessageRequested(
        QStringLiteral("Created signal definition for %1")
            .arg(proposal.identifier));
    return true;
}

bool MyCodeEditorState::startSignalSelectionMode(
    MyCodeEditor* editor,
    QString* message)
{
    cancelSignalDefinitionEditor();
    return signalSelection.start(editor, message);
}

void MyCodeEditorState::cancelSignalSelectionMode(
    MyCodeEditor* editor)
{
    signalSelection.cancel(editor);
}

bool MyCodeEditorState::signalSelectionModeActive() const
{
    return signalSelection.active();
}

QStringList MyCodeEditorState::selectedSignalNames() const
{
    return signalSelection.selectedNames();
}


bool MyCodeEditorState::toggleSignalSelectionAt(
    MyCodeEditor* editor,
    int cursorPosition,
    bool toggle,
    bool desiredState)
{
    return signalSelection.toggleAt(
        editor,
        cursorPosition,
        toggle,
        desiredState);
}

bool MyCodeEditorState::createAssignmentQueueAt(
    MyCodeEditor* editor,
    int cursorPosition,
    QString* message)
{
    if (!editor || !editor->document()) {
        if (message)
            *message = QStringLiteral("No editor document");
        return false;
    }
    const QStringList ordered =
        signalSelection.selectedNames();
    if (ordered.isEmpty()) {
        if (message)
            *message = QStringLiteral("No signals selected");
        cancelSignalSelectionMode(editor);
        return false;
    }

    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    QTextBlock block = editor->document()->findBlock(
        qBound(0, cursorPosition, documentEnd));
    if (!block.isValid()) {
        if (message)
            *message = QStringLiteral("Invalid insertion context");
        cancelSignalSelectionMode(editor);
        return false;
    }
    const QString blockText = block.text();
    int indentLength = 0;
    while (indentLength < blockText.size()
           && (blockText.at(indentLength)
                   == QLatin1Char(' ')
               || blockText.at(indentLength)
                   == QLatin1Char('\t'))) {
        ++indentLength;
    }
    const QString indent =
        blockText.left(indentLength);

    QString insertionText;
    CodeTemplateSlotList metadata;
    for (int index = 0; index < ordered.size(); ++index) {
        insertionText += indent;
        insertionText += ordered.at(index);
        insertionText += QStringLiteral(" <= ");
        CodeTemplateSlot slot;
        slot.name = QStringLiteral("rhs %1").arg(index + 1);
        slot.start = insertionText.size();
        slot.length = 0;
        metadata.append(slot);
        insertionText += QStringLiteral(";\n");
    }
    const int insertionStart = block.position();

    cancelSignalSelectionMode(editor);
    clearTemplateSlotMode(editor);
    QTextCursor insertion(editor->document());
    insertion.beginEditBlock();
    insertion.setPosition(insertionStart);
    insertion.insertText(insertionText);
    insertion.endEditBlock();
    startTemplateSlotMode(editor,
                          insertionStart,
                          insertionText.size(),
                          metadata);
    if (message) {
        *message = QStringLiteral(
            "Created assignment queue for %1 signals")
            .arg(ordered.size());
    }
    emit editor->editorStatusMessageRequested(
        QStringLiteral(
            "Created assignment queue for %1 signals")
            .arg(ordered.size()));
    return templateSlotModeActive();
}

EditorStructuralContextMenuState
MyCodeEditorState::structuralContextMenuState(
    const MyCodeEditor* editor,
    int cursorPosition) const
{
    EditorStructuralContextMenuState state;
    if (!editor)
        return state;

    DeclareSignalFactCollectionResult signalFacts;
    const DeclareSignalProposal signalProposal =
        signalDefinitionProposalAt(
            editor, cursorPosition, &signalFacts);
    const TSInstantiationTarget target =
        syntax.instantiationAt(cursorPosition);
    const bool hasInstanceSlots =
        target.ok()
        && (!target.parameterActuals.isEmpty()
            || !target.portActuals.isEmpty());
    state.signalDefinitionAvailable =
        signalFacts.acceptedForProposal()
        && signalFacts.request.identifierAcceptedBySyntax
        && !signalFacts.request.uses.isEmpty()
        && !signalProposal.identifier.isEmpty()
        && !hasDeclareSignalIssue(
            signalProposal,
            DeclareSignalIssueCode::ExistingDeclaration);
    state.instanceSlotsAvailable = hasInstanceSlots;
    return state;
}

bool MyCodeEditorState::handleMousePress(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    keywordGhost.clear(editor);

    if (event && event->button() == Qt::LeftButton)
        selections.clearCurrentSymbolReferences(editor);

    if (event && event->button() != Qt::LeftButton)
        columnMode.clearPendingColumnAnchor();

    if (signalSelection.handleMousePress(
            editor,
            event)) {
        return true;
    }

    if (templateSlotModeActive() && editor && event) {
        const QTextCursor targetCursor =
            editor->cursorForPosition(event->position().toPoint());
        if (!templateSlots.containsPosition(
                targetCursor.position())) {
            clearTemplateSlotMode(editor);
        }
    }

    if (folding.handleFoldShelfMousePress(editor, event)) {
        columnMode.clearPendingColumnAnchor();
        return true;
    }

    if (columnMode.beginSelection(editor, event))
        return true;

    if (handleBracketRangeAltClick(editor, event)) {
        columnMode.clearPendingColumnAnchor();
        return true;
    }

    const bool plainAltCaretClick =
        editor
        && event
        && event->button() == Qt::LeftButton
        && event->modifiers() == Qt::AltModifier;
    if (plainAltCaretClick) {
        const QTextCursor previous =
            editor->textCursor();
        const bool virtualTarget =
            columnMode.handlePlainVirtualCursorClick(
                editor,
                event);
        if (virtualTarget) {
            event->accept();
            return true;
        }
        const QTextCursor target =
            editor->cursorForPosition(
                event->position().toPoint());

        if (!multiCursor.active()) {
            multiCursor.setCarets({
                EditorMultiCursorCaret{
                    previous.anchor(),
                    previous.position(),
                    -1,
                },
            });
        }
        multiCursor.addCaretAt(
            target.position(),
            -1,
            true);
        event->accept();
        return true;
    }

    if (multiCursor.active()
        && event
        && event->button() == Qt::LeftButton) {
        modes.exit(
            EditorModeId::MultiCursor,
            EditorModeExitReason::Canceled);
    }

    if (modes.isActive(EditorModeId::ColumnSelection)
        && event
        && event->button() == Qt::LeftButton
        && !(event->modifiers().testFlag(Qt::ShiftModifier)
             && event->modifiers().testFlag(Qt::AltModifier))) {
        columnMode.clearSelection(editor);
    }

    if (columnMode.handlePlainVirtualCursorClick(
            editor,
            event)) {
        return true;
    }

    if (modes.isActive(EditorModeId::VirtualCursor)
        && event
        && event->button() == Qt::LeftButton) {
        clearVirtualCursor(editor);
    }

    const bool handled = sourceNavigation.handleMousePress(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
    sourceNavigation.syncMode();
    return handled;
}

bool MyCodeEditorState::handleMouseDoubleClick(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    columnMode.clearPendingColumnAnchor();
    const bool handled = sourceNavigation.handleMouseDoubleClick(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections);
    sourceNavigation.syncMode();
    if (handled || !editor || !event
        || event->button() != Qt::LeftButton) {
        return handled;
    }

    const QTextCursor hit = editor->cursorForPosition(
        event->position().toPoint());
    const QString& text = editor->cachedDocumentText();
    if (text.isEmpty())
        return false;
    const int position = qBound(
        0, hit.position(), text.size() - 1);
    EditorLexicalBoundary::Range range =
        EditorLexicalBoundary::horizontalWhitespaceAt(
            text, position);
    const bool identifier =
        EditorLexicalBoundary::identifierAt(
            text, position).isValid();
    if (!range.isValid()) {
        range = EditorLexicalBoundary::identifierAt(
            text, position);
    }
    if (!range.isValid())
        range = EditorLexicalBoundary::unitAt(text, position);
    if (!range.isValid())
        return false;

    QTextCursor selection(editor->document());
    selection.setPosition(range.start);
    selection.setPosition(range.end, QTextCursor::KeepAnchor);
    editor->setTextCursor(selection);
    if (identifier)
        selections.activateCurrentSymbolReferences(editor);
    else
        selections.clearCurrentSymbolReferences(editor);
    editor->viewport()->update();
    event->accept();
    return true;
}

bool MyCodeEditorState::handleMouseMove(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (signalSelection.handleMouseMove(
            editor,
            event)) {
        return true;
    }

    if (columnMode.updateSelectionDrag(editor, event))
        return true;

    if (folding.handleFoldRegionMouseMove(editor, event))
        gutter.handleUpdateRequest(editor, editor->viewport()->rect(), 0);

    folding.handleFoldShelfHover(editor, event);
    if (folding.handleFoldShelfMouseMove(editor, event))
        return true;

    if (sourceNavigation.handleMouseMove(
        editor,
        event,
        semanticService(),
        sourceContextProvider(editor),
        selections)) {
        sourceNavigation.syncMode();
        return true;
    }
    sourceNavigation.syncMode();
    return false;
}

bool MyCodeEditorState::handleMouseRelease(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (signalSelection.handleMouseRelease(
            editor,
            event)) {
        return true;
    }

    if (sourceNavigation.handleMouseRelease(editor, event)) {
        sourceNavigation.syncMode();
        return true;
    }
    sourceNavigation.syncMode();

    return columnMode.endSelectionDrag(editor, event);
}

void MyCodeEditorState::handleLeaveEvent(MyCodeEditor* editor)
{
    sourceNavigation.handleLeave(editor, selections);
    sourceNavigation.syncMode();
}


FormatterReport MyCodeEditorState::formatSelection(
    MyCodeEditor* editor)
{
    if (!editor) {
        FormatterReport unavailable;
        unavailable.outcome = FormatterOutcome::Rejected;
        unavailable.diagnostic = QStringLiteral(
            "No editor is available to format.");
        return unavailable;
    }

    int rangeStart = -1;
    int rangeEnd = -1;
    if (!selectedFullLineRange(editor, &rangeStart, &rangeEnd)) {
        FormatterReport unavailable;
        unavailable.outcome = FormatterOutcome::Rejected;
        unavailable.diagnostic = QStringLiteral(
            "No selection to format");
        emit editor->editorStatusMessageRequested(
            unavailable.diagnostic);
        return unavailable;
    }

    const QString oldText = editor->toPlainText();
    const QString selectedText =
        oldText.mid(rangeStart, rangeEnd - rangeStart);
    QTextDocument* document = editor->document();
    const QTextBlock firstBlock = document
        ? document->findBlock(rangeStart)
        : QTextBlock();
    const QTextBlock lastBlock = document
        ? document->findBlock(qMax(rangeStart, rangeEnd - 1))
        : QTextBlock();
    FormatterReport report;
    if (!firstBlock.isValid() || !lastBlock.isValid()) {
        report.outcome = FormatterOutcome::Rejected;
        report.diagnostic = QStringLiteral(
            "The selected line range is unavailable.");
        emit editor->editorStatusMessageRequested(report.diagnostic);
        return report;
    }

    // Format with the complete syntax context, then apply only the selected
    // logical lines.  Parsing a case item, port row, or continuation fragment
    // in isolation made Format Selection appear to fail at random.
    const FormatterReport documentReport =
        FormatterService::getInstance()->formatDocument(
            oldText,
            currentFormatterProfile);
    report = documentReport;
    const bool includesTrailingNewline =
        rangeEnd > lastBlock.position() + lastBlock.text().size();
    report.formattedText = formattedLineSlice(
        documentReport.formattedText,
        firstBlock.blockNumber(),
        lastBlock.blockNumber(),
        includesTrailingNewline);
    report.formattedLines =
        lastBlock.blockNumber() - firstBlock.blockNumber() + 1;
    report.changed = report.formattedText != selectedText;
    if (report.outcome != FormatterOutcome::ConservativeFallback) {
        report.outcome = report.changed
            ? FormatterOutcome::Applied
            : FormatterOutcome::Unchanged;
    }
    if (!report.changed) {
        emit editor->editorStatusMessageRequested(
            report.diagnostic.isEmpty()
                ? QStringLiteral("Selection already formatted")
                : report.diagnostic);
        return report;
    }

    QString newText = oldText;
    newText.replace(rangeStart,
                    rangeEnd - rangeStart,
                    report.formattedText);
    FormatterTriviaPositionMapper positionMapper(oldText,
                                                 newText);
    QList<QPair<MyCodeEditor*, FormatterCursorAnchor>> anchors;
    for (MyCodeEditor* view :
         editor->sharedDocumentViewsForFormatting()) {
        FormatterCursorAnchor anchor;
        if (anchor.capture(view, positionMapper))
            anchors.append(qMakePair(view, anchor));
    }

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(rangeStart);
    cursor.setPosition(rangeEnd, QTextCursor::KeepAnchor);
    cursor.insertText(report.formattedText);
    cursor.endEditBlock();
    for (const auto& anchoredView : anchors) {
        anchoredView.second.restore(
            anchoredView.first, positionMapper);
    }
    QString message =
        QStringLiteral("Formatted selection (%1 lines, %2)")
            .arg(report.formattedLines)
            .arg(FormatterService::profileDisplayName(
                currentFormatterProfile));
    if (!report.diagnostic.isEmpty())
        message += QStringLiteral(": ") + report.diagnostic;
    emit editor->editorStatusMessageRequested(message);
    return report;
}

void MyCodeEditorState::commentSelectionOrLine(MyCodeEditor* editor)
{
    applyLineComment(editor);
}

void MyCodeEditorState::uncommentSelectionOrLine(MyCodeEditor* editor)
{
    applyLineUncomment(editor);
}

void MyCodeEditorState::indentSelectionOrLine(MyCodeEditor* editor)
{
    applyLineIndent(editor);
}

void MyCodeEditorState::unindentSelectionOrLine(MyCodeEditor* editor)
{
    applyLineUnindent(editor);
}

namespace {
enum class CurrentStatementScanState {
    Normal,
    LineComment,
    BlockComment,
    String
};

int topLevelSemicolonEndFrom(const QString& text, int start)
{
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    CurrentStatementScanState state = CurrentStatementScanState::Normal;

    for (int i = qBound(0, start, text.size()); i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();

        if (state == CurrentStatementScanState::LineComment) {
            if (ch == QLatin1Char('\n'))
                state = CurrentStatementScanState::Normal;
            continue;
        }
        if (state == CurrentStatementScanState::BlockComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                state = CurrentStatementScanState::Normal;
                ++i;
            }
            continue;
        }
        if (state == CurrentStatementScanState::String) {
            if (ch == QLatin1Char('\\') && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (ch == QLatin1Char('"'))
                state = CurrentStatementScanState::Normal;
            continue;
        }

        if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
            state = CurrentStatementScanState::LineComment;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            state = CurrentStatementScanState::BlockComment;
            ++i;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            state = CurrentStatementScanState::String;
            continue;
        }

        if (ch == QLatin1Char('(')) {
            ++parenDepth;
            continue;
        }
        if (ch == QLatin1Char(')')) {
            parenDepth = qMax(0, parenDepth - 1);
            continue;
        }
        if (ch == QLatin1Char('[')) {
            ++bracketDepth;
            continue;
        }
        if (ch == QLatin1Char(']')) {
            bracketDepth = qMax(0, bracketDepth - 1);
            continue;
        }
        if (ch == QLatin1Char('{')) {
            ++braceDepth;
            continue;
        }
        if (ch == QLatin1Char('}')) {
            braceDepth = qMax(0, braceDepth - 1);
            continue;
        }

        if (ch == QLatin1Char(';') && parenDepth == 0
            && bracketDepth == 0 && braceDepth == 0) {
            return i + 1;
        }
    }
    return -1;
}

bool currentAssignmentRange(MyCodeEditor* editor,
                            int cursorPosition,
                            int* rangeStart,
                            int* rangeEnd)
{
    if (!editor || !editor->document() || !rangeStart || !rangeEnd)
        return false;

    const QString documentText = editor->toPlainText();
    QTextBlock block = editor->document()->findBlock(cursorPosition);
    if (!block.isValid())
        return false;

    constexpr int kMaxLookbackLines = 40;
    for (int lookedBack = 0;
         lookedBack < kMaxLookbackLines && block.isValid();
         ++lookedBack, block = block.previous()) {
        const int candidateStart = block.position();
        const int candidateEnd =
            topLevelSemicolonEndFrom(documentText, candidateStart);
        if (candidateEnd <= cursorPosition)
            continue;
        if (candidateEnd < 0)
            continue;

        const QString candidateText =
            documentText.mid(candidateStart, candidateEnd - candidateStart);
        const RtlClearAssignmentRhsReport report =
            RtlBatchEditService::getInstance()->planClearAssignmentRhs(
                RtlClearAssignmentRhsQuery{candidateText, candidateStart});
        if (!report.canApply() || report.edits.size() != 1)
            continue;

        *rangeStart = candidateStart;
        *rangeEnd = candidateEnd;
        return true;
    }
    return false;
}

bool selectedCompleteLineRange(MyCodeEditor* editor,
                               const QTextCursor& cursor,
                               int* rangeStart,
                               int* rangeEnd)
{
    if (!editor || !editor->document() || !cursor.hasSelection()
        || !rangeStart || !rangeEnd) {
        return false;
    }

    const int selectionStart = cursor.selectionStart();
    int adjustedEnd = cursor.selectionEnd();
    if (adjustedEnd <= selectionStart)
        return false;

    const QTextBlock endAtBlock =
        editor->document()->findBlock(adjustedEnd);
    if (endAtBlock.isValid()
        && adjustedEnd == endAtBlock.position()
        && adjustedEnd > selectionStart) {
        --adjustedEnd;
    } else {
        --adjustedEnd;
    }

    const QTextBlock firstBlock =
        editor->document()->findBlock(selectionStart);
    const QTextBlock lastBlock =
        editor->document()->findBlock(qMax(selectionStart, adjustedEnd));
    if (!firstBlock.isValid() || !lastBlock.isValid())
        return false;

    *rangeStart = firstBlock.position();
    *rangeEnd = lineEndPosition(editor->document(), lastBlock.blockNumber());
    return *rangeEnd >= *rangeStart;
}

void publishClearRhsFailure(MyCodeEditor* editor,
                            QString* message,
                            const QString& failure)
{
    const QString resolved = failure.isEmpty()
        ? QStringLiteral("No assignment RHS found")
        : failure;
    if (message)
        *message = resolved;
    if (editor)
        emit editor->editorStatusMessageRequested(resolved);
}
} // namespace

bool MyCodeEditorState::clearSelectedAssignmentRhs(MyCodeEditor* editor,
                                                   QString* message)
{
    if (!editor || !editor->document())
        return false;

    QTextCursor cursor = editor->textCursor();
    int selectionStart = cursor.selectionStart();
    int selectionEnd = cursor.selectionEnd();
    if (cursor.hasSelection()) {
        if (!selectedCompleteLineRange(editor,
                                       cursor,
                                       &selectionStart,
                                       &selectionEnd)) {
            publishClearRhsFailure(editor,
                                   message,
                                   QStringLiteral("No assignment RHS found"));
            return false;
        }
    } else {
        if (!currentAssignmentRange(editor,
                                    cursor.position(),
                                    &selectionStart,
                                    &selectionEnd)) {
            publishClearRhsFailure(editor,
                                   message,
                                   QStringLiteral("No assignment RHS found"));
            return false;
        }
    }

    const QString selectedText =
        editor->toPlainText().mid(selectionStart,
                                  selectionEnd - selectionStart);
    const RtlClearAssignmentRhsReport report =
        RtlBatchEditService::getInstance()->planClearAssignmentRhs(
            RtlClearAssignmentRhsQuery{selectedText, selectionStart});
    if (!report.canApply()) {
        publishClearRhsFailure(editor, message, report.failureReason);
        return false;
    }

    clearTemplateSlotMode(editor);
    cursor.beginEditBlock();
    cursor.setPosition(selectionStart);
    cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    cursor.insertText(report.replacementText);
    cursor.endEditBlock();

    startTemplateSlotMode(editor,
                          selectionStart,
                          report.replacementText.size(),
                          report.templateSlots);
    const QString successMessage =
        QStringLiteral("Cleared RHS for %1 assignment%2")
            .arg(report.edits.size())
            .arg(report.edits.size() == 1 ? QString() : QStringLiteral("s"));
    if (message)
        *message = successMessage;
    emit editor->editorStatusMessageRequested(successMessage);
    return true;
}
