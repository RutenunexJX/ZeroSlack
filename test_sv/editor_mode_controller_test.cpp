#include "editormodecontroller.h"

#include <QList>
#include <QString>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}
}

int main()
{
    const QList<EditorModeDescriptor>& descriptors =
        editorModeDescriptors();
    const QList<EditorModeId> expectedModes = {
        EditorModeId::CommandMode,
        EditorModeId::InlineCandidates,
        EditorModeId::CompletionCandidates,
        EditorModeId::TemplateSlots,
        EditorModeId::SignalSelection,
        EditorModeId::ColumnSelection,
        EditorModeId::VirtualCursor,
        EditorModeId::MultiCursor,
        EditorModeId::KeywordGhost,
        EditorModeId::FoldRegion,
        EditorModeId::FoldShelf,
        EditorModeId::SourceNavigation,
    };

    bool completeDescriptors =
        descriptors.size() == expectedModes.size();
    for (const EditorModeId id : expectedModes) {
        const EditorModeDescriptor* descriptor =
            findEditorModeDescriptor(id);
        completeDescriptors =
            completeDescriptors
            && descriptor
            && descriptor->id == id
            && descriptor->owner != EditorModeOwner::Unknown
            && !descriptor->stableId.trimmed().isEmpty()
            && !descriptor->displayName.trimmed().isEmpty()
            && !descriptor->guidance.trimmed().isEmpty()
            && descriptor->priority > 0
            && descriptor->inputs.testFlag(EditorModeInput::Escape);
    }
    expect("all interaction modes have complete stable descriptors",
           completeDescriptors);

    EditorModeController controller;
    bool symmetricExplicitMatrix = true;
    for (const EditorModeId left : expectedModes) {
        symmetricExplicitMatrix =
            symmetricExplicitMatrix
            && controller.canCoexist(left, left);
        for (const EditorModeId right : expectedModes) {
            if (left == right)
                continue;
            const bool expectedCoexistence =
                (left == EditorModeId::MultiCursor
                 && right == EditorModeId::VirtualCursor)
                || (left == EditorModeId::VirtualCursor
                    && right == EditorModeId::MultiCursor);
            symmetricExplicitMatrix =
                symmetricExplicitMatrix
                && controller.canCoexist(left, right)
                       == expectedCoexistence
                && controller.canCoexist(left, right)
                       == controller.canCoexist(right, left);
        }
    }
    expect("coexistence matrix is symmetric and explicit",
           symmetricExplicitMatrix);

    QList<EditorModeId> exitedModes;
    QList<EditorModeExitReason> exitReasons;
    controller.setExitHandler(
        EditorModeId::TemplateSlots,
        [&](EditorModeExitReason reason) {
            exitedModes.append(EditorModeId::TemplateSlots);
            exitReasons.append(reason);
        });
    controller.setExitHandler(
        EditorModeId::SignalSelection,
        [&](EditorModeExitReason reason) {
            exitedModes.append(EditorModeId::SignalSelection);
            exitReasons.append(reason);
        });

    expect("template slot mode enters",
           controller.enter(EditorModeId::TemplateSlots,
                            EditorModeEntryReason::TemplateInserted));
    expect("conflicting mode replaces prior mode through callback",
           controller.enter(EditorModeId::SignalSelection,
                            EditorModeEntryReason::UserAction)
               && !controller.isActive(EditorModeId::TemplateSlots)
               && controller.isActive(EditorModeId::SignalSelection)
               && exitedModes == QList<EditorModeId>{
                                      EditorModeId::TemplateSlots}
               && exitReasons == QList<EditorModeExitReason>{
                                      EditorModeExitReason::Conflict}
               && controller.lastExitReason(
                      EditorModeId::TemplateSlots)
                      == EditorModeExitReason::Conflict);

    controller.updatePresentation(
        EditorModeId::SignalSelection,
        QStringLiteral("Signal selection: 3"),
        QStringLiteral("Right-click to finish; Esc to cancel"));
    const EditorModeSnapshot signalSnapshot = controller.snapshot();
    expect("snapshot exposes persistent primary-mode presentation",
           signalSnapshot.primaryMode
                   == EditorModeId::SignalSelection
               && signalSnapshot.activeModes
                      == QList<EditorModeId>{
                             EditorModeId::SignalSelection}
               && signalSnapshot.displayText
                      == QStringLiteral("Signal selection: 3")
               && signalSnapshot.detailText.contains(
                      QStringLiteral("Esc"))
               && signalSnapshot.revision > 0);

    const EditorModeDescriptor* inlineDescriptor =
        findEditorModeDescriptor(EditorModeId::InlineCandidates);
    const EditorModeDescriptor* slotDescriptor =
        findEditorModeDescriptor(EditorModeId::TemplateSlots);
    const EditorModeDescriptor* signalDescriptor =
        findEditorModeDescriptor(EditorModeId::SignalSelection);
    const EditorModeDescriptor* commandDescriptor =
        findEditorModeDescriptor(EditorModeId::CommandMode);
    const EditorModeDescriptor* keywordDescriptor =
        findEditorModeDescriptor(EditorModeId::KeywordGhost);
    const EditorModeDescriptor* multiCursorDescriptor =
        findEditorModeDescriptor(EditorModeId::MultiCursor);
    expect("input ownership is declared instead of event-order implicit",
           inlineDescriptor
               && inlineDescriptor->inputs.testFlag(
                      EditorModeInput::Text)
               && inlineDescriptor->inputs.testFlag(
                      EditorModeInput::Backspace)
               && inlineDescriptor->inputs.testFlag(
                      EditorModeInput::Tab)
               && inlineDescriptor->inputs.testFlag(
                      EditorModeInput::Enter)
               && slotDescriptor
               && slotDescriptor->inputs.testFlag(
                      EditorModeInput::Tab)
               && slotDescriptor->inputs.testFlag(
                      EditorModeInput::Backtab)
               && signalDescriptor
               && signalDescriptor->inputs.testFlag(
                      EditorModeInput::ContextMenu)
               && commandDescriptor
               && commandDescriptor->inputs.testFlag(
                      EditorModeInput::Modifier)
               && commandDescriptor->inputs.testFlag(
                      EditorModeInput::Navigation)
               && keywordDescriptor
               && keywordDescriptor->inputs.testFlag(
                      EditorModeInput::Tab)
               && !keywordDescriptor->inputs.testFlag(
                      EditorModeInput::Backtab)
               && multiCursorDescriptor
               && multiCursorDescriptor->inputs.testFlag(
                      EditorModeInput::Clipboard)
               && multiCursorDescriptor->inputs.testFlag(
                      EditorModeInput::Mouse));

    EditorModeController coexistenceController;
    coexistenceController.enter(EditorModeId::VirtualCursor,
                                EditorModeEntryReason::MouseGesture);
    coexistenceController.enter(EditorModeId::MultiCursor,
                                EditorModeEntryReason::KeyboardGesture);
    expect("multi-cursor and per-caret virtual columns coexist",
           coexistenceController.isActive(EditorModeId::VirtualCursor)
               && coexistenceController.isActive(
                      EditorModeId::MultiCursor)
               && coexistenceController.highestPriorityForInput(
                      EditorModeInput::Text)
                      == EditorModeId::MultiCursor
               && coexistenceController.escapeTarget()
                      == EditorModeId::MultiCursor);
    coexistenceController.enter(EditorModeId::ColumnSelection,
                                EditorModeEntryReason::MouseGesture);
    expect("rectangular selection replaces independent multi-cursor state",
           coexistenceController.isActive(
               EditorModeId::ColumnSelection)
               && !coexistenceController.isActive(
                      EditorModeId::MultiCursor)
               && !coexistenceController.isActive(
                      EditorModeId::VirtualCursor));

    controller.enter(EditorModeId::TemplateSlots,
                     EditorModeEntryReason::TemplateInserted);
    expect("highest-priority mode is the unified Escape target",
           controller.escapeTarget()
               == controller.primaryMode()
               && controller.ownsInput(
                      controller.escapeTarget(),
                      EditorModeInput::Escape));

    controller.enter(EditorModeId::SignalSelection,
                     EditorModeEntryReason::UserAction);
    const int callbackCountBeforeExitAll = exitedModes.size();
    controller.exitAll(EditorModeExitReason::ExternalControl);
    expect("external-control exit clears state with explicit reason",
           !controller.hasActiveMode()
               && controller.snapshot().primaryMode
                      == EditorModeId::None
               && exitedModes.size() == callbackCountBeforeExitAll + 1
               && exitedModes.last()
                      == EditorModeId::SignalSelection
               && exitReasons.last()
                      == EditorModeExitReason::ExternalControl);

    expect("unknown mode is rejected without mutating state",
           !controller.enter(EditorModeId::None,
                             EditorModeEntryReason::UserAction)
               && !controller.hasActiveMode());

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
