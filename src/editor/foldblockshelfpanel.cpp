#include "foldblockshelfpanel.h"

#include "actionregistry.h"
#include "activitylogservice.h"
#include "editorhoverpopup.h"
#include "insightvisualstyle.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <utility>

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
        if (panel && panel->handleListShortcut(event))
            return;
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
    setObjectName(QStringLiteral("foldBlockShelfPanel"));
    setAcceptDrops(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName(QStringLiteral("foldShelfSearchEdit"));
    searchEdit->setPlaceholderText(QStringLiteral("Search Fold Shelf"));
    layout->addWidget(searchEdit);

    auto* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(4);

    restoreButton = new QPushButton(QStringLiteral("Restore to Active Editor"), this);
    restoreButton->setObjectName(QStringLiteral("foldShelfRestoreButton"));
    restoreButton->setEnabled(false);
    actionLayout->addWidget(restoreButton);

    renameButton = new QPushButton(QStringLiteral("Rename"), this);
    renameButton->setObjectName(QStringLiteral("foldShelfRenameButton"));
    renameButton->setEnabled(false);
    actionLayout->addWidget(renameButton);

    cleanButton = new QPushButton(QStringLiteral("Clean Stale/Consumed"), this);
    cleanButton->setObjectName(QStringLiteral("foldShelfCleanButton"));
    cleanButton->setEnabled(false);
    actionLayout->addWidget(cleanButton);

    layout->addLayout(actionLayout);

    listWidget = new FoldShelfListWidget(this);
    listWidget->setObjectName(QStringLiteral("foldShelfListWidget"));
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    listWidget->setDragEnabled(true);
    listWidget->setDragDropMode(QAbstractItemView::DragOnly);
    layout->addWidget(listWidget);

    previewPeek = new EditorHoverPopup(this);
    previewPeek->hide();

    connect(listWidget, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem* item) {
                if (!shelfModel || !item)
                    return;
                showPreview(shelfModel->item(item->data(Qt::UserRole).toString()));
            });
    connect(listWidget,
            &QListWidget::currentItemChanged,
            this,
            [this](QListWidgetItem*, QListWidgetItem*) {
                updateActionState();
            });
    connect(searchEdit,
            &QLineEdit::textChanged,
            this,
            [this](const QString&) {
                refresh();
            });
    connect(restoreButton,
            &QPushButton::clicked,
            this,
            &FoldBlockShelfPanel::handleRestoreSelectedItem);
    connect(renameButton,
            &QPushButton::clicked,
            this,
            &FoldBlockShelfPanel::handleRenameSelectedItem);
    connect(cleanButton,
            &QPushButton::clicked,
            this,
            &FoldBlockShelfPanel::handleCleanItems);
    updateModeStyle();
    updateActionState();
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

void FoldBlockShelfPanel::setActionRequestHandler(
    ActionRequestHandler handler)
{
    actionRequestHandler = std::move(handler);
}

bool FoldBlockShelfPanel::handleListShortcut(
    QKeyEvent* event)
{
    if (!event)
        return false;
    const QString shortcutText =
        effectiveActionShortcut(
            QString::fromLatin1(
                ActionIds::FoldShelfDeleteSelected));
    if (shortcutText.isEmpty())
        return false;
    const QKeySequence shortcut =
        QKeySequence::fromString(
            shortcutText,
            QKeySequence::PortableText);
    if (shortcut.matches(
            QKeySequence(event->keyCombination()))
        != QKeySequence::ExactMatch) {
        return false;
    }

    requestDeleteSelectedItem();
    event->accept();
    return true;
}

bool FoldBlockShelfPanel::requestDeleteSelectedItem(
    QString* failureReason)
{
    if (actionRequestHandler) {
        return actionRequestHandler(
            QString::fromLatin1(
                ActionIds::FoldShelfDeleteSelected),
            failureReason);
    }
    return deleteSelectedItem(failureReason);
}

void FoldBlockShelfPanel::setShelfModeActive(bool active)
{
    if (activeShelfMode == active)
        return;
    activeShelfMode = active;
    updateModeStyle();
}

bool FoldBlockShelfPanel::shelfModeActive() const
{
    return activeShelfMode;
}

void FoldBlockShelfPanel::updateModeStyle()
{
    if (!activeShelfMode) {
        setStyleSheet(QString());
        return;
    }

    setStyleSheet(InsightVisualStyle::foldShelfActiveStyleSheet(
        objectName()));
}

