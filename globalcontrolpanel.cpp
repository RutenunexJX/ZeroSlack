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

    auto* title = new QLabel(QStringLiteral("Insert and Command"), this);
    layout->addWidget(title);

    categoryTabs = new QTabBar(this);
    categoryTabs->setObjectName(QStringLiteral("globalControlCategoryTabs"));
    categoryTabs->setExpanding(true);
    categoryTabs->addTab(QStringLiteral("Symbols"));
    categoryTabs->addTab(QStringLiteral("Templates"));
    categoryTabs->addTab(QStringLiteral("Commands"));
    layout->addWidget(categoryTabs);

    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName(QStringLiteral("globalControlSearchEdit"));
    searchEdit->installEventFilter(this);
    layout->addWidget(searchEdit);

    resultList = new QListWidget(this);
    resultList->setObjectName(QStringLiteral("globalControlResultList"));
    resultList->setMinimumHeight(260);
    resultList->setMouseTracking(true);
    resultList->installEventFilter(this);
    layout->addWidget(resultList);

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
            });
    updatePlaceholder();
}

void GlobalControlPanel::setItems(const QList<GlobalControlItem>& items)
{
    currentItems = items;
    resultList->clear();
    for (int i = 0; i < currentItems.size(); ++i) {
        const GlobalControlItem& item = currentItems.at(i);
        const QString label = item.subtitle.isEmpty()
            ? item.title
            : QStringLiteral("%1\n%2").arg(item.title, item.subtitle);
        auto* listItem = new QListWidgetItem(label, resultList);
        listItem->setData(Qt::UserRole, i);
        resultList->addItem(listItem);
    }
    if (resultList->count() > 0)
        resultList->setCurrentRow(0);
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
    adjustSize();
    QScreen* screen = QGuiApplication::screenAt(globalAnchor);
    if (!screen && anchor)
        screen = anchor->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen
        ? screen->availableGeometry()
        : QRect(globalAnchor, size());
    const int maxX = qMax(available.left(), available.right() - width() + 1);
    const int maxY = qMax(available.top(), available.bottom() - height() + 1);
    const int x = qBound(available.left(), globalAnchor.x(), maxX);
    int y = globalAnchor.y() + 8;
    if (y + height() > available.bottom() + 1)
        y = globalAnchor.y() - height() - 8;
    y = qBound(available.top(), y, maxY);
    const QPoint pos(x, y);
    move(pos);
    show();
    raise();
    focusSearch(false);
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
    if (!event)
        return false;
    if (event->key() == Qt::Key_Escape) {
        hide();
    } else if (event->key() == Qt::Key_Return
               || event->key() == Qt::Key_Enter) {
        activateCurrentItem();
    } else if (event->key() == Qt::Key_Down) {
        moveSelection(1);
    } else if (event->key() == Qt::Key_Up) {
        moveSelection(-1);
    } else if (event->key() == Qt::Key_Right) {
        moveCategory(1);
    } else if (event->key() == Qt::Key_Left) {
        moveCategory(-1);
    } else if (event->key() == Qt::Key_Tab) {
        moveCategory(event->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
    } else if (event->key() == Qt::Key_Backtab) {
        moveCategory(-1);
    } else {
        return false;
    }
    event->accept();
    return true;
}

void GlobalControlPanel::activateCurrentItem()
{
    const int row = resultList ? resultList->currentRow() : -1;
    if (row < 0 || row >= currentItems.size())
        return;
    const GlobalControlItem selected = currentItems.at(row);
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
    if (!resultList || resultList->count() == 0)
        return;
    const int next = qBound(0, resultList->currentRow() + delta,
                            resultList->count() - 1);
    resultList->setCurrentRow(next);
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
        searchEdit->setPlaceholderText(QStringLiteral("Filter visible symbols"));
        break;
    case GlobalControlCategory::Templates:
        searchEdit->setPlaceholderText(QStringLiteral("Filter templates"));
        break;
    case GlobalControlCategory::Commands:
        searchEdit->setPlaceholderText(QStringLiteral("Filter commands, ow, or fd"));
        break;
    }
}
