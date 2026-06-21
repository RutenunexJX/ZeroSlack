#ifndef GLOBALCONTROLCOORDINATOR_H
#define GLOBALCONTROLCOORDINATOR_H

#include "globalcontrolservice.h"

#include <QObject>
#include <functional>
#include <memory>

class GlobalControlPanel;
class ProjectModel;
class SemanticIndex;
class QWidget;

class GlobalControlCoordinator : public QObject
{
public:
    explicit GlobalControlCoordinator(QWidget* anchor,
                                      QObject* parent = nullptr);
    ~GlobalControlCoordinator() override;

    void setProjectModel(ProjectModel* projectModel);
    void setSemanticIndex(SemanticIndex* semanticIndex);
    void setActionHandler(std::function<void(const GlobalControlItem&)> handler);
    void install();
    bool handleKeyEvent(QEvent* event);
    void open();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* anchor = nullptr;
    ProjectModel* projectModel = nullptr;
    SemanticIndex* semanticIndex = nullptr;
    std::unique_ptr<GlobalControlPanel> panel;
    GlobalControlService service;
    std::function<void(const GlobalControlItem&)> actionHandler;
    bool installed = false;
    bool shiftPressed = false;
    bool standaloneShift = false;
    qint64 lastShiftReleaseMs = -1;
    const QEvent* lastProcessedEvent = nullptr;

    void installOnWidgetTree(QWidget* widget);
    void refresh(const QString& queryText = QString());
    void dispatch(const GlobalControlItem& item);
};

#endif // GLOBALCONTROLCOORDINATOR_H
