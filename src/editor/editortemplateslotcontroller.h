#ifndef EDITORTEMPLATESLOTCONTROLLER_H
#define EDITORTEMPLATESLOTCONTROLLER_H

#include "completiontypes.h"

#include <QList>
#include <QMetaObject>
#include <QString>
#include <QtGlobal>

class EditorModeController;
class AnnotationLayer;
class MyCodeEditor;
class QKeyEvent;
class QTimer;

class EditorTemplateSlotController
{
public:
    void bind(EditorModeController* modes,
              AnnotationLayer* annotations,
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
    void publishVisibleAnnotations(MyCodeEditor* editor,
                                   int firstVisibleLine = -1,
                                   int lastVisibleLine = -1);

private:
    struct SlotRange {
        QString name;
        int start = -1;
        int end = -1;
        int tabStop = -1;
        int groupIndex = -1;
        bool visibleWhenEmpty = false;
    };

    EditorModeController* modeController = nullptr;
    AnnotationLayer* annotationLayer = nullptr;
    QList<SlotRange> ranges;
    int currentIndex = -1;
    int sessionStart = -1;
    int sessionEnd = -1;
    QTimer* blinkTimer = nullptr;
    QMetaObject::Connection blinkConnection;
    bool currentBlinkOn = true;
    bool ignoreNextCursorCheck = false;
    bool presentationPending = false;
    bool applyingLinkedEdit = false;
    quint64 presentationGeneration = 0;

    int physicalIndexForCursor(MyCodeEditor* editor) const;
    int physicalIndexForChange(int position, int removedLength) const;
    bool cursorInsideActiveRange(
        MyCodeEditor* editor) const;
    int groupCount() const;
    int firstPhysicalIndexForGroup(int groupIndex) const;
    void applyRangeChange(int physicalIndex,
                          int changeStart,
                          int changeEnd,
                          int delta);
    void mirrorLinkedEdit(MyCodeEditor* editor,
                          int sourcePhysicalIndex,
                          int relativeStart,
                          int relativeEnd,
                          const QString& insertedText);
    void refreshPresentation(MyCodeEditor* editor);
    void stopBlinkTimer();
    void ensureBlinkTimer(MyCodeEditor* editor);
    void select(MyCodeEditor* editor, int index);
};

#endif // EDITORTEMPLATESLOTCONTROLLER_H
