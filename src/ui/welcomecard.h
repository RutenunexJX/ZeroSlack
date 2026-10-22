#pragma once
#include "applicationthememanager.h"
#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVariantAnimation>
#include <cmath>

class WelcomeCard final : public QPushButton {
public:
    enum Kind { Rtl, Waveform, Registers };
    WelcomeCard(Kind kind, QWidget* parent = nullptr) : QPushButton(parent), kind(kind) {
        setObjectName(QStringLiteral("welcomeCard%1").arg(int(kind)));
        setMinimumSize(250, 310); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(310); setCursor(Qt::PointingHandCursor);
        setAccessibleName(title()); setToolTip(title());
        animation.setDuration(2400); animation.setStartValue(0.0); animation.setEndValue(1.0);
        connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            progress = value.toReal(); update();
        });
        connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::animationsEnabledChanged,
                this, [this](bool enabled) { if (!enabled) stopMotion(); });
        qApp->installEventFilter(this);
    }
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
        if (event->type() == QEvent::Enter && isVisible() && window()->isActiveWindow()
            && !window()->isMinimized() && ApplicationThemeManager::instance().animationsEnabled()) {
            animation.stop(); animation.start();
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
            stopMotion();
        }
        return QPushButton::event(event);
    }
    bool eventFilter(QObject* object, QEvent* event) override {
        if ((object == window() && (event->type() == QEvent::WindowDeactivate
                || event->type() == QEvent::WindowStateChange || event->type() == QEvent::Hide))
            || (event->type() == QEvent::ApplicationStateChange && qApp->applicationState() != Qt::ApplicationActive))
            stopMotion();
        return false;
    }
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const QColor accent = kind == Rtl ? QColor("#4278dc") : kind == Waveform ? QColor("#d46955") : QColor("#b38425");
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
        painter.translate((width()-230)/2.0,24);
        const qreal scale = qMin((width()-36)/230.0, 1.12); painter.scale(scale,scale);
        QColor softInk=ink; softInk.setAlpha(175);
        painter.setPen(QPen(softInk,1.6)); painter.setBrush(palette().color(QPalette::Base));
        painter.drawRoundedRect(QRectF(7,26,210,126),7,7);
        painter.drawLine(QPointF(7,46),QPointF(217,46));
        painter.setBrush(ink); painter.setPen(Qt::NoPen);
        for (int i=0;i<3;++i) painter.drawEllipse(QPointF(17+i*7,36),1.5,1.5);
        QFont mono("Consolas",9); painter.setFont(mono); painter.setPen(softInk);
        painter.drawText(QRectF(70,28,137,16),Qt::AlignRight|Qt::AlignVCenter,
            kind == Rtl ? "top.sv" : kind == Waveform ? "timing" : "registers");
        painter.setPen(QPen(accent,1.6));
        const qreal motion=motionRunning() ? progress : 1.0;
        if (kind == Rtl) {
            const QStringList code{"module top;", "  logic clk, rst_n;", "endmodule"};
            for (int line=0;line<code.size();++line) {
                painter.setPen(softInk); painter.drawText(QPointF(17,69+20*line),QString::number(line+1));
                painter.setPen(accent);
                const auto count = motionRunning() ? qBound(0,int((progress*3-line)*code[line].size()),int(code[line].size())) : int(code[line].size());
                painter.drawText(QPointF(35,69+20*line),code[line].left(count));
            }
            painter.drawLine(QPointF(35,133),QPointF(35+135*motion,133));
        } else if (kind == Waveform) {
            painter.setBrush(Qt::NoBrush);
            QColor grid=ink; grid.setAlpha(28); painter.setPen(QPen(grid,1));
            for (int x=18;x<210;x+=20) painter.drawLine(x,52,x,140);
            for (int lane=0;lane<2;++lane) {
                QPainterPath path; path.moveTo(17,76+lane*38);
                for (int i=0;i<9;++i) {
                    const qreal x=17+i*21, y=76+lane*38-((i+lane)%2)*16;
                    path.lineTo(x,y); path.lineTo(x+21,y);
                }
                painter.setPen(QPen(accent,lane ? 1.2 : 1.8)); painter.drawPath(path);
            }
            const qreal cursor=24+171*(motionRunning() ? progress : .63);
            painter.setPen(QPen(accent,1)); painter.drawLine(QPointF(cursor,53),QPointF(cursor,141));
            painter.setBrush(accent); painter.drawEllipse(QPointF(cursor,54),2.5,2.5);
        } else {
            painter.setPen(softInk); painter.drawText(QPointF(17,70),"0x00");
            painter.drawText(QPointF(17,98),"CTRL"); painter.drawText(QPointF(17,124),"STATUS");
            for (int i=0;i<8;++i) {
                const QRectF bit(62+i*18,56,18,20);
                QColor background=accent; background.setAlpha(motionRunning() && progress*8 >= i && progress*8 < i+2 ? 150 : 25);
                painter.setBrush(background); painter.setPen(QPen(accent,0.6)); painter.drawRect(bit);
                painter.setPen(ink); painter.drawText(bit,Qt::AlignCenter,QString::number((i%3)==1));
            }
            for (int i=0;i<3;++i) {
                QColor field=accent; field.setAlpha(motionRunning() && progress*3>=i ? 115 : 38);
                painter.setBrush(field); painter.setPen(QPen(accent,0.7)); painter.drawRoundedRect(QRectF(62+i*48,87,44,13),2,2);
            }
            painter.drawRoundedRect(QRectF(62,113,140,13),2,2);
        }
        painter.setBrush(fill); painter.setPen(QPen(softInk,2)); painter.drawRoundedRect(QRectF(179,128,46,42),8,8);
        painter.setPen(accent); mono.setPointSize(14); painter.setFont(mono);
        painter.drawText(QRectF(179,128,46,42),Qt::AlignCenter,kind==Rtl ? "</>" : kind==Waveform ? "01" : "CSR");
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
    Kind kind;
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