void FoldBlockShelfPanel::refresh()
{
    if (previewPeek && previewPeek->isVisible())
        previewPeek->closePopup();

    const QString previousId = selectedItemId();
    listWidget->clear();
    if (!shelfModel) {
        updateActionState();
        return;
    }

    const QString query = searchEdit ? searchEdit->text() : QString();
    for (const FoldShelfItem& item : shelfModel->itemsMatching(query)) {
        auto* row = new QListWidgetItem(itemDisplayText(item), listWidget);
        row->setData(Qt::UserRole, item.id);
        row->setToolTip(QStringLiteral("%1\n%2:%3-%4")
                            .arg(item.alias,
                                 item.sourceFile)
                            .arg(item.sourceStartLine)
                            .arg(item.sourceEndLine));
        if (item.id == previousId)
            listWidget->setCurrentItem(row);
    }
    updateActionState();
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
    if (item.id.isEmpty() || !previewPeek)
        return;

    PeekContentModel content;
    content.kind = PeekContentKind::FoldShelfPreview;
    content.title = QStringLiteral("Fold Block: %1").arg(item.alias);
    content.maximumSize = QSize(760, 480);

    QString source = item.sourceFile;
    if (!source.isEmpty() && item.sourceStartLine > 0) {
        source += QStringLiteral(":%1").arg(item.sourceStartLine);
        if (item.sourceEndLine > item.sourceStartLine) {
            source += QStringLiteral("-%1").arg(item.sourceEndLine);
        }
    }
    if (!source.isEmpty()) {
        content.rows.append(
            {source, PeekContentRowRole::Muted, false});
    }
    if (!item.sourceModule.isEmpty()) {
        content.rows.append(
            {QStringLiteral("module: %1").arg(item.sourceModule),
             PeekContentRowRole::Muted,
             false});
    }

    QStringList state;
    state.append(item.originKind == FoldShelfOriginKind::Moved
                     ? QStringLiteral("moved")
                     : QStringLiteral("copied"));
    if (item.consumed)
        state.append(QStringLiteral("consumed"));
    if (item.stale)
        state.append(QStringLiteral("stale"));
    content.rows.append(
        {QStringLiteral("%1 lines · %2")
             .arg(item.lineCount)
             .arg(state.join(QStringLiteral(", "))),
         PeekContentRowRole::Muted,
         false});

    content.readOnlyText.enabled = true;
    content.readOnlyText.text = item.text;
    content.readOnlyText.objectName =
        QStringLiteral("foldShelfPreviewText");
    content.readOnlyText.minimumSize = QSize(420, 220);

    QRect globalAnchor(
        mapToGlobal(rect().center()),
        QSize(1, 1));
    if (listWidget && listWidget->currentItem()) {
        const QRect rowRect =
            listWidget->visualItemRect(listWidget->currentItem());
        if (rowRect.isValid()) {
            globalAnchor = QRect(
                listWidget->viewport()->mapToGlobal(
                    rowRect.topLeft()),
                rowRect.size());
        }
    }
    previewPeek->showContent(content, globalAnchor, font());
}

bool FoldBlockShelfPanel::deleteSelectedItem(
    QString* failureReason)
{
    const auto fail =
        [failureReason](const QString& reason) {
            if (failureReason)
                *failureReason = reason;
            return false;
        };
    if (!shelfModel || !listWidget) {
        return fail(QStringLiteral(
            "Fold Shelf is unavailable"));
    }

    const QString id = selectedItemId();
    if (id.isEmpty()) {
        return fail(QStringLiteral(
            "No Fold Shelf item is selected"));
    }

    const FoldShelfItem item = shelfModel->item(id);
    if (item.id.isEmpty()) {
        return fail(QStringLiteral(
            "The selected Fold Shelf item no longer exists"));
    }

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
            if (failureReason)
                failureReason->clear();
            return true;
        }
        if (box.clickedButton() != deleteButton)
            return fail(QStringLiteral("Delete canceled"));
    }

    if (!shelfModel->removeItem(id)) {
        return fail(QStringLiteral(
            "The selected Fold Shelf item could not be deleted"));
    }
    ActivityLogService::getInstance()->append(
        QStringLiteral("Fold Shelf"),
        ActivityLogLevel::Info,
        QStringLiteral("Deleted shelf item \"%1\"").arg(item.alias));
    if (failureReason)
        failureReason->clear();
    return true;
}

void FoldBlockShelfPanel::handleRestoreSelectedItem()
{
    const QString id = selectedItemId();
    if (!id.isEmpty())
        emit restoreToActiveEditorRequested(id);
}

void FoldBlockShelfPanel::handleRenameSelectedItem()
{
    if (!shelfModel)
        return;

    const QString id = selectedItemId();
    if (id.isEmpty())
        return;

    const FoldShelfItem item = shelfModel->item(id);
    if (item.id.isEmpty())
        return;

    bool accepted = false;
    const QString alias = QInputDialog::getText(
        this,
        QStringLiteral("Rename Fold Shelf Item"),
        QStringLiteral("Alias"),
        QLineEdit::Normal,
        item.alias,
        &accepted);
    if (!accepted)
        return;

    if (!shelfModel->renameItem(id, alias)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Rename Fold Shelf Item"),
            QStringLiteral("Alias cannot be empty."));
        return;
    }

    ActivityLogService::getInstance()->append(
        QStringLiteral("Fold Shelf"),
        ActivityLogLevel::Info,
        QStringLiteral("Renamed shelf item \"%1\"").arg(alias.trimmed()));
}

void FoldBlockShelfPanel::handleCleanItems()
{
    if (!shelfModel)
        return;

    const int removed = shelfModel->removeConsumedOrStaleItems();
    if (removed <= 0)
        return;

    ActivityLogService::getInstance()->append(
        QStringLiteral("Fold Shelf"),
        ActivityLogLevel::Info,
        QStringLiteral("Cleaned %1 stale/consumed shelf item(s)")
            .arg(removed));
}

void FoldBlockShelfPanel::updateActionState()
{
    const bool hasSelection = !selectedItemId().isEmpty();
    if (restoreButton)
        restoreButton->setEnabled(hasSelection);
    if (renameButton)
        renameButton->setEnabled(hasSelection);
    if (cleanButton)
        cleanButton->setEnabled(hasCleanableItems());
}

bool FoldBlockShelfPanel::hasCleanableItems() const
{
    if (!shelfModel)
        return false;
    for (const FoldShelfItem& item : shelfModel->items()) {
        if (item.consumed || item.stale)
            return true;
    }
    return false;
}

QString FoldBlockShelfPanel::selectedItemId() const
{
    if (!listWidget)
        return QString();
    QListWidgetItem* selected = listWidget->currentItem();
    return selected ? selected->data(Qt::UserRole).toString() : QString();
}
