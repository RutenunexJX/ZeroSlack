#pragma once
#include "simulationcommandplanner.h"
#include "zeroslackexport.h"
#include <QObject>
#include <QPointer>
#include <optional>

class TabManager;
class WorkspaceManager;
class MyCodeEditor;
class NativeContextView;
class QWidget;
namespace simdock
{
class Workbench;
}

class ZEROSLACK_API SimulationCommandService final : public QObject
{
    Q_OBJECT
  public:
    SimulationCommandService(TabManager *tabs, WorkspaceManager *workspaces, simdock::Workbench *buildInputs,
                             QWidget *parent);
    ~SimulationCommandService() override;
    NativeContextView *view(QWidget *parent = nullptr);
    void prepare();
    void activate(const GlobalControlItem &item);
    void manage();
    bool canClose(QString *error) const;
    SimulationCommandRequest currentRequest() const;
    static QString materializeSupport(const QString &workspace, const QString &kind, QStringList *files);
    std::function<void()> showBrowser;
  signals:
    void catalogChanged();
    void message(const QString &text);

  private:
    void cancel();
    bool current(const GlobalControlItem &item, MyCodeEditor *editor) const;
    QPointer<TabManager> tabs;
    QPointer<WorkspaceManager> workspaces;
    QPointer<simdock::Workbench> buildInputs;
    QPointer<QWidget> owner;
    QPointer<NativeContextView> nativeView;
    std::shared_ptr<std::atomic_bool> pending;
};
