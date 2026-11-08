#include "candidatepopupnavigation.h"
#include "uicontrols.h"
#include <QShowEvent>
#include <QHideEvent>
#include "globalcontrolpanel.h"
#include "compactcandidatedelegate.h"

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
    categoryTabs->addTab(QStringLiteral("Symbols"));
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

void GlobalControlPanel::setCategory(GlobalControlCategory value)
{
    if (!categoryTabs)
        return;
    categoryTabs->setCurrentIndex(value == GlobalControlCategory::Commands ? 1 : 0);
    updatePlaceholder();
}

GlobalControlCategory GlobalControlPanel::category() const
{
    const int index = categoryTabs ? categoryTabs->currentIndex() : 0;
    return index == 1 ? GlobalControlCategory::Commands : GlobalControlCategory::Symbols;
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
    if (!chooseDirection) return;
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
    layout()->activate();
    adjustSize();
    opensAbove = popupAnchor.y() + 8 + height() > available.bottom() + 1;
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

void GlobalControlPanel::setQueryReadOnly(bool readOnly)
{
    searchEdit->setReadOnly(readOnly);
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
    if (closeOnActivation) hide();
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
    switch (category()) {
    case GlobalControlCategory::Symbols:
        searchEdit->setPlaceholderText(QStringLiteral("Search symbols"));
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
