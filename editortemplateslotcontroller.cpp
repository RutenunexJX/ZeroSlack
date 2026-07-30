#include "editortemplateslotcontroller.h"

#include "editormodecontroller.h"
#include "editorselection.h"
#include "mycodeeditor.h"

#include <QKeyEvent>
#include <QPointer>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

void EditorTemplateSlotController::bind(
    EditorModeController* modes,
    EditorSelection* selections,
    MyCodeEditor* editor)
{
    modeController = modes;
    selectionPresenter = selections;
    if (!modeController)
        return;

    const QPointer<MyCodeEditor> target(editor);
    modeController->setExitHandler(
        EditorModeId::TemplateSlots,
        [this, target](EditorModeExitReason) {
            clear(target);
        });
}

void EditorTemplateSlotController::shutdown(
    MyCodeEditor* editor)
{
    clear(editor, QString(), false);
    stopBlinkTimer();
    modeController = nullptr;
    selectionPresenter = nullptr;
}

QList<QPair<int, int>>
EditorTemplateSlotController::highlightRanges() const
{
    QList<QPair<int, int>> result;
    result.reserve(ranges.size());
    for (const SlotRange& slot : ranges) {
        result.append(
            qMakePair(
                slot.start,
                qMax(0, slot.end - slot.start)));
    }
    return result;
}

int EditorTemplateSlotController::indexForCursor(
    MyCodeEditor* editor) const
{
    if (!editor || !active())
        return -1;

    const QTextCursor cursor = editor->textCursor();
    const int selectionStart = cursor.hasSelection()
        ? cursor.selectionStart()
        : cursor.position();
    const int selectionEnd = cursor.hasSelection()
        ? cursor.selectionEnd()
        : cursor.position();
    for (int index = 0;
         index < ranges.size();
         ++index) {
        const SlotRange& slot = ranges.at(index);
        if (selectionStart >= slot.start
            && selectionEnd <= slot.end) {
            return index;
        }
    }
    return -1;
}

bool EditorTemplateSlotController::cursorInsideActiveRange(
    MyCodeEditor* editor) const
{
    return indexForCursor(editor) == currentIndex;
}

bool EditorTemplateSlotController::containsPosition(
    int position) const
{
    if (!active())
        return false;
    for (const SlotRange& slot : ranges) {
        if (position >= slot.start
            && position <= slot.end) {
            return true;
        }
    }
    return false;
}

void EditorTemplateSlotController::refreshHighlights(
    MyCodeEditor* editor)
{
    if (!editor || !selectionPresenter)
        return;
    selectionPresenter->highlightTemplateSlots(
        editor,
        highlightRanges(),
        currentIndex,
        currentBlinkOn);
}

void EditorTemplateSlotController::stopBlinkTimer()
{
    if (!blinkTimer)
        return;
    blinkTimer->stop();
    blinkTimer->deleteLater();
    blinkTimer = nullptr;
}

void EditorTemplateSlotController::ensureBlinkTimer(
    MyCodeEditor* editor)
{
    if (!editor || blinkTimer)
        return;

    currentBlinkOn = true;
    blinkTimer = new QTimer(editor);
    blinkTimer->setInterval(500);
    QObject::connect(
        blinkTimer,
        &QTimer::timeout,
        editor,
        [this, editor]() {
            if (!active()) {
                stopBlinkTimer();
                return;
            }
            currentBlinkOn = !currentBlinkOn;
            refreshHighlights(editor);
        });
    blinkTimer->start();
}

void EditorTemplateSlotController::select(
    MyCodeEditor* editor,
    int index)
{
    if (!editor
        || index < 0
        || index >= ranges.size()) {
        return;
    }

    currentIndex = index;
    const SlotRange& slot = ranges.at(index);
    QTextCursor cursor(editor->document());
    cursor.setPosition(slot.start);
    if (slot.end > slot.start) {
        cursor.setPosition(
            slot.end,
            QTextCursor::KeepAnchor);
    }
    ignoreNextCursorCheck = true;
    editor->setTextCursor(cursor);
    refreshHighlights(editor);
    if (modeController) {
        modeController->updatePresentation(
            EditorModeId::TemplateSlots,
            QStringLiteral("Slot %1/%2")
                .arg(index + 1)
                .arg(ranges.size()),
            QStringLiteral(
                "Tab/Shift+Tab cycles slots; Esc finishes"));
    }
    emit editor->editorStatusMessageRequested(
        QStringLiteral("SLOT %1/%2")
            .arg(index + 1)
            .arg(ranges.size()));
}

void EditorTemplateSlotController::start(
    MyCodeEditor* editor,
    int insertionStart,
    int insertedLength,
    const CodeTemplateSlotList& slotMetadata)
{
    clear(editor);
    if (!editor
        || !editor->document()
        || insertedLength < 0
        || slotMetadata.isEmpty()
        || !modeController) {
        return;
    }

    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    if (insertionStart < 0
        || insertionStart + insertedLength
               > documentEnd) {
        return;
    }

    for (const CodeTemplateSlot& slotInfo :
         slotMetadata) {
        if (slotInfo.start < 0
            || slotInfo.length < 0
            || slotInfo.start + slotInfo.length
                   > insertedLength) {
            continue;
        }

        SlotRange range;
        range.name = slotInfo.name;
        range.start =
            insertionStart + slotInfo.start;
        range.end =
            range.start + slotInfo.length;
        if (range.start < 0
            || range.end < range.start
            || range.end > documentEnd) {
            continue;
        }
        ranges.append(range);
    }

    if (ranges.isEmpty()) {
        clear(editor);
        return;
    }

    sessionStart = insertionStart;
    sessionEnd = insertionStart + insertedLength;
    modeController->enter(
        EditorModeId::TemplateSlots,
        EditorModeEntryReason::TemplateInserted);
    select(editor, 0);
    ensureBlinkTimer(editor);
}

