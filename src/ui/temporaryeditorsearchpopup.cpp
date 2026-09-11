#include "temporaryeditorsearchpopup.h"

#include "applicationthememanager.h"
#include "insightvisualstyle.h"

#include <QEvent>
#include <QKeyEvent>
#include <QListWidget>
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

    list = new QListWidget(this);
    list->setObjectName(QStringLiteral("temporaryEditorSearchResults"));
    list->setAlternatingRowColors(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setFocusPolicy(Qt::NoFocus);
    list->setTextElideMode(Qt::ElideMiddle);
    layout->addWidget(list);

    QObject::connect(
        list,
        &QListWidget::itemClicked,
        this,
        [this](QListWidgetItem* item) {
            if (item)
                activateRow(list->row(item));
        });
    QObject::connect(
        list,
        &QListWidget::itemActivated,
        this,
        [this](QListWidgetItem* item) {
            if (item)
                activateRow(list->row(item));
        });

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
    candidatesValue.clear();
    list->clear();
    if (queryValue.isEmpty()) {
        hide();
        return;
    }

    for (const EditorSearchCandidate& candidate : candidates) {
        if (!candidate.isValid()) {
            continue;
        }
        candidatesValue.append(candidate);
        const QString type = editorSearchCandidateTypeLabel(candidate.type);
        QString text = QStringLiteral("%1   [%2]")
                           .arg(candidate.title, type);
        if (!candidate.disambiguation.trimmed().isEmpty()) {
            text += QStringLiteral("\n%1")
                        .arg(candidate.disambiguation.trimmed());
        }
        auto* item = new QListWidgetItem(text, list);
        item->setData(Qt::UserRole, static_cast<int>(candidate.type));
        item->setData(Qt::UserRole + 1, candidate.disambiguation);
        item->setToolTip(
            QStringLiteral("%1 - %2")
                .arg(candidate.title, candidate.disambiguation));
        item->setSizeHint(QSize(1, kCandidateRowHeight));
    }

    list->setCurrentRow(-1);
    list->clearSelection();
    if (candidatesValue.isEmpty()) {
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
    candidatesValue.clear();
    list->clear();
    hide();
}

void TemporaryEditorSearchPopup::setActivationHandler(
    ActivationHandler handler)
{
    activationHandler = std::move(handler);
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

QListWidget* TemporaryEditorSearchPopup::resultsList() const
{
    return list;
}

EditorSearchCandidates
TemporaryEditorSearchPopup::visibleCandidates() const
{
    return candidatesValue;
}

bool TemporaryEditorSearchPopup::eventFilter(
    QObject* watched,
    QEvent* event)
{
    if (watched != searchField || !event
        || event->type() != QEvent::KeyPress) {
        return QFrame::eventFilter(watched, event);
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    switch (keyEvent->key()) {
    case Qt::Key_Down:
        moveSelection(1);
        return true;
    case Qt::Key_Up:
        moveSelection(-1);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return activateCurrentOrUniqueExact();
    case Qt::Key_Escape:
        hide();
        list->setCurrentRow(-1);
        list->clearSelection();
        return true;
    default:
        break;
    }
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
    const int contentHeight = list->count() * kCandidateRowHeight + 2;
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
    setGeometry(popupX, popupY, width, height);
}

void TemporaryEditorSearchPopup::moveSelection(int delta)
{
    if (candidatesValue.isEmpty())
        return;
    if (!isVisible()) {
        repositionBelowSearchField();
        show();
        raise();
    }
    int row = list->currentRow();
    const int candidateCount = static_cast<int>(candidatesValue.size());
    if (row < 0)
        row = delta > 0 ? 0 : candidateCount - 1;
    else
        row = std::clamp(row + delta, 0, candidateCount - 1);
    list->setCurrentRow(row);
    list->scrollToItem(list->item(row));
}

bool TemporaryEditorSearchPopup::activateCurrentOrUniqueExact()
{
    const int row = list->currentRow();
    if (row >= 0) {
        activateRow(row);
        return true;
    }
    if (candidatesValue.size() != 1)
        return true;
    const EditorSearchCandidate& only = candidatesValue.first();
    if (only.title.compare(queryValue, Qt::CaseInsensitive) != 0)
        return true;
    activateRow(0);
    return true;
}

void TemporaryEditorSearchPopup::activateRow(int row)
{
    if (row < 0 || row >= static_cast<int>(candidatesValue.size()))
        return;
    const EditorSearchCandidate selected = candidatesValue.at(row);
    clearCandidates();
    if (activationHandler)
        activationHandler(selected);
}
