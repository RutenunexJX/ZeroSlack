#pragma once

#include "zeroslackexport.h"
#include <QColor>
#include <QHash>
#include <QPalette>
#include <QStringList>
#include <QSizeF>
#include <memory>

class QPainter;

// Immutable, bounded vector scene. A negative progress selects its static pose.
class ZEROSLACK_API WelcomeAnimation {
public:
    WelcomeAnimation();
    static WelcomeAnimation parse(const QByteArray& bytes, QString* error);
    bool isValid() const;
    bool hasMotion() const;
    int durationMs() const;
    QSizeF canvasSize() const;
    QColor accent(const QPalette& palette) const;
    void paint(QPainter& painter, const QPalette& palette, const QColor& cardFill,
               qreal progress = -1) const;
private:
    struct Data;
    std::shared_ptr<const Data> data;
};

class ZEROSLACK_API WelcomeAnimationLibrary {
public:
    explicit WelcomeAnimationLibrary(const QString& directory);
    static const WelcomeAnimationLibrary& applicationLibrary();
    const WelcomeAnimation& scene(const QString& id) const;
    QStringList diagnostics() const { return errors; }
    QString directory() const { return root; }
private:
    QString root;
    QHash<QString, WelcomeAnimation> scenes;
    QStringList errors;
};
