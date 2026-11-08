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
    NativeContextView *view(QWidget *parent = nullptr);
    void manage();
    bool canClose(QString *error) const;
    SimulationCommandRequest currentRequest() const;
    static QString materializeSupport(const QString &workspace, const QString &kind, QStringList *files);
    std::function<void()> showBrowser;
  signals:
    void message(const QString &text);

  private:
    QPointer<TabManager> tabs;
    QPointer<WorkspaceManager> workspaces;
    QPointer<simdock::Workbench> buildInputs;
    QPointer<QWidget> owner;
    QPointer<NativeContextView> nativeView;
};
