#include "editortemplateslotcontroller.h"
#include "documenttemplatemodel.h"
#include "templateinputservice.h"
#include <QSet>

#include "annotationlayer.h"
#include "editormodecontroller.h"
#include "mycodeeditor.h"

#include <QKeyEvent>
#include <QKeySequence>
#include <QHash>
#include <QPoint>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {
constexpr const char* kTemplateSlotAnnotationSource =
    "template-slots";

QPair<int, int> visibleLineRange(MyCodeEditor* editor,
                                 int firstVisibleLine,
                                 int lastVisibleLine)
{
    if (!editor || !editor->document())
        return {-1, -1};
    if (firstVisibleLine >= 0
        && lastVisibleLine >= firstVisibleLine) {
        return {
            qBound(0,
                   firstVisibleLine,
                   qMax(0, editor->blockCount() - 1)),
            qBound(0,
                   lastVisibleLine,
                   qMax(0, editor->blockCount() - 1))
        };
    }

    const QTextBlock first = editor->cursorForPosition(QPoint(0, 0)).block();
    QTextBlock last = editor->cursorForPosition(
        QPoint(0, qMax(0, editor->viewport()->height() - 1))).block();
    if (!first.isValid())
        return {-1, -1};
    if (!last.isValid()
        || last.blockNumber() < first.blockNumber()) {
        last = first;
    }
    return {first.blockNumber(), last.blockNumber()};
}
}

void EditorTemplateSlotController::bind(
    EditorModeController* modes,
    AnnotationLayer* annotations,
    MyCodeEditor* editor)
{
    modeController = modes;
    annotationLayer = annotations;
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
    annotationLayer = nullptr;
}

int EditorTemplateSlotController::physicalIndexForCursor(
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
    const auto containsCursor =
        [selectionStart, selectionEnd](const SlotRange& slot) {
            return selectionStart >= slot.start
                && selectionEnd <= slot.end;
        };

    // A collapsed cursor at a shared endpoint belongs to both adjacent
    // closed ranges. Preserve the controller's active logical group first;
    // Tab navigation has already selected that group intentionally, and
    // ordinary edits commonly leave the caret at its inclusive end.
    for (int index = 0;
         index < ranges.size();
         ++index) {
        const SlotRange& slot = ranges.at(index);
        if (slot.groupIndex == currentIndex
            && containsCursor(slot)) {
            return index;
        }
    }

    // Outside the active group, an exact zero-length marker is less
    // ambiguous than the inclusive end of a neighbouring non-empty slot.
    if (!cursor.hasSelection()) {
        for (int index = 0;
             index < ranges.size();
             ++index) {
            const SlotRange& slot = ranges.at(index);
            if (slot.start == slot.end
                && selectionStart == slot.start) {
                return index;
            }
        }
    }
    for (int index = 0;
         index < ranges.size();
         ++index) {
        const SlotRange& slot = ranges.at(index);
        if (containsCursor(slot)) {
            return index;
        }
    }
    return -1;
}

int EditorTemplateSlotController::physicalIndexForChange(
    int position,
    int removedLength) const
{
    if (!active())
        return -1;
    const int changeEnd = position + removedLength;
    for (int index = 0; index < ranges.size(); ++index) {
        const SlotRange& slot = ranges.at(index);
        if (slot.groupIndex != currentIndex)
            continue;
        if (position >= slot.start
            && changeEnd <= slot.end) {
            return index;
        }
    }
    return -1;
}

