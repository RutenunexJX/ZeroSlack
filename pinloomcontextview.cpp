#include "pinloomcontextview.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
QString entrySecondaryText(const PinloomHostEntry& entry)
{
    QStringList parts;
    if (!entry.type.trimmed().isEmpty())
        parts.append(entry.type.toUpper());
    const QString summary = entry.matchSummary.trimmed().isEmpty()
        ? entry.summary.simplified()
        : entry.matchSummary.simplified();
    if (!summary.isEmpty())
        parts.append(summary);
    return parts.join(QStringLiteral("  "));
}

QString entryToolTip(const PinloomHostEntry& entry)
{
    QStringList lines;
    const QString secondary = entrySecondaryText(entry);
    if (!secondary.trimmed().isEmpty())
        lines.append(secondary);
    if (!entry.location.trimmed().isEmpty())
        lines.append(entry.location.trimmed());
    return lines.join(QLatin1Char('\n'));
}

QString detailsText(const PinloomHostDocument& document)
{
    QStringList lines;
    if (!document.entry.type.isEmpty())
        lines.append(document.entry.type.toUpper());
    if (!document.entry.location.trimmed().isEmpty())
        lines.append(document.entry.location.trimmed());
    QStringList keys = document.details.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys) {
        if (key == QStringLiteral("location"))
            continue;
        const QVariant value = document.details.value(key);
        QString text;
        if (value.metaType().id() == QMetaType::QStringList)
            text = value.toStringList().join(QStringLiteral(", "));
        else
            text = value.toString();
        if (!text.trimmed().isEmpty())
            lines.append(QStringLiteral("%1: %2").arg(key, text));
    }
    return lines.join(QLatin1Char('\n'));
}
}

PinloomContextView::PinloomContextView(PinloomHostClient* client,
                                       QWidget* parent)
    : QWidget(parent)
    , clientValue(client)
{
    setObjectName(QStringLiteral("pinloomContextView"));
    buildUi();
}

QVariantMap PinloomContextView::saveState() const
{
    QVariantMap state;
    state.insert(QStringLiteral("query"), searchEdit->text());
    if (selectedEntry.isValid()) {
        state.insert(QStringLiteral("identity"),
                     selectedEntry.identity.toVariantMap());
        state.insert(QStringLiteral("entry"),
                     selectedEntry.toVariantMap());
    }
    return state;
}

void PinloomContextView::restoreState(const QVariantMap& state)
{
    const QString query = state.value(QStringLiteral("query")).toString();
    {
        const QSignalBlocker blocker(searchEdit);
        searchEdit->setText(query);
    }
    const PinloomHostIdentity identity =
        PinloomHostIdentity::fromVariantMap(
            state.value(QStringLiteral("identity")).toMap());
    const PinloomHostEntry cached =
        PinloomHostEntry::fromVariantMap(
            state.value(QStringLiteral("entry")).toMap());
    preferredIdentity = identity;
    setLinkSource(state.value(QStringLiteral("linkSource")).toMap());
    if (cached.isValid())
        selectEntry(cached, false);
    else if (identity.isValid())
        resolveEntry(identity, false);
    startSearch();
}

PinloomHostEntry PinloomContextView::currentEntry() const
{
    return selectedEntry;
}

QLineEdit* PinloomContextView::searchField() const
{
    return searchEdit;
}

QListWidget* PinloomContextView::resultList() const
{
    return results;
}

QPlainTextEdit* PinloomContextView::previewEditor() const
{
    return contentPreview;
}

QToolButton* PinloomContextView::openButton() const
{
    return openTargetButton;
}

QToolButton* PinloomContextView::copyLinkButton() const
{
    return copyUriButton;
}

QString PinloomContextView::statusText() const
{
    return statusLabel->text();
}

void PinloomContextView::setLinkHandler(LinkHandler handler)
{
    linkHandler = std::move(handler);
}

void PinloomContextView::setLinkSource(const QVariantMap& source)
{
    activeLinkSource = source;
    const bool active = !source.isEmpty()
        && !source.value(QStringLiteral("selectedText")).toString().isEmpty();
    linkPanel->setVisible(active);
    if (!active)
        return;
    const QString relativePath =
        source.value(QStringLiteral("relativeFilePath")).toString();
    const int startLine =
        source.value(QStringLiteral("startLine")).toInt();
    const int endLine =
        source.value(QStringLiteral("endLine")).toInt();
    linkSourceLabel->setText(
        startLine == endLine
            ? QStringLiteral("Link %1:%2").arg(relativePath).arg(startLine)
            : QStringLiteral("Link %1:%2-%3")
                  .arg(relativePath)
                  .arg(startLine)
                  .arg(endLine));
    QString title =
        source.value(QStringLiteral("suggestedTitle")).toString().trimmed();
    if (title.isEmpty()) {
        const QStringList rows =
            source.value(QStringLiteral("selectedText"))
                .toString().split(QLatin1Char('\n'));
        for (const QString& row : rows) {
            if (!row.simplified().isEmpty()) {
                title = row.simplified().left(72);
                break;
            }
        }
    }
    linkTitleEdit->setText(title);
    attachEntryButton->setEnabled(selectedEntry.isValid());
    createAnchorButton->setEnabled(clientValue);
}

