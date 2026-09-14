#include "editorinsighttargetpickcontroller.h"

#include "annotationlayer.h"
#include "editormodecontroller.h"
#include "mycodeeditor.h"
#include "tsdocument.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {
constexpr const char* kInsightTargetAnnotationSource = "insight-target";

QPair<int, int> visibleLineRange(MyCodeEditor* editor,
                                 int firstVisibleLine,
                                 int lastVisibleLine)
{
    if (!editor || !editor->document())
        return {-1, -1};
    const int lastBlock = qMax(0, editor->blockCount() - 1);
    if (firstVisibleLine >= 0
        && lastVisibleLine >= firstVisibleLine) {
        return {qBound(0, firstVisibleLine, lastBlock),
                qBound(0, lastVisibleLine, lastBlock)};
    }

    const QTextBlock first =
        editor->cursorForPosition(QPoint(0, 0)).block();
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

void EditorInsightTargetPickController::bind(
    EditorModeController* modes,
    AnnotationLayer* annotations,
    MyCodeEditor* editor,
    SyntaxSource syntax)
{
    modeController = modes;
    annotationLayer = annotations;
    syntaxSource = std::move(syntax);
    if (!modeController)
        return;

    const QPointer<MyCodeEditor> target(editor);
    modeController->setExitHandler(
        EditorModeId::InsightTargetPick,
        [this, target](EditorModeExitReason) {
            clear(target);
        });
}

void EditorInsightTargetPickController::shutdown(MyCodeEditor* editor)
{
    clear(editor);
    stopBlinkTimer();
    modeController = nullptr;
    annotationLayer = nullptr;
    syntaxSource = {};
    nameSetResolver = {};
}

void EditorInsightTargetPickController::setNameSetResolver(
    NameSetResolver resolver)
{
    nameSetResolver = std::move(resolver);
}

bool EditorInsightTargetPickController::start(
    MyCodeEditor* editor,
    EditorInsightTargetClass targetClass,
    Validator validator,
    PickedHandler handler,
    QString* message)
{
    if (!editor || !editor->document()) {
        if (message)
            *message = QStringLiteral("No editor document");
        return false;
    }
    if (!modeController) {
        if (message) {
            *message =
                QStringLiteral("Editor mode controller unavailable");
        }
        return false;
    }

    clear(editor);
    classValue = targetClass;
    validatorValue = std::move(validator);
    pickedHandler = std::move(handler);
    modeController->enter(EditorModeId::InsightTargetPick,
                          EditorModeEntryReason::UserAction);
    refreshCandidates(editor, -1, -1);
    if (candidateList.isEmpty()) {
        // Nothing on screen can be this insight's target. Say so and leave the
        // mode running: scrolling is the fix, and the candidates follow.
        emit editor->editorStatusMessageRequested(
            QStringLiteral(
                "No target for this insight is visible; scroll to one, or Esc to cancel"));
    }
    ensureBlinkTimer(editor);
    refreshPresentation(editor);
    updateModePresentation();
    if (message)
        *message = QStringLiteral("Pick an insight target");
    return true;
}

void EditorInsightTargetPickController::clear(MyCodeEditor* editor,
                                              const QString& message,
                                              EditorModeExitReason reason)
{
    if (clearing)
        return;
    clearing = true;
    const bool wasActive = active();
    candidateList.clear();
    activeKey.clear();
    lastFirstLine = -1;
    lastLastLine = -1;
    validatorValue = {};
    pickedHandler = {};
    stopBlinkTimer();
    if (annotationLayer) {
        annotationLayer->removeSource(
            QString::fromLatin1(kInsightTargetAnnotationSource));
    }
    if (editor)
        editor->viewport()->update();
    if (wasActive && modeController)
        modeController->exit(EditorModeId::InsightTargetPick, reason);
    if (editor && !message.trimmed().isEmpty())
        emit editor->editorStatusMessageRequested(message);
    clearing = false;
}

bool EditorInsightTargetPickController::active() const
{
    return modeController
        && modeController->isActive(EditorModeId::InsightTargetPick);
}

bool EditorInsightTargetPickController::blinkOn() const
{
    return currentBlinkOn;
}

EditorInsightTargetClass
EditorInsightTargetPickController::targetClass() const
{
    return classValue;
}

QList<EditorInsightTargetCandidate>
EditorInsightTargetPickController::candidates() const
{
    return candidateList;
}

int EditorInsightTargetPickController::activeIndex() const
{
    return indexForKey(activeKey);
}

QPair<int, int>
EditorInsightTargetPickController::lastEnumeratedLineRangeForTest() const
{
    return {lastFirstLine, lastLastLine};
}

QString EditorInsightTargetPickController::candidateKey(
    const EditorInsightTargetCandidate& candidate)
{
    return QStringLiteral("%1:%2")
        .arg(candidate.startChar)
        .arg(candidate.name);
}

int EditorInsightTargetPickController::indexForKey(
    const QString& key) const
{
    if (key.isEmpty())
        return -1;
    for (int index = 0; index < candidateList.size(); ++index) {
        if (candidateKey(candidateList.at(index)) == key)
            return index;
    }
    return -1;
}

void EditorInsightTargetPickController::refreshCandidates(
    MyCodeEditor* editor,
    int firstVisibleLine,
    int lastVisibleLine)
{
    candidateList.clear();
    if (!editor || !editor->document() || !active())
        return;
    const TSDocument* syntax = syntaxSource ? syntaxSource() : nullptr;
    if (!syntax)
        return;

    const auto [firstVisible, lastVisible] =
        visibleLineRange(editor, firstVisibleLine, lastVisibleLine);
    if (firstVisible < 0 || lastVisible < firstVisible)
        return;
    lastFirstLine = firstVisible;
    lastLastLine = lastVisible;

    QTextDocument* document = editor->document();
    const QTextBlock firstBlock =
        document->findBlockByNumber(firstVisible);
    const QTextBlock lastBlock =
        document->findBlockByNumber(lastVisible);
    if (!firstBlock.isValid() || !lastBlock.isValid())
        return;
    const int startChar = firstBlock.position();
    const int endChar = lastBlock.position() + lastBlock.text().size();

    if (classValue == EditorInsightTargetClass::Scope) {
        // Scope heads are not identifiers, so they are read straight from the
        // syntax tree, one query per visible line, and only heads that are
        // themselves on screen survive.
        QSet<int> seenStarts;
        for (int line = firstVisible; line <= lastVisible; ++line) {
            const QTextBlock block = document->findBlockByNumber(line);
            if (!block.isValid()
                || !editor->sourceLineVisible(block.blockNumber())) {
                continue;
            }
            const int cursorChar = block.position();
            const TSAlwaysScopeTarget always =
                syntax->alwaysScopeTarget(cursorChar);
            const TSModuleScopeTarget module =
                syntax->moduleScopeTarget(cursorChar);
            struct Head {
                bool ok;
                int startChar;
                int endChar;
                int startLine;
                QString label;
            };
            const QList<Head> heads = {
                {always.ok(), always.startChar, always.endChar,
                 always.startLine, always.label},
                {module.ok(), module.startChar, module.endChar,
                 module.startLine, module.label}};
            for (const Head& head : heads) {
                if (!head.ok
                    || head.startLine < firstVisible
                    || head.startLine > lastVisible
                    || seenStarts.contains(head.startChar)) {
                    continue;
                }
                const QTextBlock headBlock =
                    document->findBlockByNumber(head.startLine);
                if (!headBlock.isValid())
                    continue;
                seenStarts.insert(head.startChar);
                EditorInsightTargetCandidate candidate;
                candidate.name = head.label.trimmed().isEmpty()
                    ? headBlock.text().trimmed()
                    : head.label;
                candidate.scopeLabel = candidate.name;
                candidate.startChar = head.startChar;
                candidate.endChar = qMin(
                    headBlock.position() + headBlock.text().size(),
                    head.endChar);
                candidate.startLine = head.startLine;
                candidate.scopeStartChar = head.startChar;
                candidate.scopeEndChar = head.endChar;
                if (candidate.isValid())
                    candidateList.append(candidate);
            }
        }
    } else {
        // One snapshot query per publish, then a hash lookup per identifier:
        // resolving every visible identifier through the definition service
        // would make blinking cost a semantic round trip per symbol.
        QSet<QString> names;
        if (nameSetResolver) {
            const QList<QString> resolved = nameSetResolver(classValue);
            for (const QString& name : resolved)
                names.insert(name);
        }
        if (names.isEmpty())
            return;
        const QList<TSIdentifierTarget> identifiers =
            syntax->identifiersInRange(startChar, endChar);
        for (const TSIdentifierTarget& identifier : identifiers) {
            if (!identifier.ok() || !names.contains(identifier.text))
                continue;
            const QTextBlock block =
                document->findBlock(identifier.startChar);
            if (!block.isValid()
                || block.blockNumber() < firstVisible
                || block.blockNumber() > lastVisible
                || !editor->sourceLineVisible(block.blockNumber())) {
                continue;
            }
            EditorInsightTargetCandidate candidate;
            candidate.name = identifier.text;
            candidate.startChar = identifier.startChar;
            candidate.endChar = identifier.endChar;
            candidate.startLine = block.blockNumber();
            candidateList.append(candidate);
        }
    }

    std::sort(candidateList.begin(),
              candidateList.end(),
              [](const EditorInsightTargetCandidate& left,
                 const EditorInsightTargetCandidate& right) {
                  return left.startChar < right.startChar;
              });
    if (indexForKey(activeKey) < 0) {
        activeKey = candidateList.isEmpty()
            ? QString()
            : candidateKey(candidateList.first());
    }
}

void EditorInsightTargetPickController::publishVisibleAnnotations(
    MyCodeEditor* editor,
    int firstVisibleLine,
    int lastVisibleLine)
{
    if (!annotationLayer)
        return;
    if (!editor || !editor->document() || !active()) {
        annotationLayer->removeSource(
            QString::fromLatin1(kInsightTargetAnnotationSource));
        return;
    }

    refreshCandidates(editor, firstVisibleLine, lastVisibleLine);
    if (candidateList.isEmpty()) {
        annotationLayer->removeSource(
            QString::fromLatin1(kInsightTargetAnnotationSource));
        return;
    }

    ++presentationGeneration;
    const int current = activeIndex();
    QList<EditorAnnotation> annotations;
    annotations.reserve(candidateList.size());
    for (int index = 0; index < candidateList.size(); ++index) {
        const EditorInsightTargetCandidate& candidate =
            candidateList.at(index);
        const QTextBlock block =
            editor->document()->findBlock(candidate.startChar);
        if (!block.isValid())
            continue;
        const int blockStart = block.position();
        const int blockEnd = blockStart + block.text().size();
        EditorAnnotation annotation;
        annotation.kind = EditorAnnotationKind::InsightTarget;
        annotation.placement = EditorAnnotationPlacement::Overlay;
        annotation.range.startPosition =
            qBound(blockStart, candidate.startChar, blockEnd);
        annotation.range.endPosition =
            qBound(annotation.range.startPosition,
                   qMin(candidate.endChar, blockEnd),
                   blockEnd);
        annotation.range.firstLine = block.blockNumber();
        annotation.range.lastLine = block.blockNumber();
        annotation.detail = candidate.name;
        annotation.semanticKey =
            QStringLiteral("insight-target:%1").arg(candidateKey(candidate));
        annotation.priority =
            AnnotationLayer::defaultPriority(annotation.kind)
            + (index == current ? 10 : 0);
        annotation.sourceGeneration = presentationGeneration;
        annotation.active = index == current;
        annotation.phaseVisible = currentBlinkOn;
        annotations.append(std::move(annotation));
    }

    if (annotations.isEmpty()) {
        annotationLayer->removeSource(
            QString::fromLatin1(kInsightTargetAnnotationSource));
        return;
    }
    annotationLayer->setSourceAnnotations(
        QString::fromLatin1(kInsightTargetAnnotationSource), annotations);
}

void EditorInsightTargetPickController::refreshPresentation(
    MyCodeEditor* editor)
{
    publishVisibleAnnotations(editor);
    if (editor)
        editor->viewport()->update();
}

void EditorInsightTargetPickController::updateModePresentation()
{
    if (!modeController)
        return;
    const int current = activeIndex();
    modeController->updatePresentation(
        EditorModeId::InsightTargetPick,
        candidateList.isEmpty()
            ? QStringLiteral("Insight target: none visible")
            : QStringLiteral("Insight target %1/%2")
                  .arg(current + 1)
                  .arg(candidateList.size()),
        QStringLiteral(
            "Tab/Shift+Tab moves between blinking targets; Enter or click "
            "selects; Esc cancels"));
}

void EditorInsightTargetPickController::stopBlinkTimer()
{
    QObject::disconnect(blinkConnection);
    blinkConnection = {};
    if (!blinkTimer)
        return;
    blinkTimer->stop();
    blinkTimer->deleteLater();
    blinkTimer = nullptr;
}

void EditorInsightTargetPickController::ensureBlinkTimer(
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

void EditorInsightTargetPickController::select(MyCodeEditor* editor,
                                               int index)
{
    if (index < 0 || index >= candidateList.size())
        return;
    activeKey = candidateKey(candidateList.at(index));
    refreshPresentation(editor);
    updateModePresentation();
    if (editor) {
        emit editor->editorStatusMessageRequested(
            QStringLiteral("Insight target %1/%2: %3")
                .arg(index + 1)
                .arg(candidateList.size())
                .arg(candidateList.at(index).name));
    }
}

void EditorInsightTargetPickController::step(MyCodeEditor* editor,
                                             int delta)
{
    if (candidateList.isEmpty())
        return;
    const int count = candidateList.size();
    const int current = activeIndex();
    // The controller's own index is the truth: recomputing it from the cursor
    // would let two candidates on one line turn Shift+Tab into a forward step.
    const int next = current < 0
        ? (delta > 0 ? 0 : count - 1)
        : ((current + delta) % count + count) % count;
    select(editor, next);
}

bool EditorInsightTargetPickController::commit(MyCodeEditor* editor,
                                               int index)
{
    if (index < 0 || index >= candidateList.size())
        return false;
    const EditorInsightTargetCandidate candidate = candidateList.at(index);
    QString reason;
    if (validatorValue && !validatorValue(candidate, &reason)) {
        // A rejected target keeps the mode alive with its reason on screen:
        // only the service that builds the view knows whether a symbol works,
        // and the user should be able to try the next one straight away.
        if (editor) {
            emit editor->editorStatusMessageRequested(
                reason.trimmed().isEmpty()
                    ? QStringLiteral(
                          "%1 cannot be this insight's target").arg(candidate.name)
                    : reason);
        }
        return false;
    }
    const PickedHandler handler = pickedHandler;
    clear(editor, QString(), EditorModeExitReason::Completed);
    if (handler)
        handler(candidate);
    return true;
}

bool EditorInsightTargetPickController::handleKeyPress(
    MyCodeEditor* editor,
    QKeyEvent* event)
{
    if (!editor || !event || !active())
        return false;

    if (event->key() == Qt::Key_Escape) {
        clear(editor, QStringLiteral("Insight target selection canceled"));
        event->accept();
        return true;
    }

    const Qt::KeyboardModifiers modifiers =
        event->modifiers()
        & (Qt::ShiftModifier | Qt::ControlModifier
           | Qt::AltModifier | Qt::MetaModifier);
    const bool forward = event->key() == Qt::Key_Tab
        && modifiers == Qt::NoModifier;
    const bool backward = event->key() == Qt::Key_Backtab
        || (event->key() == Qt::Key_Tab
            && modifiers == Qt::ShiftModifier);
    if (forward || backward) {
        step(editor, backward ? -1 : 1);
        event->accept();
        return true;
    }

    if ((event->key() == Qt::Key_Return
         || event->key() == Qt::Key_Enter)
        && modifiers == Qt::NoModifier) {
        commit(editor, activeIndex());
        event->accept();
        return true;
    }
    return false;
}

bool EditorInsightTargetPickController::handleMousePress(
    MyCodeEditor* editor,
    QMouseEvent* event)
{
    if (!editor || !event || !active()
        || event->button() != Qt::LeftButton) {
        return false;
    }
    const int position =
        editor->cursorForPosition(event->position().toPoint()).position();
    for (int index = 0; index < candidateList.size(); ++index) {
        const EditorInsightTargetCandidate& candidate =
            candidateList.at(index);
        if (position < candidate.startChar || position > candidate.endChar)
            continue;
        activeKey = candidateKey(candidate);
        commit(editor, index);
        event->accept();
        return true;
    }
    // A click somewhere else is an ordinary click; the mode keeps running so
    // the user can scroll or place the caret and then pick.
    return false;
}
