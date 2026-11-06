#ifndef LINKEDANCHORGEOMETRY_H
#define LINKEDANCHORGEOMETRY_H

#include "zeroslackexport.h"
#include <QJsonObject>
#include <QLine>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

class MyCodeEditor;

namespace LinkedAnchorGeometry {
struct Anchor {
    QString id;
    bool available = false;
    int start = -1;
    int end = -1;
};

struct Selection {
    int start = 0;
    int end = 0;
    QString selected;
};

struct Sample {
    Anchor anchor;
    QRect first;
    QRect last;
};

struct Overlay {
    bool draw = false;
    QRect band;
    QLine leading;
    QLine trailing;
    QRect outline;
};

ZEROSLACK_API Selection sampleSelection(const MyCodeEditor& editor, const QString& selected);
ZEROSLACK_API Sample sampleAnchor(const MyCodeEditor& editor, const Anchor& anchor, bool sampleEnd = false);

ZEROSLACK_API bool active(const Anchor& anchor, const Selection& selection);
ZEROSLACK_API Overlay overlay(const Sample& sample, const Selection& selection,
                                const QSize& viewportSize);
}

#endif