bool PinloomContextView::linkModeActive() const
{
    return linkPanel && linkPanel->isVisible();
}

void PinloomContextView::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(0, 0, 0, 0);
    searchRow->setSpacing(4);
    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName(QStringLiteral("pinloomContextSearch"));
    searchEdit->setPlaceholderText(QStringLiteral("Search Pinloom"));
    searchEdit->setClearButtonEnabled(true);
    reloadButton = new QToolButton(this);
    reloadButton->setObjectName(QStringLiteral("pinloomContextReload"));
    reloadButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    reloadButton->setToolTip(QStringLiteral("Refresh Pinloom results"));
    searchRow->addWidget(searchEdit, 1);
    searchRow->addWidget(reloadButton);
    root->addLayout(searchRow);

    linkPanel = new QFrame(this);
    linkPanel->setObjectName(QStringLiteral("pinloomContextLinkPanel"));
    linkPanel->setFrameShape(QFrame::StyledPanel);
    auto* linkLayout = new QVBoxLayout(linkPanel);
    linkLayout->setContentsMargins(8, 6, 8, 6);
    linkLayout->setSpacing(4);
    linkSourceLabel = new QLabel(linkPanel);
    linkSourceLabel->setObjectName(QStringLiteral("pinloomContextLinkSource"));
    QFont linkSourceFont = linkSourceLabel->font();
    linkSourceFont.setBold(true);
    linkSourceLabel->setFont(linkSourceFont);
    linkLayout->addWidget(linkSourceLabel);
    linkTitleEdit = new QLineEdit(linkPanel);
    linkTitleEdit->setObjectName(QStringLiteral("pinloomContextLinkTitle"));
    linkTitleEdit->setPlaceholderText(QStringLiteral("Anchor title"));
    linkLayout->addWidget(linkTitleEdit);
    auto* linkButtons = new QHBoxLayout;
    attachEntryButton = new QToolButton(linkPanel);
    attachEntryButton->setObjectName(QStringLiteral("pinloomContextAttachEntry"));
    attachEntryButton->setText(QStringLiteral("Link Selected"));
    attachEntryButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    attachEntryButton->setEnabled(false);
    createAnchorButton = new QToolButton(linkPanel);
    createAnchorButton->setObjectName(QStringLiteral("pinloomContextCreateAnchor"));
    createAnchorButton->setText(QStringLiteral("Create Anchor"));
    createAnchorButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    linkButtons->addWidget(attachEntryButton);
    linkButtons->addWidget(createAnchorButton);
    linkButtons->addStretch(1);
    linkLayout->addLayout(linkButtons);
    root->addWidget(linkPanel);
    linkPanel->hide();

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName(QStringLiteral("pinloomContextSplitter"));
    splitter->setChildrenCollapsible(false);
    results = new QListWidget(splitter);
    results->setObjectName(QStringLiteral("pinloomContextResults"));
    results->setSelectionMode(QAbstractItemView::SingleSelection);
    results->setAlternatingRowColors(true);

    auto* preview = new QWidget(splitter);
    auto* previewLayout = new QVBoxLayout(preview);
    previewLayout->setContentsMargins(0, 4, 0, 0);
    previewLayout->setSpacing(4);
    auto* titleRow = new QHBoxLayout;
    titleLabel = new QLabel(QStringLiteral("Pinloom"), preview);
    titleLabel->setObjectName(QStringLiteral("pinloomContextTitle"));
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleRow->addWidget(titleLabel, 1);
    openTargetButton = new QToolButton(preview);
    openTargetButton->setObjectName(QStringLiteral("pinloomContextOpen"));
    openTargetButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    openTargetButton->setToolTip(QStringLiteral("Open the selected Pinloom target"));
    openTargetButton->setEnabled(false);
    copyUriButton = new QToolButton(preview);
    copyUriButton->setObjectName(QStringLiteral("pinloomContextCopyLink"));
    copyUriButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    copyUriButton->setToolTip(QStringLiteral("Copy stable Pinloom link"));
    copyUriButton->setEnabled(false);
    titleRow->addWidget(openTargetButton);
    titleRow->addWidget(copyUriButton);
    previewLayout->addLayout(titleRow);
    detailsLabel = new QLabel(preview);
    detailsLabel->setObjectName(QStringLiteral("pinloomContextDetails"));
    detailsLabel->setWordWrap(true);
    detailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewLayout->addWidget(detailsLabel);
    contentPreview = new QPlainTextEdit(preview);
    contentPreview->setObjectName(QStringLiteral("pinloomContextContent"));
    contentPreview->setReadOnly(true);
    contentPreview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    contentPreview->setFont(QFontDatabase::systemFont(
        QFontDatabase::FixedFont));
    previewLayout->addWidget(contentPreview, 1);

    splitter->addWidget(results);
    splitter->addWidget(preview);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    statusLabel = new QLabel(this);
    statusLabel->setObjectName(QStringLiteral("pinloomContextStatus"));
    statusLabel->setWordWrap(true);
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(statusLabel);

    connect(searchEdit, &QLineEdit::textChanged,
            this, [this]() {
                preferredIdentity = {};
                startSearch();
            });
    connect(reloadButton, &QToolButton::clicked,
            this, &PinloomContextView::startSearch);
    connect(results, &QListWidget::currentItemChanged,
            this,
            [this](QListWidgetItem* current) {
                if (!current)
                    return;
                const PinloomHostEntry entry =
                    PinloomHostEntry::fromVariantMap(
                        current->data(Qt::UserRole).toMap());
                if (entry.isValid())
                    selectEntry(entry);
            });
    connect(results, &QListWidget::itemActivated,
            this, [this]() { openCurrentEntry(); });
    connect(openTargetButton, &QToolButton::clicked,
            this, &PinloomContextView::openCurrentEntry);
    connect(copyUriButton, &QToolButton::clicked,
            this, &PinloomContextView::copyCurrentUri);
    connect(attachEntryButton, &QToolButton::clicked,
            this, &PinloomContextView::attachCurrentEntry);
    connect(createAnchorButton, &QToolButton::clicked,
            this, &PinloomContextView::createSourceAnchor);
}

