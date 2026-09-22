#pragma once

#include <QWidget>
#include <QStringList>
#include <functional>

class QAction;

class ModuleBlockDiagramToolbar final : public QWidget
{
public:
    explicit ModuleBlockDiagramToolbar(QWidget* parent = nullptr);
    void setBreadcrumbs(const QStringList& labels);
    void setHistoryAvailable(bool back, bool forward);
    void setHosted(bool hosted);

    QAction* backAction = nullptr;
    QAction* fitAction = nullptr;
    QAction* forwardAction = nullptr;
    std::function<void(int)> breadcrumbActivated;

private:
    QWidget* breadcrumbs = nullptr;
    QWidget* fitButton = nullptr;
};
