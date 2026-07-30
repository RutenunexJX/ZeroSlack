#include "editormodecontroller.h"

#include <QHash>

#include <algorithm>
#include <utility>

namespace {
using Input = EditorModeInput;

const QList<EditorModeDescriptor>& descriptors()
{
    static const QList<EditorModeDescriptor> value = {
        {
            EditorModeId::InlineCandidates,
            EditorModeOwner::Completion,
            QStringLiteral("inline-candidates"),
            QStringLiteral("Inline candidates"),
            QStringLiteral(
                "Type or Backspace to filter; Tab/Enter accepts; "
                "Esc cancels"),
            80,
            Input::Escape | Input::Tab | Input::Backtab
                | Input::Enter | Input::Text | Input::Backspace
                | Input::Navigation,
            EditorModeEscapeBehavior::Cancel,
        },
        {
            EditorModeId::CompletionCandidates,
            EditorModeOwner::Completion,
            QStringLiteral("completion-candidates"),
            QStringLiteral("Completion candidates"),
            QStringLiteral(
                "Navigate candidates; Tab/Enter accepts; Esc closes"),
            75,
            Input::Escape | Input::Tab | Input::Backtab
                | Input::Enter | Input::Text | Input::Backspace
                | Input::Navigation,
            EditorModeEscapeBehavior::Close,
        },
        {
            EditorModeId::TemplateSlots,
            EditorModeOwner::TemplateInsertion,
            QStringLiteral("template-slots"),
            QStringLiteral("Slot mode"),
            QStringLiteral(
                "Tab/Shift+Tab cycles slots; Esc finishes"),
            70,
            Input::Escape | Input::Tab | Input::Backtab
                | Input::Mouse,
            EditorModeEscapeBehavior::Finish,
        },
        {
            EditorModeId::SignalSelection,
            EditorModeOwner::SignalSelection,
            QStringLiteral("signal-selection"),
            QStringLiteral("Signal selection"),
            QStringLiteral(
                "Drag across identifiers; right-click finishes; "
                "Esc cancels"),
            100,
            Input::Escape | Input::Tab | Input::Backtab
                | Input::Enter | Input::Text | Input::Backspace
                | Input::Navigation | Input::Mouse
                | Input::ContextMenu | Input::DragDrop,
            EditorModeEscapeBehavior::Cancel,
        },
        {
            EditorModeId::ColumnSelection,
            EditorModeOwner::ColumnEditing,
            QStringLiteral("column-selection"),
            QStringLiteral("Column selection"),
            QStringLiteral(
                "Type, navigate, or use the clipboard across rows; "
                "Esc cancels"),
            65,
            Input::Escape | Input::Tab | Input::Backtab
                | Input::Enter | Input::Text | Input::Backspace
                | Input::Navigation | Input::Mouse
                | Input::Clipboard,
            EditorModeEscapeBehavior::Cancel,
        },
        {
            EditorModeId::VirtualCursor,
            EditorModeOwner::VirtualCursor,
            QStringLiteral("virtual-cursor"),
            QStringLiteral("Virtual column"),
            QStringLiteral(
                "Type to materialize the column; arrows move; "
                "Esc cancels"),
            60,
            Input::Escape | Input::Text | Input::Backspace
                | Input::Navigation | Input::Mouse,
            EditorModeEscapeBehavior::Cancel,
        },
        {
            EditorModeId::FoldRegion,
            EditorModeOwner::Folding,
            QStringLiteral("fold-region"),
            QStringLiteral("Fold region"),
            QStringLiteral(
                "Select the start and end lines; Esc cancels"),
            95,
            Input::Escape | Input::Tab | Input::Backtab
                | Input::Enter | Input::Text | Input::Backspace
                | Input::Navigation | Input::Mouse,
            EditorModeEscapeBehavior::Cancel,
        },
        {
            EditorModeId::FoldShelf,
            EditorModeOwner::Folding,
            QStringLiteral("fold-shelf"),
            QStringLiteral("Fold shelf"),
            QStringLiteral(
                "Type a shelf key to recall; Esc closes"),
            90,
            Input::Escape | Input::Enter | Input::Text
                | Input::Backspace | Input::Navigation
                | Input::Mouse,
            EditorModeEscapeBehavior::Close,
        },
        {
            EditorModeId::SourceNavigation,
            EditorModeOwner::SourceNavigation,
            QStringLiteral("source-navigation"),
            QStringLiteral("Source navigation"),
            QStringLiteral(
                "Choose a source target; Esc closes"),
            55,
            Input::Escape | Input::Enter | Input::Navigation
                | Input::Mouse,
            EditorModeEscapeBehavior::Close,
        },
    };
    return value;
}

int key(EditorModeId id)
{
    return static_cast<int>(id);
}
}

