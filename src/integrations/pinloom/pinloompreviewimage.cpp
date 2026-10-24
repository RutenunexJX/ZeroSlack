#include "pinloompreviewimage.h"
#include "uicontrols.h"
#include "uidialogs.h"
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <atomic>
#include <optional>

PinloomPreviewImageLabel::PinloomPreviewImageLabel(QWidget* parent) : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    setMinimumSize(1, 1);
    setMargin(6);
}
void PinloomPreviewImageLabel::setSourcePixmap(const QPixmap& pixmap, const QString& text)
{
    source = pixmap;
    setAccessibleName(text.isEmpty() ? QStringLiteral("Pinloom image preview") : text);
    setToolTip(QStringLiteral("Click to open a larger preview"));
    setCursor(Qt::PointingHandCursor);
    updateScaledPixmap();
}
void PinloomPreviewImageLabel::clearSourcePixmap()
{
    source = {};
    pressed = false;
    clear();
    setAccessibleName({});
    setToolTip({});
    unsetCursor();
}
QPixmap PinloomPreviewImageLabel::sourcePixmap() const { return source; }
void PinloomPreviewImageLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    updateScaledPixmap();
}
void PinloomPreviewImageLabel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !source.isNull()) {
        pressed = true;
        event->accept();
        return;
    }
    QLabel::mousePressEvent(event);
}
void PinloomPreviewImageLabel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && pressed) {
        pressed = false;
        if (!source.isNull() && rect().contains(event->position().toPoint()) && activated) activated();
        event->accept();
        return;
    }
    QLabel::mouseReleaseEvent(event);
}
void PinloomPreviewImageLabel::updateScaledPixmap()
{
    if (source.isNull()) return;
    const QSize available = contentsRect().size() - QSize(2 * margin(), 2 * margin());
    if (available.width() > 0 && available.height() > 0)
        setPixmap(source.scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QString pinloomDocumentFileName(const PinloomHostDocument& document)
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

namespace {
constexpr qint64 kMaximumPreviewFileBytes = 64LL * 1024LL * 1024LL;
constexpr int kMaximumPreviewDimension = 16384;
constexpr qint64 kMaximumPreviewPixels = 64LL * 1024LL * 1024LL;
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

PinloomPreviewImageResult loadPreviewImage(const PinloomHostPreview& preview, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    if (cancelled->load()) return {};
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

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {{}, QStringLiteral("Pinloom preview file cannot be opened.")};
    if (file.size() > kMaximumPreviewFileBytes || (preview.byteSize >= 0 && file.size() != preview.byteSize))
        return {{}, QStringLiteral("Pinloom preview file changed. Retry to refresh it.")};
    if (cancelled->load()) return {};
    QImageReader reader(&file);
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
    if ((preview.pixelWidth > 0 && preview.pixelWidth != imageSize.width())
        || (preview.pixelHeight > 0 && preview.pixelHeight != imageSize.height()))
        return {{}, QStringLiteral("Pinloom preview dimensions changed. Retry to refresh it.")};
    if (cancelled->load()) return {};
    const QImage image = reader.read();
    if (cancelled->load()) return {};
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
    return {image, {}};
}
}

struct PinloomPreviewImageLoader::State {
    struct Request { PinloomHostPreview preview; Handler handler; };
    std::optional<Request> pending;
    QFutureWatcher<PinloomPreviewImageResult>* watcher = nullptr;
    std::shared_ptr<std::atomic_bool> cancelled;
};
PinloomPreviewImageLoader::PinloomPreviewImageLoader(QObject* parent)
    : QObject(parent), state(std::make_unique<State>()) {}
PinloomPreviewImageLoader::~PinloomPreviewImageLoader() { cancel(); }
bool PinloomPreviewImageLoader::cancel()
{
    const bool active = state->pending.has_value() || (state->cancelled && !state->cancelled->load());
    if (state->cancelled) state->cancelled->store(true);
    state->pending.reset();
    return active;
}
void PinloomPreviewImageLoader::load(const PinloomHostPreview& preview, Handler handler)
{
    cancel();
    state->pending = State::Request{preview, std::move(handler)};
    startPending();
}
void PinloomPreviewImageLoader::startPending()
{
    if (state->watcher || !state->pending) return;
    auto request = std::move(*state->pending);
    state->pending.reset();
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    state->cancelled = cancelled;
    auto* watcher = new QFutureWatcher<PinloomPreviewImageResult>(this);
    state->watcher = watcher;
    connect(watcher, &QFutureWatcher<PinloomPreviewImageResult>::finished, this,
            [this, watcher, cancelled, handler = std::move(request.handler)] {
        const auto result = watcher->result();
        state->watcher = nullptr;
        state->cancelled.reset();
        watcher->deleteLater();
        QPointer<PinloomPreviewImageLoader> guard(this);
        if (!cancelled->load() && handler) handler(result);
        if (guard) startPending();
    });
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(configured)
    watcher->setFuture(QtConcurrent::run(&pool, [preview = request.preview, cancelled] {
        return loadPreviewImage(preview, cancelled);
    }));
}

QDialog* openPinloomImagePreview(QWidget* owner, const QPixmap& pixmap, const QString& title, const QString& accessibleText)
{
    if (!owner || pixmap.isNull()) return nullptr;
    auto* dialog = new UiDialog(owner, Qt::Window);
    dialog->setObjectName(QStringLiteral("pinloomContextImageDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    auto* scroll = UiControls::scrollArea(dialog);
    scroll->setObjectName(QStringLiteral("pinloomContextImageScroll"));
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setWidgetResizable(false);
    auto* image = new QLabel(scroll);
    image->setObjectName(QStringLiteral("pinloomContextLargeImage"));
    image->setAlignment(Qt::AlignCenter);
    image->setPixmap(pixmap);
    image->resize(pixmap.size());
    image->setAccessibleName(accessibleText);
    scroll->setWidget(image);
    layout->addWidget(scroll);

    QSize available(1200, 800);
    if (QScreen* screen = QGuiApplication::screenAt(owner->mapToGlobal(owner->rect().center())))
        available = screen->availableGeometry().size();
    const QSize maximum(qMax(480, available.width() * 4 / 5),
                        qMax(320, available.height() * 4 / 5));
    QSize target = pixmap.size() + QSize(40, 70);
    target = target.expandedTo(QSize(480, 320)).boundedTo(maximum);
    dialog->resize(target);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    return dialog;
}
