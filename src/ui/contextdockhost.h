#ifndef CONTEXTDOCKHOST_H
#define CONTEXTDOCKHOST_H

#include "contextresource.h"
#include "zeroslackexport.h"

#include <QHash>
#include <QStringList>
#include <QWidget>
#include <QPointer>

class QScrollArea;
class QLabel;
class QMimeData;
class ContextFloatingWindow;
class QAbstractButton;
class QToolButton;

class ZEROSLACK_API ContextDockHost final : public QWidget
{
    Q_OBJECT

public:
    explicit ContextDockHost(QWidget* parent = nullptr);
    ~ContextDockHost() override;

    int resourceCount() const;
    int areaResourceCount(bool bottom) const;
    QWidget* bottomWidget() const;
    bool isBottomResource(const QString& key) const;
    bool moveResourceToArea(const QString& key, bool bottom, int index = -1);
    int sectionWidth(const QString& key) const;
    bool setSectionWidth(const QString& key, int width);
    QStringList resourceKeys() const;
    bool containsResource(const QString& key) const;
    ContextResource resourceAt(int index) const;
    ContextResource currentResource() const;
    QWidget* viewForResource(const QString& key) const;

    bool addResource(const ContextResource& resource,
                     QWidget* view,
                     bool fullViewAvailable = false);
    bool updateResource(const ContextResource& resource);
    bool setFullViewAvailable(const QString& key,
                              bool available);
    bool activateResource(const QString& key);
    QWidget* takeResource(const QString& key);
    bool removeResource(const QString& key);
    bool isSectionCollapsed(const QString& key) const;
    bool setSectionCollapsed(const QString& key, bool collapsed, bool animate = true);
    int sectionHeight(const QString& key) const;
    bool setSectionHeight(const QString& key, int height);
    bool moveResource(const QString& key, int index);
    QWidget* sectionWidget(const QString& key) const;
    QWidget* sectionHeader(const QString& key) const;
    QLabel* sectionStatus(const QString& key) const;
    QAbstractButton* sectionScope(const QString& key) const;
    int insertionIndex(const QPoint& globalPosition) const;
    bool setSectionDetachable(const QString& key, bool detachable);
    QWidget* sectionDragHandle(const QString& key) const;
    QRect viewportGlobalRect(bool bottom) const;
    QRect projectedSectionRect(bool bottom, int index, const QRect& viewport, QWidget* incoming) const;
    bool acceptFloatingDrop(ContextFloatingWindow* source, const QString& key, const QPoint& globalPosition);
    void previewFloatingDrop(ContextFloatingWindow* source, const QPoint& globalPosition);
    void clearFloatingDropPreview();
    static const char* resourceMimeType() { return "application/x-zeroslack-context-resource"; }
    QSize minimumSizeHint() const override { return QSize(120, 32); }

signals:
    void closeResourceRequested(const QString& key);
    void unpinResourceRequested(const QString& key);
    void fullViewResourceRequested(const ContextResource& resource);
    void currentResourceChanged(const ContextResource& resource);
    void resourceOrderChanged();
    void sectionLayoutChanged();
    void dragOutRequested(const QString& key, const QPoint& globalPosition);
    void floatingDropRequested(const QString& key, int index, bool bottom);
    void sectionDragStarted();
    void sectionDragFinished();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct Section;
    QPointer<QScrollArea> scroll;
    QWidget* stack = nullptr;
    QPointer<QWidget> bottomRoot;
    QPointer<QScrollArea> bottomScroll;
    QWidget* bottomStack = nullptr;
    QWidget* insertionMarker = nullptr;
    QHash<QString, Section*> sections;
    QStringList order;
    QString focusedKey;
    QString draggedKey;
    QPoint dragStart;
    int resizeStartHeight = 0;
    int resizeStartWidth = 0;
    QString resizeNeighbor;
    QHash<QString, int> resizeLengths;
    bool resizingSection = false;
    bool arranging = false;
    QHash<QString, ContextResource> resources;

    int indexOfResource(const QString& key) const;
    void refreshSectionStatus(const QString& key);
    void arrangeSections();
    void arrangeArea(bool bottom);
    bool isBottomPosition(const QPoint& globalPosition) const;
    bool containsDropPosition(const QPoint& globalPosition) const;
    void finishSectionDrag(const QString& key, const QPoint& globalPosition);
    void settleMotion();
    void focusSection(const QString& key);
    int minimumSectionHeight(const Section* section) const;
    void showInsertion(const QPoint& globalPosition);
    bool validFloatingSource(QObject* source, const QMimeData* mime) const;
};

#endif // CONTEXTDOCKHOST_H
