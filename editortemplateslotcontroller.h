#ifndef EDITORTEMPLATESLOTCONTROLLER_H
#define EDITORTEMPLATESLOTCONTROLLER_H

#include "completiontypes.h"

#include <QList>
#include <QString>

class EditorModeController;
class EditorSelection;
class MyCodeEditor;
class QKeyEvent;
class QTimer;

class EditorTemplateSlotController
{
public:
    void bind(EditorModeController* modes,
              EditorSelection* selections,
              MyCodeEditor* editor);
    void shutdown(MyCodeEditor* editor);

    void start(MyCodeEditor* editor,
               int insertionStart,
               int insertedLength,
               const CodeTemplateSlotList& slotMetadata);
    void clear(MyCodeEditor* editor,
               const QString& message = QString(),
               bool updatePresentation = true);

    bool active() const;
    int activeIndex() const;
    int slotCount() const;
    bool blinkOn() const;
    bool containsPosition(int position) const;

    bool handleKeyPress(MyCodeEditor* editor,
                        QKeyEvent* event);
    void handleContentsChange(MyCodeEditor* editor,
                              int position,
                              int charsRemoved,
                              int charsAdded,
                              bool updatePresentation = true);
    void handleCursorChanged(MyCodeEditor* editor);

    void markPresentationPending();
    void flushPendingPresentation(MyCodeEditor* editor);

private:
    struct SlotRange {
        QString name;
        int start = -1;
        int end = -1;
    };

    EditorModeController* modeController = nullptr;
    EditorSelection* selectionPresenter = nullptr;
    QList<SlotRange> ranges;
    int currentIndex = -1;
    int sessionStart = -1;
    int sessionEnd = -1;
    QTimer* blinkTimer = nullptr;
    bool currentBlinkOn = true;
    bool ignoreNextCursorCheck = false;
    bool presentationPending = false;

    QList<QPair<int, int>> highlightRanges() const;
    int indexForCursor(MyCodeEditor* editor) const;
    bool cursorInsideActiveRange(
        MyCodeEditor* editor) const;
    void refreshHighlights(MyCodeEditor* editor);
    void stopBlinkTimer();
    void ensureBlinkTimer(MyCodeEditor* editor);
    void select(MyCodeEditor* editor, int index);
};

#endif // EDITORTEMPLATESLOTCONTROLLER_H
