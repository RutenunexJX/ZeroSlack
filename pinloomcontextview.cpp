#include "pinloomcontextview.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

class PinloomPreviewImageLabel final : public QLabel
{
public:
    explicit PinloomPreviewImageLabel(QWidget* parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(1, 1);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    }

    void setSourcePixmap(const QPixmap& pixmap, const QString& altText)
    {
        source = pixmap;
        setAccessibleName(altText);
        setToolTip(altText.trimmed().isEmpty()
                       ? QStringLiteral("Click to open a larger preview")
                       : QStringLiteral("%1\nClick to open a larger preview")
                             .arg(altText));
        setCursor(source.isNull()
                      ? Qt::ArrowCursor
                      : Qt::PointingHandCursor);
        updateScaledPixmap();
    }

    void clearSourcePixmap()
    {
        source = {};
        clear();
        setAccessibleName({});
        setToolTip({});
        setCursor(Qt::ArrowCursor);
    }

    const QPixmap& sourcePixmap() const
    {
        return source;
    }

    std::function<void()> activated;

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        updateScaledPixmap();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QLabel::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton
            && !source.isNull()
            && activated) {
            activated();
        }
    }

private:
    QPixmap source;

    void updateScaledPixmap()
    {
        if (source.isNull() || size().isEmpty()) {
            clear();
            return;
        }
        QLabel::setPixmap(source.scaled(
            size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
};

namespace {
constexpr qint64 kMaximumPreviewFileBytes = 64LL * 1024LL * 1024LL;
constexpr int kMaximumPreviewDimension = 16384;
constexpr qint64 kMaximumPreviewPixels = 64LL * 1024LL * 1024LL;

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

QString variantText(const QVariant& value)
{
    if (!value.isValid() || value.isNull())
        return {};
    if (value.metaType().id() == QMetaType::QStringList)
        return value.toStringList().join(QStringLiteral(", "));
    const QJsonValue json = QJsonValue::fromVariant(value);
    if (json.isArray()) {
        return QString::fromUtf8(
            QJsonDocument(json.toArray()).toJson(QJsonDocument::Compact));
    }
    if (json.isObject()) {
        return QString::fromUtf8(
            QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact));
    }
    if (json.isBool())
        return json.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return value.toString();
}

QStringList variantStringList(const QVariant& value)
{
    if (value.metaType().id() == QMetaType::QStringList)
        return value.toStringList();
    QStringList lines;
    for (const QVariant& item : value.toList()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty())
            lines.append(text);
    }
    return lines;
}

QString documentFileName(const PinloomHostDocument& document)
{
    QString result =
        document.details.value(QStringLiteral("fileName")).toString().trimmed();
    if (result.isEmpty()) {
        result = document.entry.metadata
                     .value(QStringLiteral("fileName")).toString().trimmed();
    }
    if (!result.isEmpty())
        return result;
    QString location = document.entry.location.trimmed();
    const QUrl locationUrl(location, QUrl::StrictMode);
    if (locationUrl.isLocalFile())
        location = locationUrl.toLocalFile();
    result = QFileInfo(location).fileName().trimmed();
    if (result.isEmpty() && !document.preview.filePath.isEmpty())
        result = QFileInfo(document.preview.filePath).fileName().trimmed();
    return result;
}

QString summaryDetailsText(const PinloomHostDocument& document)
{
    QStringList lines;
    QStringList locationParts;
    const QString fileName = documentFileName(document);
    if (!fileName.isEmpty())
        locationParts.append(fileName);
    int page = document.preview.page;
    if (page <= 0)
        page = document.details.value(QStringLiteral("page")).toInt();
    if (page > 0)
        locationParts.append(QStringLiteral("Page %1").arg(page));
    if (!locationParts.isEmpty())
        lines.append(locationParts.join(QStringLiteral("  ·  ")));

    QStringList aliases = document.entry.aliases;
    if (aliases.isEmpty()) {
        aliases = variantStringList(
            document.details.value(QStringLiteral("aliases")));
    }
    QStringList tags = document.entry.tags;
    if (tags.isEmpty()) {
        tags = variantStringList(
            document.details.value(QStringLiteral("tags")));
    }
    if (!aliases.isEmpty())
        lines.append(QStringLiteral("Aliases: %1").arg(aliases.join(QStringLiteral(", "))));
    if (!tags.isEmpty())
        lines.append(QStringLiteral("Tags: %1").arg(tags.join(QStringLiteral(", "))));
    return lines.join(QLatin1Char('\n'));
}

QString technicalDetailsText(const PinloomHostDocument& document)
{
    QStringList lines;
    const auto append = [&lines](const QString& key, const QVariant& value) {
        const QString text = variantText(value).trimmed();
        if (!text.isEmpty())
            lines.append(QStringLiteral("%1: %2").arg(key, text));
    };
    append(QStringLiteral("entryId"), document.entry.identity.entryId);
    append(QStringLiteral("resourceId"), document.entry.identity.resourceId);
    append(QStringLiteral("anchorId"), document.entry.identity.anchorId);
    append(QStringLiteral("clipId"), document.entry.identity.clipId);
    append(QStringLiteral("uri"),
           document.entry.uri.toString(QUrl::FullyEncoded));
    append(QStringLiteral("type"), document.entry.type);
    append(QStringLiteral("location"), document.entry.location);
    append(QStringLiteral("contentType"), document.contentType);

    QStringList keys = document.details.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString& key : keys) {
        if (key == QStringLiteral("title")
            || key == QStringLiteral("fileName")
            || key == QStringLiteral("page")
            || key == QStringLiteral("aliases")
            || key == QStringLiteral("tags")
            || key == QStringLiteral("location")) {
            continue;
        }
        append(key, document.details.value(key));
    }
    if (document.preview.present) {
        append(QStringLiteral("preview.kind"), document.preview.kind);
        append(QStringLiteral("preview.state"), document.preview.state);
        append(QStringLiteral("preview.mimeType"), document.preview.mimeType);
        append(QStringLiteral("preview.uri"),
               document.preview.uri.toString(QUrl::FullyEncoded));
        append(QStringLiteral("preview.filePath"), document.preview.filePath);
        if (document.preview.byteSize >= 0)
            append(QStringLiteral("preview.byteSize"), document.preview.byteSize);
        if (document.preview.pixelWidth > 0)
            append(QStringLiteral("preview.pixelWidth"), document.preview.pixelWidth);
        if (document.preview.pixelHeight > 0)
            append(QStringLiteral("preview.pixelHeight"), document.preview.pixelHeight);
        if (document.preview.page > 0)
            append(QStringLiteral("preview.page"), document.preview.page);
        append(QStringLiteral("preview.cropped"), document.preview.cropped);
        append(QStringLiteral("preview.altText"), document.preview.altText);
        append(QStringLiteral("preview.error"), document.preview.error);
        append(QStringLiteral("preview.validationError"),
               document.preview.validationError);
    }
    return lines.join(QLatin1Char('\n'));
}

