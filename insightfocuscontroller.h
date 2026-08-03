#ifndef INSIGHTFOCUSCONTROLLER_H
#define INSIGHTFOCUSCONTROLLER_H

#include "actionregistry.h"

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSizePolicy>
#include <QString>

#include <functional>

class QAction;
class QDockWidget;
class QLabel;
class QLineEdit;
class QMainWindow;
class QPushButton;
class QShortcut;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

struct InsightFocusPanelRegistration {
    QString id;
    QString title;
    QDockWidget* dock = nullptr;
    std::function<void()> fit;
    std::function<void()> zoomIn;
    std::function<void()> zoomOut;
    std::function<void(const QString&)> setSearchText;
    std::function<QString()> searchText;
    std::function<void()> showInspector;
};

class InsightFocusController : public QObject,
                               public ActionExecutionHost
{
public:
    InsightFocusController(QStackedWidget* centralStack,
                           QWidget* editorPage,
                           QObject* parent = nullptr);
    ~InsightFocusController() override;

    bool registerPanel(
        const InsightFocusPanelRegistration& registration);
    bool enter(const QString& panelId);
    void leaveToEditor();
    void returnToDock();
    void setBeforeEnterHandler(std::function<void()> handler);

    bool isFocused() const;
    QString focusedPanelId() const;
    QWidget* focusedPanelWidget() const;
    QWidget* focusPage() const;

private:
    struct PanelEntry {
        InsightFocusPanelRegistration registration;
        QPointer<QDockWidget> dock;
        QPointer<QWidget> panelWidget;
        QPointer<QPushButton> enterButton;
        int savedMinimumHeight = 0;
        int savedMaximumHeight = 0;
        QSizePolicy savedSizePolicy;
        bool constraintsSaved = false;
    };

    struct DockVisibility {
        QPointer<QDockWidget> dock;
        bool visible = false;
    };

    enum class ActiveDockRestore {
        PriorVisibility,
        Hidden,
        Visible
    };

    QPointer<QStackedWidget> stack;
    QPointer<QWidget> editor;
    QPointer<QWidget> page;
    QPointer<QWidget> contentHost;
    QPointer<QLabel> titleLabel;
    QPointer<QLineEdit> searchEdit;
    QPointer<QPushButton> fitButton;
    QPointer<QPushButton> zoomOutButton;
    QPointer<QPushButton> zoomInButton;
    QPointer<QPushButton> inspectorButton;
    QPointer<QAction> fitAction;
    QPointer<QAction> zoomOutAction;
    QPointer<QAction> zoomInAction;
    QVBoxLayout* contentLayout = nullptr;
    QHash<QString, PanelEntry> panels;
    QList<DockVisibility> savedDockVisibility;
    QPointer<QMainWindow> savedMainWindow;
    QByteArray savedMainWindowState;
    QString activePanelId;
    bool syncingSearch = false;
    std::function<void()> beforeEnterHandler;

    PanelEntry* activeEntry();
    const PanelEntry* activeEntry() const;
    void captureAndHideDocks();
    void restoreDockVisibility(
        QDockWidget* activeDock,
        ActiveDockRestore activeDockRestore);
    void restoreActivePanel(ActiveDockRestore activeDockRestore,
                            bool restoreDocks = true);
    static void expandPanelForFocus(PanelEntry& entry,
                                    QWidget* panel);
    static void restorePanelConstraints(PanelEntry& entry,
                                        QWidget* panel);
    void updateToolbar(const PanelEntry& entry);
    QAction* createGraphViewAction(
        const QString& actionId);
    void bindGraphViewButton(
        QPushButton* button,
        QAction* action);
    QAction* graphViewAction(
        const QString& actionId) const;
    void refreshGraphViewActionAvailability(
        const PanelEntry* entry);
    ActionExecutionResult requestGraphViewAction(
        const QString& actionId);
    ActionExecutionResult executeActionRoute(
        const ActionDescriptor& descriptor,
        const ActionInvocation& invocation) override;
};

#endif // INSIGHTFOCUSCONTROLLER_H
