#pragma once

#include <QList>
#include <QString>

namespace LinkedRailLayout {

// All coordinates are logical pixels. The caller supplies a stable group order.
struct Input {
    QString id;
    qint64 idealY = 0;
    int cardCount = 0;
    qint64 preferredHeight = 0;
    bool expanded = false;
};

struct Placement {
    QString id;
    qint64 contentY = 0;
    qint64 height = 0;
    qint64 viewportY = 0;
    int outlines = 0;
    bool visible = false;
    bool operator==(const Placement& other) const;
};

struct Result {
    QList<Placement> groups;
    qint64 totalHeight = 0;
    bool valid = true;
    bool operator==(const Result& other) const;
};

Result arrange(const QList<Input>& groups, qint64 viewportHeight,
               qint64 scrollOffset, qint64 gap, qint64 contentTop = 0);

} // namespace LinkedRailLayout
