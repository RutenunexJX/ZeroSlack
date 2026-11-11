#pragma once
#include "applicationthememanager.h"
#include "roundedicons.h"
#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QPainter>
#include <QPushButton>

class WelcomeCard final : public QPushButton {
public:
    enum Kind { Rtl, Waveform, Registers };
    WelcomeCard(Kind kind, QWidget* parent = nullptr) : QPushButton(parent), kind(kind) {
        setObjectName(QStringLiteral("welcomeCard%1").arg(int(kind)));
        setMinimumSize(250, 310); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(310); setCursor(Qt::PointingHandCursor);
        setAccessibleName(title()); setToolTip(title());
        connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged, this, [this] { update(); });
    }
    QString title() const {
        switch (kind) {
        case Rtl: return tr("RTL Development");
        case Waveform: return tr("Waveform Design");
        case Registers: return tr("Register Design");
        }
        return {};
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const QColor accent = palette().color(QPalette::Highlight);
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
        const QRectF artwork(18,24,width()-36,180);
        const auto icon = RoundedIcons::icon(kind==Rtl ? RoundedIcons::ModuleGraph
            : kind==Waveform ? RoundedIcons::Wave : RoundedIcons::IpCatalog);
        icon.paint(&painter,QRect(artwork.center().toPoint()-QPoint(48,48),QSize(96,96)),
                   Qt::AlignCenter,isEnabled() ? QIcon::Normal : QIcon::Disabled);
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
    Kind kind;
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
