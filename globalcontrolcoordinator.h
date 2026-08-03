#ifndef GLOBALCONTROLCOORDINATOR_H
#define GLOBALCONTROLCOORDINATOR_H

#include "globalcontrolservice.h"

#include <QObject>
#include <functional>
#include <memory>

class GlobalControlPanel;
class QWidget;

class GlobalControlCoordinator : public QObject
{
public:
    explicit GlobalControlCoordinator(QWidget* anchor,
                                      QObject* parent = nullptr);
    ~GlobalControlCoordinator() override;

    void setActionHandler(std::function<void(const GlobalControlItem&)> handler);
    void setOpeningHandler(std::function<void()> handler);
    void setOpenRequestHandler(
        std::function<bool()> handler);
    void install();
    bool handleKeyEvent(QEvent* event);
    void open();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* anchor = nullptr;
    std::unique_ptr<GlobalControlPanel> panel;
    GlobalControlService service;
    std::function<void(const GlobalControlItem&)> actionHandler;
    std::function<void()> openingHandler;
    std::function<bool()> openRequestHandler;
    bool installed = false;

    void refresh(const QString& queryText = QString());
    void dispatch(const GlobalControlItem& item);
};

#endif // GLOBALCONTROLCOORDINATOR_H