void PinloomContextView::startSearch()
{
    const quint64 generation = ++searchGeneration;
    if (!clientValue) {
        applySearchResults({}, QStringLiteral("Pinloom client is unavailable."),
                           generation);
        return;
    }
    setStatus(QStringLiteral("Searching Pinloom..."));
    const PinloomHostIdentity preferred =
        preferredIdentity.isValid()
        ? preferredIdentity
        : selectedEntry.identity;
    const QPointer<PinloomContextView> self(this);
    clientValue->search(
        searchEdit->text(),
        50,
        [self, generation, preferred](
            const QList<PinloomHostEntry>& entries,
            const QString& error) {
            if (self)
                self->applySearchResults(entries, error, generation, preferred);
        });
}

void PinloomContextView::applySearchResults(
    const QList<PinloomHostEntry>& entries,
    const QString& error,
    quint64 generation,
    const PinloomHostIdentity& preferred)
{
    if (generation != searchGeneration)
        return;
    int preferredRow = -1;
    {
        const QSignalBlocker blocker(results);
        results->clear();
        for (int row = 0; row < entries.size(); ++row) {
            const PinloomHostEntry& entry = entries.at(row);
            auto* item = new QListWidgetItem(entry.title, results);
            item->setData(Qt::UserRole, entry.toVariantMap());
            item->setToolTip(entryToolTip(entry));
            if (entry.identity.entryId == preferred.entryId
                && preferred.isValid()) {
                preferredRow = row;
            }
        }
    }
    if (!error.isEmpty()) {
        setStatus(error);
        return;
    }
    setStatus(entries.isEmpty()
                  ? QStringLiteral("No Pinloom entries match this query.")
                  : QStringLiteral("%1 Pinloom entries").arg(entries.size()));
    if (preferredRow >= 0)
        results->setCurrentRow(preferredRow);
    else if (!preferred.isValid() && !entries.isEmpty())
        results->setCurrentRow(0);
}

void PinloomContextView::selectEntry(const PinloomHostEntry& entry,
                                     bool announceChange)
{
    if (!entry.isValid())
        return;
    selectedEntry = entry;
    if (attachEntryButton)
        attachEntryButton->setEnabled(
            linkModeActive() && selectedEntry.isValid());
    preferredIdentity = entry.identity;
    titleLabel->setText(entry.title);
    detailsLabel->setText(entrySecondaryText(entry));
    openTargetButton->setEnabled(true);
    openTargetButton->setToolTip(
        entry.type == QStringLiteral("clip")
            ? QStringLiteral("Insert this Saved Clip through Pinloom")
            : QStringLiteral("Open this Pinloom target"));
    copyUriButton->setEnabled(entry.uri.isValid());
    if (announceChange)
        emit currentEntryChanged(selectedEntry);
    resolveEntry(entry.identity, announceChange);
}

