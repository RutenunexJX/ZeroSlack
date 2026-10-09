#include "editingtimeservice.h"
#include "mycodeeditor.h"
#include "tabmanager.h"

#include <QApplication>
#include <QDir>
#include <QKeyEvent>
#include <QStandardPaths>
#include <algorithm>
#include <chrono>

EditingTimeService::EditingTimeService(const QString& path, QObject* parent, Clock clock)
    : QObject(parent), store(path), clock(clock ? std::move(clock) : monotonicNow)
{
    setObjectName(QStringLiteral("editingTimeService"));
    saveTimer.setInterval(2000);
    connect(&saveTimer, &QTimer::timeout, this, &EditingTimeService::synchronize);
    saveTimer.start();
    if (qApp) {
        qApp->installEventFilter(this);
        connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive) stopAll();
            });
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
            stopAll();
            synchronize();
        });
    }
}

EditingTimeService::~EditingTimeService()
{
    if (qApp) qApp->removeEventFilter(this);
    stopAll();
    synchronize();
}

qint64 EditingTimeService::monotonicNow()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

QString EditingTimeService::defaultStoragePath()
{
    const QString override = qEnvironmentVariable("ZEROSLACK_EDITING_TIME_STORAGE_PATH");
    return override.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
              .filePath(QStringLiteral("editing-time.json")) : override;
}

void EditingTimeService::attachTabManager(TabManager* manager)
{
    if (!manager) return;
    connect(manager, &TabManager::tabCreated, this, &EditingTimeService::attachEditor);
    connect(manager, &TabManager::auxiliaryViewCreated, this, &EditingTimeService::attachEditor);
    for (auto* editor : manager->openEditors()) attachEditor(editor);
    for (auto* editor : manager->auxiliaryViews()) attachEditor(editor);
}

void EditingTimeService::attachEditor(MyCodeEditor* editor)
{
    if (!editor || editors.contains(editor)) return;
    editors.insert(editor);
    connect(editor, &MyCodeEditor::userEditStarted, this,
            [this, editor](int key, bool repeat) { beginInput(editor, key, repeat); });
    connect(editor, &MyCodeEditor::userEditFinished, this,
            [this, editor](bool changed) { finishInput(editor, changed); });
    connect(editor, &MyCodeEditor::editingInteractionEnded, this,
            [this, editor] { stopEditor(editor); });
    connect(editor, &QObject::destroyed, this, [this, editor] {
        stopEditor(editor);
        inputs.remove(editor);
        editors.remove(editor);
    });
}

void EditingTimeService::beginInput(MyCodeEditor* editor, int key, bool repeat)
{
    if (!editor->isVisible() || editor->isReadOnly()) return;
    Input input{clock(), -1, key, repeat};
    if (key == 0 && shortcutEditor == editor)
        input = shortcutInput;
    input.revision = editor->semanticDocumentRevision();
    input.document = editor->document();
    input.generation = saved.generation;
    inputs.insert(editor, input);
}

void EditingTimeService::finishInput(MyCodeEditor* editor, bool textChanged)
{
    const auto found = inputs.find(editor);
    if (found == inputs.end()) return;
    const Input input = found.value();
    inputs.erase(found);
    if (!textChanged || (input.stoppedAt >= 0 && !input.changedBeforeStop)) return;
    const qint64 end = input.stoppedAt >= 0 ? input.stoppedAt : clock();
    const qint64 start = input.generation == saved.generation
        ? input.start : qMax(input.start, saved.resetAt);
    if (input.key > 0 && input.stoppedAt < 0 && editor->hasFocus()
        && (!input.repeat || heldKeys.value(editor).contains(input.key))) {
        if (!isActive()) activeSince = start;
        heldKeys[editor].insert(input.key);
    } else {
        // IME commits and menu operations have no physical hold interval.
        addInterval(start, isActive() ? qMin(end, activeSince) : end);
    }
    emit changed();
}