class EditorModeController::Impl
{
public:
    struct ActiveMode
    {
        EditorModeEntryReason entryReason =
            EditorModeEntryReason::None;
        QString displayText;
        QString detailText;
        quint64 sequence = 0;
    };

    QHash<int, ActiveMode> active;
    QHash<int, ExitHandler> exitHandlers;
    QHash<int, EditorModeExitReason> lastExitReasons;
    ChangeHandler changeHandler;
    quint64 revision = 0;
    quint64 nextSequence = 0;

    QList<EditorModeId> orderedActiveModes() const
    {
        QList<EditorModeId> ids;
        ids.reserve(active.size());
        for (auto it = active.cbegin(); it != active.cend(); ++it)
            ids.append(static_cast<EditorModeId>(it.key()));
        std::sort(ids.begin(),
                  ids.end(),
                  [this](EditorModeId left, EditorModeId right) {
                      const EditorModeDescriptor* leftDescriptor =
                          findEditorModeDescriptor(left);
                      const EditorModeDescriptor* rightDescriptor =
                          findEditorModeDescriptor(right);
                      const int leftPriority =
                          leftDescriptor
                          ? leftDescriptor->priority
                          : 0;
                      const int rightPriority =
                          rightDescriptor
                          ? rightDescriptor->priority
                          : 0;
                      if (leftPriority != rightPriority)
                          return leftPriority > rightPriority;
                      return active.value(key(left)).sequence
                             > active.value(key(right)).sequence;
                  });
        return ids;
    }
};

const QList<EditorModeDescriptor>& editorModeDescriptors()
{
    return descriptors();
}

const EditorModeDescriptor* findEditorModeDescriptor(EditorModeId id)
{
    for (const EditorModeDescriptor& descriptor : descriptors()) {
        if (descriptor.id == id)
            return &descriptor;
    }
    return nullptr;
}

QString editorModeStableId(EditorModeId id)
{
    const EditorModeDescriptor* descriptor =
        findEditorModeDescriptor(id);
    return descriptor ? descriptor->stableId : QString();
}

QString editorModeOwnerText(EditorModeOwner owner)
{
    switch (owner) {
    case EditorModeOwner::Unknown:
        return QStringLiteral("unknown");
    case EditorModeOwner::Completion:
        return QStringLiteral("completion");
    case EditorModeOwner::TemplateInsertion:
        return QStringLiteral("template insertion");
    case EditorModeOwner::SignalSelection:
        return QStringLiteral("signal selection");
    case EditorModeOwner::ColumnEditing:
        return QStringLiteral("column editing");
    case EditorModeOwner::VirtualCursor:
        return QStringLiteral("virtual cursor");
    case EditorModeOwner::Folding:
        return QStringLiteral("folding");
    case EditorModeOwner::SourceNavigation:
        return QStringLiteral("source navigation");
    }
    return QStringLiteral("unknown");
}

QString editorModeExitReasonText(EditorModeExitReason reason)
{
    switch (reason) {
    case EditorModeExitReason::None:
        return QStringLiteral("none");
    case EditorModeExitReason::Completed:
        return QStringLiteral("completed");
    case EditorModeExitReason::Canceled:
        return QStringLiteral("canceled");
    case EditorModeExitReason::Conflict:
        return QStringLiteral("conflict");
    case EditorModeExitReason::CursorMoved:
        return QStringLiteral("cursor moved");
    case EditorModeExitReason::DocumentChanged:
        return QStringLiteral("document changed");
    case EditorModeExitReason::DocumentClosed:
        return QStringLiteral("document closed");
    case EditorModeExitReason::TabChanged:
        return QStringLiteral("tab changed");
    case EditorModeExitReason::ExternalControl:
        return QStringLiteral("external control");
    case EditorModeExitReason::FileIdentityChanged:
        return QStringLiteral("file identity changed");
    case EditorModeExitReason::Replaced:
        return QStringLiteral("replaced");
    }
    return QStringLiteral("unknown");
}

EditorModeController::EditorModeController()
    : d(std::make_unique<Impl>())
{
}

EditorModeController::~EditorModeController() = default;

EditorModeController::EditorModeController(
    EditorModeController&&) noexcept = default;

EditorModeController& EditorModeController::operator=(
    EditorModeController&&) noexcept = default;

