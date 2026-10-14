#include "candidatepopupnavigation.h"
#include "uicontrols.h"
#include <QShowEvent>
#include <QHideEvent>
#include "globalcontrolpanel.h"

#include "insightvisualstyle.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QSignalBlocker>
#include <QTabBar>
#include <QVBoxLayout>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStyle>
#include <utility>

namespace {
class CompactCandidateDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
        return {420, qMax(30, QFontMetrics(option.font).height() + 12)};
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem row(option); initStyleOption(&row, index); row.text.clear();
        const auto* style = row.widget ? row.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &row, painter, row.widget);
        painter->save(); painter->setFont(option.font);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const auto foreground = option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        auto rect = option.rect.adjusted(10, 4, -10, -4);
        const auto badge = index.data(Qt::UserRole + 1).toString();
        const auto summary = index.data(Qt::UserRole + 2).toString();
        const QFontMetrics metrics(option.font);
        if (!badge.isEmpty()) {
            QColor color = badge.contains(QStringLiteral("enum")) ? QColor("#c76812")
                : badge == QStringLiteral("module") || badge == QStringLiteral("instance") ? QColor("#366dcc")
                : badge == QStringLiteral("function") || badge == QStringLiteral("task") ? QColor("#8058b4")
                : badge == QStringLiteral("parameter") || badge == QStringLiteral("localparam") ? QColor("#93721a") : QColor("#258274");
            const int width = metrics.horizontalAdvance(badge) + 12;
            QRect box(rect.right() - width + 1, rect.top(), width, rect.height());
            painter->fillRect(box, color); painter->setPen(Qt::white);
            painter->drawText(box, Qt::AlignCenter, badge); rect.setRight(box.left() - 12);
        } else if (!summary.isEmpty()) {
            const int width = qMin(rect.width() / 2, metrics.horizontalAdvance(summary));
            QRect details(rect.right() - width + 1, rect.top(), width, rect.height());
            auto muted = foreground; muted.setAlpha(150); painter->setPen(muted);
            painter->drawText(details, Qt::AlignRight | Qt::AlignVCenter, metrics.elidedText(summary, Qt::ElideRight, width));
            rect.setRight(details.left() - 16);
        }
        painter->setPen(option.state.testFlag(QStyle::State_Enabled) ? foreground : option.palette.color(QPalette::Disabled, QPalette::Text));
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(index.data().toString(), Qt::ElideRight, rect.width()));
        painter->restore();
    }
};
}

GlobalControlPanel::GlobalControlPanel(QWidget* parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("globalControlPanel"));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(520);
    setMaximumWidth(720);
    InsightVisualStyle::applyGlobalControlPanel(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title = UiControls::label(QStringLiteral("Insert and Command"), this);
    layout->addWidget(title);

    categoryTabs = UiControls::tabBar(this);
    categoryTabs->setObjectName(QStringLiteral("globalControlCategoryTabs"));
    categoryTabs->setExpanding(true);
    categoryTabs->addTab(QStringLiteral("All"));
    categoryTabs->addTab(QStringLiteral("Templates"));
    categoryTabs->addTab(QStringLiteral("Commands"));
    layout->addWidget(categoryTabs);

    searchEdit = UiControls::lineEdit(this);
    searchEdit->setObjectName(QStringLiteral("globalControlSearchEdit"));
    searchEdit->installEventFilter(this);
    layout->addWidget(searchEdit);

    resultList = UiControls::listWidget(this);
    resultList->setObjectName(QStringLiteral("globalControlResultList"));
    resultList->setMinimumHeight(260);
    resultList->setMouseTracking(true);
    resultList->setItemDelegate(new CompactCandidateDelegate(resultList));
    resultList->setUniformItemSizes(true);
    resultList->installEventFilter(this);
    layout->addWidget(resultList);
    availabilityNotice = UiControls::label(QString(), this);
    availabilityNotice->setObjectName(QStringLiteral("globalControlNotice"));
    availabilityNotice->setWordWrap(true);
    availabilityNotice->hide();
    layout->addWidget(availabilityNotice);

    connect(searchEdit, &QLineEdit::textChanged,
            this, [this](const QString& text) {
                if (queryChangedHandler)
                    queryChangedHandler(text);
            });
    connect(resultList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { activateCurrentItem(); });
    connect(categoryTabs, &QTabBar::currentChanged,
            this, [this](int) {
                updatePlaceholder();
                if (categoryChangedHandler)
                    categoryChangedHandler(category());
                updatePopupGeometry();
            });
    updatePlaceholder();
}

