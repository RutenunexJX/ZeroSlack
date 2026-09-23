#pragma once

#include <QWidget>

class QAction;

class ModuleBlockDiagramToolbar final : public QWidget
{
public:
    explicit ModuleBlockDiagramToolbar(QWidget* parent = nullptr);
    void setModuleMode(bool active);
    void setHosted(bool hosted);

    QAction* fitAction = nullptr;

private:
    bool moduleMode = false;
    bool hosted = false;
};