QString normalizedLocalPath(const QString& path)
{
    QFileInfo info(path);
    QString result = info.canonicalFilePath();
    if (result.isEmpty())
        result = info.absoluteFilePath();
    result = QDir::cleanPath(result);
#ifdef Q_OS_WIN
    result = result.toLower();
#endif
    return result;
}

struct PreviewImageLoadResult {
    QPixmap pixmap;
    QString error;
};

PreviewImageLoadResult loadPreviewImage(const PinloomHostPreview& preview)
{
    if (!preview.validationError.isEmpty())
        return {{}, preview.validationError};
    if (!preview.isImage())
        return {{}, QStringLiteral("Pinloom preview is not an image.")};
    if (!preview.isReady()) {
        return {{}, preview.error.trimmed().isEmpty()
                        ? QStringLiteral("Pinloom image preview is unavailable.")
                        : preview.error.trimmed()};
    }

    QString uriPath;
    if (!preview.uri.isEmpty()) {
        if (!preview.uri.isValid()
            || !preview.uri.isLocalFile()
            || !preview.uri.host().isEmpty()) {
            return {{}, QStringLiteral(
                            "Pinloom preview URI is not a local file URI.")};
        }
        uriPath = preview.uri.toLocalFile();
    }
    QString filePath = preview.filePath.trimmed();
    if (!filePath.isEmpty()
        && (!QFileInfo(filePath).isAbsolute()
            || QDir::fromNativeSeparators(filePath)
                   .startsWith(QStringLiteral("//")))) {
        return {{}, QStringLiteral(
                        "Pinloom preview file path is not an absolute local path.")};
    }
    if (!filePath.isEmpty() && !uriPath.isEmpty()
        && normalizedLocalPath(filePath) != normalizedLocalPath(uriPath)) {
        return {{}, QStringLiteral(
                        "Pinloom preview URI and file path refer to different files.")};
    }
    if (filePath.isEmpty())
        filePath = uriPath;
    if (filePath.isEmpty())
        return {{}, QStringLiteral("Pinloom preview has no local file path.")};

    const QFileInfo info(filePath);
    if (!info.exists()) {
        return {{}, QStringLiteral("Pinloom preview file does not exist: %1")
                        .arg(QDir::toNativeSeparators(info.absoluteFilePath()))};
    }
    if (!info.isFile() || !info.isReadable()) {
        return {{}, QStringLiteral("Pinloom preview file is not readable: %1")
                        .arg(QDir::toNativeSeparators(info.absoluteFilePath()))};
    }
    if (info.size() < 0 || info.size() > kMaximumPreviewFileBytes) {
        return {{}, QStringLiteral(
                        "Pinloom preview file exceeds the 64 MiB safety limit.")};
    }
    if (!preview.mimeType.isEmpty()
        && preview.mimeType.compare(QStringLiteral("image/png"),
                                    Qt::CaseInsensitive) != 0) {
        return {{}, QStringLiteral("Pinloom preview MIME type is not image/png.")};
    }

    QImageReader reader(info.absoluteFilePath());
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead()) {
        return {{}, QStringLiteral("Pinloom preview PNG cannot be read: %1")
                        .arg(reader.errorString())};
    }
    if (reader.format().compare(QByteArrayLiteral("png"),
                                Qt::CaseInsensitive) != 0) {
        return {{}, QStringLiteral("Pinloom preview file is not a PNG image.")};
    }
    const QSize imageSize = reader.size();
    if (!imageSize.isValid()
        || imageSize.isEmpty()
        || imageSize.width() > kMaximumPreviewDimension
        || imageSize.height() > kMaximumPreviewDimension
        || qint64(imageSize.width())
               > kMaximumPreviewPixels / imageSize.height()) {
        return {{}, QStringLiteral(
                        "Pinloom preview dimensions exceed the safety limit.")};
    }
    const QImage image = reader.read();
    if (image.isNull()) {
        return {{}, QStringLiteral("Pinloom preview PNG decoding failed: %1")
                        .arg(reader.errorString())};
    }
    if (image.width() > kMaximumPreviewDimension
        || image.height() > kMaximumPreviewDimension
        || qint64(image.width()) > kMaximumPreviewPixels / image.height()) {
        return {{}, QStringLiteral(
                        "Decoded Pinloom preview dimensions exceed the safety limit.")};
    }
    const QPixmap pixmap = QPixmap::fromImage(image);
    if (pixmap.isNull())
        return {{}, QStringLiteral("Pinloom preview could not be displayed.")};
    return {pixmap, {}};
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
    if (boundMode) {
        state.insert(QStringLiteral("boundEntries"), activeBoundEntries);
        return state;
    }
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
    activeBoundEntries =
        state.value(QStringLiteral("boundEntries")).toList();
    boundMode = !activeBoundEntries.isEmpty();
    searchEdit->setVisible(!boundMode);
    reloadButton->setVisible(!boundMode);
    if (boundMode) {
        ++searchGeneration;
        setLinkSource({});
        const PinloomHostIdentity preferred =
            PinloomHostIdentity::fromVariantMap(
                state.value(QStringLiteral("identity")).toMap());
        applyBoundEntries(activeBoundEntries, preferred);
        return;
    }
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

