#include "globalcontrolpanel.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QVBoxLayout>
#include <utility>

GlobalControlPanel::GlobalControlPanel(QWidget* parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("globalControlPanel"));
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(520);
    setMaximumWidth(720);
    setStyleSheet(QStringLiteral(
        "QFrame#globalControlPanel { background:#20242b; color:#f4f4f5; "
        "border:1px solid #4b5563; border-radius:8px; }"
        "QLabel { color:#d4d4d8; font-weight:600; padding:10px 12px 2px 12px; }"
        "QLineEdit { margin:6px 10px; padding:8px; border:1px solid #52525b; "
        "border-radius:5px; background:#111827; color:#f9fafb; }"
        "QListWidget { margin:4px 10px 10px 10px; border:0; background:#20242b; "
        "color:#e5e7eb; outline:0; }"
        "QListWidget::item { padding:6px 8px; border-radius:4px; }"
        "QListWidget::item:selected { background:#2563eb; color:white; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* title = new QLabel(QStringLiteral("ZeroSlack Global Control / Search Everywhere"), this);
    layout->addWidget(title);

    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName(QStringLiteral("globalControlSearchEdit"));
    searchEdit->setPlaceholderText(QStringLiteral("Search actions, commands, files, symbols, templates..."));
    layout->addWidget(searchEdit);

    resultList = new QListWidget(this);
    resultList->setObjectName(QStringLiteral("globalControlResultList"));
    resultList->setMinimumHeight(260);
    resultList->setMouseTracking(true);
    layout->addWidget(resultList);

    connect(searchEdit, &QLineEdit::textChanged,
            this, [this](const QString& text) {
                if (queryChangedHandler)
                    queryChangedHandler(text);
            });
    connect(resultList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { activateCurrentItem(); });
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

void GlobalControlPanel::showCentered(QWidget* anchor)
{
    if (searchEdit)
        searchEdit->clear();

    adjustSize();
    QWidget* target = anchor ? anchor->window() : nullptr;
    QRect rect;
    if (target)
        rect = target->geometry();
    else if (QScreen* screen = QGuiApplication::primaryScreen())
        rect = screen->availableGeometry();

    const QPoint pos(rect.center().x() - width() / 2,
                     rect.top() + qMax(80, rect.height() / 5));
    move(pos);
    show();
    raise();
    focusSearch();
}

void GlobalControlPanel::focusSearch()
{
    if (searchEdit) {
        searchEdit->setFocus(Qt::ShortcutFocusReason);
        searchEdit->selectAll();
    }
}

QString GlobalControlPanel::queryText() const
{
    return searchEdit ? searchEdit->text() : QString();
}

void GlobalControlPanel::setQueryChangedHandler(
    std::function<void(const QString&)> handler)
{
    queryChangedHandler = std::move(handler);
}

void GlobalControlPanel::setItemActivatedHandler(
    std::function<void(const GlobalControlItem&)> handler)
{
    itemActivatedHandler = std::move(handler);
}

void GlobalControlPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        activateCurrentItem();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Down) {
        moveSelection(1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Up) {
        moveSelection(-1);
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

void GlobalControlPanel::activateCurrentItem()
{
    const int row = resultList ? resultList->currentRow() : -1;
    if (row < 0 || row >= currentItems.size())
        return;
    const GlobalControlItem selected = currentItems.at(row);
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