void PinloomContextView::resolveEntry(
    const PinloomHostIdentity& identity,
    bool announceChange)
{
    const quint64 generation = ++resolveGeneration;
    if (!clientValue) {
        showDocument({}, QStringLiteral("Pinloom client is unavailable."),
                     generation, announceChange);
        return;
    }
    contentPreview->setPlainText(QStringLiteral("Loading..."));
    const QPointer<PinloomContextView> self(this);
    clientValue->resolve(
        identity,
        [self, generation, announceChange](
            const PinloomHostDocument& document,
            const QString& error) {
            if (self)
                self->showDocument(document, error, generation, announceChange);
        });
}

void PinloomContextView::showDocument(
    const PinloomHostDocument& document,
    const QString& error,
    quint64 generation,
    bool announceChange)
{
    if (generation != resolveGeneration)
        return;
    if (!error.isEmpty() || !document.isValid()) {
        contentPreview->setPlainText(error.isEmpty()
            ? QStringLiteral("Pinloom entry is unavailable.")
            : error);
        setStatus(error.isEmpty()
            ? QStringLiteral("Pinloom entry is unavailable.")
            : error);
        return;
    }
    selectedEntry = document.entry;
    preferredIdentity = document.entry.identity;
    titleLabel->setText(document.entry.title);
    detailsLabel->setText(detailsText(document));
    contentPreview->setPlainText(
        document.content.trimmed().isEmpty()
            ? QStringLiteral(
                "This Pinloom entry has no embedded text content. "
                "Use Open to jump to its authoritative target.")
            : document.content);
    openTargetButton->setEnabled(true);
    copyUriButton->setEnabled(document.entry.uri.isValid());
    if (announceChange)
        emit currentEntryChanged(selectedEntry);
}

void PinloomContextView::openCurrentEntry()
{
    if (!clientValue || !selectedEntry.isValid())
        return;
    setStatus(QStringLiteral("Opening through Pinloom..."));
    const QPointer<PinloomContextView> self(this);
    clientValue->open(
        selectedEntry.identity,
        [self](bool opened, const QString& message) {
            if (!self)
                return;
            self->setStatus(message.isEmpty()
                ? (opened ? QStringLiteral("Pinloom entry opened.")
                          : QStringLiteral("Pinloom entry could not be opened."))
                : message);
        });
}

void PinloomContextView::copyCurrentUri()
{
    if (!selectedEntry.uri.isValid())
        return;
    QApplication::clipboard()->setText(
        selectedEntry.uri.toString(QUrl::FullyEncoded));
    setStatus(QStringLiteral("Pinloom link copied."));
}

void PinloomContextView::attachCurrentEntry()
{
    if (!linkModeActive() || !selectedEntry.isValid())
        return;
    finishLink(selectedEntry);
}

void PinloomContextView::createSourceAnchor()
{
    if (!clientValue || !linkModeActive())
        return;
    attachEntryButton->setEnabled(false);
    createAnchorButton->setEnabled(false);
    setStatus(QStringLiteral("Creating Pinloom source anchor..."));
    const QVariantMap source = activeLinkSource;
    const QString title = linkTitleEdit->text().trimmed();
    const QPointer<PinloomContextView> self(this);
    clientValue->createSourceAnchor(
        source,
        title,
        [self](const PinloomHostEntry& entry,
               const QString& error) {
            if (!self)
                return;
            self->createAnchorButton->setEnabled(true);
            self->attachEntryButton->setEnabled(
                self->selectedEntry.isValid());
            if (!error.isEmpty() || !entry.isValid()) {
                self->setStatus(
                    error.isEmpty()
                        ? QStringLiteral("Pinloom did not return the created anchor.")
                        : error);
                return;
            }
            self->selectEntry(entry);
            self->finishLink(entry);
        });
}

void PinloomContextView::finishLink(const PinloomHostEntry& entry)
{
    if (!linkHandler) {
        setStatus(QStringLiteral("Code-link storage is unavailable."));
        return;
    }
    QString failureReason;
    if (!linkHandler(activeLinkSource, entry, &failureReason)) {
        setStatus(failureReason.trimmed().isEmpty()
                      ? QStringLiteral("The Pinloom link could not be saved.")
                      : failureReason);
        return;
    }
    activeLinkSource.clear();
    linkPanel->hide();
    setStatus(QStringLiteral("Pinloom link attached to the code selection."));
}

void PinloomContextView::setStatus(const QString& status)
{
    statusLabel->setText(status);
}
