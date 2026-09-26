#include "contexttoolbox.h"
#include "uicontrols.h"
#include "uitypography.h"
#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

ContextToolbox::ContextToolbox(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("contextToolbox"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    auto* title = UiControls::label(tr("Toolbox"), this);
    UiTypography::apply(title, UiTypography::Role::PanelTitle);
    layout->addWidget(title);
    status = UiControls::label(this);
    status->setWordWrap(true);
    status->hide();
    layout->addWidget(status);
    scroll = UiControls::scrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->installEventFilter(this);
    auto* content = new QWidget(scroll);
    grid = new QGridLayout(content);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(8);
    grid->setAlignment(Qt::AlignTop);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    utility = UiControls::toolButton(this);
    utility->setToolButtonStyle(Qt::ToolButtonIconOnly);
    utility->hide();
    layout->addWidget(utility, 0, Qt::AlignLeft);
}

void ContextToolbox::addEntry(const QString& id, const QString& title, const QIcon& icon, bool pinned)
{
    auto* card = new QFrame(scroll->widget());
    card->setObjectName(QStringLiteral("contextToolCard"));
    card->setMinimumWidth(64);
    card->setStyleSheet(QStringLiteral("QFrame#contextToolCard { background: palette(base); border: 1px solid palette(midlight); border-radius: 6px; }"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(3);
    auto* run = UiControls::toolButton(card);
    run->setObjectName(QStringLiteral("contextToolRun.%1").arg(id));
    run->setProperty("providerId", id);
    run->setToolButtonStyle(Qt::ToolButtonIconOnly);
    run->setIcon(icon);
    run->setIconSize(QSize(28, 28));
    run->setToolTip(title);
    run->setAccessibleName(title);
    run->setMinimumSize(48, 48);
    run->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(run);
    auto* pin = UiControls::checkBox(QString(), card);
    pin->setObjectName(QStringLiteral("contextToolPin.%1").arg(id));
    pin->setAccessibleName(tr("Show %1 in sidebar").arg(title));
    pin->setToolTip(pin->accessibleName());
    pin->setFixedSize(24, 24);
    pin->setChecked(pinned);
    layout->addWidget(pin, 0, Qt::AlignRight);
    connect(run, &QToolButton::clicked, this, [this, id] { emit toolRequested(id); });
    connect(pin, &QCheckBox::toggled, this, [this, id](bool value) { emit pinChanged(id, value); });
    cards.append({id, card, run, pin});
    columns = 0;
    reflow();
}

void ContextToolbox::removeEntry(const QString& id)
{
    for (int i = 0; i < cards.size(); ++i) {
        if (cards.at(i).id != id) continue;
        auto* widget = cards.takeAt(i).widget;
        grid->removeWidget(widget);
        widget->deleteLater();
        columns = 0;
        reflow();
        return;
    }
}

void ContextToolbox::setPinned(const QString& id, bool pinned)
{
    for (const auto& card : cards) if (card.id == id) {
        const QSignalBlocker blocker(card.pin);
        card.pin->setChecked(pinned);
    }
}

void ContextToolbox::setEntryIcon(const QString& id, const QIcon& icon)
{
    for (const auto& card : cards) if (card.id == id) card.run->setIcon(icon);
}

void ContextToolbox::setUtilityAction(QAction* action)
{
    utility->setDefaultAction(action);
    utility->setVisible(action != nullptr);
}

void ContextToolbox::setStatus(const QString& message)
{
    status->setText(message);
    status->setVisible(!message.isEmpty());
}

bool ContextToolbox::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == scroll->viewport() && event->type() == QEvent::Resize) reflow();
    return QWidget::eventFilter(watched, event);
}

void ContextToolbox::reflow()
{
    int cellWidth = 64;
    for (const auto& card : cards) cellWidth = qMax(cellWidth, card.widget->minimumSizeHint().width());
    const int count = qMax(1, (scroll->viewport()->width() + grid->spacing()) / (cellWidth + grid->spacing()));
    if (columns == count) return;
    columns = count;
    for (const auto& card : cards) grid->removeWidget(card.widget);
    for (int i = 0; i < cards.size(); ++i) grid->addWidget(cards.at(i).widget, i / columns, i % columns);
}
