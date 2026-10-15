#pragma once
#include "duttemplateplanner.h"
#include <QObject>
#include <QPointer>
#include <functional>
class GlobalControlPanel;
class MyCodeEditor;
class DutTemplateSession final : public QObject {
public:
    DutTemplateSession(MyCodeEditor *editor, SimulationCommandRequest request,
        std::function<QString()> current, std::function<void(const QString&)> message, QObject *parent);
    ~DutTemplateSession() override;
    void start();
protected:
    bool eventFilter(QObject *, QEvent *) override;
private:
    enum class Stage { Action, Module, Instance, Parameter, Binding, Confirm };
    QPointer<MyCodeEditor> editor;
    GlobalControlPanel *panel;
    SimulationCommandRequest request;
    QList<TemplateInstance> instances;
    DutTemplateOptions options;
    DutTemplateAnalysis analysis;
    Stage stage = Stage::Module;
    int field = 0;
    bool busy = false, done = false;
    std::function<QString()> current;
    std::function<void(const QString&)> message;
    void load(std::function<void()> next);
    void show(Stage next, const QString &value = {});
    void refresh(const QString &query);
    void accept(const GlobalControlItem &item);
    void bindings();
    void finish(bool remove = false);
    void close(const QString &reason = {});
};
