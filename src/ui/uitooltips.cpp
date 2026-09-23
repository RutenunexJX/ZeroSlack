#include "uitooltips.h"
#include "applicationthememanager.h"
#include "uicontrols.h"
#include "uitypography.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QHelpEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QScreen>
#include <QTabBar>
#include <QTextLayout>
#include <QTimer>
#include <QToolTip>
#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaToolTip.h"
#endif

namespace {
#ifdef ZEROSLACK_ENABLE_ELA
QString wrappedText(const QString& text, const QFont& font, int width) {
    QStringList lines;
    for (const auto& paragraph : text.split('\n')) {
        if (paragraph.isEmpty()) { lines.append(QString()); continue; }
        QTextLayout layout(paragraph, font);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true) {
            auto line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(width);
            lines.append(paragraph.mid(line.textStart(), line.textLength()));
        }
        layout.endLayout();
    }
    return lines.join('\n');
}

class ToolTipService final : public QObject {
public:
    explicit ToolTipService(QObject* parent) : QObject(parent) {
        setObjectName(QStringLiteral("uiToolTipService"));
        expiry.setSingleShot(true);
        connect(&expiry, &QTimer::timeout, this, &ToolTipService::hide);
        connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged,
                this, &ToolTipService::hide);
        qApp->installEventFilter(this);
    }
    ~ToolTipService() override { delete tip.data(); }
    void hide() {
        expiry.stop();
        if (tip) tip->hide();
        anchor.clear();
        anchorRect = {};
    }
    void show(const QPoint& position, const QString& text, QWidget* owner, const QRect& area, int duration) {
        if (text.isEmpty()) { hide(); return; }
        if (!tip) {
            // Qt owns hover timing; do not install Ela's delayed parent hover filter.
            tip = new ElaToolTip;
            tip->setObjectName(QStringLiteral("uiToolTip"));
            tip->setAttribute(Qt::WA_ShowWithoutActivating);
            tip->setFocusPolicy(Qt::NoFocus);
            tip->setBorderRadius(7);
            caption = UiControls::label(tip);
            caption->setObjectName(QStringLiteral("uiToolTipText"));
            caption->setTextFormat(Qt::PlainText);
            caption->setWordWrap(false);
            UiTypography::apply(caption, UiTypography::Role::Body);
            tip->setCustomWidget(caption);
        }
        auto* window = owner ? owner->window() : QApplication::activeWindow();
        if (tip->parentWidget() != window)
            tip->setParent(window, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        tip->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        auto* screen = QGuiApplication::screenAt(position);
        if (!screen) screen = QGuiApplication::primaryScreen();
        const QRect available = screen->availableGeometry().adjusted(4, 4, -4, -4);
        const int width = qMax(1, qMin(420, available.width() - 32));
        caption->setMaximumWidth(width);
        caption->setText(wrappedText(text, caption->font(), width));
        tip->setAccessibleName(text);
        tip->setMaximumSize(available.size());
        tip->resize(tip->sizeHint().boundedTo(available.size()));
        QPoint target = position + QPoint(12, 18);
        if (target.y() + tip->height() > available.bottom() + 1)
            target.setY(position.y() - tip->height() - 8);
        target.setX(qBound(available.left(), target.x(), qMax(available.left(), available.right() - tip->width() + 1)));
        target.setY(qBound(available.top(), target.y(), qMax(available.top(), available.bottom() - tip->height() + 1)));
        anchor = owner;
        anchorRect = area.isEmpty() && owner ? owner->rect() : area;
        tip->move(target);
        tip->show();
        tip->raise();
        expiry.start(duration > 0 ? duration : int(qBound(qsizetype(4000), 40 * text.size(), qsizetype(12000))));
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (ApplicationThemeManager::instance().backend() != UiStyleBackend::Ela) return false;
        auto* widget = qobject_cast<QWidget*>(watched);
        if (event->type() == QEvent::ToolTip && widget) {
            auto* help = static_cast<QHelpEvent*>(event);
            QString text = widget->toolTip();
            QRect area = widget->rect();
            if (auto* view = qobject_cast<QAbstractItemView*>(widget->parentWidget()); view && widget == view->viewport()) {
                const auto index = view->indexAt(help->pos());
                text = index.data(Qt::ToolTipRole).toString();
                area = view->visualRect(index);
            } else if (auto* graph = qobject_cast<QGraphicsView*>(widget->parentWidget()); graph && widget == graph->viewport()) {
                for (auto* item = graph->itemAt(help->pos()); item; item = item->parentItem())
                    if (!item->toolTip().isEmpty()) { text = item->toolTip(); break; }
                area = QRect(help->pos() - QPoint(4, 4), QSize(9, 9));
            } else if (auto* tabs = qobject_cast<QTabBar*>(widget)) {
                const int index = tabs->tabAt(help->pos());
                text = tabs->tabToolTip(index);
                area = tabs->tabRect(index);
            } else if (auto* menu = qobject_cast<QMenu*>(widget); menu && menu->toolTipsVisible()) {
                if (auto* action = menu->actionAt(help->pos())) {
                    text = action->toolTip();
                    area = menu->actionGeometry(action);
                }
            }
            if (text.isEmpty()) { hide(); return false; }
            show(help->globalPos(), text, widget, area, widget->toolTipDuration());
            event->accept();
            return true;
        }
        if (!tip || !tip->isVisible()) return false;
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
        case QEvent::ApplicationDeactivate:
            hide(); break;
        case QEvent::Leave:
            if (widget == anchor) hide();
            break;
        case QEvent::Hide:
        case QEvent::Close:
        case QEvent::WindowDeactivate:
        case QEvent::Move:
        case QEvent::Resize:
            if (anchor && (widget == anchor || widget == anchor->window())) hide();
            break;
        case QEvent::MouseMove:
            if (widget == anchor && !anchorRect.contains(static_cast<QMouseEvent*>(event)->position().toPoint())) hide();
            break;
        default: break;
        }
        return false;
    }
private:
    QPointer<ElaToolTip> tip;
    QPointer<QLabel> caption;
    QPointer<QWidget> anchor;
    QRect anchorRect;
    QTimer expiry;
};

ToolTipService* service() {
    static QPointer<ToolTipService> instance;
    if (!instance) instance = new ToolTipService(qApp);
    return instance;
}
#endif
}

void UiToolTips::install() {
#ifdef ZEROSLACK_ENABLE_ELA
    service();
#endif
}
void UiToolTips::showText(const QPoint& position, const QString& text, QWidget* owner, const QRect& anchor, int duration) {
#ifdef ZEROSLACK_ENABLE_ELA
    if (ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela) {
        service()->show(position, text, owner, anchor, duration);
        return;
    }
#endif
    QToolTip::showText(position, text, owner, anchor, duration);
}
void UiToolTips::hideText() {
#ifdef ZEROSLACK_ENABLE_ELA
    service()->hide();
#endif
    QToolTip::hideText();
}