QLabel* PinloomContextView::imagePreviewLabel() const
{
    return imagePreview;
}

QToolButton* PinloomContextView::technicalDetailsToggle() const
{
    return technicalDetailsButton;
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

bool PinloomContextView::boundModeActive() const
{
    return boundMode;
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
    openTargetButton->setText(QStringLiteral("Open"));
    openTargetButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
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
    technicalDetailsButton = new QToolButton(preview);
    technicalDetailsButton->setObjectName(
        QStringLiteral("pinloomContextTechnicalDetailsToggle"));
    technicalDetailsButton->setText(QStringLiteral("Technical details"));
    technicalDetailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    technicalDetailsButton->setArrowType(Qt::RightArrow);
    technicalDetailsButton->setCheckable(true);
    technicalDetailsButton->setChecked(false);
    previewLayout->addWidget(technicalDetailsButton, 0, Qt::AlignLeft);
    technicalDetailsLabel = new QLabel(preview);
    technicalDetailsLabel->setObjectName(
        QStringLiteral("pinloomContextTechnicalDetails"));
    technicalDetailsLabel->setWordWrap(true);
    technicalDetailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    technicalDetailsLabel->hide();
    previewLayout->addWidget(technicalDetailsLabel);

    previewStack = new QStackedWidget(preview);
    previewStack->setObjectName(QStringLiteral("pinloomContextPreviewStack"));
    contentPreview = new QPlainTextEdit(previewStack);
    contentPreview->setObjectName(QStringLiteral("pinloomContextContent"));
    contentPreview->setReadOnly(true);
    contentPreview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    contentPreview->setFont(QFontDatabase::systemFont(
        QFontDatabase::FixedFont));
    imagePreview = new PinloomPreviewImageLabel(previewStack);
    imagePreview->setObjectName(QStringLiteral("pinloomContextImage"));
    imagePreview->activated = [this]() { openImagePreview(); };
    previewStack->addWidget(contentPreview);
    previewStack->addWidget(imagePreview);
    previewStack->setCurrentWidget(contentPreview);
    previewLayout->addWidget(previewStack, 1);

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
    connect(technicalDetailsButton, &QToolButton::toggled,
            this, [this](bool expanded) {
                technicalDetailsButton->setArrowType(
                    expanded ? Qt::DownArrow : Qt::RightArrow);
                technicalDetailsLabel->setVisible(expanded);
            });
    connect(attachEntryButton, &QToolButton::clicked,
            this, &PinloomContextView::attachCurrentEntry);
    connect(createAnchorButton, &QToolButton::clicked,
            this, &PinloomContextView::createSourceAnchor);
}

void PinloomContextView::startSearch()
{
    if (boundMode)
        return;
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

void PinloomContextView::applyBoundEntries(
    const QVariantList& encodedEntries,
    const PinloomHostIdentity& preferred)
{
    ++resolveGeneration;
    QList<PinloomHostEntry> entries;
    entries.reserve(encodedEntries.size());
    for (const QVariant& encoded : encodedEntries) {
        const PinloomHostEntry entry =
            PinloomHostEntry::fromVariantMap(encoded.toMap());
        if (entry.isValid())
            entries.append(entry);
    }
    int preferredRow = -1;
    {
        const QSignalBlocker blocker(results);
        results->clear();
        for (int row = 0; row < entries.size(); ++row) {
            const PinloomHostEntry& entry = entries.at(row);
            auto* item = new QListWidgetItem(entry.title, results);
            item->setData(Qt::UserRole, entry.toVariantMap());
            item->setToolTip(entryToolTip(entry));
            if (preferred.isValid()
                && entry.identity.entryId == preferred.entryId) {
                preferredRow = row;
            }
        }
    }
    setStatus(QStringLiteral("%1 linked Pinloom item(s)")
                  .arg(entries.size()));
    if (entries.isEmpty()) {
        selectedEntry = {};
        preferredIdentity = {};
        titleLabel->setText(QStringLiteral("Pinloom bindings"));
        detailsLabel->clear();
        technicalDetailsButton->setChecked(false);
        technicalDetailsButton->hide();
        technicalDetailsLabel->clear();
        technicalDetailsLabel->hide();
        openTargetButton->setEnabled(false);
        copyUriButton->setEnabled(false);
        showTextPreview(QStringLiteral(
            "No linked Pinloom item is available."));
        return;
    }
    results->setCurrentRow(preferredRow >= 0 ? preferredRow : 0);
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
    PinloomHostDocument loadingDocument;
    loadingDocument.entry = entry;
    detailsLabel->setText(summaryDetailsText(loadingDocument));
    technicalDetailsLabel->setText(
        technicalDetailsText(loadingDocument));
    technicalDetailsButton->show();
    technicalDetailsButton->setChecked(false);
    technicalDetailsLabel->hide();
    openTargetButton->setEnabled(true);
    openTargetButton->setText(QStringLiteral("Open"));
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
    showTextPreview(QStringLiteral("Loading..."));
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
        showTextPreview(error.isEmpty()
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
    detailsLabel->setText(summaryDetailsText(document));
    technicalDetailsLabel->setText(technicalDetailsText(document));
    technicalDetailsButton->show();
    technicalDetailsButton->setChecked(false);
    technicalDetailsLabel->hide();

    if (document.preview.present) {
        const PreviewImageLoadResult loaded =
            loadPreviewImage(document.preview);
        if (!loaded.error.isEmpty()) {
            showImageFailure(loaded.error);
        } else {
            imagePreview->setSourcePixmap(
                loaded.pixmap, document.preview.altText);
            previewStack->setCurrentWidget(imagePreview);
            openTargetButton->setText(
                QStringLiteral("Open original location"));
            openTargetButton->setToolTip(
                QStringLiteral("Open the original location through Pinloom"));
        }
    } else {
        openTargetButton->setText(QStringLiteral("Open"));
        openTargetButton->setToolTip(
            document.entry.type == QStringLiteral("clip")
                ? QStringLiteral("Insert this Saved Clip through Pinloom")
                : QStringLiteral("Open this Pinloom target"));
        showTextPreview(
            document.content.trimmed().isEmpty()
                ? QStringLiteral(
                    "This Pinloom entry has no embedded text content. "
                    "Use Open to jump to its authoritative target.")
                : document.content);
    }
    openTargetButton->setEnabled(true);
    copyUriButton->setEnabled(document.entry.uri.isValid());
    if (announceChange)
        emit currentEntryChanged(selectedEntry);
}

void PinloomContextView::showTextPreview(const QString& text)
{
    imagePreview->clearSourcePixmap();
    contentPreview->setPlainText(text);
    previewStack->setCurrentWidget(contentPreview);
}

void PinloomContextView::showImageFailure(const QString& reason)
{
    const QString failure = reason.trimmed().isEmpty()
        ? QStringLiteral("Pinloom image preview could not be rendered.")
        : reason.trimmed();
    showTextPreview(
        QStringLiteral("Image preview unavailable.\n\n%1").arg(failure));
    openTargetButton->setText(QStringLiteral("Open original location"));
    openTargetButton->setToolTip(
        QStringLiteral("Open the original location through Pinloom"));
    setStatus(failure);
}

void PinloomContextView::openImagePreview()
{
    if (!imagePreview || imagePreview->sourcePixmap().isNull())
        return;

    auto* dialog = new QDialog(this, Qt::Window);
    dialog->setObjectName(QStringLiteral("pinloomContextImageDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(titleLabel->text());
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    auto* scroll = new QScrollArea(dialog);
    scroll->setObjectName(QStringLiteral("pinloomContextImageScroll"));
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setWidgetResizable(false);
    auto* image = new QLabel(scroll);
    image->setObjectName(QStringLiteral("pinloomContextLargeImage"));
    image->setAlignment(Qt::AlignCenter);
    image->setPixmap(imagePreview->sourcePixmap());
    image->resize(imagePreview->sourcePixmap().size());
    image->setAccessibleName(imagePreview->accessibleName());
    scroll->setWidget(image);
    layout->addWidget(scroll);

    QSize available(1200, 800);
    if (QScreen* screen = QGuiApplication::screenAt(mapToGlobal(rect().center())))
        available = screen->availableGeometry().size();
    const QSize maximum(qMax(480, available.width() * 4 / 5),
                        qMax(320, available.height() * 4 / 5));
    QSize target = imagePreview->sourcePixmap().size() + QSize(40, 70);
    target = target.expandedTo(QSize(480, 320)).boundedTo(maximum);
    dialog->resize(target);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
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
