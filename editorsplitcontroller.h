#ifndef EDITORSPLITCONTROLLER_H
#define EDITORSPLITCONTROLLER_H

#include <QHash>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QString>
#include <QVariant>

class QMouseEvent;
class QSplitter;
class QTabBar;
class QTabWidget;
class QWidget;

enum class EditorSplitDirection {
    Left,
    Right,
    Above,
    Below,
    Center
};

struct EditorTabContextAction {
    QString actionId;
    QString label;
    QString executionRoute;
    bool enabled = false;
    bool separatorBefore = false;
};

class EditorSplitController : public QObject
{
    Q_OBJECT

public:
    explicit EditorSplitController(QTabWidget* initialGroup,
                                   QObject* parent = nullptr);

    void setHost(QWidget* host);
    QWidget* host() const;
    QTabWidget* initialGroup() const;
    QTabWidget* activeGroup() const;
    void setActiveGroup(QTabWidget* group);
    QList<QTabWidget*> groups() const;
    int groupCount() const;
    QTabWidget* groupForPage(QWidget* page) const;

    QTabWidget* createSplit(QTabWidget* source,
                            EditorSplitDirection direction);
    bool movePage(QWidget* page,
                  QTabWidget* destination,
                  int destinationIndex = -1);
    QTabWidget* movePageToSplit(
        QWidget* page,
        EditorSplitDirection direction,
        QTabWidget* relativeTo = nullptr);
    bool mergeGroup(QTabWidget* source,
                    QTabWidget* destination = nullptr);
    void removeEmptyGroups();

    bool isGroupMaximized() const;
    void toggleActiveGroupMaximized();
    void restoreGroupLayout();
    void equalizeSplitSizes();

    bool handleDropForTest(
        const QString& viewId,
        QTabWidget* target,
        EditorSplitDirection direction);
    QList<EditorTabContextAction> tabContextActions(
        QTabWidget* group,
        int index) const;

signals:
    void groupCreated(QTabWidget* group);
    void groupRemoved();
    void activeGroupChanged(QTabWidget* group);
    void tabCloseRequested(QTabWidget* group, int index);
    void tabActionRequested(const QString& actionId,
                            QTabWidget* group,
                            int index);
    void pageMoved(QWidget* page,
                   QTabWidget* source,
                   QTabWidget* destination);
    void layoutChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct PagePresentation {
        QString text;
        QIcon icon;
        QString toolTip;
        QVariant data;
    };

    QPointer<QWidget> splitHost;
    QPointer<QWidget> rootWidget;
    QPointer<QTabWidget> firstGroup;
    QPointer<QTabWidget> currentGroup;
    QList<QPointer<QTabWidget>> tabGroups;
    QPointer<QTabBar> pressedBar;
    QPoint dragStartPosition;
    int pressedTabIndex = -1;
    QHash<QSplitter*, QList<int>> savedSplitterSizes;
    QHash<QTabWidget*, bool> savedGroupVisibility;
    bool groupMaximized = false;

    QTabWidget* createGroup();
    void configureGroup(QTabWidget* group);
    void bindDropTarget(QWidget* target);
    QTabWidget* groupForObject(QObject* object) const;
    QWidget* pageForViewId(const QString& viewId) const;
    QString viewIdForPage(QWidget* page) const;
    PagePresentation takePagePresentation(
        QTabWidget* group,
        int index,
        QWidget** page) const;
    void insertPage(QTabWidget* group,
                    QWidget* page,
                    const PagePresentation& presentation,
                    int destinationIndex);
    void collapseRedundantSplitter(QSplitter* splitter);
    QTabWidget* fallbackMergeDestination(
        QTabWidget* source) const;
    void collectSplitterSizes(QWidget* widget);
    void restoreSplitterSizes(QWidget* widget);
    void equalizeSplitter(QWidget* widget);
    EditorSplitDirection dropDirection(
        QTabWidget* target,
        const QPoint& position) const;
    bool startTabDrag(QTabBar* bar, QMouseEvent* event);
    bool handleTabDrop(const QString& viewId,
                       QTabWidget* target,
                       EditorSplitDirection direction);
    void showTabContextMenu(QTabBar* bar,
                            const QPoint& position);
};

#endif // EDITORSPLITCONTROLLER_H
