#ifndef LIVEINSIGHTTOOLPAGE_H
#define LIVEINSIGHTTOOLPAGE_H

#include "liveinsighttypes.h"

#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>

class RtlInsightWorkbench;
class QMainWindow;
class QHideEvent;
class QShowEvent;

struct LiveInsightToolContext {
    QString workspaceRoot;
    QString workspaceId;
    QString documentId;
    quint64 documentRevision = 0;
    quint64 semanticRevision = 0;
    QString fileName;
    QString documentText;
    QString moduleName;
    QString signalName;
    QString signalAccessPath;
    QString scopeLabel;
    bool dirty = false;
    int scopeStartPosition = -1;
    int scopeEndPosition = -1;
    int scopeStartLineZeroBased = 0;
};

class LiveInsightToolPage final : public QWidget
{
public:
    using NavigationHandler =
        std::function<bool(const QString&, int, int)>;
    using StatusHandler =
        std::function<void(const QString&, int)>;
    using VisibilityHandler = std::function<void(bool)>;

    explicit LiveInsightToolPage(
        LiveInsightKind kind,
        QWidget* parent = nullptr);
    ~LiveInsightToolPage() override;

    LiveInsightKind kind() const;
    void setNavigationHandler(NavigationHandler handler);
    void setStatusHandler(StatusHandler handler);
    void setVisibilityHandler(VisibilityHandler handler);
    // Drops the chrome a titled host already provides (workbench title line,
    // detach entry).
    void setCompactChrome(bool compact);
    void fitGraph();
    void setContext(const LiveInsightToolContext& context);
    void refreshTargetContext(const LiveInsightToolContext& editorContext);
    bool hasVisibleSurface() const;
    // The context this page last rendered from.
    const LiveInsightToolContext& contextForTest() const
    {
        return currentContext;
    }

    RtlInsightWorkbench* workbenchForTest() const;
    QMainWindow* detachToWindow();
    QMainWindow* detachedWindowForTest() const;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    LiveInsightKind insightKind;
    LiveInsightToolContext currentContext;
    NavigationHandler navigationHandler;
    StatusHandler statusHandler;
    VisibilityHandler visibilityHandler;
    RtlInsightWorkbench* workbench = nullptr;
    QPointer<QMainWindow> detachedWindow;
    QPointer<LiveInsightToolPage> detachedPage;

    void createSurface();
    void renderContext();
    void notifyVisibility();
};

#endif // LIVEINSIGHTTOOLPAGE_H
