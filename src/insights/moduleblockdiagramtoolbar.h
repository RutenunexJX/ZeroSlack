#pragma once

#include <QWidget>
#include <QStringList>
#include <functional>

class QAction;
class QGridLayout;
class QLineEdit;
class QMenu;
class QToolBar;
class QToolButton;

class ModuleBlockDiagramToolbar final : public QWidget
{
public:
    explicit ModuleBlockDiagramToolbar(QWidget* parent = nullptr);
    void setBreadcrumbs(const QStringList& labels);
    void setSearchText(const QString& text);
    void setMoreMenu(QMenu* menu);
    void setZoom(qreal scale);
    void setFoldAvailable(bool enabled, bool collapsed);

    QAction* backAction = nullptr;
    QAction* fitAction = nullptr;
    QAction* zoomInAction = nullptr;
    QAction* zoomOutAction = nullptr;
    QAction* foldAction = nullptr;
    std::function<void(int)> breadcrumbActivated;
    std::function<void(const QString&)> searchChanged;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void arrange();
    QWidget* navigation = nullptr;
    QWidget* breadcrumbs = nullptr;
    QToolBar* tools = nullptr;
    QToolButton* more = nullptr;
    QToolButton* zoom = nullptr;
    QLineEdit* search = nullptr;
    QGridLayout* row = nullptr;
    QAction* searchAction = nullptr;
    QStringList pathLabels;
    bool stacked = false;
};
