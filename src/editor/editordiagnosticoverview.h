#pragma once
#include <QWidget>
#include <QPointer>
#include <QList>
class MyCodeEditor;
class MyCodeEditorState;
class QScrollBar;
class EditorDiagnosticOverview final : public QWidget {
public:
    EditorDiagnosticOverview(MyCodeEditor*,MyCodeEditorState*);
    ~EditorDiagnosticOverview() override;
    void schedule();
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void paintEvent(QPaintEvent*) override;
private:
    QPointer<MyCodeEditor> editor;
    QPointer<QScrollBar> bar;
    MyCodeEditorState* state;
    struct Mark {QRect rect;QList<int> diagnostics;bool error=false;};
    QList<Mark> marks;
    bool pending=false,consumeRelease=false,forcingBar=false;
    Qt::ScrollBarPolicy previousPolicy;
    void rebuild();
    QList<int> hit(const QPoint&) const;
    void activate(int);
    QRect track(bool slider=false) const;
};
