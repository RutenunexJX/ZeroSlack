#pragma once

#include "zeroslackexport.h"
#include <QIcon>
#include <QPointer>
#include <QWidget>
#include <array>

class QAction;
class QMenu;
class QToolButton;
class QFrame;

// Presentation only: QAction ownership and execution remain with the editor.
class ZEROSLACK_API EditorRadialMenu final : public QWidget {
public:
    explicit EditorRadialMenu(QMenu* commands);
    static void exec(QMenu* commands, const QPoint& globalPosition);
    void popupAt(const QPoint& globalPosition);
    static QIcon actionIcon(const QString& actionId);
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
private:
    void selectGroup(int index, bool focusFirst = false);
    int groupAt(const QPoint& point) const;
    QRect barGeometry(int group, int count) const;
    void collect(QMenu* menu);
    std::array<QList<QPointer<QAction>>, 6> groups;
    std::array<QToolButton*, 6> groupButtons{};
    QFrame* bar = nullptr;
    QToolButton* closeButton = nullptr;
    QPointer<QAction> chosen;
    QPoint center;
    int currentGroup = -1;
};
