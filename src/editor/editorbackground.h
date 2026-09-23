#ifndef EDITORBACKGROUND_H
#define EDITORBACKGROUND_H

#include <QColor>
#include <QPixmap>
#include <QRect>
#include <QString>

class QPainter;

class EditorBackground
{
public:
    void setOptions(const QString& preset, const QString& customPath, int opacity);
    bool enabled() const;
    void paint(QPainter& painter, const QRect& viewport, const QRect& dirty,
               qreal devicePixelRatio, const QColor& base);

private:
    QString imagePath;
    int imageOpacity = 55;
    bool loadAttempted = false;
    QPixmap source;
    QColor sourceBackground;
    QPixmap scaled;
    QSize scaledSize;
    qreal scaledDpr = 0;
    QColor scaledBase;
    qreal scaledOpacity = -1;
};

#endif
