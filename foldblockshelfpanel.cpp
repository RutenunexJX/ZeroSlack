#include "foldblockshelfpanel.h"

#include "activitylogservice.h"

#include <QDialog>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
class FoldShelfListWidget : public QListWidget
{
public:
    explicit FoldShelfListWidget(FoldBlockShelfPanel* owner)
        : QListWidget(owner)
        , panel(owner)
    {
    }

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        Q_UNUSED(supportedActions)
        if (!panel || !panel->model())
            return;

        QListWidgetItem* selected = currentItem();
        if (!selected)
            return;

        const FoldShelfItem item =
            panel->model()->item(selected->data(Qt::UserRole).toString());
        if (item.id.isEmpty() || item.text.isEmpty())
            return;

        auto* drag = new QDrag(this);
        auto* mime = new QMimeData;
        mime->setData(foldShelfItemMimeType(), encodeFoldShelfItem(item));
        mime->setText(item.text);
        drag->setMimeData(mime);
        drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Delete && panel) {
            panel->requestDeleteSelectedItem();
            event->accept();
            return;
        }
        QListWidget::keyPressEvent(event);
    }

private:
    FoldBlockShelfPanel* panel = nullptr;
};

QString itemDisplayText(const FoldShelfItem& item)
{
    QString suffix = item.originKind == FoldShelfOriginKind::Moved
        ? QStringLiteral("moved")
        : QStringLiteral("copied");
    if (item.consumed)
        suffix += QStringLiteral(", consumed");
    if (item.stale)
        suffix += QStringLiteral(", stale");
    return QStringLiteral("%1  (%2 lines, %3)")
        .arg(item.alias)
        .arg(item.lineCount)
        .arg(suffix);
}
}

FoldBlockShelfPanel::FoldBlockShelfPanel(QWidget* parent)
    : QWidget(parent)
{
    setAcceptDrops(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    listWidget = new FoldShelfListWidget(this);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    listWidget->setDragEnabled(true);
    listWidget->setDragDropMode(QAbstractItemView::DragOnly);
    layout->addWidget(listWidget);

    connect(listWidget, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
                if (!shelfModel || !item)
                    return;
                showPreview(shelfModel->item(item->data(Qt::UserRole).toString()));
            });
}

void FoldBlockShelfPanel::setModel(FoldBlockShelfModel* model)
{
    if (shelfModel == model)
        return;
    if (shelfModel)
        disconnect(shelfModel, nullptr, this, nullptr);
    shelfModel = model;
    if (shelfModel) {
        connect(shelfModel, &FoldBlockShelfModel::changed,
                this, &FoldBlockShelfPanel::refresh);
    }
    refresh();
}

FoldBlockShelfModel* FoldBlockShelfPanel::model() const
{
    return shelfModel;
}

void FoldBlockShelfPanel::requestDeleteSelectedItem()
{
    handleDeleteSelectedItem();
}

void FoldBlockShelfPanel::refresh()
{
    listWidget->clear();
    if (!shelfModel)
        return;

    for (const FoldShelfItem& item : shelfModel->items()) {
        auto* row = new QListWidgetItem(itemDisplayText(item), listWidget);
        row->setData(Qt::UserRole, item.id);
        row->setToolTip(QStringLiteral("%1\n%2:%3-%4")
                            .arg(item.alias,
                                 item.sourceFile)
                            .arg(item.sourceStartLine)
                            .arg(item.sourceEndLine));
    }
}

void FoldBlockShelfPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event || !event->mimeData()->hasFormat(foldShelfBlockMimeType()))
        return;

    const Qt::DropAction action =
        event->modifiers().testFlag(Qt::ControlModifier)
            ? Qt::CopyAction
            : Qt::MoveAction;
    event->setDropAction(action);
    event->accept();
}

void FoldBlockShelfPanel::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event || !event->mimeData()->hasFormat(foldShelfBlockMimeType()))
        return;

    const Qt::DropAction action =
        event->modifiers().testFlag(Qt::ControlModifier)
            ? Qt::CopyAction
            : Qt::MoveAction;
    event->setDropAction(action);
    event->accept();
}

void FoldBlockShelfPanel::dropEvent(QDropEvent* event)
{
    if (!event
        || !shelfModel
        || !event->mimeData()->hasFormat(foldShelfBlockMimeType())) {
        return;
    }

    FoldShelfItem item = decodeFoldShelfItem(
        event->mimeData()->data(foldShelfBlockMimeType()));
    if (item.text.isEmpty())
        return;

    const Qt::DropAction action =
        event->modifiers().testFlag(Qt::ControlModifier)
            ? Qt::CopyAction
            : Qt::MoveAction;
    item.originKind = action == Qt::MoveAction
        ? FoldShelfOriginKind::Moved
        : FoldShelfOriginKind::Copied;
    shelfModel->addItem(item);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Fold Shelf"),
        ActivityLogLevel::Info,
        QStringLiteral("%1 fold block \"%2\" into shelf")
            .arg(action == Qt::MoveAction
                     ? QStringLiteral("Moved")
                     : QStringLiteral("Copied"),
                 item.alias));
    event->setDropAction(action);
    event->accept();
}

void FoldBlockShelfPanel::showPreview(const FoldShelfItem& item)
{
    if (item.id.isEmpty())
        return;

    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Fold Block: %1").arg(item.alias));
    auto* layout = new QVBoxLayout(dialog);
    auto* preview = new QPlainTextEdit(dialog);
    preview->setReadOnly(true);
    preview->setPlainText(item.text);
    layout->addWidget(preview);
    dialog->resize(720, 420);
    dialog->show();
}

void FoldBlockShelfPanel::handleDeleteSelectedItem()
{
    if (!shelfModel || !listWidget)
        return;

    QListWidgetItem* selected = listWidget->currentItem();
    if (!selected)
        return;

    const QString id = selected->data(Qt::UserRole).toString();
    const FoldShelfItem item = shelfModel->item(id);
    if (item.id.isEmpty())
        return;

    if (item.originKind == FoldShelfOriginKind::Moved && !item.consumed) {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("Delete Moved Fold Block"));
        box.setText(QStringLiteral("This fold block was moved from source. Restore it before deleting?"));
        QPushButton* restoreButton =
            box.addButton(QStringLiteral("Restore"), QMessageBox::AcceptRole);
        QPushButton* deleteButton =
            box.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == restoreButton) {
            emit restoreItemRequested(id);
            return;
        }
        if (box.clickedButton() != deleteButton)
            return;
    }

    shelfModel->removeItem(id);
    ActivityLogService::getInstance()->append(
        QStringLiteral("Fold Shelf"),
        ActivityLogLevel::Info,
        QStringLiteral("Deleted shelf item \"%1\"").arg(item.alias));
}