bool EditorTemplateSlotController::cursorInsideActiveRange(
    MyCodeEditor* editor) const
{
    const int physicalIndex =
        physicalIndexForCursor(editor);
    return physicalIndex >= 0
        && ranges.at(physicalIndex).groupIndex
               == currentIndex;
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

void EditorTemplateSlotController::publishVisibleAnnotations(
    MyCodeEditor* editor,
    int firstVisibleLine,
    int lastVisibleLine)
{
    if (!annotationLayer)
        return;
    if (!editor
        || !editor->document()
        || !active()) {
        annotationLayer->removeSource(
            QString::fromLatin1(
                kTemplateSlotAnnotationSource));
        return;
    }

    const auto [firstVisible, lastVisible] =
        visibleLineRange(
            editor,
            firstVisibleLine,
            lastVisibleLine);
    if (firstVisible < 0 || lastVisible < firstVisible) {
        annotationLayer->removeSource(
            QString::fromLatin1(
                kTemplateSlotAnnotationSource));
        return;
    }

    ++presentationGeneration;
    QList<EditorAnnotation> annotations;
    annotations.reserve(ranges.size());
    const int documentEnd =
        qMax(0, editor->document()->characterCount() - 1);
    for (const SlotRange& original : std::as_const(ranges)) {
        auto slot = original;
        if (slot.input.value("kind").toString() == "Code") {
            if (slot.groupIndex != currentIndex) continue;
            const int position = editor->textCursor().position();
            if (position < slot.start || position > slot.end) continue;
            const auto* model = editor->templateModel();
            if (model->inputAt(editor->selectedTemplateId(), position, position) < 0) continue;
            // A container is not one writable rectangle: children own their
            // fixed text. Mark only the active handwriting caret.
            slot.start = slot.end = position;
            slot.visibleWhenEmpty = true;
        }
        if (slot.end <= slot.start
            && !slot.visibleWhenEmpty) {
            continue;
        }

        const int boundedStart =
            qBound(0, slot.start, documentEnd);
        const int boundedLastPosition = qBound(
            boundedStart,
            slot.end > slot.start
                ? slot.end - 1 : slot.start,
            documentEnd);
        const QTextBlock firstBlock =
            editor->document()->findBlock(boundedStart);
        const QTextBlock lastBlock =
            editor->document()->findBlock(
                boundedLastPosition);
        if (!firstBlock.isValid()
            || !lastBlock.isValid()) {
            continue;
        }

        const int firstLine =
            qMax(firstVisible,
                 firstBlock.blockNumber());
        const int finalLine =
            qMin(lastVisible,
                 lastBlock.blockNumber());
        for (int line = firstLine;
             line <= finalLine;
             ++line) {
            const QTextBlock block =
                editor->document()
                    ->findBlockByNumber(line);
            if (!block.isValid()
                || !editor->sourceLineVisible(block.blockNumber())) {
                continue;
            }
            const int blockStart = block.position();
            const int blockEnd =
                block.position() + block.text().size();
            EditorAnnotation annotation;
            annotation.kind =
                EditorAnnotationKind::TemplateSlot;
            annotation.placement =
                EditorAnnotationPlacement::Overlay;
            annotation.range.startPosition =
                qBound(blockStart,
                       boundedStart,
                       blockEnd);
            annotation.range.endPosition =
                qBound(
                    annotation.range.startPosition,
                    qMin(slot.end, blockEnd),
                    blockEnd);
            annotation.range.firstLine = line;
            annotation.range.lastLine = line;
            annotation.detail = slot.name;
            annotation.semanticKey =
                QStringLiteral(
                    "template-slot:%1:%2:%3")
                    .arg(slot.groupIndex)
                    .arg(slot.start)
                    .arg(line);
            annotation.priority =
                AnnotationLayer::defaultPriority(
                    annotation.kind)
                + (slot.groupIndex == currentIndex
                       ? 10 : 0);
            annotation.sourceGeneration =
                presentationGeneration;
            annotation.active =
                slot.groupIndex == currentIndex;
            annotation.phaseVisible = editor->templateInputEditing() || currentBlinkOn;
            annotations.append(
                std::move(annotation));
        }
    }

    if (annotations.isEmpty()) {
        annotationLayer->removeSource(
            QString::fromLatin1(
                kTemplateSlotAnnotationSource));
    } else {
        annotationLayer->setSourceAnnotations(
            QString::fromLatin1(
                kTemplateSlotAnnotationSource),
            annotations);
    }
}

void EditorTemplateSlotController::refreshPresentation(
    MyCodeEditor* editor)
{
    publishVisibleAnnotations(editor);
    if (editor)
        editor->viewport()->update();
}

void EditorTemplateSlotController::stopBlinkTimer()
{
    QObject::disconnect(blinkConnection);
    blinkConnection = {};
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
    blinkConnection = QObject::connect(
        blinkTimer,
        &QTimer::timeout,
        editor,
        [this, editor]() {
            if (!active()) {
                stopBlinkTimer();
                return;
            }
            currentBlinkOn = !currentBlinkOn;
            refreshPresentation(editor);
        });
    blinkTimer->start();
}

void EditorTemplateSlotController::select(
    MyCodeEditor* editor,
    int index)
{
    const int physicalIndex =
        firstPhysicalIndexForGroup(index);
    if (!editor
        || index < 0
        || index >= groupCount()
        || physicalIndex < 0) {
        return;
    }

    currentIndex = index;
    const SlotRange& slot = ranges.at(physicalIndex);
    QTextCursor cursor(editor->document());
    cursor.setPosition(slot.start);
    if (slot.input.value("kind").toString() == "Code") {
        cursor.setPosition(slot.end);
    } else if (slot.end > slot.start) {
        cursor.setPosition(
            slot.end,
            QTextCursor::KeepAnchor);
    }
    ignoreNextCursorCheck = true;
    editor->setTextCursor(cursor);
    refreshPresentation(editor);
    const auto unit = slot.input.value("displayUnit").toString();
    const auto label = QStringLiteral("Slot %1/%2").arg(index + 1).arg(groupCount())
        + (unit.isEmpty() ? QString() : QStringLiteral(" · %1 (%2)").arg(slot.name, unit));
    if (modeController) {
        modeController->updatePresentation(
            EditorModeId::TemplateSlots,
            label,
            QStringLiteral(
                "Tab/Shift+Tab cycles slots; Esc finishes"));
    }
    emit editor->editorStatusMessageRequested(
        label);
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
        || editor->documentReadOnly()
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

    for (const CodeTemplateSlot& slotInfo : slotMetadata) {
        if (slotInfo.start < 0
            || slotInfo.length < 0
            || slotInfo.start + slotInfo.length
                   > insertedLength) {
            continue;
        }

        SlotRange range;
        range.name = slotInfo.name;
        range.input = slotInfo.input;
        range.inputContext = slotInfo.inputContext;
        range.tabStop = slotInfo.tabStop;
        range.visibleWhenEmpty =
            slotInfo.visibleWhenEmpty;
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

    QList<int> explicitTabStops;
    bool hasFinalTabStop = false;
    for (const SlotRange& slot : std::as_const(ranges)) {
        if (slot.tabStop == 0) {
            hasFinalTabStop = true;
        } else if (slot.tabStop > 0
                   && !explicitTabStops.contains(
                       slot.tabStop)) {
            explicitTabStops.append(slot.tabStop);
        }
    }
    std::sort(explicitTabStops.begin(),
              explicitTabStops.end());

    QHash<int, int> explicitGroupIndexes;
    int nextGroupIndex = 0;
    for (const int tabStop :
         std::as_const(explicitTabStops)) {
        explicitGroupIndexes.insert(
            tabStop, nextGroupIndex++);
    }
    for (SlotRange& range : ranges) {
        if (range.tabStop > 0) {
            range.groupIndex =
                explicitGroupIndexes.value(
                    range.tabStop);
        } else if (range.tabStop < 0) {
            range.groupIndex =
                nextGroupIndex++;
        }
    }
    if (hasFinalTabStop) {
        const int finalGroupIndex = nextGroupIndex;
        for (SlotRange& range : ranges) {
            if (range.tabStop == 0)
                range.groupIndex = finalGroupIndex;
        }
    }

    sessionStart = insertionStart;
    sessionEnd = insertionStart + insertedLength;
    modeController->enter(
        EditorModeId::TemplateSlots,
        EditorModeEntryReason::TemplateInserted);
    select(editor, 0);
    if (!editor->templateInputEditing()) ensureBlinkTimer(editor);
}

void EditorTemplateSlotController::syncPersistentRanges(MyCodeEditor* editor, const CodeTemplateSlotList& inputs)
{
    if (!active() || ranges.size() != inputs.size()) {
        const auto cursor = editor->textCursor();
        start(editor, 0, editor->document()->characterCount() - 1, inputs);
        editor->setTextCursor(cursor);
        return;
    }
    for (int i = 0; i < inputs.size(); ++i) {
        ranges[i].start = inputs[i].start;
        ranges[i].end = inputs[i].start + inputs[i].length;
        ranges[i].inputContext = inputs[i].inputContext;
    }
    sessionEnd = editor->document()->characterCount() - 1;
    pendingLinkedEdits.clear();
    refreshPresentation(editor);
}

bool EditorTemplateSlotController::active() const
{
    return modeController
        && modeController->isActive(
            EditorModeId::TemplateSlots)
        && currentIndex >= 0
        && currentIndex < groupCount();
}

int EditorTemplateSlotController::activeIndex() const
{
    return currentIndex;
}

int EditorTemplateSlotController::slotCount() const
{
    return groupCount();
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
    pendingLinkedEdits.clear();
    currentIndex = -1;
    sessionStart = -1;
    sessionEnd = -1;
    currentBlinkOn = true;
    ignoreNextCursorCheck = false;
    presentationPending = false;
    applyingLinkedEdit = false;
    if (annotationLayer) {
        annotationLayer->removeSource(
            QString::fromLatin1(
                kTemplateSlotAnnotationSource));
    }
    if (editor && updatePresentation)
        editor->viewport()->update();
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

    if (event->matches(QKeySequence::Undo)
        || event->matches(QKeySequence::Redo)) {
        clear(editor);
        return false;
    }

    if (event->key() == Qt::Key_Escape) {
        const auto issue = validationError(editor, firstPhysicalIndexForGroup(currentIndex));
        clear(
            editor,
            issue.isEmpty() ? QStringLiteral("Slot Mode canceled") : QStringLiteral("Slot Mode canceled. %1").arg(issue));
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
    const bool navigationKey =
        (forward || backward)
        && (modifiers == Qt::NoModifier
            || modifiers == Qt::ShiftModifier);
    if (navigationKey) {
        if (!cursorInsideActiveRange(editor)) {
            clear(editor);
            return false;
        }
        const int count = groupCount();
        if (count <= 0) {
            clear(editor);
            return false;
        }
        const auto issue = validationError(editor, firstPhysicalIndexForGroup(currentIndex));
        if (!issue.isEmpty() && !backward) {
            emit editor->editorStatusMessageRequested(issue);
            event->accept(); return true;
        }
        // currentIndex is the logical slot selected by the controller.  Do
        // not infer it again from a shared endpoint immediately before
        // navigation; adjacent empty slots can occupy the same cursor
        // position and would turn Shift+Tab into a forward step.
        select(
            editor,
            backward
                ? (currentIndex - 1 + count) % count
                : (currentIndex + 1) % count);
        event->accept();
        return true;
    }

    const int activePhysical = firstPhysicalIndexForGroup(currentIndex);
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)
        && modifiers == Qt::NoModifier && activePhysical >= 0 && cursorInsideActiveRange(editor)) {
        const auto slot = ranges.at(activePhysical);
        if (!slot.input.isEmpty() && slot.input.value("kind").toString() != "Value" && slot.input.value("kind").toString() != "Code") {
            const TemplateInputContext empty;
            const auto choices = TemplateInputService::candidates(slot.input,
                slot.inputContext ? *slot.inputContext : empty, bindings(editor));
            if (choices.values.isEmpty()) emit editor->editorStatusMessageRequested(choices.reason);
            else {
                const int previous = choices.values.indexOf(bindings(editor).value(slot.name));
                const int next = previous < 0 ? (event->key() == Qt::Key_Up ? choices.values.size() - 1 : 0)
                    : (previous + (event->key() == Qt::Key_Up ? -1 : 1) + choices.values.size()) % choices.values.size();
                replaceSlot(editor, activePhysical, choices.values.at(next));
                invalidateDependents(editor, {slot.name});
                select(editor, currentIndex);
                if (!choices.evidence.value(choices.values.at(next)).isEmpty())
                    emit editor->editorStatusMessageRequested(choices.evidence.value(choices.values.at(next)));
            }
            event->accept(); return true;
        }
    }
    const int cursorPhysicalIndex =
        physicalIndexForCursor(editor);
    const int cursorGroupIndex =
        cursorPhysicalIndex >= 0
        ? ranges.at(cursorPhysicalIndex).groupIndex
        : -1;
    if (cursorGroupIndex >= 0
        && cursorGroupIndex != currentIndex) {
        currentIndex = cursorGroupIndex;
        refreshPresentation(editor);
    }
    if (!cursorInsideActiveRange(editor))
        clear(editor);
    return false;
}

void EditorTemplateSlotController::handleContentsChange(
    MyCodeEditor* editor,
    int position,
    int charsRemoved,
    int charsAdded,
    bool updatePresentation)
{
    if (!editor || !active() || applyingLinkedEdit || editor->templateModel()->changing())
        return;

    const int changeStart = position;
    const int changeEnd = position + charsRemoved;
    if (changeStart < sessionStart
        || changeStart > sessionEnd) {
        clear(editor, QString(), updatePresentation);
        return;
    }

    const int sourcePhysicalIndex =
        physicalIndexForChange(position, charsRemoved);
    if (sourcePhysicalIndex < 0) {
        clear(editor, QString(), updatePresentation);
        return;
    }

    const SlotRange activeSlot =
        ranges.at(sourcePhysicalIndex);
    const int relativeStart =
        changeStart - activeSlot.start;
    const int relativeEnd =
        changeEnd - activeSlot.start;
    const int delta = charsAdded - charsRemoved;
    applyRangeChange(sourcePhysicalIndex,
                     changeStart,
                     changeEnd,
                     delta);
    if (ranges.at(sourcePhysicalIndex).end
        < ranges.at(sourcePhysicalIndex).start) {
        clear(editor, QString(), updatePresentation);
        return;
    }
    // Capture now, but mirror after Qt finishes its contentsChange notification.
    // Qt suppresses nested document notifications, which would stale the syntax cache.
    QTextCursor insertion(editor->document());
    insertion.setPosition(position);
    insertion.setPosition(position + charsAdded, QTextCursor::KeepAnchor);
    const QString insertedText = insertion.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    pendingLinkedEdits.append({sourcePhysicalIndex, relativeStart, relativeEnd, insertedText});
    // Qt has not updated its layout while contentsChange is being delivered.
    // The editor transaction publishes annotations after layout catches up.
    markPresentationPending();
}

void EditorTemplateSlotController::flushPendingLinkedEdits(MyCodeEditor* editor)
{
    const auto edits = std::exchange(pendingLinkedEdits, {});
    QStringList changed;
    for (const auto& edit : edits) {
        if (!active()) break;
        mirrorLinkedEdit(editor, edit.source, edit.start, edit.end, edit.text);
        if (edit.source >= 0 && edit.source < ranges.size()) changed << ranges.at(edit.source).name;
    }
    if (active() && !changed.isEmpty()) invalidateDependents(editor, changed);
}

QHash<QString, QString> EditorTemplateSlotController::bindings(MyCodeEditor* editor) const
{
    QHash<QString, QString> result;
    if (!editor) return result;
    for (const auto& slot : ranges) {
        QTextCursor cursor(editor->document());
        const int limit = editor->document()->characterCount() - 1;
        if (slot.start < 0 || slot.end > limit) return {};
        cursor.setPosition(slot.start); cursor.setPosition(slot.end, QTextCursor::KeepAnchor);
        result.insert(slot.name, cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n')));
    }
    return result;
}

QString EditorTemplateSlotController::allValidationError(MyCodeEditor* editor) const
{
    for (int group = 0; group < groupCount(); ++group) {
        const auto issue = validationError(editor, firstPhysicalIndexForGroup(group));
        if (!issue.isEmpty()) return issue;
    }
    return {};
}

QString EditorTemplateSlotController::validationError(MyCodeEditor* editor, int physicalIndex) const
{
    if (physicalIndex < 0 || physicalIndex >= ranges.size()) return {};
    const auto& slot = ranges.at(physicalIndex);
    const TemplateInputContext empty;
    const auto values = bindings(editor);
    const auto issue = TemplateInputService::validate(slot.input, values.value(slot.name),
        slot.inputContext ? *slot.inputContext : empty, values);
    return issue.isEmpty() ? issue : slot.name + QStringLiteral(": ") + issue;
}

void EditorTemplateSlotController::replaceSlot(MyCodeEditor* editor, int physicalIndex, const QString& value, bool joinPrevious)
{
    if (!editor || physicalIndex < 0 || physicalIndex >= ranges.size()) return;
    if (editor->replaceTemplateSlotValue(ranges.at(physicalIndex).start, ranges.at(physicalIndex).end - ranges.at(physicalIndex).start, value)) return;
    if (editor->isReadOnly() || editor->templateInputEditing()) return;
    const auto group = ranges.at(physicalIndex).groupIndex;
    QList<int> targets;
    for (int i = 0; i < ranges.size(); ++i) if (ranges.at(i).groupIndex == group) targets << i;
    std::sort(targets.begin(), targets.end(), [this](int a, int b) { return ranges.at(a).start > ranges.at(b).start; });
    applyingLinkedEdit = true;
    QTextCursor transaction(editor->document());
    if (joinPrevious) transaction.joinPreviousEditBlock(); else transaction.beginEditBlock();
    for (int index : targets) {
        const auto slot = ranges.at(index);
        QTextCursor cursor(editor->document());
        cursor.setPosition(slot.start); cursor.setPosition(slot.end, QTextCursor::KeepAnchor);
        cursor.insertText(value);
        applyRangeChange(index, slot.start, slot.end, value.size() - (slot.end - slot.start));
    }
    transaction.endEditBlock();
    applyingLinkedEdit = false;
    refreshPresentation(editor);
}

void EditorTemplateSlotController::invalidateDependents(MyCodeEditor* editor, const QStringList& changed)
{
    QStringList pending = changed;
    QSet<QString> visited;
    while (active() && !pending.isEmpty()) {
        const auto upstream = pending.takeFirst();
        if (visited.contains(upstream)) continue;
        visited.insert(upstream);
        for (int i = 0; i < ranges.size(); ++i) {
            const auto slot = ranges.at(i);
            if (slot.input.value("instanceSlot").toString() != upstream && slot.input.value("enumOf").toString() != upstream) continue;
            if (bindings(editor).value(slot.name).isEmpty() || validationError(editor, i).isEmpty()) continue;
            replaceSlot(editor, i, QString(), true);
            pending << slot.name;
            emit editor->editorStatusMessageRequested(QStringLiteral("%1 changed; fill %2 again.").arg(upstream, slot.name));
        }
    }
}

void EditorTemplateSlotController::handleCursorChanged(
    MyCodeEditor* editor)
{
    if (!active() || applyingLinkedEdit)
        return;
    if (ignoreNextCursorCheck) {
        ignoreNextCursorCheck = false;
        return;
    }

    const int cursorPhysicalIndex =
        physicalIndexForCursor(editor);
    const int cursorGroupIndex =
        cursorPhysicalIndex >= 0
        ? ranges.at(cursorPhysicalIndex).groupIndex
        : -1;
    if (cursorGroupIndex >= 0) {
        if (cursorGroupIndex != currentIndex) {
            currentIndex = cursorGroupIndex;
            refreshPresentation(editor);
            emit editor->editorStatusMessageRequested(
                QStringLiteral("SLOT %1/%2")
                    .arg(currentIndex + 1)
                    .arg(groupCount()));
            if (modeController) {
                modeController->updatePresentation(
                    EditorModeId::TemplateSlots,
                    QStringLiteral("Slot %1/%2")
                        .arg(currentIndex + 1)
                        .arg(groupCount()),
                    QStringLiteral(
                        "Tab/Shift+Tab cycles slots; Esc finishes"));
            }
        }
        return;
    }
    if (!cursorInsideActiveRange(editor) && !editor->templateAuthoringLocked())
        clear(editor);
}

int EditorTemplateSlotController::groupCount() const
{
    int count = 0;
    for (const SlotRange& slot : ranges)
        count = qMax(count, slot.groupIndex + 1);
    return count;
}

int EditorTemplateSlotController::firstPhysicalIndexForGroup(
    int groupIndex) const
{
    for (int index = 0; index < ranges.size(); ++index) {
        if (ranges.at(index).groupIndex == groupIndex)
            return index;
    }
    return -1;
}

void EditorTemplateSlotController::applyRangeChange(
    int physicalIndex,
    int changeStart,
    int changeEnd,
    int delta)
{
    if (physicalIndex < 0
        || physicalIndex >= ranges.size()) {
        return;
    }
    ranges[physicalIndex].end += delta;
    for (int index = 0; index < ranges.size(); ++index) {
        if (index == physicalIndex)
            continue;
        if (ranges.at(index).start >= changeEnd) {
            ranges[index].start += delta;
            ranges[index].end += delta;
        }
    }
    sessionEnd += delta;
    Q_UNUSED(changeStart)
}

void EditorTemplateSlotController::mirrorLinkedEdit(
    MyCodeEditor* editor,
    int sourcePhysicalIndex,
    int relativeStart,
    int relativeEnd,
    const QString& insertedText)
{
    if (!editor
        || !editor->document()
        || sourcePhysicalIndex < 0
        || sourcePhysicalIndex >= ranges.size()) {
        return;
    }
    const int groupIndex =
        ranges.at(sourcePhysicalIndex).groupIndex;
    QList<int> targets;
    for (int index = 0; index < ranges.size(); ++index) {
        if (index != sourcePhysicalIndex
            && ranges.at(index).groupIndex
                   == groupIndex) {
            targets.append(index);
        }
    }
    if (targets.isEmpty())
        return;

    std::sort(
        targets.begin(),
        targets.end(),
        [this](int left, int right) {
            return ranges.at(left).start
                > ranges.at(right).start;
        });
    for (const int targetIndex : std::as_const(targets)) {
        const SlotRange& slot = ranges.at(targetIndex);
        if (relativeStart < 0
            || relativeEnd < relativeStart
            || relativeEnd > slot.end - slot.start) {
            clear(editor);
            return;
        }
    }

    applyingLinkedEdit = true;
    QTextCursor mirror(editor->document());
    mirror.joinPreviousEditBlock();
    for (const int targetIndex : std::as_const(targets)) {
        const int mirrorStart =
            ranges.at(targetIndex).start + relativeStart;
        const int mirrorEnd =
            ranges.at(targetIndex).start + relativeEnd;
        mirror.setPosition(mirrorStart);
        mirror.setPosition(mirrorEnd,
                           QTextCursor::KeepAnchor);
        mirror.insertText(insertedText);
        applyRangeChange(
            targetIndex,
            mirrorStart,
            mirrorEnd,
            insertedText.size()
                - (mirrorEnd - mirrorStart));
    }
    mirror.endEditBlock();
    applyingLinkedEdit = false;
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
        refreshPresentation(editor);
    else if (annotationLayer) {
        annotationLayer->removeSource(
            QString::fromLatin1(
                kTemplateSlotAnnotationSource));
        if (editor)
            editor->viewport()->update();
    }
    presentationPending = false;
}
