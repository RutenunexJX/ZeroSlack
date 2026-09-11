#include "waveformpreviewloader.h"

#include <QFileInfo>
#include <QLibrary>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QStringList>
#include <QVariant>
#include <QWidget>

#include <array>
#include <cstddef>
#include <utility>

namespace {
QString absolutePath(const QString& path)
{
    return QFileInfo(path).absoluteFilePath();
}

void setFailure(QString* destination, const QString& message)
{
    if (destination)
        *destination = message;
}

QString viewError(QWidget* view, const QString& fallback)
{
    if (!view)
        return fallback;
    const QString detail = view->property("lastError").toString().trimmed();
    return detail.isEmpty() ? fallback : detail;
}
}

WaveformPreviewLoader::WaveformPreviewLoader() = default;
WaveformPreviewLoader::~WaveformPreviewLoader() = default;

bool WaveformPreviewLoader::loadLibrary(
    const QString& libraryPath,
    QString* failureReason)
{
    setFailure(failureReason, QString());
    const QString requestedLibrary = absolutePath(libraryPath);
    if (!QFileInfo(requestedLibrary).isFile()) {
        setFailure(failureReason,
                   QStringLiteral("Wave widget library is missing: %1")
                       .arg(requestedLibrary));
        return false;
    }

    if (library && currentLibraryPath == requestedLibrary
        && abiVersionFunction && createViewFunction && setPreviewFunction) {
        return true;
    }

    auto candidate = std::make_unique<QLibrary>(requestedLibrary);
    candidate->setLoadHints(
        QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
    if (!candidate->load()) {
        setFailure(failureReason,
                   QStringLiteral("Cannot load Wave widgets: %1")
                       .arg(candidate->errorString()));
        return false;
    }

    const auto abiVersion = reinterpret_cast<AbiVersionFunction>(
        candidate->resolve("wavewidgets_abi_version"));
    const auto createView = reinterpret_cast<CreateViewFunction>(
        candidate->resolve("wavewidgets_create_waveform_view_v1"));
    const auto setPreview = reinterpret_cast<SetPreviewFunction>(
        candidate->resolve("wavewidgets_set_waveform_preview_v1"));
    if (!abiVersion || !createView || !setPreview) {
        setFailure(failureReason,
                   QStringLiteral(
                       "Wave widgets do not expose the waveform-view/v1 ABI."));
        return false;
    }
    const int abi = abiVersion();
    if (abi != kSupportedAbiVersion) {
        setFailure(failureReason,
                   QStringLiteral("Unsupported Wave widget ABI %1; expected %2.")
                       .arg(abi)
                       .arg(kSupportedAbiVersion));
        return false;
    }

    library = std::move(candidate);
    currentLibraryPath = requestedLibrary;
    abiVersionFunction = abiVersion;
    createViewFunction = createView;
    setPreviewFunction = setPreview;
    return true;
}

QWidget* WaveformPreviewLoader::createView(
    const QString& libraryPath,
    QWidget* parent,
    QString* failureReason)
{
    if (!loadLibrary(libraryPath, failureReason))
        return nullptr;

    QWidget* view = nullptr;
    std::array<char, 4096> error{};
    const int result = createViewFunction(
        parent, &view, error.data(), error.size());
    if (result != 0 || !view) {
        const QString detail = QString::fromUtf8(error.data()).trimmed();
        setFailure(failureReason,
                   detail.isEmpty()
                       ? QStringLiteral(
                             "Wave widgets rejected the waveform view request (%1).")
                             .arg(result)
                       : detail);
        return nullptr;
    }
    if (!validateView(view, failureReason)) {
        delete view;
        return nullptr;
    }
    const QMetaObject::Connection navigationConnection = QObject::connect(
        view,
        SIGNAL(sourceNavigationRequested(QString,int,int,QString,QString)),
        this,
        SLOT(handleSourceNavigationRequested(QString,int,int,QString,QString)),
        Qt::DirectConnection);
    if (!navigationConnection) {
        setFailure(failureReason,
                   QStringLiteral(
                       "Waveform view source-navigation connection failed."));
        delete view;
        return nullptr;
    }
    return view;
}

bool WaveformPreviewLoader::validateView(
    QWidget* view,
    QString* failureReason) const
{
    if (!view) {
        setFailure(failureReason, QStringLiteral("Waveform view is null."));
        return false;
    }
    if (view->property("wavewidgets.abiVersion").toInt()
            != kSupportedAbiVersion
        || view->property("wavewidgets.contract").toString()
            != QString::fromLatin1(kViewContract)
        || view->property("wavewidgets.previewContract").toString()
            != QString::fromLatin1(kPreviewContract)) {
        setFailure(failureReason,
                   QStringLiteral("Waveform view contract validation failed."));
        return false;
    }

    const QStringList capabilities =
        view->property("wavewidgets.capabilities").toStringList();
    const QStringList requiredCapabilities{
        QStringLiteral("wave-preview/v1"),
        QStringLiteral("generation-replace/v1"),
        QStringLiteral("waveform-theme/v1"),
        QStringLiteral("compact-density/v1"),
        QStringLiteral("source-navigation/v1")};
    for (const QString& capability : requiredCapabilities) {
        if (!capabilities.contains(capability)) {
            setFailure(failureReason,
                       QStringLiteral("Waveform view capability is missing: %1")
                           .arg(capability));
            return false;
        }
    }

    const QMetaObject* meta = view->metaObject();
    const QList<QByteArray> requiredMethods{
        QByteArrayLiteral("replacePreviewPayload(QByteArray)"),
        QByteArrayLiteral("setPresentationState(QString,QString)"),
        QByteArrayLiteral("setThemeName(QString)"),
        QByteArrayLiteral("setCompact(bool)"),
        QByteArrayLiteral("fitAll()"),
        QByteArrayLiteral("zoomIn()"),
        QByteArrayLiteral("zoomOut()")};
    for (const QByteArray& signature : requiredMethods) {
        if (meta->indexOfMethod(signature.constData()) < 0) {
            setFailure(failureReason,
                       QStringLiteral("Waveform view method is missing: %1")
                           .arg(QString::fromLatin1(signature)));
            return false;
        }
    }
    if (meta->indexOfSignal(
            "sourceNavigationRequested(QString,int,int,QString,QString)") < 0) {
        setFailure(failureReason,
                   QStringLiteral(
                       "Waveform view source-navigation signal is missing."));
        return false;
    }
    const QList<QByteArray> requiredProperties{
        QByteArrayLiteral("previewGeneration"),
        QByteArrayLiteral("previewMode"),
        QByteArrayLiteral("presentationState"),
        QByteArrayLiteral("lastError"),
        QByteArrayLiteral("themeName"),
        QByteArrayLiteral("compact")};
    for (const QByteArray& property : requiredProperties) {
        if (meta->indexOfProperty(property.constData()) < 0) {
            setFailure(failureReason,
                       QStringLiteral("Waveform view property is missing: %1")
                           .arg(QString::fromLatin1(property)));
            return false;
        }
    }
    setFailure(failureReason, QString());
    return true;
}

bool WaveformPreviewLoader::replacePreview(
    QWidget* view,
    const QByteArray& payload,
    QString* failureReason) const
{
    setFailure(failureReason, QString());
    if (!setPreviewFunction || !view || payload.isEmpty()) {
        setFailure(failureReason,
                   QStringLiteral("Waveform preview loader is not ready."));
        return false;
    }
    std::array<char, 4096> error{};
    const int result = setPreviewFunction(
        view,
        payload.constData(),
        static_cast<std::size_t>(payload.size()),
        error.data(),
        error.size());
    if (result == 0)
        return true;
    const QString detail = QString::fromUtf8(error.data()).trimmed();
    setFailure(failureReason,
               detail.isEmpty()
                   ? viewError(
                         view,
                         QStringLiteral("Waveform preview was rejected (%1).")
                             .arg(result))
                   : detail);
    return false;
}

bool WaveformPreviewLoader::setPresentationState(
    QWidget* view,
    const QString& state,
    const QString& message,
    QString* failureReason) const
{
    setFailure(failureReason, QString());
    if (!view) {
        setFailure(failureReason, QStringLiteral("Waveform view is unavailable."));
        return false;
    }
    bool accepted = false;
    const bool invoked = QMetaObject::invokeMethod(
        view,
        "setPresentationState",
        Qt::DirectConnection,
        Q_RETURN_ARG(bool, accepted),
        Q_ARG(QString, state),
        Q_ARG(QString, message));
    if (!invoked || !accepted) {
        setFailure(failureReason,
                   viewError(view,
                             QStringLiteral(
                                 "Waveform presentation state was rejected.")));
        return false;
    }
    return true;
}

bool WaveformPreviewLoader::invokeBoolean(
    QWidget* view,
    const char* method,
    const QString& value,
    QString* failureReason) const
{
    setFailure(failureReason, QString());
    if (!view) {
        setFailure(failureReason, QStringLiteral("Waveform view is unavailable."));
        return false;
    }
    bool accepted = false;
    const bool invoked = QMetaObject::invokeMethod(
        view,
        method,
        Qt::DirectConnection,
        Q_RETURN_ARG(bool, accepted),
        Q_ARG(QString, value));
    if (!invoked || !accepted) {
        setFailure(failureReason,
                   viewError(view,
                             QStringLiteral("Waveform view operation failed: %1")
                                 .arg(QString::fromLatin1(method))));
        return false;
    }
    return true;
}

bool WaveformPreviewLoader::setTheme(
    QWidget* view,
    const QString& theme,
    QString* failureReason) const
{
    return invokeBoolean(view, "setThemeName", theme, failureReason);
}

bool WaveformPreviewLoader::setCompact(
    QWidget* view,
    bool compact,
    QString* failureReason) const
{
    setFailure(failureReason, QString());
    if (!view) {
        setFailure(failureReason, QStringLiteral("Waveform view is unavailable."));
        return false;
    }
    const bool invoked = QMetaObject::invokeMethod(
        view,
        "setCompact",
        Qt::DirectConnection,
        Q_ARG(bool, compact));
    if (!invoked) {
        setFailure(failureReason,
                   QStringLiteral("Waveform compact-density operation failed."));
        return false;
    }
    return true;
}

bool WaveformPreviewLoader::invokeVoid(
    QWidget* view,
    const char* method,
    QString* failureReason) const
{
    setFailure(failureReason, QString());
    if (!view || !QMetaObject::invokeMethod(
            view, method, Qt::DirectConnection)) {
        setFailure(failureReason,
                   QStringLiteral("Waveform view operation failed: %1")
                       .arg(QString::fromLatin1(method)));
        return false;
    }
    return true;
}

bool WaveformPreviewLoader::fitAll(
    QWidget* view,
    QString* failureReason) const
{
    return invokeVoid(view, "fitAll", failureReason);
}

bool WaveformPreviewLoader::zoomIn(
    QWidget* view,
    QString* failureReason) const
{
    return invokeVoid(view, "zoomIn", failureReason);
}

bool WaveformPreviewLoader::zoomOut(
    QWidget* view,
    QString* failureReason) const
{
    return invokeVoid(view, "zoomOut", failureReason);
}

void WaveformPreviewLoader::setSourceNavigationHandler(
    std::function<void(const QString&, int, int,
                       const QString&, const QString&)> handler)
{
    sourceNavigationHandler = std::move(handler);
}

void WaveformPreviewLoader::handleSourceNavigationRequested(
    const QString& sourceFile,
    int sourceLine,
    int sourceColumn,
    const QString& semanticId,
    const QString& laneId)
{
    if (sourceNavigationHandler) {
        sourceNavigationHandler(sourceFile,
                                sourceLine,
                                sourceColumn,
                                semanticId,
                                laneId);
    }
}

QString WaveformPreviewLoader::loadedLibraryPath() const
{
    return currentLibraryPath;
}
