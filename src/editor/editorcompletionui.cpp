#include "editorcompletionui.h"

#include "completionmodel.h"
#include "editorsemanticcontextservice.h"
#include "mycodeeditor.h"
#include "uicontrols.h"

#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QCompleter>
#include <QKeyEvent>
#include <QModelIndex>
#include <QListView>
#include <QStyledItemDelegate>
#include <QRect>
#include <algorithm>

namespace {
class CompletionDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto hint = QStyledItemDelegate::sizeHint(option, index);
        QStyleOptionViewItem content(option);
        initStyleOption(&content, index);
        hint.setHeight(qMax(hint.height(), qMax(28, content.fontMetrics.height() + 10)));
        hint.rwidth() += 12;
        return hint;
    }
};
}

void EditorCompletionUi::init(MyCodeEditor* editor)
{
    model = new CompletionModel(editor);
    completer = new QCompleter(editor);
    completer->setModel(model);
    completer->setWidget(editor);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setMaxVisibleItems(15);
    auto* candidates = UiControls::listView();
    candidates->setObjectName(QStringLiteral("editorCompletionPopup"));
    candidates->setUniformItemSizes(false);
    candidates->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    candidates->setTextElideMode(Qt::ElideRight);
    UiControls::enableSmoothScrolling(candidates);
    completer->setPopup(candidates);
    // QCompleter installs its delegate in setPopup(). Apply row sizing afterwards.
    candidates->setItemDelegate(new CompletionDelegate(candidates));
}

void EditorCompletionUi::attachToEditor(
    MyCodeEditor* editor,
    const std::function<void(const QModelIndex&)>& handleActivated)
{
    detach();
    init(editor);
    activationConnection = QObject::connect(
        completer,
        QOverload<const QModelIndex&>::of(&QCompleter::activated),
        editor,
        handleActivated);
}

void EditorCompletionUi::detach()
{
    QObject::disconnect(activationConnection);
    activationConnection = {};
}

QAbstractItemView* EditorCompletionUi::popup() const
{
    return completer->popup();
}

bool EditorCompletionUi::popupVisible() const
{
    return popup()->isVisible();
}

void EditorCompletionUi::hidePopup() const
{
    popup()->hide();
}

int EditorCompletionUi::rowCount() const
{
    return model->rowCount();
}

bool EditorCompletionUi::hasRows() const
{
    return rowCount() > 0;
}

QModelIndex EditorCompletionUi::currentIndex() const
{
    return popup()->currentIndex();
}

QModelIndex EditorCompletionUi::firstSelectableIndex() const
{
    const auto* proxy = qobject_cast<QAbstractProxyModel*>(completer->completionModel());
    return proxy ? proxy->mapFromSource(model->firstSelectableIndex()) : QModelIndex();
}

void EditorCompletionUi::activateIndex(const QModelIndex& index) const
{
    emit completer->activated(index);
}

EditorCompletionActivationContext
EditorCompletionUi::activationContextForIndex(
    const QModelIndex& index) const
{
    const auto* proxy = qobject_cast<const QAbstractProxyModel*>(index.model());
    const auto sourceIndex = proxy ? proxy->mapToSource(index) : index;
    if (sourceIndex.model() != model)
        return {};
    const CompletionModel::CompletionItem item = model->getItem(sourceIndex);
    EditorCompletionActivationContext context;
    context.selectable = model->isSelectableIndex(sourceIndex);
    context.itemText = item.text;
    context.defaultValue = item.defaultValue;
    context.selectionStart = item.selectionStart;
    context.selectionLength = item.selectionLength;
    context.templateSlots = item.templateSlots;
    return context;
}

EditorCompletionPopupKeyContext EditorCompletionUi::popupKeyContextForEvent(
    QKeyEvent* event) const
{
    EditorCompletionPopupKeyContext context;
    context.key = event->key();
    context.modifiers = int(event->modifiers());
    context.currentIndexValid = currentIndex().isValid();
    context.hasRows = hasRows();
    return context;
}

void EditorCompletionUi::updateCommandModeCompletions(
    const CommandModeCompletionState& state,
    bool allowSymbolFallback) const
{
    const bool usesSymbolRecords =
        state.intent == InlineCommandIntent::SemanticCompletion
        || state.intent == InlineCommandIntent::PackageImport;
    if (!usesSymbolRecords || state.helpRequested) {
        model->updateInlineCommandCompletions(state);
        return;
    }

    model->updateSymbolRecordCompletions(
        state.symbolRecords,
        state.completionPrefix,
        state.commandKind,
        allowSymbolFallback);
}

void EditorCompletionUi::updateIncludeFileCompletions(
    const QStringList& filePaths,
    const QString& prefix) const
{
    model->updateIncludeFileCompletions(filePaths, prefix);
}

void EditorCompletionUi::updateIncludeNewHeaderCompletions(
    const QList<IncludeNewHeaderChoice>& choices,
    const QString& title) const
{
    model->updateIncludeNewHeaderCompletions(choices, title);
}

void EditorCompletionUi::showForCursor(
    const QRect& cursorRectangle,
    bool selectFirstCompletion) const
{
    if (!hasRows())
        return;

    QRect popupRectangle = cursorRectangle;
    const int hintedWidth = popup()->sizeHintForColumn(0);
    const int popupWidth = std::clamp(hintedWidth + 24, 240, 720);
    popupRectangle.setWidth(popupWidth);
    if (selectFirstCompletion) {
        const QModelIndex selectableIndex = firstSelectableIndex();
        if (selectableIndex.isValid())
            popup()->setCurrentIndex(selectableIndex);
    }

    completer->complete(popupRectangle);
}
