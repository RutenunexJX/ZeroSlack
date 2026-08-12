#ifndef TEMPORARYEDITORDRAWER_H
#define TEMPORARYEDITORDRAWER_H

#include "zeroslackexport.h"

#include "editorlocation.h"
#include "editorsearchcandidate.h"

#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QWidget>

class DrawerEdgeHandle;
class EditorDropPreviewOverlay;
class QChangeEvent;
class QEnterEvent;
class QEvent;
class QFocusEvent;
class QHBoxLayout;
class QHideEvent;
class QLabel;
class QLeaveEvent;
class QLineEdit;
class QPaintEvent;
class QResizeEvent;
class QSizeGrip;
class QShowEvent;
class QTimer;
class QToolButton;
class QVBoxLayout;
class TemporaryEditorDrawerEventFilterTestAccess;
class TemporaryEditorSearchPopup;

// A child-only overlay for an editor region.  Document/view construction and
// navigation history deliberately remain controller responsibilities.
class ZEROSLACK_API TemporaryEditorDrawer final : public QWidget
{
    Q_OBJECT

public:
    enum class State {
        Hidden,
        Floating,
        EdgeStowed
    };
    Q_ENUM(State)

    enum class Edge {
        None,
        Left,
        Right,
        Top,
        Bottom
    };
    Q_ENUM(Edge)

    explicit TemporaryEditorDrawer(QWidget* editorRegion);
    ~TemporaryEditorDrawer() override;

    State state() const;
    Edge edge() const;
    bool pinned() const;
    bool isExpandedFromHandle() const;
    bool isHandleVisible() const;
    bool interactionActive() const;
    bool dockPreviewVisible() const;
    Edge dockPreviewEdge() const;

    EditorLocation location() const;
    QString targetTitle() const;
    QWidget* editorWidget() const;
    QWidget* handleWidget() const;
    QLineEdit* searchField() const;
    QToolButton* backButton() const;
    QToolButton* forwardButton() const;
    QToolButton* pinButton() const;
    QToolButton* closeButton() const;

    // Rectangles are returned in editor-region coordinates so tests and the
    // controller do not need to reason about the drawer's transient geometry.
    QRect handleRect() const;
    QRect contentRect() const;
    QRect dockPreviewRect() const;
    QRect floatingGeometry() const;
    QSize preferredSize() const;

    int autoCollapseDelayMs() const;
    void setAutoCollapseDelayMs(int milliseconds);
    void setExternalInteractionActive(bool active);

    void setLocation(const EditorLocation& location);
    void setEditorWidget(QWidget* editor);
    QWidget* takeEditorWidget();
    void setNavigationAvailability(bool canGoBack,
                                   bool canGoForward);
    void setState(State state);
    void setEdge(Edge edge);
    void setPinned(bool pinned);
    void setFloatingGeometry(const QRect& geometry);
    void setPreferredSize(const QSize& size);
    void setSearchCandidates(
        const EditorSearchCandidates& candidates,
        const QString& query);

public slots:
    void open(const EditorLocation& location);
    void requestOpen(const EditorLocation& location);
    void requestBack();
    void requestForward();
    void closeDrawer();
    void stow(Edge edge);
    void expandFromHandle();
    void collapseToHandle();
    void showDockPreview(Edge edge);
    void clearDockPreview();
    void refreshTheme();

signals:
    void stateChanged(TemporaryEditorDrawer::State state);
    void edgeChanged(TemporaryEditorDrawer::Edge edge);
    void pinnedChanged(bool pinned);
    void pinChanged(bool pinned);
    void expandedFromHandleChanged(bool expanded);
    void interactionActiveChanged(bool active);
    void dockPreviewChanged(bool visible,
                            TemporaryEditorDrawer::Edge edge,
                            const QRect& previewRect);

    void locationChanged(const EditorLocation& location);
    void opened(const EditorLocation& location);
    void openRequested(const EditorLocation& location);
    void backRequested();
    void forwardRequested();
    void searchTextChanged(const QString& text);
    void searchCandidateActivated(
        const EditorSearchCandidate& candidate);
    void closeRequested();