void EditingTimeService::addInterval(qint64 start, qint64 end)
{
    if (end <= start) return;
    if (pending.isEmpty() || start > pending.last().end) {
        pending.append({start, end});
        return;
    }
    if (start >= pending.last().start) {
        pending.last().end = qMax(pending.last().end, end);
        return;
    }
    pending.append({start, end});
    // Usually already ordered. Nested/reentrant operations may overlap.
    std::sort(pending.begin(), pending.end(), [](const auto& a, const auto& b) {
        return a.start < b.start;
    });
    QList<EditingTimeInterval> merged;
    for (const auto& span : pending) {
        if (!merged.isEmpty() && span.start <= merged.last().end)
            merged.last().end = qMax(merged.last().end, span.end);
        else
            merged.append(span);
    }
    pending = std::move(merged);
}

void EditingTimeService::checkpoint(qint64 now)
{
    if (!isActive()) return;
    addInterval(activeSince, now);
    activeSince = now;
}

qint64 EditingTimeService::totalNanoseconds() const
{
    qint64 total = saved.total;
    for (const auto& span : pending) total += span.end - span.start;
    if (isActive()) total += qMax(qint64(0), clock() - activeSince);
    return total;
}

void EditingTimeService::stopInput(MyCodeEditor* editor, qint64 now)
{
    auto input = inputs.find(editor);
    if (input == inputs.end() || input->stoppedAt >= 0) return;
    input->stoppedAt = now;
    input->changedBeforeStop = input->document == editor->document()
        && input->revision != editor->semanticDocumentRevision();
}

void EditingTimeService::stopEditor(MyCodeEditor* editor)
{
    const qint64 now = clock();
    if (heldKeys.contains(editor)) {
        checkpoint(now);
        heldKeys.remove(editor);
    }
    stopInput(editor, now);
    if (shortcutEditor == editor) shortcutEditor.clear();
    emit changed();
}

void EditingTimeService::stopAll()
{
    const qint64 now = clock();
    checkpoint(now);
    heldKeys.clear();
    shortcutEditor.clear();
    for (auto* editor : inputs.keys()) stopInput(editor, now);
    emit changed();
}

void EditingTimeService::releaseKey(int key)
{
    checkpoint(clock());
    for (auto it = heldKeys.begin(); it != heldKeys.end();) {
        it->remove(key);
        if (it->isEmpty()) it = heldKeys.erase(it);
        else ++it;
    }
    if (shortcutInput.key == key) shortcutEditor.clear();
    emit changed();
}

bool EditingTimeService::acceptResult(const EditingTimeSaveResult& result)
{
    if (!result.ok) {
        if (lastError != result.error) {
            lastError = result.error;
            emit persistenceFailed(lastError);
            emit changed();
        }
        return false;
    }
    saved = result.snapshot;
    pending.clear();
    lastError.clear();
    emit changed();
    return true;
}

bool EditingTimeService::synchronize()
{
    checkpoint(clock());
    return acceptResult(store.synchronize(saved.generation, pending));
}

bool EditingTimeService::reset()
{
    stopAll();
    if (!acceptResult(store.reset(clock()))) return false;
    inputs.clear();
    return true;
}

bool EditingTimeService::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ApplicationDeactivate) {
        stopAll();
    } else if (event->type() == QEvent::KeyRelease) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (!key->isAutoRepeat()) releaseKey(key->key());
    } else if (event->type() == QEvent::ShortcutOverride) {
        auto* editor = qobject_cast<MyCodeEditor*>(watched);
        const auto* key = static_cast<QKeyEvent*>(event);
        if (editor && editors.contains(editor) && editor->hasFocus()
            && key->key() != Qt::Key_Control && key->key() != Qt::Key_Shift
            && key->key() != Qt::Key_Alt && key->key() != Qt::Key_Meta) {
            shortcutEditor = editor;
            shortcutInput = {clock(), -1, key->key(), key->isAutoRepeat()};
            const auto generation = ++shortcutGeneration;
            QTimer::singleShot(0, this, [this, generation] {
                if (shortcutGeneration == generation) shortcutEditor.clear();
            });
        }
    } else if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide
               || event->type() == QEvent::Close || event->type() == QEvent::ReadOnlyChange) {
        auto* editor = qobject_cast<MyCodeEditor*>(watched);
        if (editors.contains(editor)) stopEditor(editor);
    } else if (event->type() == QEvent::WindowDeactivate) {
        for (auto* editor : editors)
            if (editor->window() == watched) stopEditor(editor);
    }
    return QObject::eventFilter(watched, event);
}
