#include "editormulticursorcontroller.h"

#include "editormodecontroller.h"
#include "mycodeeditor.h"
#include "tsdocument.h"

#include <QPointer>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QSet>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QtGlobal>

#include <algorithm>
#include <utility>

struct EditorMultiCursorController::State
{
    QPointer<MyCodeEditor> editor;
    EditorModeController* modes = nullptr;
    QList<EditorMultiCursorCaret> carets;
    int primaryIndex = -1;
    bool suppressModeExitHandler = false;
    bool applyingEdit = false;
};

namespace {
struct TaggedCaret
{
    EditorMultiCursorCaret caret;
    bool primary = false;
};

struct PendingEdit
{
    int start = -1;
    int end = -1;
    QString replacement;
    int sourceCaret = -1;
    int caretOffset = -1;
};

struct DocumentRange
{
    int start = -1;
    int end = -1;
};

int maximumDocumentPosition(const QTextDocument* document)
{
    return document ? qMax(0, document->characterCount() - 1) : 0;
}

EditorMultiCursorCaret editorCaret(MyCodeEditor* editor)
{
    EditorMultiCursorCaret result;
    if (!editor)
        return result;
    const QTextCursor cursor = editor->textCursor();
    result.anchor = cursor.anchor();
    result.position = cursor.position();
    return result;
}

bool sameCaret(const EditorMultiCursorCaret& left,
               const EditorMultiCursorCaret& right)
{
    return left.anchor == right.anchor
        && left.position == right.position
        && left.virtualColumn == right.virtualColumn;
}

bool sameCarets(const QList<EditorMultiCursorCaret>& left,
                const QList<EditorMultiCursorCaret>& right)
{
    if (left.size() != right.size())
        return false;
    for (int index = 0; index < left.size(); ++index) {
        if (!sameCaret(left.at(index), right.at(index)))
            return false;
    }
    return true;
}

int physicalColumn(const QTextDocument* document, int position)
{
    if (!document)
        return 0;
    const QTextBlock block = document->findBlock(position);
    return block.isValid() ? position - block.position() : 0;
}

bool isAtLineEnd(const QTextDocument* document, int position)
{
    if (!document)
        return false;
    const QTextBlock block = document->findBlock(position);
    return block.isValid()
        && position == block.position() + block.text().size();
}

bool caretsConflict(const EditorMultiCursorCaret& left,
                    const EditorMultiCursorCaret& right)
{
    const int leftStart = left.selectionStart();
    const int leftEnd = left.selectionEnd();
    const int rightStart = right.selectionStart();
    const int rightEnd = right.selectionEnd();
    const bool leftPoint = leftStart == leftEnd;
    const bool rightPoint = rightStart == rightEnd;
    if (leftPoint && rightPoint)
        return leftStart == rightStart;
    if (leftPoint)
        return leftStart > rightStart && leftStart < rightEnd;
    if (rightPoint)
        return rightStart > leftStart && rightStart < leftEnd;
    return qMax(leftStart, rightStart) < qMin(leftEnd, rightEnd);
}

QList<EditorMultiCursorCaret> normalizeCarets(
    QTextDocument* document,
    const QList<EditorMultiCursorCaret>& input,
    int requestedPrimary,
    int* normalizedPrimary)
{
    if (normalizedPrimary)
        *normalizedPrimary = -1;
    if (!document)
        return {};

    const int maximum = maximumDocumentPosition(document);
    QList<TaggedCaret> tagged;
    tagged.reserve(input.size());
    for (int index = 0; index < input.size(); ++index) {
        EditorMultiCursorCaret caret = input.at(index);
        if (caret.position < 0 && caret.anchor < 0)
            continue;
        if (caret.position < 0)
            caret.position = caret.anchor;
        if (caret.anchor < 0)
            caret.anchor = caret.position;
        caret.position = qBound(0, caret.position, maximum);
        caret.anchor = qBound(0, caret.anchor, maximum);
        if (caret.hasSelection()
            || !isAtLineEnd(document, caret.position)
            || caret.virtualColumn
                   <= physicalColumn(document, caret.position)) {
            caret.virtualColumn = -1;
        }
        tagged.append(
            TaggedCaret{caret, index == requestedPrimary});
    }

    std::sort(tagged.begin(),
              tagged.end(),
              [](const TaggedCaret& left,
                 const TaggedCaret& right) {
                  if (left.caret.selectionStart()
                      != right.caret.selectionStart()) {
                      return left.caret.selectionStart()
                          < right.caret.selectionStart();
                  }
                  if (left.caret.selectionEnd()
                      != right.caret.selectionEnd()) {
                      return left.caret.selectionEnd()
                          < right.caret.selectionEnd();
                  }
                  return left.caret.virtualColumn
                      < right.caret.virtualColumn;
              });

    QList<TaggedCaret> merged;
    merged.reserve(tagged.size());
    for (const TaggedCaret& candidate : std::as_const(tagged)) {
        if (merged.isEmpty()
            || !caretsConflict(merged.constLast().caret,
                               candidate.caret)) {
            merged.append(candidate);
            continue;
        }

        TaggedCaret& previous = merged.last();
        const int start = qMin(previous.caret.selectionStart(),
                               candidate.caret.selectionStart());
        const int end = qMax(previous.caret.selectionEnd(),
                             candidate.caret.selectionEnd());
        previous.caret.anchor = start;
        previous.caret.position = end;
        previous.caret.virtualColumn = -1;
        previous.primary = previous.primary || candidate.primary;
    }

    QList<EditorMultiCursorCaret> result;
    result.reserve(merged.size());
    int primary = -1;
    for (int index = 0; index < merged.size(); ++index) {
        result.append(merged.at(index).caret);
        if (merged.at(index).primary)
            primary = index;
    }
    if (!result.isEmpty() && primary < 0)
        primary = 0;
    if (normalizedPrimary)
        *normalizedPrimary = primary;
    return result;
}

void synchronizePrimaryCursor(EditorMultiCursorController::State& state)
{
    MyCodeEditor* editor = state.editor;
    if (!editor
        || state.primaryIndex < 0
        || state.primaryIndex >= state.carets.size()) {
        return;
    }

    const EditorMultiCursorCaret& primary =
        state.carets.at(state.primaryIndex);
    QTextCursor cursor(editor->document());
    cursor.setPosition(primary.anchor);
    cursor.setPosition(primary.position, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
}

void updateMode(EditorMultiCursorController::State& state,
                EditorModeExitReason singleCaretExitReason =
                    EditorModeExitReason::Completed)
{
    if (!state.modes)
        return;

    const bool shouldBeActive = state.carets.size() > 1;
    const bool isActive =
        state.modes->isActive(EditorModeId::MultiCursor);
    if (shouldBeActive && !isActive) {
        state.modes->enter(EditorModeId::MultiCursor,
                           EditorModeEntryReason::KeyboardGesture);
    } else if (!shouldBeActive && isActive) {
        state.suppressModeExitHandler = true;
        state.modes->exit(EditorModeId::MultiCursor,
                          singleCaretExitReason);
        state.suppressModeExitHandler = false;
    }

    if (shouldBeActive) {
        state.modes->updatePresentation(
            EditorModeId::MultiCursor,
            QStringLiteral("Multi-cursor: %1")
                .arg(state.carets.size()),
            QStringLiteral(
                "Type and edit at every caret; Esc returns to one caret"));
    }
}

void replaceStateCarets(EditorMultiCursorController::State& state,
                        const QList<EditorMultiCursorCaret>& carets,
                        int primaryIndex,
                        EditorModeExitReason singleCaretExitReason =
                            EditorModeExitReason::Completed)
{
    if (!state.editor)
        return;
    state.carets = normalizeCarets(state.editor->document(),
                                   carets,
                                   primaryIndex,
                                   &state.primaryIndex);
    synchronizePrimaryCursor(state);
    updateMode(state, singleCaretExitReason);
    if (state.editor)
        state.editor->viewport()->update();
}

QList<EditorMultiCursorOccurrence> scopedOccurrences(
    QTextDocument* document,
    const QList<EditorMultiCursorOccurrence>& occurrences,
    int scopeStart,
    int scopeEnd)
{
    if (!document)
        return {};
    const int maximum = maximumDocumentPosition(document);
    const int first =
        scopeStart < 0 ? 0 : qBound(0, scopeStart, maximum);
    const int last =
        scopeEnd < 0 ? maximum : qBound(0, scopeEnd, maximum);
    if (last < first)
        return {};

    QList<EditorMultiCursorOccurrence> result;
    result.reserve(occurrences.size());
    for (const EditorMultiCursorOccurrence& occurrence :
         occurrences) {
        if (!occurrence.isValid()
            || occurrence.start < first
            || occurrence.end > last
            || occurrence.end > maximum) {
            continue;
        }
        result.append(occurrence);
    }
    std::sort(result.begin(),
              result.end(),
              [](const EditorMultiCursorOccurrence& left,
                 const EditorMultiCursorOccurrence& right) {
                  if (left.start != right.start)
                      return left.start < right.start;
                  return left.end < right.end;
              });
    result.erase(std::unique(
                     result.begin(),
                     result.end(),
                     [](const EditorMultiCursorOccurrence& left,
                        const EditorMultiCursorOccurrence& right) {
                         return left.start == right.start
                             && left.end == right.end;
                     }),
                 result.end());
    return result;
}

bool caretMatchesOccurrence(
    const EditorMultiCursorCaret& caret,
    const EditorMultiCursorOccurrence& occurrence)
{
    return caret.hasSelection()
        && caret.selectionStart() == occurrence.start
        && caret.selectionEnd() == occurrence.end;
}

bool pendingEditLess(const PendingEdit& left,
                     const PendingEdit& right)
{
    if (left.start != right.start)
        return left.start < right.start;
    return left.end < right.end;
}

bool pendingEditsConflict(const PendingEdit& left,
                          const PendingEdit& right)
{
    if (left.start == left.end && right.start == right.end)
        return left.start == right.start;
    return qMax(left.start, right.start)
        < qMin(left.end, right.end);
}

int mappedPosition(int position,
                   const QList<PendingEdit>& ascendingEdits)
{
    int shift = 0;
    for (const PendingEdit& edit :
         ascendingEdits) {
        if (position < edit.start)
            break;
        if (position >= edit.end) {
            shift += edit.replacement.size()
                - (edit.end - edit.start);
            continue;
        }
        return edit.start + shift
            + edit.replacement.size();
    }
    return position + shift;
}

int finalPositionForEdit(
    const PendingEdit& target,
    const QList<PendingEdit>& ascendingEdits)
{
    int shift = 0;
    for (const PendingEdit& edit :
         ascendingEdits) {
        if (edit.sourceCaret == target.sourceCaret)
            break;
        shift += edit.replacement.size()
            - (edit.end - edit.start);
    }
    const int caretOffset =
        target.caretOffset < 0
        ? target.replacement.size()
        : qBound(0,
                 target.caretOffset,
                 target.replacement.size());
    return target.start + shift + caretOffset;
}

bool applyEdits(EditorMultiCursorController::State& state,
                QList<PendingEdit> edits,
                QList<EditorMultiCursorCaret> resultingCarets,
                bool nonDocumentStateChanged)
{
    MyCodeEditor* editor = state.editor;
    if (!editor)
        return false;

    edits.erase(
        std::remove_if(
            edits.begin(),
            edits.end(),
            [](const PendingEdit& edit) {
                return edit.start == edit.end
                    && edit.replacement.isEmpty();
            }),
        edits.end());
    std::sort(edits.begin(), edits.end(), pendingEditLess);
    for (int index = 1; index < edits.size(); ++index) {
        if (pendingEditsConflict(edits.at(index - 1),
                                 edits.at(index))) {
            return false;
        }
    }

    if (!edits.isEmpty()) {
        if (editor->isReadOnly())
            return false;
        state.applyingEdit = true;
        auto synchronous =
            editor->beginSynchronousEditTransaction();
        QTextCursor transaction(editor->document());
        transaction.beginEditBlock();
        for (auto iterator = edits.crbegin();
             iterator != edits.crend();
             ++iterator) {
            transaction.setPosition(iterator->start);
            transaction.setPosition(iterator->end,
                                    QTextCursor::KeepAnchor);
            if (iterator->replacement.isEmpty())
                transaction.removeSelectedText();
            else
                transaction.insertText(iterator->replacement);
        }
        transaction.endEditBlock();
        state.applyingEdit = false;
    }

    for (int index = 0; index < resultingCarets.size(); ++index) {
        const auto ownEdit = std::find_if(
            edits.cbegin(),
            edits.cend(),
            [index](const PendingEdit& edit) {
                return edit.sourceCaret == index;
            });
        EditorMultiCursorCaret& caret =
            resultingCarets[index];
        if (ownEdit != edits.cend()) {
            const int finalPosition =
                finalPositionForEdit(*ownEdit, edits);
            caret.anchor = finalPosition;
            caret.position = finalPosition;
            caret.virtualColumn = -1;
        } else {
            caret.anchor =
                mappedPosition(caret.anchor, edits);
            caret.position =
                mappedPosition(caret.position, edits);
        }
    }

    const int primary = state.primaryIndex;
    replaceStateCarets(state, resultingCarets, primary);
    return !edits.isEmpty() || nonDocumentStateChanged;
}

bool insertPerCaret(EditorMultiCursorController::State& state,
                    const QStringList& replacements)
{
    if (!state.editor
        || state.carets.isEmpty()
        || replacements.size() != state.carets.size()) {
        return false;
    }

    QList<PendingEdit> edits;
    edits.reserve(state.carets.size());
    QList<EditorMultiCursorCaret> resultingCarets =
        state.carets;
    for (int index = 0; index < state.carets.size(); ++index) {
        const EditorMultiCursorCaret& caret =
            state.carets.at(index);
        QString replacement = replacements.at(index);
        if (!caret.hasSelection()
            && caret.virtualColumn >= 0) {
            const int column =
                physicalColumn(state.editor->document(),
                               caret.position);
            if (caret.virtualColumn > column) {
                replacement.prepend(
                    QString(caret.virtualColumn - column,
                            QLatin1Char(' ')));
            }
        }
        edits.append(PendingEdit{
            caret.selectionStart(),
            caret.selectionEnd(),
            replacement,
            index,
        });
    }
    return applyEdits(state,
                      std::move(edits),
                      std::move(resultingCarets),
                      false);
}

int previousCharacterPosition(QTextDocument* document,
                              int position)
{
    QTextCursor cursor(document);
    cursor.setPosition(position);
    return cursor.movePosition(QTextCursor::PreviousCharacter)
        ? cursor.position()
        : position;
}

int nextCharacterPosition(QTextDocument* document,
                          int position)
{
    QTextCursor cursor(document);
    cursor.setPosition(position);
    return cursor.movePosition(QTextCursor::NextCharacter)
        ? cursor.position()
        : position;
}

QChar matchingCloser(QChar opening)
{
    if (opening == QLatin1Char('('))
        return QLatin1Char(')');
    if (opening == QLatin1Char('['))
        return QLatin1Char(']');
    if (opening == QLatin1Char('{'))
        return QLatin1Char('}');
    if (opening == QLatin1Char('"'))
        return QLatin1Char('"');
    return {};
}

bool closingCharacter(QChar value)
{
    return value == QLatin1Char(')')
        || value == QLatin1Char(']')
        || value == QLatin1Char('}')
        || value == QLatin1Char('"');
}

bool syntaxLiteralAt(const TSDocument* document,
                     int position)
{
    if (!document || position < 0)
        return false;
    return document->isCommentAt(position)
        || document->isStringAt(position);
}

QString virtualPadding(
    QTextDocument* document,
    const EditorMultiCursorCaret& caret)
{
    if (!document
        || caret.hasSelection()
        || caret.virtualColumn < 0) {
        return {};
    }
    const int column =
        physicalColumn(document, caret.position);
    if (caret.virtualColumn <= column)
        return {};
    return QString(caret.virtualColumn - column,
                   QLatin1Char(' '));
}

QString normalizedClipboardText(QString text)
{
    text.replace(QStringLiteral("\r\n"),
                 QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

QString documentRangeText(QTextDocument* document,
                          int start,
                          int end)
{
    if (!document || start < 0 || end <= start)
        return {};
    QTextCursor cursor(document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    text.replace(QChar::ParagraphSeparator,
                 QLatin1Char('\n'));
    return text;
}

DocumentRange wholeLineDeletionRange(
    QTextDocument* document,
    const QTextBlock& block)
{
    if (!document || !block.isValid())
        return {};
    const QTextBlock next = block.next();
    if (next.isValid()) {
        return DocumentRange{
            block.position(),
            next.position(),
        };
    }

    const int end = maximumDocumentPosition(document);
    if (block.previous().isValid()) {
        return DocumentRange{
            qMax(0, block.position() - 1),
            end,
        };
    }
    return DocumentRange{
        block.position(),
        block.position()
            + static_cast<int>(block.text().size()),
    };
}

QList<PendingEdit> mergedDeletionEdits(
    QList<DocumentRange> ranges)
{
    ranges.erase(
        std::remove_if(
            ranges.begin(),
            ranges.end(),
            [](const DocumentRange& range) {
                return range.start < 0
                    || range.end <= range.start;
            }),
        ranges.end());
    std::sort(
        ranges.begin(),
        ranges.end(),
        [](const DocumentRange& left,
           const DocumentRange& right) {
            if (left.start != right.start)
                return left.start < right.start;
            return left.end < right.end;
        });

    QList<DocumentRange> merged;
    for (const DocumentRange& range :
         std::as_const(ranges)) {
        if (merged.isEmpty()
            || range.start > merged.constLast().end) {
            merged.append(range);
            continue;
        }
        merged.last().end =
            qMax(merged.constLast().end, range.end);
    }

    QList<PendingEdit> edits;
    edits.reserve(merged.size());
    for (const DocumentRange& range :
         std::as_const(merged)) {
        edits.append(PendingEdit{
            range.start,
            range.end,
            QString(),
            -1,
        });
    }
    return edits;
}

QList<int> affectedBlockNumbers(
    QTextDocument* document,
    const QList<EditorMultiCursorCaret>& carets)
{
    if (!document)
        return {};
    QSet<int> blockNumbers;
    for (const EditorMultiCursorCaret& caret :
         carets) {
        const int firstPosition =
            caret.selectionStart();
        int lastPosition = caret.selectionEnd();
        if (caret.hasSelection()
            && lastPosition > firstPosition) {
            --lastPosition;
        }
        const QTextBlock first =
            document->findBlock(firstPosition);
        const QTextBlock last =
            document->findBlock(lastPosition);
        if (!first.isValid() || !last.isValid())
            continue;
        for (int number = first.blockNumber();
             number <= last.blockNumber();
             ++number) {
            blockNumbers.insert(number);
        }
    }

    QList<int> result = blockNumbers.values();
    std::sort(result.begin(), result.end());
    return result;
}
}

EditorMultiCursorController::EditorMultiCursorController()
    : state(std::make_unique<State>())
{
}

EditorMultiCursorController::~EditorMultiCursorController()
{
    shutdown();
}

void EditorMultiCursorController::bind(
    EditorModeController* modes,
    MyCodeEditor* editor)
{
    if (state->modes) {
        state->modes->setExitHandler(
            EditorModeId::MultiCursor, {});
        if (state->modes->isActive(
                EditorModeId::MultiCursor)) {
            state->modes->exit(
                EditorModeId::MultiCursor,
                EditorModeExitReason::Replaced);
        }
    }
    state->modes = modes;
    state->editor = editor;
    state->carets.clear();
    state->primaryIndex = -1;
    if (editor) {
        state->carets.append(editorCaret(editor));
        state->primaryIndex = 0;
    }
    if (modes) {
        modes->setExitHandler(
            EditorModeId::MultiCursor,
            [this](EditorModeExitReason) {
                if (state->suppressModeExitHandler
                    || state->carets.isEmpty()) {
                    return;
                }
                const int primary =
                    qBound(0,
                           state->primaryIndex,
                           state->carets.size() - 1);
                const EditorMultiCursorCaret caret =
                    state->carets.at(primary);
                state->carets = {caret};
                state->primaryIndex = 0;
                synchronizePrimaryCursor(*state);
                if (state->editor)
                    state->editor->viewport()->update();
            });
    }
}

void EditorMultiCursorController::shutdown(
    MyCodeEditor* editor)
{
    if (editor && state->editor != editor)
        return;
    if (state->modes) {
        state->modes->setExitHandler(
            EditorModeId::MultiCursor, {});
        if (state->modes->isActive(
                EditorModeId::MultiCursor)) {
            state->modes->exit(
                EditorModeId::MultiCursor,
                EditorModeExitReason::DocumentClosed);
        }
    }
    state->carets.clear();
    state->primaryIndex = -1;
    state->editor = nullptr;
    state->modes = nullptr;
}

bool EditorMultiCursorController::active() const
{
    return state->carets.size() > 1;
}

bool EditorMultiCursorController::applyingDocumentEdit() const
{
    return state->applyingEdit;
}

int EditorMultiCursorController::caretCount() const
{
    return state->carets.size();
}

EditorMultiCursorSnapshot
EditorMultiCursorController::snapshot() const
{
    EditorMultiCursorSnapshot result;
    result.carets = state->carets;
    result.primaryIndex = state->primaryIndex;
    result.active = active();
    return result;
}

bool EditorMultiCursorController::resetToEditorCursor()
{
    if (!state->editor)
        return false;
    const EditorMultiCursorCaret current =
        editorCaret(state->editor);
    const bool changed =
        state->carets.size() != 1
        || state->primaryIndex != 0
        || !sameCaret(
            state->carets.constFirst(),
            current);
    replaceStateCarets(
        *state,
        {current},
        0,
        EditorModeExitReason::Completed);
    return changed;
}

bool EditorMultiCursorController::setCarets(
    const QList<EditorMultiCursorCaret>& carets,
    int primaryIndex)
{
    if (!state->editor || carets.isEmpty())
        return false;
    int normalizedPrimary = -1;
    const QList<EditorMultiCursorCaret> normalized =
        normalizeCarets(state->editor->document(),
                        carets,
                        primaryIndex,
                        &normalizedPrimary);
    if (normalized.isEmpty())
        return false;
    const bool changed =
        !sameCarets(state->carets, normalized)
        || state->primaryIndex != normalizedPrimary;
    state->carets = normalized;
    state->primaryIndex = normalizedPrimary;
    synchronizePrimaryCursor(*state);
    updateMode(*state);
    if (state->editor)
        state->editor->viewport()->update();
    return changed;
}

bool EditorMultiCursorController::addCaret(
    const EditorMultiCursorCaret& caret,
    bool makePrimary)
{
    if (!state->editor)
        return false;
    QList<EditorMultiCursorCaret> candidate =
        state->carets;
    if (candidate.isEmpty())
        candidate.append(editorCaret(state->editor));
    const int oldCount = candidate.size();
    const int oldPrimary =
        state->primaryIndex >= 0 ? state->primaryIndex : 0;
    candidate.append(caret);
    if (!setCarets(candidate,
                   makePrimary
                       ? candidate.size() - 1
                       : oldPrimary)) {
        return false;
    }
    return state->carets.size() > oldCount;
}

bool EditorMultiCursorController::addCaretAt(
    int position,
    int virtualColumn,
    bool makePrimary)
{
    return addCaret(EditorMultiCursorCaret{
                        position,
                        position,
                        virtualColumn,
                    },
                    makePrimary);
}

bool EditorMultiCursorController::setVirtualColumn(
    int caretIndex,
    int virtualColumn)
{
    if (!state->editor
        || caretIndex < 0
        || caretIndex >= state->carets.size()) {
        return false;
    }
    EditorMultiCursorCaret caret =
        state->carets.at(caretIndex);
    if (caret.hasSelection()
        || !isAtLineEnd(state->editor->document(),
                        caret.position)) {
        return false;
    }
    const int column =
        physicalColumn(state->editor->document(),
                       caret.position);
    const int normalized =
        virtualColumn > column ? virtualColumn : -1;
    if (caret.virtualColumn == normalized)
        return false;
    state->carets[caretIndex].virtualColumn =
        normalized;
    synchronizePrimaryCursor(*state);
    if (state->editor)
        state->editor->viewport()->update();
    return true;
}

bool EditorMultiCursorController::addNextOccurrence(
    const QList<EditorMultiCursorOccurrence>& occurrences,
    int scopeStart,
    int scopeEnd)
{
    if (!state->editor || state->carets.isEmpty())
        return false;
    const QList<EditorMultiCursorOccurrence> candidates =
        scopedOccurrences(state->editor->document(),
                          occurrences,
                          scopeStart,
                          scopeEnd);
    if (candidates.isEmpty())
        return false;

    const int primary =
        qBound(0,
               state->primaryIndex,
               state->carets.size() - 1);
    const EditorMultiCursorCaret primaryCaret =
        state->carets.at(primary);
    bool hasStructuredSelection = false;
    for (const EditorMultiCursorOccurrence& candidate :
         candidates) {
        if (caretMatchesOccurrence(primaryCaret,
                                   candidate)) {
            hasStructuredSelection = true;
            break;
        }
    }
    if (!primaryCaret.hasSelection()
        && !hasStructuredSelection) {
        for (const EditorMultiCursorOccurrence& candidate :
             candidates) {
            if (primaryCaret.position >= candidate.start
                && primaryCaret.position <= candidate.end) {
                QList<EditorMultiCursorCaret> carets =
                    state->carets;
                carets[primary] = EditorMultiCursorCaret{
                    candidate.start,
                    candidate.end,
                    -1,
                };
                return setCarets(carets, primary);
            }
        }
    }

    const int pivot = primaryCaret.selectionEnd();
    QList<int> orderedIndexes;
    orderedIndexes.reserve(candidates.size());
    for (int index = 0; index < candidates.size(); ++index) {
        if (candidates.at(index).start >= pivot)
            orderedIndexes.append(index);
    }
    for (int index = 0; index < candidates.size(); ++index) {
        if (candidates.at(index).start < pivot)
            orderedIndexes.append(index);
    }

    for (const int candidateIndex :
         std::as_const(orderedIndexes)) {
        const EditorMultiCursorOccurrence& candidate =
            candidates.at(candidateIndex);
        bool alreadySelected = false;
        for (const EditorMultiCursorCaret& caret :
             std::as_const(state->carets)) {
            if (caretMatchesOccurrence(caret, candidate)) {
                alreadySelected = true;
                break;
            }
        }
        if (!alreadySelected) {
            return addCaret(
                EditorMultiCursorCaret{
                    candidate.start,
                    candidate.end,
                    -1,
                },
                true);
        }
    }
    return false;
}

int EditorMultiCursorController::selectAllOccurrences(
    const QList<EditorMultiCursorOccurrence>& occurrences,
    int scopeStart,
    int scopeEnd)
{
    if (!state->editor)
        return 0;
    const QList<EditorMultiCursorOccurrence> candidates =
        scopedOccurrences(state->editor->document(),
                          occurrences,
                          scopeStart,
                          scopeEnd);
    if (candidates.isEmpty())
        return 0;

    const int oldPrimary =
        state->carets.isEmpty()
            ? 0
            : qBound(0,
                     state->primaryIndex,
                     state->carets.size() - 1);
    const int oldPosition =
        state->carets.isEmpty()
            ? state->editor->textCursor().position()
            : state->carets.at(oldPrimary).position;
    QList<EditorMultiCursorCaret> carets;
    carets.reserve(candidates.size());
    int primary = 0;
    for (int index = 0; index < candidates.size(); ++index) {
        const EditorMultiCursorOccurrence& candidate =
            candidates.at(index);
        carets.append(EditorMultiCursorCaret{
            candidate.start,
            candidate.end,
            -1,
        });
        if (oldPosition >= candidate.start
            && oldPosition <= candidate.end) {
            primary = index;
        }
    }
    setCarets(carets, primary);
    return state->carets.size();
}

bool EditorMultiCursorController::insertText(
    const QString& text)
{
    if (text.isEmpty())
        return false;
    return insertPerCaret(
        *state,
        QStringList(state->carets.size(), text));
}

bool EditorMultiCursorController::insertTextStructurally(
    const QString& text,
    const TSDocument* syntaxDocument)
{
    if (text.size() != 1)
        return insertText(text);
    const QChar typed = text.at(0);
    const QChar closer = matchingCloser(typed);
    if (closer.isNull() && !closingCharacter(typed))
        return insertText(text);
    if (!state->editor || state->carets.isEmpty())
        return false;

    QTextDocument* document = state->editor->document();
    const int documentEnd =
        maximumDocumentPosition(document);
    QList<PendingEdit> edits;
    edits.reserve(state->carets.size());
    QList<EditorMultiCursorCaret> resultingCarets =
        state->carets;
    bool movedAcrossExistingCloser = false;

    for (int index = 0;
         index < state->carets.size();
         ++index) {
        const EditorMultiCursorCaret& caret =
            state->carets.at(index);
        const int position =
            qBound(0, caret.position, documentEnd);
        if (!caret.hasSelection()
            && closingCharacter(typed)
            && position < documentEnd
            && document->characterAt(position) == typed) {
            const int next =
                nextCharacterPosition(document, position);
            resultingCarets[index].anchor = next;
            resultingCarets[index].position = next;
            resultingCarets[index].virtualColumn = -1;
            movedAcrossExistingCloser = true;
            continue;
        }

        const QString padding =
            virtualPadding(document, caret);
        QString replacement;
        int caretOffset = -1;
        if (!closer.isNull()) {
            if (caret.hasSelection()) {
                replacement =
                    QString(typed)
                    + documentRangeText(
                        document,
                        caret.selectionStart(),
                        caret.selectionEnd())
                    + QString(closer);
            } else {
                const int probe =
                    position > 0 ? position - 1 : position;
                if (!syntaxLiteralAt(
                        syntaxDocument, probe)) {
                    replacement =
                        padding
                        + QString(typed)
                        + QString(closer);
                    caretOffset =
                        padding.size() + 1;
                }
            }
        }
        if (replacement.isEmpty()) {
            replacement = padding + QString(typed);
            caretOffset = replacement.size();
        }
        edits.append(PendingEdit{
            caret.selectionStart(),
            caret.selectionEnd(),
            replacement,
            index,
            caretOffset,
        });
    }

    return applyEdits(*state,
                      std::move(edits),
                      std::move(resultingCarets),
                      movedAcrossExistingCloser);
}

bool EditorMultiCursorController::insertNewline()
{
    return insertText(QStringLiteral("\n"));
}

bool EditorMultiCursorController::insertStructuralNewline(
    const TSDocument* syntaxDocument,
    int indentWidth)
{
    if (!state->editor || state->carets.isEmpty())
        return false;

    QTextDocument* document = state->editor->document();
    QList<PendingEdit> edits;
    edits.reserve(state->carets.size());
    QList<EditorMultiCursorCaret> resultingCarets =
        state->carets;
    for (int index = 0;
         index < state->carets.size();
         ++index) {
        const EditorMultiCursorCaret& caret =
            state->carets.at(index);
        const int insertionPosition =
            caret.selectionStart();
        TSStructuralNewlineTarget target;
        if (syntaxDocument) {
            target =
                syntaxDocument->structuralNewlineTarget(
                    insertionPosition,
                    indentWidth);
        }
        if (!target.ok()) {
            target.insertionText =
                QStringLiteral("\n");
            target.caretOffset = 1;
        }

        const QString padding =
            virtualPadding(document, caret);
        edits.append(PendingEdit{
            caret.selectionStart(),
            caret.selectionEnd(),
            padding + target.insertionText,
            index,
            static_cast<int>(padding.size())
                + target.caretOffset,
        });
    }
    return applyEdits(*state,
                      std::move(edits),
                      std::move(resultingCarets),
                      false);
}

bool EditorMultiCursorController::backspace()
{
    if (!state->editor || state->carets.isEmpty())
        return false;
    QList<PendingEdit> edits;
    QList<EditorMultiCursorCaret> resultingCarets =
        state->carets;
    bool virtualChanged = false;
    for (int index = 0; index < state->carets.size(); ++index) {
        const EditorMultiCursorCaret& caret =
            state->carets.at(index);
        if (caret.hasSelection()) {
            edits.append(PendingEdit{
                caret.selectionStart(),
                caret.selectionEnd(),
                QString(),
                index,
            });
            continue;
        }
        const int column =
            physicalColumn(state->editor->document(),
                           caret.position);
        if (caret.virtualColumn > column) {
            resultingCarets[index].virtualColumn =
                caret.virtualColumn - 1;
            if (resultingCarets.at(index).virtualColumn
                <= column) {
                resultingCarets[index].virtualColumn = -1;
            }
            virtualChanged = true;
            continue;
        }
        const int previous =
            previousCharacterPosition(
                state->editor->document(),
                caret.position);
        if (previous < caret.position) {
            edits.append(PendingEdit{
                previous,
                caret.position,
                QString(),
                index,
            });
        }
    }
    return applyEdits(*state,
                      std::move(edits),
                      std::move(resultingCarets),
                      virtualChanged);
}

bool EditorMultiCursorController::deleteForward()
{
    if (!state->editor || state->carets.isEmpty())
        return false;
    QList<PendingEdit> edits;
    QList<EditorMultiCursorCaret> resultingCarets =
        state->carets;
    for (int index = 0; index < state->carets.size(); ++index) {
        const EditorMultiCursorCaret& caret =
            state->carets.at(index);
        if (caret.hasSelection()) {
            edits.append(PendingEdit{
                caret.selectionStart(),
                caret.selectionEnd(),
                QString(),
                index,
            });
            continue;
        }
        if (caret.virtualColumn >= 0)
            continue;
        const int next =
            nextCharacterPosition(
                state->editor->document(),
                caret.position);
        if (next > caret.position) {
            edits.append(PendingEdit{
                caret.position,
                next,
                QString(),
                index,
            });
        }
    }
    return applyEdits(*state,
                      std::move(edits),
                      std::move(resultingCarets),
                      false);
}

bool EditorMultiCursorController::unindent(int spaceCount)
{
    if (!state->editor
        || state->carets.isEmpty()
        || spaceCount <= 0) {
        return false;
    }

    QTextDocument* document =
        state->editor->document();
    const QList<int> blockNumbers =
        affectedBlockNumbers(document, state->carets);
    QList<int> removedByBlock(
        document->blockCount(), 0);
    QList<PendingEdit> edits;
    edits.reserve(blockNumbers.size());
    for (const int blockNumber : blockNumbers) {
        const QTextBlock block =
            document->findBlockByNumber(blockNumber);
        if (!block.isValid())
            continue;
        const QString text = block.text();
        int count = 0;
        while (count < text.size()
               && count < spaceCount
               && text.at(count) == QLatin1Char(' ')) {
            ++count;
        }
        if (count <= 0)
            continue;
        removedByBlock[blockNumber] = count;
        edits.append(PendingEdit{
            block.position(),
            block.position() + count,
            QString(),
            -1,
        });
    }

    QList<EditorMultiCursorCaret> resultingCarets =
        state->carets;
    for (EditorMultiCursorCaret& caret :
         resultingCarets) {
        if (caret.virtualColumn < 0)
            continue;
        const QTextBlock block =
            document->findBlock(caret.position);
        if (!block.isValid())
            continue;
        const int removed =
            removedByBlock.value(block.blockNumber());
        if (removed <= 0)
            continue;
        const int newPhysical =
            qMax(0,
                 physicalColumn(document,
                                caret.position)
                     - removed);
        const int newVirtual =
            qMax(0, caret.virtualColumn - removed);
        caret.virtualColumn =
            newVirtual > newPhysical
            ? newVirtual : -1;
    }
    return applyEdits(*state,
                      std::move(edits),
                      std::move(resultingCarets),
                      false);
}

bool EditorMultiCursorController::moveCarets(
    EditorMultiCursorMove move,
    bool keepSelection)
{
    if (!state->editor || state->carets.isEmpty())
        return false;

    QTextDocument* document =
        state->editor->document();
    QList<EditorMultiCursorCaret> moved =
        state->carets;
    const int lastBlockNumber =
        qMax(0, document->blockCount() - 1);
    for (int index = 0;
         index < moved.size();
         ++index) {
        const EditorMultiCursorCaret original =
            moved.at(index);
        EditorMultiCursorCaret next = original;
        const QTextBlock block =
            document->findBlock(original.position);
        if (!block.isValid())
            continue;

        const int physical =
            original.position - block.position();
        const int desiredColumn =
            original.virtualColumn >= 0
            ? original.virtualColumn
            : physical;
        const bool point =
            !original.hasSelection();

        if (point
            && original.virtualColumn >= 0
            && (move == EditorMultiCursorMove::Left
                || move == EditorMultiCursorMove::Right)) {
            int virtualColumn =
                original.virtualColumn
                + (move
                       == EditorMultiCursorMove::Left
                   ? -1 : 1);
            next.virtualColumn =
                virtualColumn > physical
                ? virtualColumn : -1;
            if (next.virtualColumn < 0) {
                next.position =
                    block.position() + physical;
                next.anchor = next.position;
            }
            moved[index] = next;
            continue;
        }

        if (point
            && !keepSelection
            && (move == EditorMultiCursorMove::Up
                || move == EditorMultiCursorMove::Down)) {
            const int targetLine =
                qBound(
                    0,
                    block.blockNumber()
                        + (move
                               == EditorMultiCursorMove::Up
                           ? -1 : 1),
                    lastBlockNumber);
            const QTextBlock target =
                document->findBlockByNumber(
                    targetLine);
            if (target.isValid()) {
                const int targetColumn =
                    qMin(desiredColumn,
                         target.text().size());
                next.position =
                    target.position()
                    + targetColumn;
                next.anchor = next.position;
                next.virtualColumn =
                    desiredColumn
                            > target.text().size()
                    ? desiredColumn : -1;
                moved[index] = next;
            }
            continue;
        }

        QTextCursor cursor(document);
        cursor.setPosition(original.anchor);
        cursor.setPosition(
            original.position,
            QTextCursor::KeepAnchor);
        QTextCursor::MoveOperation operation =
            QTextCursor::NoMove;
        switch (move) {
        case EditorMultiCursorMove::Left:
            operation =
                QTextCursor::PreviousCharacter;
            break;
        case EditorMultiCursorMove::Right:
            operation =
                QTextCursor::NextCharacter;
            break;
        case EditorMultiCursorMove::Up:
            operation = QTextCursor::Up;
            break;
        case EditorMultiCursorMove::Down:
            operation = QTextCursor::Down;
            break;
        case EditorMultiCursorMove::LineStart:
            operation = QTextCursor::StartOfLine;
            break;
        case EditorMultiCursorMove::LineEnd:
            operation = QTextCursor::EndOfLine;
            break;
        }
        cursor.movePosition(
            operation,
            keepSelection
                ? QTextCursor::KeepAnchor
                : QTextCursor::MoveAnchor);
        next.anchor = cursor.anchor();
        next.position = cursor.position();
        next.virtualColumn = -1;
        moved[index] = next;
    }

    const QList<EditorMultiCursorCaret> before =
        state->carets;
    replaceStateCarets(
        *state,
        moved,
        state->primaryIndex);
    return !sameCarets(before, state->carets);
}

QString EditorMultiCursorController::copySelections() const
{
    if (!state->editor || state->carets.isEmpty())
        return {};

    QTextDocument* document =
        state->editor->document();
    QSet<int> copiedPointBlocks;
    QStringList chunks;
    chunks.reserve(state->carets.size());
    bool allChunksAreWholeLines = true;
    for (const EditorMultiCursorCaret& caret :
         std::as_const(state->carets)) {
        if (caret.hasSelection()) {
            allChunksAreWholeLines = false;
            chunks.append(
                documentRangeText(
                    document,
                    caret.selectionStart(),
                    caret.selectionEnd()));
            continue;
        }

        const QTextBlock block =
            document->findBlock(caret.position);
        if (!block.isValid()
            || copiedPointBlocks.contains(
                block.blockNumber())) {
            continue;
        }
        copiedPointBlocks.insert(block.blockNumber());
        chunks.append(block.text());
    }
    if (chunks.isEmpty())
        return {};
    QString result = chunks.join(QLatin1Char('\n'));
    if (allChunksAreWholeLines)
        result.append(QLatin1Char('\n'));
    return result;
}

bool EditorMultiCursorController::cutSelections(
    QString* copiedText)
{
    const QString copied = copySelections();
    if (copiedText)
        *copiedText = copied;
    if (!state->editor || state->carets.isEmpty())
        return false;

    QTextDocument* document =
        state->editor->document();
    QSet<int> cutPointBlocks;
    QList<DocumentRange> ranges;
    ranges.reserve(state->carets.size());
    QList<EditorMultiCursorCaret> resultingCarets =
        state->carets;
    for (const EditorMultiCursorCaret& caret :
         std::as_const(state->carets)) {
        if (caret.hasSelection()) {
            ranges.append(DocumentRange{
                caret.selectionStart(),
                caret.selectionEnd(),
            });
            continue;
        }

        const QTextBlock block =
            document->findBlock(caret.position);
        if (!block.isValid()
            || cutPointBlocks.contains(
                block.blockNumber())) {
            continue;
        }
        cutPointBlocks.insert(block.blockNumber());
        ranges.append(
            wholeLineDeletionRange(document, block));
    }
    QList<PendingEdit> edits =
        mergedDeletionEdits(std::move(ranges));
    return applyEdits(*state,
                      std::move(edits),
                      std::move(resultingCarets),
                      false);
}

bool EditorMultiCursorController::pasteText(
    const QString& clipboardText,
    bool distributeMatchingRows)
{
    if (!state->editor || state->carets.isEmpty())
        return false;
    const QString normalized =
        normalizedClipboardText(clipboardText);
    if (normalized.isEmpty())
        return false;
    QStringList replacements;
    QStringList rows =
        normalized.split(QLatin1Char('\n'),
                         Qt::KeepEmptyParts);
    if (rows.size() == state->carets.size() + 1
        && rows.constLast().isEmpty()) {
        rows.removeLast();
    }
    if (distributeMatchingRows
        && rows.size() > 1
        && rows.size() == state->carets.size()) {
        replacements = rows;
    } else {
        replacements =
            QStringList(state->carets.size(), normalized);
    }
    return insertPerCaret(*state, replacements);
}

bool EditorMultiCursorController::escapeToSingleCursor()
{
    if (state->carets.size() <= 1)
        return false;
    const int primary =
        qBound(0,
               state->primaryIndex,
               state->carets.size() - 1);
    const EditorMultiCursorCaret caret =
        state->carets.at(primary);
    replaceStateCarets(
        *state,
        QList<EditorMultiCursorCaret>{caret},
        0,
        EditorModeExitReason::Canceled);
    return true;
}

void EditorMultiCursorController::paint(
    MyCodeEditor* editor,
    QPaintEvent* event) const
{
    if (!editor
        || !event
        || state->editor != editor
        || state->carets.size() <= 1) {
        return;
    }

    QPainter painter(editor->viewport());
    QColor selectionColor =
        editor->palette().color(
            QPalette::Highlight);
    selectionColor.setAlpha(64);
    QColor caretColor =
        editor->palette().color(
            QPalette::Highlight);
    caretColor.setAlpha(220);
    const QFontMetrics metrics(editor->font());
    const qreal spaceWidth =
        metrics.horizontalAdvance(
            QLatin1Char(' '));

    for (const EditorMultiCursorCaret& caret :
         std::as_const(state->carets)) {
        if (caret.hasSelection()) {
            const int selectionStart =
                caret.selectionStart();
            const int selectionEnd =
                caret.selectionEnd();
            QTextBlock block =
                editor->document()->findBlock(
                    selectionStart);
            while (block.isValid()
                   && block.position()
                          < selectionEnd) {
                const int blockStart =
                    block.position();
                const int blockTextEnd =
                    blockStart + block.text().size();
                const int segmentStart =
                    qMax(selectionStart,
                         blockStart);
                const int segmentEnd =
                    qMin(selectionEnd,
                         blockTextEnd);
                QTextCursor startCursor(
                    editor->document());
                startCursor.setPosition(
                    segmentStart);
                QTextCursor endCursor(
                    editor->document());
                endCursor.setPosition(
                    segmentEnd);
                const QRect startRect =
                    editor->cursorRect(
                        startCursor);
                const QRect endRect =
                    editor->cursorRect(
                        endCursor);
                QRectF highlight(
                    startRect.left(),
                    startRect.top(),
                    qMax(
                        2,
                        endRect.left()
                            - startRect.left()),
                    startRect.height());
                if (selectionEnd
                        > blockTextEnd
                    && block.next().isValid()) {
                    highlight.setRight(
                        editor->viewport()
                            ->width());
                }
                if (event->rect().intersects(
                        highlight.toAlignedRect())) {
                    painter.fillRect(
                        highlight,
                        selectionColor);
                }
                if (block == editor->document()
                                 ->findBlock(
                                     selectionEnd)) {
                    break;
                }
                block = block.next();
            }
        }

        QTextCursor cursor(
            editor->document());
        cursor.setPosition(caret.position);
        QRect caretRect =
            editor->cursorRect(cursor);
        if (caret.virtualColumn >= 0) {
            const QTextBlock block =
                cursor.block();
            const int physical =
                caret.position
                - block.position();
            caretRect.translate(
                qRound(
                    (caret.virtualColumn
                     - physical)
                    * spaceWidth),
                0);
        }
        if (!event->rect().intersects(
                caretRect.adjusted(-2, 0, 2, 0))) {
            continue;
        }
        painter.fillRect(
            QRect(caretRect.left(),
                  caretRect.top(),
                  2,
                  caretRect.height()),
            caretColor);
    }
}