    void editorWidgetChanged(QWidget* editor);
    void preferredSizeChanged(const QSize& size);
    void floatingGeometryChanged(const QRect& geometry);
    void geometryPreferenceChanged(
        const QSize& preferredSize,
        TemporaryEditorDrawer::Edge edge);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    friend class TemporaryEditorDrawerEventFilterTestAccess;

    QPointer<QWidget> editor;
    QWidget* chrome = nullptr;
    QWidget* titleBar = nullptr;
    QWidget* contentHost = nullptr;
    QVBoxLayout* contentLayout = nullptr;
    QLabel* titleLabel = nullptr;
    QLineEdit* searchEdit = nullptr;
    QToolButton* backToolButton = nullptr;
    QToolButton* forwardToolButton = nullptr;
    QToolButton* pinToolButton = nullptr;
    QToolButton* closeToolButton = nullptr;
    QSizeGrip* sizeGrip = nullptr;
    DrawerEdgeHandle* edgeHandle = nullptr;
    QPointer<EditorDropPreviewOverlay> previewOverlay;
    QTimer* autoCollapseTimer = nullptr;
    QTimer* applicationEventArmTimer = nullptr;
    TemporaryEditorSearchPopup* searchPopup = nullptr;

    EditorLocation currentLocation;
    State stateValue = State::Hidden;
    Edge edgeValue = Edge::None;
    Edge previewEdgeValue = Edge::None;
    QSize preferredSizeValue = QSize(640, 440);
    QRect floatingGeometryValue;
    QRect resizeStartGeometry;
    QPoint resizeStartGlobal;
    QPoint dragOffset;
    QList<QPointer<QWidget>> protectedPopups;
    QList<QPointer<QWidget>> localCompleterPopups;
    QHash<QWidget*, QMetaObject::Connection>
        popupDestroyedConnections;

    int autoCollapseDelayValue = 550;
    bool pinnedValue = false;
    bool expandedFromHandleValue = false;
    bool hoverWithinValue = false;
    bool focusWithinValue = false;
    bool mousePressedValue = false;
    bool draggingTitleBar = false;
    bool resizingWithGrip = false;
    bool externalInteractionValue = false;
    bool cachedInteractionActive = false;
    bool applyingTheme = false;
    bool applicationEventFilterInstalled = false;
    bool popupObservationArmed = false;
    bool releaseObservationArmed = false;
    quint64 applicationEventDispatchCount = 0;
    quint64 focusOwnershipEvaluationCount = 0;

    void buildChrome();
    void updateTitle();
    void updatePinPresentation();
    void updateGripGeometry();
    void updateVisibleParts();
    void updateApplicationEventFilterLifecycle();
    void setApplicationEventFilterEnabled(bool enabled);
    void installLocalInteractionFilters(QWidget* root);
    void removeLocalInteractionFilters(QWidget* root);
    void armPopupObservation();
    void cancelApplicationEventObservation();
    void updateApplicationEventFilterNeed();
    void discoverCompleterPopups();
    void updateInteractionState();
    void updateFocusWithin();
    void scheduleAutoCollapse();
    void cancelAutoCollapse();
    void synchronizeGeometryWithRegion();
    void setFloatingPreservingGeometry();
    void finishTitleDrag(const QPoint& globalPosition);
    void finishGripResize();

    bool computedInteractionActive() const;
    bool objectBelongsToDrawer(const QObject* object) const;
    bool isProtectedPopup(const QWidget* widget) const;
    bool isLocalCompleterPopup(const QWidget* widget) const;
    bool isPopupLike(const QWidget* widget) const;
    bool isTitleDragSurface(const QObject* object) const;
    bool handleTitleBarEvent(QEvent* event);
    bool handleSizeGripEvent(QEvent* event);
    void trackPopup(QWidget* popup, bool visible);
    void pruneProtectedPopups();

    Edge edgeAt(const QPoint& editorRegionPosition) const;
    QRect boundedFloatingGeometry(const QRect& geometry) const;
    QRect expandedGeometryForEdge(Edge edge) const;
    QRect collapsedHandleGeometry(Edge edge) const;
    QSize boundedPreferredSize(const QSize& size) const;
};

Q_DECLARE_METATYPE(TemporaryEditorDrawer::State)
Q_DECLARE_METATYPE(TemporaryEditorDrawer::Edge)

#endif // TEMPORARYEDITORDRAWER_H
