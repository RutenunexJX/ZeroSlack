#ifndef PANELLAYOUTSTATE_H
#define PANELLAYOUTSTATE_H

#include <QString>
#include <QStringList>

struct PanelLayoutState {
    QStringList bottomPanelOrder;
    QStringList closedBottomPanels;
    QStringList pinnedBottomPanels;
    QString activeBottomPanel;
    int expandedBottomHeight = 240;
    bool bottomCollapsed = false;
    bool navigationVisible = true;
    bool valid = false;
};

#endif // PANELLAYOUTSTATE_H
