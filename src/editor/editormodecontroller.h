#ifndef EDITORMODECONTROLLER_H
#define EDITORMODECONTROLLER_H

#include <QFlags>
#include <QList>
#include <QMetaType>
#include <QString>

#include <functional>
#include <memory>

enum class EditorModeId
{
    None,
    CommandMode,
    InlineCandidates,
    CompletionCandidates,
    TemplateSlots,
    SignalSelection,
    ColumnSelection,
    VirtualCursor,
    MultiCursor,
    KeywordGhost,
    FoldRegion,
    SourceNavigation,
    InsightTargetPick,
};

enum class EditorModeOwner
{
    Unknown,
    Command,
    Completion,
    KeywordCompletion,
    TemplateInsertion,
    SignalSelection,
    ColumnEditing,
    VirtualCursor,
    MultiCursor,
    Folding,
    SourceNavigation,
    InsightTargeting,
};

enum class EditorModeInput : quint32
{
    None = 0,
    Escape = 1U << 0,
    Tab = 1U << 1,
    Backtab = 1U << 2,
    Enter = 1U << 3,
    Text = 1U << 4,
    Backspace = 1U << 5,
    Navigation = 1U << 6,
    Mouse = 1U << 7,
    ContextMenu = 1U << 8,
    DragDrop = 1U << 9,
    Clipboard = 1U << 10,
    Modifier = 1U << 11,
};
Q_DECLARE_FLAGS(EditorModeInputs, EditorModeInput)
Q_DECLARE_OPERATORS_FOR_FLAGS(EditorModeInputs)

enum class EditorModeEscapeBehavior
{
    Cancel,
    Close,
    Finish,
};

enum class EditorModeEntryReason
{
    None,
    UserAction,
    InlineCommand,
    TemplateInserted,
    MouseGesture,
    KeyboardGesture,
    ContextMenu,
    Restore,
};

enum class EditorModeExitReason
{
    None,
    Completed,
    Canceled,
    Conflict,
    CursorMoved,
    DocumentChanged,
    DocumentClosed,
    TabChanged,
    ExternalControl,
    FileIdentityChanged,
    Replaced,
};

struct EditorModeDescriptor
{
    EditorModeId id = EditorModeId::None;
    EditorModeOwner owner = EditorModeOwner::Unknown;
    QString stableId;
    QString displayName;
    QString guidance;
    int priority = 0;
    EditorModeInputs inputs;
    EditorModeEscapeBehavior escapeBehavior =
        EditorModeEscapeBehavior::Cancel;
};

struct EditorModeSnapshot
{
    quint64 revision = 0;
    QList<EditorModeId> activeModes;
    EditorModeId primaryMode = EditorModeId::None;
    QString displayText;
    QString detailText;

    bool hasActiveMode() const
    {
        return primaryMode != EditorModeId::None;
    }
};

const QList<EditorModeDescriptor>& editorModeDescriptors();
const EditorModeDescriptor* findEditorModeDescriptor(EditorModeId id);
QString editorModeStableId(EditorModeId id);
QString editorModeOwnerText(EditorModeOwner owner);
QString editorModeExitReasonText(EditorModeExitReason reason);

class EditorModeController
{
public:
    using ExitHandler =
        std::function<void(EditorModeExitReason)>;
    using ChangeHandler =
        std::function<void(const EditorModeSnapshot&)>;

    EditorModeController();
    ~EditorModeController();

    EditorModeController(const EditorModeController&) = delete;
    EditorModeController& operator=(const EditorModeController&) =
        delete;
    EditorModeController(EditorModeController&&) noexcept;
    EditorModeController& operator=(EditorModeController&&) noexcept;

    bool enter(EditorModeId id, EditorModeEntryReason reason);
    bool exit(EditorModeId id, EditorModeExitReason reason);
    void exitAll(EditorModeExitReason reason);

    bool isActive(EditorModeId id) const;
    bool hasActiveMode() const;
    bool canCoexist(EditorModeId left, EditorModeId right) const;
    bool ownsInput(EditorModeId id, EditorModeInput input) const;

    EditorModeId primaryMode() const;
    EditorModeId highestPriorityForInput(
        EditorModeInput input) const;
    EditorModeId escapeTarget() const;
    EditorModeSnapshot snapshot() const;

    bool updatePresentation(EditorModeId id,
                            const QString& displayText,
                            const QString& detailText = {});
    void setExitHandler(EditorModeId id, ExitHandler handler);
    void setChangeHandler(ChangeHandler handler);
    EditorModeExitReason lastExitReason(EditorModeId id) const;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

Q_DECLARE_METATYPE(EditorModeId)
Q_DECLARE_METATYPE(EditorModeExitReason)
Q_DECLARE_METATYPE(EditorModeSnapshot)

#endif // EDITORMODECONTROLLER_H
