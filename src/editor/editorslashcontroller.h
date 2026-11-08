#pragma once
#include "ordinarytemplateplan.h"
#include <QObject>
#include <QPointer>
#include <QTextCursor>

class MyCodeEditor;
class EditorModeController;
class QFrame;
class QLineEdit;
class QListWidget;
class QLabel;
class QKeyEvent;

class EditorSlashController final : public QObject {
public:
    EditorSlashController(MyCodeEditor* editor, EditorModeController* modes);
    ~EditorSlashController() override;
    bool trigger(QKeyEvent* event);
    void cancel(bool restoreFocus = false);
    bool active() const;
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    MyCodeEditor* editor;
    EditorModeController* modes;
    QFrame* popup = nullptr;
    QLineEdit* query = nullptr;
    QListWidget* list = nullptr;
    QLabel* feedback = nullptr;
    QPointer<QTextDocument> source;
    QTextCursor original;
    QString file;
    quint64 revision = 0;
    quint64 catalogRevision = 0;
    bool searching = false;
    bool preedit = false;
    bool compositionCommit = false;
    QList<UserTemplateRecord> snapshot;
    QList<int> matches;
    bool current() const;
    void ensurePopup();
    void filter();
    void confirm();
};
