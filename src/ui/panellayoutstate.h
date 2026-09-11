#ifndef PANELLAYOUTSTATE_H
#define PANELLAYOUTSTATE_H

#include <QString>
#include <QStringList>
#include <QHash>
#include <QVariantMap>

struct PanelLayoutState {
    static constexpr int kVersion = 2;
    static constexpr int kDefaultBottomHeight = 280;

    int version = kVersion;
    QStringList bottomPanelOrder;
    QStringList closedBottomPanels;
    QStringList pinnedBottomPanels;
    QString activeBottomPanel;
    QString lastBottomPanel;
    QHash<QString, int> bottomPanelHeights;
    QHash<QString, QVariantMap> bottomPanelViewStates;
    int expandedBottomHeight = kDefaultBottomHeight;
    bool bottomCollapsed = false;
    bool navigationVisible = true;
    bool valid = false;
};

#endif // PANELLAYOUTSTATE_H
