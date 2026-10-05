#include "candidatepopupnavigation.h"
#include "uicontrols.h"
#include "temporaryeditorsearchpopup.h"

#include "applicationthememanager.h"
#include "insightvisualstyle.h"

#include <QEvent>
#include <QKeyEvent>
#include <QListView>
#include <QAbstractListModel>
#include <QLineEdit>
#include <QPalette>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
constexpr int kPopupMaximumHeight = 260;
constexpr int kPopupMinimumWidth = 360;
constexpr int kCandidateRowHeight = 42;
}

class TemporaryEditorSearchModel final : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;
    EditorSearchCandidates candidates;
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : candidates.size(); }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= candidates.size()) return {};
        const auto& candidate = candidates[index.row()];
        switch (role) {
        case Qt::DisplayRole: {
            QString text = QStringLiteral("%1   [%2]").arg(candidate.title, editorSearchCandidateTypeLabel(candidate.type));
            if (!candidate.disambiguation.trimmed().isEmpty()) text += QLatin1Char('\n') + candidate.disambiguation.trimmed();
            return text;
        }
        case Qt::UserRole: return static_cast<int>(candidate.type);
        case Qt::UserRole + 1: return candidate.disambiguation;
        case Qt::ToolTipRole: return QStringLiteral("%1 - %2").arg(candidate.title, candidate.disambiguation);
        case Qt::SizeHintRole: return QSize(1, kCandidateRowHeight);
        default: return {};
        }
    }
    void replace(const EditorSearchCandidates& input) {
        beginResetModel();
        candidates = input;
        if (std::any_of(candidates.cbegin(), candidates.cend(), [](const auto& value) { return !value.isValid(); }))
            candidates.removeIf([](const auto& value) { return !value.isValid(); });
        endResetModel();
    }
};

TemporaryEditorSearchPopup::TemporaryEditorSearchPopup(
    QWidget* drawerParent)
    : QFrame(drawerParent, Qt::Widget)
{
    setObjectName(QStringLiteral("temporaryEditorSearchPopup"));
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);
    setAutoFillBackground(true);
    setFocusPolicy(Qt::NoFocus);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    list = new QListView(this);
    model = new TemporaryEditorSearchModel(list);
    list->setModel(model);
    list->setUniformItemSizes(true);
    list->setObjectName(QStringLiteral("temporaryEditorSearchResults"));
    list->setAlternatingRowColors(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setFocusPolicy(Qt::NoFocus);
    list->setTextElideMode(Qt::ElideMiddle);
    layout->addWidget(list);

    connect(list, &QListView::clicked, this, [this](const QModelIndex& index) { activateRow(index.row()); });
    connect(list, &QListView::activated, this, [this](const QModelIndex& index) { activateRow(index.row()); });

    refreshTheme();
    connect(&ApplicationThemeManager::instance(),
            &ApplicationThemeManager::themeChanged,
            this,
            [this](ThemeMode) { refreshTheme(); });
    hide();
}

TemporaryEditorSearchPopup::~TemporaryEditorSearchPopup()
{
    if (searchField)
        searchField->removeEventFilter(this);
}

void TemporaryEditorSearchPopup::attachSearchField(QLineEdit* field)
{
    if (searchField == field)
        return;
    if (searchField)
        searchField->removeEventFilter(this);
    searchField = field;
    if (searchField)
        searchField->installEventFilter(this);
}

void TemporaryEditorSearchPopup::setCandidates(
    const EditorSearchCandidates& candidates,
    const QString& query)
{
    queryValue = query.trimmed();
    model->replace(queryValue.isEmpty() ? EditorSearchCandidates{} : candidates);
    list->setCurrentIndex({});
    list->clearSelection();
    if (model->candidates.isEmpty()) {
        hide();
        return;
    }
    repositionBelowSearchField();
    show();
    raise();
}

void TemporaryEditorSearchPopup::clearCandidates()
{
    queryValue.clear();
    model->replace({});
    hide();
}

void TemporaryEditorSearchPopup::setActivationHandler(
    ActivationHandler handler)
{
    activationHandler = std::move(handler);
}

void TemporaryEditorSearchPopup::setCancellationHandler(std::function<void()> handler)
{
    cancellationHandler = std::move(handler);
}