bool EditorTemplateSlotController::active() const
{
    return modeController
        && modeController->isActive(
            EditorModeId::TemplateSlots)
        && currentIndex >= 0
        && currentIndex < ranges.size();
}

int EditorTemplateSlotController::activeIndex() const
{
    return currentIndex;
}

int EditorTemplateSlotController::slotCount() const
{
    return ranges.size();
}

bool EditorTemplateSlotController::blinkOn() const
{
    return currentBlinkOn;
}

void EditorTemplateSlotController::clear(
    MyCodeEditor* editor,
    const QString& message,
    bool updatePresentation)
{
    const bool wasActive =
        active() || !ranges.isEmpty();
    const bool controllerActive =
        modeController
        && modeController->isActive(
            EditorModeId::TemplateSlots);
    stopBlinkTimer();
    ranges.clear();
    currentIndex = -1;
    sessionStart = -1;
    sessionEnd = -1;
    currentBlinkOn = true;
    ignoreNextCursorCheck = false;
    presentationPending = false;
    if (editor
        && updatePresentation
        && selectionPresenter) {
        selectionPresenter->clearTemplateSlots(editor);
    }
    if (wasActive
        && editor
        && !message.isEmpty()) {
        emit editor->editorStatusMessageRequested(
            message);
    }
    if (controllerActive) {
        modeController->exit(
            EditorModeId::TemplateSlots,
            message.contains(
                QStringLiteral("cancel"),
                Qt::CaseInsensitive)
                ? EditorModeExitReason::Canceled
                : EditorModeExitReason::Completed);
    }
}

bool EditorTemplateSlotController::handleKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    if (!editor || !event || !active())
        return false;

    const int cursorSlotIndex =
        indexForCursor(editor);
    if (cursorSlotIndex >= 0
        && cursorSlotIndex != currentIndex) {
        currentIndex = cursorSlotIndex;
        refreshHighlights(editor);
    }
    if (!cursorInsideActiveRange(editor)) {
        clear(editor);
        return false;
    }

    if (event->key() == Qt::Key_Escape) {
        clear(
            editor,
            QStringLiteral("Slot Mode canceled"));
        event->accept();
        return true;
    }

    const Qt::KeyboardModifiers modifiers =
        event->modifiers()
        & (Qt::ShiftModifier
           | Qt::ControlModifier
           | Qt::AltModifier
           | Qt::MetaModifier);
    const bool forward =
        event->key() == Qt::Key_Tab
        && modifiers == Qt::NoModifier;
    const bool backward =
        event->key() == Qt::Key_Backtab
        || (event->key() == Qt::Key_Tab
            && modifiers == Qt::ShiftModifier);
    if ((!forward && !backward)
        || (modifiers != Qt::NoModifier
            && modifiers != Qt::ShiftModifier)) {
        return false;
    }

    const int count = ranges.size();
    select(
        editor,
        backward
            ? (currentIndex - 1 + count) % count
            : (currentIndex + 1) % count);
    event->accept();
    return true;
}

void EditorTemplateSlotController::handleContentsChange(
    MyCodeEditor* editor,
    int position,
    int charsRemoved,
    int charsAdded,
    bool updatePresentation)
{
    if (!editor || !active())
        return;

    const int changeStart = position;
    const int changeEnd = position + charsRemoved;
    if (changeStart < sessionStart
        || changeStart > sessionEnd) {
        clear(editor, QString(), updatePresentation);
        return;
    }

    SlotRange& activeSlot = ranges[currentIndex];
    if (changeStart < activeSlot.start
        || changeEnd > activeSlot.end) {
        clear(editor, QString(), updatePresentation);
        return;
    }

    const int delta = charsAdded - charsRemoved;
    activeSlot.end += delta;
    if (activeSlot.end < activeSlot.start) {
        clear(editor, QString(), updatePresentation);
        return;
    }
    for (int index = 0;
         index < ranges.size();
         ++index) {
        if (index == currentIndex
            || ranges.at(index).start < changeEnd) {
            continue;
        }
        ranges[index].start += delta;
        ranges[index].end += delta;
    }
    sessionEnd += delta;
    if (updatePresentation)
        refreshHighlights(editor);
}

void EditorTemplateSlotController::handleCursorChanged(
    MyCodeEditor* editor)
{
    if (!active())
        return;
    if (ignoreNextCursorCheck) {
        ignoreNextCursorCheck = false;
        return;
    }

    const int cursorSlotIndex =
        indexForCursor(editor);
    if (cursorSlotIndex >= 0) {
        if (cursorSlotIndex != currentIndex) {
            currentIndex = cursorSlotIndex;
            refreshHighlights(editor);
            emit editor->editorStatusMessageRequested(
                QStringLiteral("SLOT %1/%2")
                    .arg(currentIndex + 1)
                    .arg(ranges.size()));
            if (modeController) {
                modeController->updatePresentation(
                    EditorModeId::TemplateSlots,
                    QStringLiteral("Slot %1/%2")
                        .arg(currentIndex + 1)
                        .arg(ranges.size()),
                    QStringLiteral(
                        "Tab/Shift+Tab cycles slots; Esc finishes"));
            }
        }
        return;
    }
    if (!cursorInsideActiveRange(editor))
        clear(editor);
}

void EditorTemplateSlotController::markPresentationPending()
{
    if (active())
        presentationPending = true;
}

void EditorTemplateSlotController::flushPendingPresentation(
    MyCodeEditor* editor)
{
    if (!presentationPending)
        return;
    if (active())
        refreshHighlights(editor);
    else if (editor && selectionPresenter)
        selectionPresenter->clearTemplateSlots(editor);
    presentationPending = false;
}
