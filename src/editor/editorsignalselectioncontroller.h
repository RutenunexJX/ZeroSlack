#ifndef EDITORSIGNALSELECTIONCONTROLLER_H
#define EDITORSIGNALSELECTIONCONTROLLER_H

#include <QHash>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

class EditorModeController;
class EditorSelection;
class MyCodeEditor;
class QMouseEvent;

struct EditorSignalSelectionCandidate
{
    QString identity;
    QString name;
    int start = -1;
    int length = 0;

    bool isValid() const
    {
        return !identity.isEmpty()
            && !name.isEmpty()
            && start >= 0
            && length > 0;
    }
};

class EditorSignalSelectionController
{
public:
    using CandidateResolver =
        std::function<std::optional<
            EditorSignalSelectionCandidate>(int)>;

    void bind(EditorModeController* modes,
              EditorSelection* selections,
              MyCodeEditor* editor,
              CandidateResolver resolver);
    void shutdown(MyCodeEditor* editor);

    bool start(MyCodeEditor* editor,
               QString* message = nullptr);
    void cancel(MyCodeEditor* editor);
    bool active() const;
    bool hasSelection() const;
    QStringList selectedNames() const;

    bool toggleAt(MyCodeEditor* editor,
                  int cursorPosition,
                  bool toggle,
                  bool desiredState = true);
    void completeForContextMenu();

    bool handleMousePress(MyCodeEditor* editor,
                          QMouseEvent* event);
    bool handleMouseMove(MyCodeEditor* editor,
                         QMouseEvent* event);
    bool handleMouseRelease(MyCodeEditor* editor,
                            QMouseEvent* event);

private:
    EditorModeController* modeController = nullptr;
    EditorSelection* selectionPresenter = nullptr;
    CandidateResolver candidateResolver;
    QHash<QString, EditorSignalSelectionCandidate>
        selected;
    bool dragging = false;
    bool dragSelect = false;
    bool resultPending = false;
    QString lastDragIdentity;
    QPoint lastDragPoint{-1, -1};

    void resetGesture();
    void clearSelection(MyCodeEditor* editor);
    void refreshOverlay(MyCodeEditor* editor);
};

#endif // EDITORSIGNALSELECTIONCONTROLLER_H