void GlobalControlPanel::setItems(const QList<GlobalControlItem>& items, const QString& notice)
{
    currentItems = items;
    resultList->clear();
    for (int i = 0; i < currentItems.size(); ++i) {
        const GlobalControlItem& item = currentItems.at(i);
        auto* listItem = new QListWidgetItem(item.title, resultList);
        listItem->setData(Qt::UserRole, i);
        listItem->setData(Qt::UserRole + 1, item.typeBadge);
        listItem->setData(Qt::UserRole + 2, item.parameterSummary);
        listItem->setToolTip(item.disabledReason.isEmpty() ? item.insertionText : item.disabledReason);
        if (!item.disabledReason.isEmpty()) listItem->setFlags(listItem->flags() & ~Qt::ItemIsEnabled);
        resultList->addItem(listItem);
    }
    for (int i = 0; i < currentItems.size(); ++i)
        if (currentItems.at(i).disabledReason.isEmpty()) { resultList->setCurrentRow(i); break; }
    const bool unavailable = !currentItems.isEmpty() && resultList->currentRow() < 0;
    const auto message = !notice.isEmpty() ? notice
        : unavailable ? currentItems.first().disabledReason : QString();
    availabilityNotice->setText(message);
    availabilityNotice->setVisible(!message.isEmpty());
    updatePopupGeometry();
}

void GlobalControlPanel::setSimulationOnly(bool enabled)
{
    categoryTabs->setVisible(!enabled);
    setProperty("simulationOnly", enabled);
    updatePlaceholder();
    updatePopupGeometry();
}

void GlobalControlPanel::setCategory(GlobalControlCategory value)
{
    if (!categoryTabs)
        return;
    categoryTabs->setCurrentIndex(static_cast<int>(value));
    updatePlaceholder();
}

GlobalControlCategory GlobalControlPanel::category() const
{
    const int index = categoryTabs ? categoryTabs->currentIndex() : 0;
    return static_cast<GlobalControlCategory>(qBound(0, index, 2));
}

QString GlobalControlPanel::queryText() const
{
    return searchEdit ? searchEdit->text() : QString();
}

void GlobalControlPanel::resetQuery(const QString& text)
{
    if (!searchEdit)
        return;
    const QSignalBlocker blocker(searchEdit);
    searchEdit->setText(text);
    searchEdit->setCursorPosition(text.size());
}

void GlobalControlPanel::showCentered(QWidget* anchor)
{
    QPoint globalAnchor;
    if (QWidget* target = anchor ? anchor->window() : nullptr) {
        const QRect rect = target->geometry();
        globalAnchor = QPoint(rect.center().x(),
                              rect.top() + qMax(80, rect.height() / 5));
    } else if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect rect = screen->availableGeometry();
        globalAnchor = QPoint(rect.center().x(),
                              rect.top() + qMax(80, rect.height() / 5));
    }
    showAt(anchor, globalAnchor);
}

void GlobalControlPanel::showAt(QWidget* anchor,
                                const QPoint& globalAnchor)
{
    if (!isVisible()) previousFocus = QApplication::focusWidget();
    positioningWidget = anchor;
    popupAnchor = globalAnchor;
    updatePopupGeometry(true);
    show();
    raise();
    focusSearch(false);
}

void GlobalControlPanel::updatePopupGeometry(bool chooseDirection)
{
    if (!chooseDirection && !isVisible()) return;
    const bool compact = property("simulationOnly").toBool()
        || category() == GlobalControlCategory::Templates;
    if (!compact && !compactLayout && !chooseDirection) return;
    ensurePolished();
    for (auto* child : findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        child->ensurePolished();
    QScreen* screen = QGuiApplication::screenAt(popupAnchor);
    if (!screen && positioningWidget)
        screen = positioningWidget->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen
        ? screen->availableGeometry()
        : QRect(popupAnchor, sizeHint());
    if (compact) {
        // The template popup owns its height; an intermediate list/notice
        // update must not grow the native window through a stale layout minimum.
        layout()->setSizeConstraint(QLayout::SetNoConstraint);
        setMinimumHeight(0);
        const int preferredWidth = chooseDirection ? sizeHint().width() : this->width();
        const int width = qMin(available.width(), qBound(minimumWidth(), preferredWidth, maximumWidth()));
        const int count = qMin(8, resultList->count());
        const int rowHeight = count ? resultList->sizeHintForRow(0) + 2 * resultList->spacing() : 0;
        const int frameHeight = count ? 2 * resultList->frameWidth() : 0;
        resultList->setVisible(count > 0);
        resultList->setFixedHeight(0);
        layout()->invalidate();
        const auto heightForWidth = [this, width] {
            return layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width)
                : layout()->totalSizeHint().height();
        };
        const int chromeHeight = heightForWidth();
        const int below = qMax(0, available.bottom() + 1 - popupAnchor.y() - 8);
        const int above = qMax(0, popupAnchor.y() - 8 - available.top());
        const int desiredHeight = chromeHeight + count * rowHeight + frameHeight;
        if (chooseDirection)
            opensAbove = desiredHeight > below && above > below;
        int space = opensAbove ? above : below;
        const int opposite = opensAbove ? below : above;
        if (space < chromeHeight + rowHeight + frameHeight && opposite > space) {
            opensAbove = !opensAbove;
            space = opposite;
        }
        // Keep the chosen side while filtering. Reduce whole visible rows
        // before letting the shared bounds helper move the popup at an edge.
        const int rows = count ? qBound(1, (space - chromeHeight - frameHeight) / rowHeight, count) : 0;
        resultList->setFixedHeight(rows * rowHeight + frameHeight);
        layout()->invalidate();
        layout()->activate();
        resize(width, heightForWidth());
    } else {
        layout()->setSizeConstraint(QLayout::SetDefaultConstraint);
        resultList->show();
        resultList->setMinimumHeight(260);
        resultList->setMaximumHeight(QWIDGETSIZE_MAX);
        layout()->invalidate();
        layout()->activate();
        adjustSize();
        opensAbove = popupAnchor.y() + 8 + height() > available.bottom() + 1;
    }
    compactLayout = compact;
    const int y = opensAbove ? popupAnchor.y() - height() - 8 : popupAnchor.y() + 8;
    setGeometry(CandidatePopupNavigation::fitToBounds(
        QRect(QPoint(popupAnchor.x(), y), size()), available));
}

