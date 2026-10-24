#pragma once
#include "applicationthememanager.h"
#include "welcomeanimation.h"
#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QPainter>
#include <QPushButton>
#include <QVariantAnimation>

class WelcomeCard final : public QPushButton {
public:
    enum Kind { Rtl, Waveform, Registers };
    WelcomeCard(Kind kind, QWidget* parent = nullptr) : QPushButton(parent), kind(kind), scene(WelcomeAnimationLibrary::applicationLibrary().scene(resourceId(kind))), animation(this) {
        setObjectName(QStringLiteral("welcomeCard%1").arg(int(kind)));
        setMinimumSize(250, 310); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(310); setCursor(Qt::PointingHandCursor);
        setAccessibleName(title()); setToolTip(title());
        animation.setObjectName(QStringLiteral("welcomeMotion"));
        animation.setDuration(scene.durationMs()); animation.setStartValue(0.0); animation.setEndValue(1.0);
        setProperty("welcomeResourceValid", scene.isValid());
        setProperty("welcomeResourceId", resourceId(kind));
        connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            progress = value.toReal(); update();
        });
        connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::animationsEnabledChanged,
                this, [this](bool enabled) { if (!enabled) stopMotion(); });
        connect(&animation, &QVariantAnimation::finished, this, [this] { update(); });
        connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged, this, [this] { update(); });
        qApp->installEventFilter(this);
    }
    ~WelcomeCard() override { animation.stop(); if (qApp) qApp->removeEventFilter(this); }
    QString title() const {
        switch (kind) {
        case Rtl: return tr("RTL Development");
        case Waveform: return tr("Waveform Design");
        case Registers: return tr("Register Design");
        }
        return {};
    }
    bool motionRunning() const { return animation.state() == QAbstractAnimation::Running; }
protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::Enter && scene.hasMotion() && isVisible() && window()->isActiveWindow()
            && !window()->isMinimized() && ApplicationThemeManager::instance().animationsEnabled()) {
            animation.stop(); animation.start();
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
            stopMotion();
        }
        return QPushButton::event(event);
    }
    bool eventFilter(QObject* object, QEvent* event) override {
        if ((object == window() && (event->type() == QEvent::WindowDeactivate
                || event->type() == QEvent::WindowStateChange || event->type() == QEvent::Hide || event->type() == QEvent::Close))
            || (event->type() == QEvent::ApplicationStateChange && qApp->applicationState() != Qt::ApplicationActive))
            stopMotion();
        return false;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const QColor accent = scene.accent(palette());
        const QColor ink = palette().color(QPalette::Text);
        QColor fill = palette().color(QPalette::Base);
        const qreal tint = dark ? .13 : .065;
        fill.setRedF(fill.redF() * (1-tint) + accent.redF()*tint);
        fill.setGreenF(fill.greenF() * (1-tint) + accent.greenF()*tint);
        fill.setBlueF(fill.blueF() * (1-tint) + accent.blueF()*tint);
        painter.setBrush(fill);
        painter.setPen(QPen(underMouse() || hasFocus() ? accent : palette().color(QPalette::Midlight), hasFocus() ? 2 : 1));
        painter.drawRoundedRect(QRectF(rect()).adjusted(2,2,-2,-2),12,12);
        if (!isEnabled()) {
            painter.setPen(palette().color(QPalette::PlaceholderText));
            painter.drawText(QRect(20,10,width()-40,20),Qt::AlignRight|Qt::AlignVCenter,tr("Unavailable"));
        }
        painter.save();
        const QRectF artwork(18,24,width()-36,180);
        const QSizeF canvas = scene.canvasSize();
        const qreal scale = qMin(qreal(1.12),qMin(artwork.width()/canvas.width(), artwork.height()/canvas.height()));
        painter.setClipRect(artwork);
        painter.translate(artwork.center().x()-canvas.width()*scale/2,
                          artwork.center().y()-canvas.height()*scale/2);
        painter.scale(scale,scale);
        painter.setClipRect(QRectF(QPointF(),canvas),Qt::IntersectClip);
        scene.paint(painter,palette(),fill,motionRunning() ? progress : -1);
        painter.restore();
        QFont heading=font(); heading.setPointSizeF(17); heading.setWeight(QFont::DemiBold);
        painter.setFont(heading); painter.setPen(ink);
        painter.drawText(QRect(20,212,width()-40,31),Qt::AlignLeft|Qt::AlignVCenter,title());
        QFont body=font(); body.setPointSizeF(10); painter.setFont(body); painter.setPen(palette().color(QPalette::PlaceholderText));
        const QString detail = kind==Rtl ? tr("Write HDL and explore hierarchy.\nBuild and run testbenches.")
            : kind==Waveform ? tr("Draw timing and edit signals.\nInspect cursors and time ranges.")
            : tr("Define addresses, fields, and access.\nMaintain and export register maps.");
        painter.drawText(QRect(20,249,width()-40,48),Qt::AlignLeft|Qt::TextWordWrap,detail);
    }
private:
    void stopMotion() { if (motionRunning()) { animation.stop(); update(); } }
    static QString resourceId(Kind kind) {
        switch (kind) { case Rtl: return QStringLiteral("rtl"); case Waveform: return QStringLiteral("waveform");
        case Registers: return QStringLiteral("registers"); }
        return {};
    }
    Kind kind;
    const WelcomeAnimation& scene;
    QVariantAnimation animation;
    qreal progress=1;
};

class WelcomeCardGrid final : public QWidget {
public:
    explicit WelcomeCardGrid(QWidget* parent = nullptr) : QWidget(parent) {
        grid = new QGridLayout(this); grid->setContentsMargins(0,0,0,0); grid->setSpacing(16);
        grid->setSizeConstraint(QLayout::SetNoConstraint);
        for (int i=0;i<3;++i) {
            cards[i]=new WelcomeCard(static_cast<WelcomeCard::Kind>(i),this);
            grid->addWidget(cards[i],0,i);
        }
    }
    WelcomeCard* card(int index) const { return cards[index]; }
    QSize minimumSizeHint() const override { return {250,grid->minimumSize().height()}; }
    QSize sizeHint() const override { return {990,grid->sizeHint().height()}; }
protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        const int nextColumns = width() >= 800 ? 3 : width() >= 530 ? 2 : 1;
        if (columns==nextColumns) return;
        columns=nextColumns;
        for (auto* card:cards) grid->removeWidget(card);
        for (int i=0;i<3;++i) { grid->setColumnStretch(i,i<columns ? 1 : 0); grid->addWidget(cards[i],i/columns,i%columns); }
        updateGeometry();
    }
private:
    QGridLayout* grid;
    WelcomeCard* cards[3];
    int columns=3;
};
