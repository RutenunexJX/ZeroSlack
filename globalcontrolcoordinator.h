#ifndef GLOBALCONTROLCOORDINATOR_H
#define GLOBALCONTROLCOORDINATOR_H

#include "globalcontrolservice.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QPoint>
#include <functional>
#include <memory>

class GlobalControlPanel;
class QWidget;

class ZEROSLACK_API GlobalControlCoordinator : public QObject
{
public:
    explicit GlobalControlCoordinator(QWidget* anchor,
                                      QObject* parent = nullptr);
    ~GlobalControlCoordinator() override;

    void setActionHandler(std::function<void(const GlobalControlItem&)> handler);
    void setOpeningHandler(std::function<void()> handler);
    void setContextProvider(
        std::function<GlobalControlQueryContext()> provider);
    void setItemProvider(
        std::function<QList<GlobalControlItem>(
            GlobalControlCategory,
            const QString&,
            const GlobalControlQueryContext&)> provider);
    void setOpenRequestHandler(
        std::function<bool()> handler);
    void setAnchorPositionProvider(
        std::function<QPoint()> provider);
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
    std::function<GlobalControlQueryContext()> contextProvider;
    std::function<QList<GlobalControlItem>(
        GlobalControlCategory,
        const QString&,
        const GlobalControlQueryContext&)> itemProvider;
    std::function<bool()> openRequestHandler;
    std::function<QPoint()> anchorPositionProvider;
    bool installed = false;
    GlobalControlQueryContext currentContext;

    void refresh(const QString& queryText = QString());
    void dispatch(const GlobalControlItem& item);
};

#endif // GLOBALCONTROLCOORDINATOR_H