void GlobalControlPanel::focusSearch(bool selectAll)
{
    if (searchEdit) {
        searchEdit->setFocus(Qt::ShortcutFocusReason);
        if (selectAll)
            searchEdit->selectAll();
        else
            searchEdit->setCursorPosition(searchEdit->text().size());
    }
}

void GlobalControlPanel::setQueryChangedHandler(
    std::function<void(const QString&)> handler)
{
    queryChangedHandler = std::move(handler);
}

void GlobalControlPanel::setCategoryChangedHandler(
    std::function<void(GlobalControlCategory)> handler)
{
    categoryChangedHandler = std::move(handler);
}

void GlobalControlPanel::setItemActivatedHandler(
    std::function<void(const GlobalControlItem&)> handler)
{
    itemActivatedHandler = std::move(handler);
}

void GlobalControlPanel::keyPressEvent(QKeyEvent* event)
{
    if (handleKey(event))
        return;
    QFrame::keyPressEvent(event);
}

bool GlobalControlPanel::eventFilter(QObject* watched, QEvent* event)
{
    if ((watched == searchEdit || watched == resultList)
        && event->type() == QEvent::KeyPress
        && handleKey(static_cast<QKeyEvent*>(event))) {
        return true;
    }
    return QFrame::eventFilter(watched, event);
}

bool GlobalControlPanel::handleKey(QKeyEvent* event)
{
    if (CandidatePopupNavigation::handleKey(event, [this](int delta) { moveSelection(delta); },
        [this] { activateCurrentItem(); return true; }, [this] { hide(); })) return true;
    if (!event) return false;
    if (event->key() == Qt::Key_Tab)
        moveCategory(event->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
    else if (event->key() == Qt::Key_Backtab) moveCategory(-1);
    else return false;
    event->accept();
    return true;
}

void GlobalControlPanel::activateCurrentItem()
{
    const int row = resultList ? resultList->currentRow() : -1;
    if (!CandidatePopupNavigation::canActivate(resultList) || row < 0 || row >= currentItems.size())
        return;
    const GlobalControlItem selected = currentItems.at(row);
    if (!selected.disabledReason.isEmpty()) return;
    if (selected.kind == GlobalControlItemKind::Domain && searchEdit) {
        searchEdit->setText(selected.id);
        searchEdit->setCursorPosition(searchEdit->text().size());
        focusSearch();
        return;
    }
    hide();
    if (itemActivatedHandler)
        itemActivatedHandler(selected);
}

void GlobalControlPanel::moveSelection(int delta)
{
    CandidatePopupNavigation::moveSelection(resultList, delta);
}

void GlobalControlPanel::moveCategory(int delta)
{
    if (!categoryTabs || categoryTabs->count() == 0)
        return;
    const int count = categoryTabs->count();
    const int next = (categoryTabs->currentIndex() + delta + count) % count;
    categoryTabs->setCurrentIndex(next);
    focusSearch();
}

void GlobalControlPanel::updatePlaceholder()
{
    if (!searchEdit)
        return;
    if (property("simulationOnly").toBool()) {
        searchEdit->setPlaceholderText(QStringLiteral("Filter simulation templates"));
        return;
    }
    switch (category()) {
    case GlobalControlCategory::Symbols:
        searchEdit->setPlaceholderText(QStringLiteral("Search symbols"));
        break;
    case GlobalControlCategory::Templates:
        searchEdit->setPlaceholderText(QStringLiteral("Filter simulation templates"));
        break;
    case GlobalControlCategory::Commands:
        searchEdit->setPlaceholderText(QStringLiteral("Filter commands, ow, or fd"));
        break;
    }
}

void GlobalControlPanel::showEvent(QShowEvent* event)
{
    QWidget* focus = QApplication::focusWidget();
    if (focus && focus != this && !isAncestorOf(focus)) previousFocus = focus;
    QFrame::showEvent(event);
}

void GlobalControlPanel::hideEvent(QHideEvent* event)
{
    QFrame::hideEvent(event);
    if (previousFocus && previousFocus->isVisible()) {
        previousFocus->window()->activateWindow();
        previousFocus->setFocus(Qt::OtherFocusReason);
    }
}
