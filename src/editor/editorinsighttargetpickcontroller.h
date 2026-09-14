#ifndef EDITORINSIGHTTARGETPICKCONTROLLER_H
#define EDITORINSIGHTTARGETPICKCONTROLLER_H

#include "editormodecontroller.h"

#include <QList>
#include <QMetaObject>
#include <QString>
#include <QtGlobal>

#include <functional>

class EditorModeController;
class AnnotationLayer;
class MyCodeEditor;
class TSDocument;
class QKeyEvent;
class QMouseEvent;
class QTimer;

// What a requesting insight can render. The editor never learns about
// LiveInsightKind: the caller maps its own kind onto one of these classes.
enum class EditorInsightTargetClass {
    Signal,  // signal and port identifiers
    Module,  // module declarations and the type names of instances
    Scope    // always / module scope heads
};

struct EditorInsightTargetCandidate {
    QString name;
    QString scopeLabel;
    int startChar = -1;
    int endChar = -1;
    int startLine = 0;
    int scopeStartChar = -1;
    int scopeEndChar = -1;

    bool isValid() const
    {
        return endChar > startChar && startChar >= 0 && !name.isEmpty();
    }
};

// Editor mode that blinks every target the requesting insight could render in
// the visible region and lets the user pick one. Candidates are enumerated
// from the visible lines only, and the expensive check runs once, on the
// candidate the user actually chose.
class EditorInsightTargetPickController
{
public:
    // Names that may blink for a given class, answered from one snapshot query
    // per publish instead of a service call per identifier.
    using NameSetResolver =
        std::function<QList<QString>(EditorInsightTargetClass)>;
    // Second-stage check on the chosen candidate. Returning false keeps the
    // mode running and shows the reason; it must never be silently dropped.
    using Validator = std::function<bool(
        const EditorInsightTargetCandidate&, QString* reason)>;
    using PickedHandler =
        std::function<void(const EditorInsightTargetCandidate&)>;
    using SyntaxSource = std::function<const TSDocument*()>;

    void bind(EditorModeController* modes,
              AnnotationLayer* annotations,
              MyCodeEditor* editor,
              SyntaxSource syntax);
    void shutdown(MyCodeEditor* editor);

    void setNameSetResolver(NameSetResolver resolver);

    bool start(MyCodeEditor* editor,
               EditorInsightTargetClass targetClass,
               Validator validator,
               PickedHandler handler,
               QString* message = nullptr);
    void clear(MyCodeEditor* editor,
               const QString& message = QString(),
               EditorModeExitReason reason =
                   EditorModeExitReason::Canceled);

    bool active() const;
    bool blinkOn() const;
    EditorInsightTargetClass targetClass() const;
    QList<EditorInsightTargetCandidate> candidates() const;
    int activeIndex() const;
    QPair<int, int> lastEnumeratedLineRangeForTest() const;

    bool handleKeyPress(MyCodeEditor* editor, QKeyEvent* event);
    bool handleMousePress(MyCodeEditor* editor, QMouseEvent* event);
    void publishVisibleAnnotations(MyCodeEditor* editor,
                                   int firstVisibleLine = -1,
                                   int lastVisibleLine = -1);

private:
    EditorModeController* modeController = nullptr;
    AnnotationLayer* annotationLayer = nullptr;
    SyntaxSource syntaxSource;
    NameSetResolver nameSetResolver;
    Validator validatorValue;
    PickedHandler pickedHandler;
    EditorInsightTargetClass classValue = EditorInsightTargetClass::Signal;
    QList<EditorInsightTargetCandidate> candidateList;
    QString activeKey;
    QTimer* blinkTimer = nullptr;
    QMetaObject::Connection blinkConnection;
    bool currentBlinkOn = true;
    bool clearing = false;
    quint64 presentationGeneration = 0;
    int lastFirstLine = -1;
    int lastLastLine = -1;

    void refreshCandidates(MyCodeEditor* editor,
                           int firstVisibleLine,
                           int lastVisibleLine);
    void select(MyCodeEditor* editor, int index);
    void step(MyCodeEditor* editor, int delta);
    bool commit(MyCodeEditor* editor, int index);
    void refreshPresentation(MyCodeEditor* editor);
    void updateModePresentation();
    void ensureBlinkTimer(MyCodeEditor* editor);
    void stopBlinkTimer();
    int indexForKey(const QString& key) const;
    static QString candidateKey(const EditorInsightTargetCandidate& candidate);
};

#endif // EDITORINSIGHTTARGETPICKCONTROLLER_H