bool EditorModeController::enter(EditorModeId id,
                                 EditorModeEntryReason reason)
{
    const EditorModeDescriptor* descriptor =
        findEditorModeDescriptor(id);
    if (!descriptor)
        return false;

    if (d->active.contains(key(id))) {
        Impl::ActiveMode& state = d->active[key(id)];
        state.entryReason = reason;
        state.sequence = ++d->nextSequence;
        ++d->revision;
        if (d->changeHandler)
            d->changeHandler(snapshot());
        return true;
    }

    const QList<EditorModeId> activeModes =
        d->orderedActiveModes();
    for (const EditorModeId activeMode : activeModes) {
        if (!canCoexist(activeMode, id))
            exit(activeMode, EditorModeExitReason::Conflict);
    }

    Impl::ActiveMode state;
    state.entryReason = reason;
    state.displayText = descriptor->displayName;
    state.detailText = descriptor->guidance;
    state.sequence = ++d->nextSequence;
    d->active.insert(key(id), state);
    ++d->revision;
    if (d->changeHandler)
        d->changeHandler(snapshot());
    return true;
}

bool EditorModeController::exit(EditorModeId id,
                                EditorModeExitReason reason)
{
    if (!d->active.contains(key(id)))
        return false;

    d->active.remove(key(id));
    d->lastExitReasons.insert(key(id), reason);
    ++d->revision;

    const ExitHandler handler = d->exitHandlers.value(key(id));
    if (handler)
        handler(reason);
    if (d->changeHandler)
        d->changeHandler(snapshot());
    return true;
}

void EditorModeController::exitAll(EditorModeExitReason reason)
{
    const QList<EditorModeId> activeModes =
        d->orderedActiveModes();
    for (const EditorModeId id : activeModes)
        exit(id, reason);
}

bool EditorModeController::isActive(EditorModeId id) const
{
    return d->active.contains(key(id));
}

bool EditorModeController::hasActiveMode() const
{
    return !d->active.isEmpty();
}

bool EditorModeController::canCoexist(EditorModeId left,
                                      EditorModeId right) const
{
    if (left == EditorModeId::None || right == EditorModeId::None)
        return true;
    if (!findEditorModeDescriptor(left)
        || !findEditorModeDescriptor(right)) {
        return false;
    }
    return left == right;
}

bool EditorModeController::ownsInput(EditorModeId id,
                                     EditorModeInput input) const
{
    if (!isActive(id))
        return false;
    const EditorModeDescriptor* descriptor =
        findEditorModeDescriptor(id);
    return descriptor && descriptor->inputs.testFlag(input);
}

EditorModeId EditorModeController::primaryMode() const
{
    const QList<EditorModeId> activeModes =
        d->orderedActiveModes();
    return activeModes.isEmpty()
               ? EditorModeId::None
               : activeModes.first();
}

EditorModeId EditorModeController::highestPriorityForInput(
    EditorModeInput input) const
{
    const QList<EditorModeId> activeModes =
        d->orderedActiveModes();
    for (const EditorModeId id : activeModes) {
        if (ownsInput(id, input))
            return id;
    }
    return EditorModeId::None;
}

EditorModeId EditorModeController::escapeTarget() const
{
    return highestPriorityForInput(EditorModeInput::Escape);
}

EditorModeSnapshot EditorModeController::snapshot() const
{
    EditorModeSnapshot result;
    result.revision = d->revision;
    result.activeModes = d->orderedActiveModes();
    result.primaryMode =
        result.activeModes.isEmpty()
        ? EditorModeId::None
        : result.activeModes.first();
    if (result.primaryMode != EditorModeId::None) {
        const Impl::ActiveMode state =
            d->active.value(key(result.primaryMode));
        result.displayText = state.displayText;
        result.detailText = state.detailText;
    }
    return result;
}

bool EditorModeController::updatePresentation(
    EditorModeId id,
    const QString& displayText,
    const QString& detailText)
{
    auto it = d->active.find(key(id));
    if (it == d->active.end())
        return false;
    if (it->displayText == displayText
        && it->detailText == detailText) {
        return true;
    }
    it->displayText = displayText;
    it->detailText = detailText;
    ++d->revision;
    if (d->changeHandler)
        d->changeHandler(snapshot());
    return true;
}

void EditorModeController::setExitHandler(
    EditorModeId id,
    ExitHandler handler)
{
    if (!findEditorModeDescriptor(id))
        return;
    if (handler)
        d->exitHandlers.insert(key(id), std::move(handler));
    else
        d->exitHandlers.remove(key(id));
}

void EditorModeController::setChangeHandler(ChangeHandler handler)
{
    d->changeHandler = std::move(handler);
    if (d->changeHandler)
        d->changeHandler(snapshot());
}

EditorModeExitReason EditorModeController::lastExitReason(
    EditorModeId id) const
{
    return d->lastExitReasons.value(
        key(id),
        EditorModeExitReason::None);
}