void TemporaryEditorSearchPopup::refreshTheme()
{
    const InsightTheme& theme = InsightVisualStyle::theme();
    QPalette popupPalette = palette();
    popupPalette.setColor(QPalette::Window, theme.panelBackground);
    popupPalette.setColor(QPalette::WindowText, theme.textPrimary);
    popupPalette.setColor(QPalette::Mid, theme.borderStrong);
    popupPalette.setColor(QPalette::Dark, theme.borderStrong);
    popupPalette.setColor(QPalette::Light, theme.border);
    setPalette(popupPalette);

    QPalette listPalette = list->palette();
    listPalette.setColor(QPalette::Base, theme.input.background);
    listPalette.setColor(
        QPalette::AlternateBase, theme.itemView.alternateBackground);
    listPalette.setColor(QPalette::Text, theme.input.text);
    listPalette.setColor(
        QPalette::Highlight, theme.input.selectionBackground);
    listPalette.setColor(
        QPalette::HighlightedText, theme.input.selectionText);
    list->setPalette(listPalette);
    update();
}

void TemporaryEditorSearchPopup::synchronizeGeometry()
{
    if (isVisible())
        repositionBelowSearchField();
}

QListView* TemporaryEditorSearchPopup::resultsList() const
{
    return list;
}

EditorSearchCandidates
TemporaryEditorSearchPopup::visibleCandidates() const
{
    return model->candidates;
}

bool TemporaryEditorSearchPopup::eventFilter(
    QObject* watched,
    QEvent* event)
{
    if (watched != searchField || !event
        || event->type() != QEvent::KeyPress) {
        return QFrame::eventFilter(watched, event);
    }

    if (CandidatePopupNavigation::handleKey(static_cast<QKeyEvent*>(event),
        [this](int delta) { moveSelection(delta); },
        [this] { return activateCurrentOrUniqueExact(); },
        [this] { if (cancellationHandler) cancellationHandler(); hide(); list->setCurrentIndex({}); list->clearSelection(); })) return true;
    return QFrame::eventFilter(watched, event);
}

void TemporaryEditorSearchPopup::repositionBelowSearchField()
{
    if (!searchField || !parentWidget())
        return;
    const QPoint below = searchField->mapTo(
        parentWidget(), QPoint(0, searchField->height()));
    const int popupX = std::clamp(
        below.x(), 0, qMax(0, parentWidget()->width() - 1));
    const int availableWidth = qMax(
        1, parentWidget()->width() - popupX - 6);
    const int width = qMin(
        availableWidth,
        qMax(kPopupMinimumWidth, searchField->width()));
    const int contentHeight = model->rowCount() * kCandidateRowHeight + 2;
    const int desiredHeight = qMin(kPopupMaximumHeight, contentHeight);
    const int spaceBelow = parentWidget()->height() - below.y() - 6;
    const QPoint searchTop = searchField->mapTo(
        parentWidget(), QPoint(0, 0));
    const int spaceAbove = searchTop.y() - 6;
    const bool placeAbove = spaceBelow < qMin(desiredHeight, 80)
        && spaceAbove > spaceBelow;
    const int availableHeight = qMax(
        1, placeAbove ? spaceAbove : spaceBelow);
    const int height = qMin(availableHeight, desiredHeight);
    const int popupY = placeAbove
        ? qMax(0, searchTop.y() - height)
        : std::clamp(
              below.y(),
              0,
              qMax(0, parentWidget()->height() - height));
    setGeometry(CandidatePopupNavigation::fitToBounds(QRect(popupX, popupY, width, height), parentWidget()->rect()));
}

void TemporaryEditorSearchPopup::moveSelection(int delta)
{
    if (model->candidates.isEmpty())
        return;
    if (!isVisible()) {
        repositionBelowSearchField();
        show();
        raise();
    }
    CandidatePopupNavigation::moveSelection(list, delta, true);
}

bool TemporaryEditorSearchPopup::activateCurrentOrUniqueExact()
{
    const int row = list->currentIndex().row();
    if (row >= 0) {
        activateRow(row);
        return true;
    }
    if (model->candidates.size() != 1)
        return true;
    const EditorSearchCandidate& only = model->candidates.first();
    if (only.title.compare(queryValue, Qt::CaseInsensitive) != 0)
        return true;
    activateRow(0);
    return true;
}

void TemporaryEditorSearchPopup::activateRow(int row)
{
    if (row < 0 || row >= static_cast<int>(model->candidates.size())) return;
    const EditorSearchCandidate selected = model->candidates.at(row);
    clearCandidates();
    if (activationHandler)
        activationHandler(selected);
}
