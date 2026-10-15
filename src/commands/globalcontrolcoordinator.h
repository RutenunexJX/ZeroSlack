#ifndef GLOBALCONTROLCOORDINATOR_H
#define GLOBALCONTROLCOORDINATOR_H

#include "globalcontrolservice.h"
#include "zeroslackexport.h"

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <functional>
#include <memory>

class GlobalControlPanel;
class QWidget;
class MyCodeEditor;

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
            const GlobalControlQueryContext&, QString*)> provider);
    void setOpenRequestHandler(
        std::function<bool()> handler);
    void setAnchorPositionProvider(
        std::function<QPoint()> provider);
    void install();
    bool handleKeyEvent(QEvent* event);
    void open();
    void openTemplates(MyCodeEditor* editor, const QString& query, bool rawPrefix = false);
    void refreshVisibleItems();

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
        const GlobalControlQueryContext&, QString*)> itemProvider;
    std::function<bool()> openRequestHandler;
    std::function<QPoint()> anchorPositionProvider;
    bool installed = false;
    GlobalControlQueryContext currentContext;
    QPointer<MyCodeEditor> templateEditor;
    bool f24Held = false, nativePrefixPending = false;
    bool toggleSpaceHeld = false;
    void setF24Held(bool held, MyCodeEditor* editor = nullptr);
    bool handleTemplateEvent(QObject* watched, QEvent* event);
    bool restoreNativePrefix(const QString& query);

    void refresh(const QString& queryText = QString());
    void dispatch(const GlobalControlItem& item);
};

#endif // GLOBALCONTROLCOORDINATOR_H
