#ifndef INSIGHTFOCUSCONTROLLER_H
#define INSIGHTFOCUSCONTROLLER_H

#include <QObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>

#include <functional>

class QDockWidget;
class QLabel;
class QLineEdit;
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

class InsightFocusController : public QObject
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

    bool isFocused() const;
    QString focusedPanelId() const;
    QWidget* focusedPanelWidget() const;
    QWidget* focusPage() const;

private:
    struct PanelEntry {
        InsightFocusPanelRegistration registration;
        QPointer<QWidget> panelWidget;
        QPointer<QPushButton> enterButton;
    };

    struct DockVisibility {
        QPointer<QDockWidget> dock;
        bool visible = false;
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
    QVBoxLayout* contentLayout = nullptr;
    QHash<QString, PanelEntry> panels;
    QList<DockVisibility> savedDockVisibility;
    QString activePanelId;
    bool syncingSearch = false;

    PanelEntry* activeEntry();
    const PanelEntry* activeEntry() const;
    void captureAndHideDocks();
    void restoreDockVisibility(QDockWidget* activeDock,
                               bool showActiveDock);
    void restoreActivePanel(bool showDock,
                            bool restoreDocks = true);
    void updateToolbar(const PanelEntry& entry);
};

#endif // INSIGHTFOCUSCONTROLLER_H
