#pragma once
#include "moduletemplateplan.h"
#include <QFrame>
#include <QFutureWatcher>
#include <QPointer>
#include <QTextCursor>

class MyCodeEditor;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QPlainTextEdit;
class QLabel;
class QCheckBox;
class QComboBox;
class QPushButton;
class QTimer;

class ModuleTemplatePopup final : public QFrame {
public:
    ModuleTemplatePopup(MyCodeEditor* editor, const QVariantMap& configuration);
    ~ModuleTemplatePopup() override;
    void openAtCursor();
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    struct Job { ModuleTemplateModel model; ModuleTemplatePlan plan; bool inspection = false; };
    QPointer<MyCodeEditor> editor;
    QPointer<QTextDocument> document;
    QTextCursor cursor;
    SimulationCommandRequest source;
    QString mode;
    quint64 revision = 0, catalogRevision = 0, generation = 0, runningGeneration = 0;
    bool closing = false, filling = false, modelReady = false;
    QList<SemanticSymbolRecord> modules;
    ModuleTemplateModel model;
    ModuleTemplatePlan plan;
    QLineEdit *query, *prefix, *suffix;
    QListWidget* list;
    QTableWidget* parameters;
    QPlainTextEdit* preview;
    QLabel* feedback;
    QCheckBox* includeParameters;
    QComboBox* keyword;
    QPushButton* insert;
    QTimer *debounce, *contextTimer;
    QFutureWatcher<Job>* watcher;
    std::shared_ptr<std::atomic_bool> cancelled;
    SimulationCommandRequest capture() const;
    bool current() const;
    ModuleTemplateOptions options() const;
    void filter();
    void selectModule();
    void invalidate();
    void startJob();
    void showParameters();
    void apply();
    void dismiss();
};
