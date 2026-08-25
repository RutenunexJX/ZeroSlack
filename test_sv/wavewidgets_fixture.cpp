#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

namespace {
constexpr qint64 kMaximumJsonInteger = 9'007'199'254'740'991LL;

void writeFixtureError(char* destination,
                       const std::size_t capacity,
                       const QByteArray& message)
{
    if (!destination || capacity == 0)
        return;
    const auto count = std::min(
        capacity - 1, static_cast<std::size_t>(message.size()));
    std::memcpy(destination, message.constData(), count);
    destination[count] = '\0';
}

bool hasExactKeys(const QJsonObject& object,
                  const QSet<QString>& required,
                  const QSet<QString>& optional = {})
{
    for (const QString& key : required) {
        if (!object.contains(key))
            return false;
    }
    for (auto iterator = object.constBegin();
         iterator != object.constEnd(); ++iterator) {
        if (!required.contains(iterator.key())
            && !optional.contains(iterator.key())) {
            return false;
        }
    }
    return true;
}

bool jsonInteger(const QJsonValue& value, qint64* result)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < -static_cast<double>(kMaximumJsonInteger)
        || number > static_cast<double>(kMaximumJsonInteger)) {
        return false;
    }
    if (result)
        *result = static_cast<qint64>(number);
    return true;
}
}

class FixtureWaveWorkspace final : public QWidget
{
    Q_OBJECT

public:
    using QWidget::QWidget;

    Q_INVOKABLE bool canRevealSourceObject(
        const QString& semanticId,
        const QString& sourceFile,
        int sourceLine,
        int sourceColumn,
        const QString& symbolName,
        const QString& accessPath) const
    {
        Q_UNUSED(semanticId)
        Q_UNUSED(symbolName)
        Q_UNUSED(accessPath)
        return sourceFile == QStringLiteral("rtl/test.sv")
            && sourceLine == 7 && sourceColumn == 3;
    }

    Q_INVOKABLE bool revealSourceObject(
        const QString& semanticId,
        const QString& sourceFile,
        int sourceLine,
        int sourceColumn,
        const QString& symbolName,
        const QString& accessPath)
    {
        return canRevealSourceObject(
            semanticId, sourceFile, sourceLine, sourceColumn,
            symbolName, accessPath);
    }

signals:
    void sourceNavigationRequested(
        const QString& sourceFile,
        int sourceLine,
        int sourceColumn,
        const QString& semanticId,
        const QString& kind);
};

class FixtureWaveformView final : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qulonglong previewGeneration READ previewGeneration)
    Q_PROPERTY(QString previewMode READ previewMode)
    Q_PROPERTY(QString presentationState READ presentationState)
    Q_PROPERTY(QString lastError READ lastError)
    Q_PROPERTY(QString themeName READ themeName)
    Q_PROPERTY(bool compact READ compact)

public:
    explicit FixtureWaveformView(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("FixtureWaveformView"));
        setMinimumSize(160, 96);
    }

    qulonglong previewGeneration() const noexcept { return generation; }
    QString previewMode() const { return mode; }
    QString presentationState() const { return state; }
    QString lastError() const { return error; }
    QString themeName() const { return theme; }
    bool compact() const noexcept { return compactDensity; }

    Q_INVOKABLE bool replacePreviewPayload(const QByteArray& payload)
    {
        QString validationError;
        qulonglong nextGeneration = 0;
        QString nextMode;
        QHash<QString, SourceLocation> nextSources;
        if (!validatePayload(payload,
                             &nextGeneration,
                             &nextMode,
                             &nextSources,
                             &validationError)) {
            error = validationError;
            state = validationError.contains(
                        QStringLiteral("stale"), Qt::CaseInsensitive)
                ? QStringLiteral("stale")
                : QStringLiteral("failed");
            return false;
        }
        generation = nextGeneration;
        mode = nextMode;
        sources = std::move(nextSources);
        if (!sources.contains(selectedLane))
            selectedLane.clear();
        error.clear();
        state = QStringLiteral("ready");
        return true;
    }

    Q_INVOKABLE bool setPresentationState(const QString& nextState,
                                          const QString& message = {})
    {
        static const QSet<QString> supported{
            QStringLiteral("empty"),
            QStringLiteral("loading"),
            QStringLiteral("ready"),
            QStringLiteral("failed"),
            QStringLiteral("stale")};
        if (!supported.contains(nextState)) {
            error = QStringLiteral("Unsupported presentation state");
            return false;
        }
        state = nextState;
        statusMessage = message;
        return true;
    }

    Q_INVOKABLE bool setThemeName(const QString& nextTheme)
    {
        static const QSet<QString> supported{
            QStringLiteral("system"),
            QStringLiteral("light"),
            QStringLiteral("dark")};
        if (!supported.contains(nextTheme)) {
            error = QStringLiteral("Unsupported waveform theme");
            return false;
        }
        theme = nextTheme;
        return true;
    }

    Q_INVOKABLE void setCompact(bool nextCompact)
    {
        compactDensity = nextCompact;
    }

    Q_INVOKABLE void fitAll() {}
    Q_INVOKABLE void zoomIn() {}
    Q_INVOKABLE void zoomOut() {}

    Q_INVOKABLE bool selectLane(const QString& stableId)
    {
        if (!sources.contains(stableId))
            return false;
        selectedLane = stableId;
        return true;
    }

signals:
    void sourceNavigationRequested(const QString& sourceFile,
                                   int sourceLine,
                                   int sourceColumn,
                                   const QString& semanticId,
                                   const QString& laneId);

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if ((event->key() == Qt::Key_Return
             || event->key() == Qt::Key_Enter)
            && sources.contains(selectedLane)) {
            const SourceLocation source = sources.value(selectedLane);
            emit sourceNavigationRequested(source.file,
                                           source.line,
                                           source.column,
                                           source.semanticId,
                                           selectedLane);
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    struct SourceLocation {
        QString file;
        int line = 0;
        int column = 1;
        QString semanticId;
    };

    bool validatePayload(const QByteArray& payload,
                         qulonglong* nextGeneration,
                         QString* nextMode,
                         QHash<QString, SourceLocation>* nextSources,
                         QString* validationError)
    {
        const auto reject = [validationError](const QString& message) {
            if (validationError)
                *validationError = message;
            return false;
        };
        if (payload.isEmpty() || payload.size() > 8 * 1024 * 1024)
            return reject(QStringLiteral("Payload exceeds the 8 MiB limit"));
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
            payload, &parseError);
        if (parseError.error != QJsonParseError::NoError
            || !document.isObject()) {
            return reject(QStringLiteral("Malformed wave-preview/v1 JSON"));
        }
        const QJsonObject root = document.object();
        if (!hasExactKeys(
                root,
                {QStringLiteral("contract"),
                 QStringLiteral("generation"),
                 QStringLiteral("mode"),
                 QStringLiteral("timebase"),
                 QStringLiteral("lanes")})) {
            return reject(QStringLiteral("Malformed wave-preview/v1 root"));
        }
        if (root.value(QStringLiteral("contract")).toString()
            != QStringLiteral("wave-preview/v1")) {
            return reject(QStringLiteral("Unsupported preview contract"));
        }
        qint64 signedGeneration = 0;
        if (!jsonInteger(root.value(QStringLiteral("generation")),
                         &signedGeneration)
            || signedGeneration < 0) {
            return reject(QStringLiteral("Invalid preview generation"));
        }
        const auto candidateGeneration =
            static_cast<qulonglong>(signedGeneration);
        if (hasAcceptedPayload && candidateGeneration <= generation)
            return reject(QStringLiteral("Stale preview generation"));

        const QString candidateMode =
            root.value(QStringLiteral("mode")).toString();
        if (candidateMode != QStringLiteral("symbolic")
            && candidateMode != QStringLiteral("simulated")) {
            return reject(QStringLiteral("Unsupported preview mode"));
        }
        const QJsonObject timebase =
            root.value(QStringLiteral("timebase")).toObject();
        if (!hasExactKeys(
                timebase,
                {QStringLiteral("unit"),
                 QStringLiteral("start"),
                 QStringLiteral("end")})) {
            return reject(QStringLiteral("Malformed preview timebase"));
        }
        static const QSet<QString> units{
            QStringLiteral("tick"), QStringLiteral("fs"),
            QStringLiteral("ps"), QStringLiteral("ns"),
            QStringLiteral("us"), QStringLiteral("ms"),
            QStringLiteral("s")};
        qint64 timeStart = 0;
        qint64 timeEnd = 0;
        if (!units.contains(timebase.value(QStringLiteral("unit")).toString())
            || !jsonInteger(timebase.value(QStringLiteral("start")),
                            &timeStart)
            || !jsonInteger(timebase.value(QStringLiteral("end")), &timeEnd)
            || timeEnd <= timeStart) {
            return reject(QStringLiteral("Invalid preview timebase"));
        }

        const QJsonValue lanesValue = root.value(QStringLiteral("lanes"));
        if (!lanesValue.isArray() || lanesValue.toArray().size() > 512)
            return reject(QStringLiteral("Invalid preview lane array"));
        QSet<QString> laneIds;
        QHash<QString, SourceLocation> candidateSources;
        int segmentCount = 0;
        for (const QJsonValue& laneValue : lanesValue.toArray()) {
            if (!laneValue.isObject())
                return reject(QStringLiteral("Malformed preview lane"));
            const QJsonObject lane = laneValue.toObject();
            if (!hasExactKeys(
                    lane,
                    {QStringLiteral("id"), QStringLiteral("name"),
                     QStringLiteral("kind"), QStringLiteral("width"),
                     QStringLiteral("provenance"),
                     QStringLiteral("segments")},
                    {QStringLiteral("source")})) {
                return reject(QStringLiteral("Malformed preview lane"));
            }
            const QString id = lane.value(QStringLiteral("id")).toString();
            const QString name = lane.value(QStringLiteral("name")).toString();
            const QString kind = lane.value(QStringLiteral("kind")).toString();
            const QString provenance =
                lane.value(QStringLiteral("provenance")).toString();
            qint64 width = 0;
            if (id.isEmpty() || id.size() > 512 || laneIds.contains(id)
                || name.isEmpty() || name.size() > 512
                || (kind != QStringLiteral("bit")
                    && kind != QStringLiteral("bus")
                    && kind != QStringLiteral("clock"))
                || !jsonInteger(lane.value(QStringLiteral("width")), &width)
                || width < 1 || width > 65'536
                || ((kind == QStringLiteral("bit")
                     || kind == QStringLiteral("clock")) && width != 1)
                || provenance.isEmpty() || provenance.size() > 96
                || (candidateMode == QStringLiteral("symbolic")
                    && provenance != QStringLiteral("zeroslack-symbolic"))) {
                return reject(QStringLiteral("Invalid preview lane contract"));
            }
            laneIds.insert(id);

            if (lane.contains(QStringLiteral("source"))) {
                const QJsonObject source =
                    lane.value(QStringLiteral("source")).toObject();
                if (!hasExactKeys(
                        source,
                        {QStringLiteral("file"), QStringLiteral("line")},
                        {QStringLiteral("column"),
                         QStringLiteral("semanticId")})) {
                    return reject(QStringLiteral("Malformed lane source"));
                }
                const QString file = QDir::cleanPath(
                    QDir::fromNativeSeparators(
                        source.value(QStringLiteral("file")).toString()));
                qint64 line = 0;
                qint64 column = 1;
                if (file.isEmpty() || file == QLatin1String(".")
                    || file == QLatin1String("..")
                    || file.startsWith(QStringLiteral("../"))
                    || QDir::isAbsolutePath(file)
                    || !jsonInteger(source.value(QStringLiteral("line")),
                                    &line)
                    || line < 1 || line > 10'000'000
                    || (source.contains(QStringLiteral("column"))
                        && (!jsonInteger(
                                source.value(QStringLiteral("column")),
                                &column)
                            || column < 1 || column > 1'000'000))) {
                    return reject(QStringLiteral("Invalid lane source"));
                }
                candidateSources.insert(
                    id,
                    SourceLocation{
                        file,
                        static_cast<int>(line),
                        static_cast<int>(column),
                        source.value(QStringLiteral("semanticId")).toString()});
            }

            const QJsonValue segmentsValue =
                lane.value(QStringLiteral("segments"));
            if (!segmentsValue.isArray())
                return reject(QStringLiteral("Malformed lane segments"));
            qint64 previousEnd = timeStart;
            for (const QJsonValue& segmentValue : segmentsValue.toArray()) {
                ++segmentCount;
                if (segmentCount > 200'000 || !segmentValue.isObject())
                    return reject(QStringLiteral("Invalid segment count"));
                const QJsonObject segment = segmentValue.toObject();
                if (!hasExactKeys(
                        segment,
                        {QStringLiteral("start"), QStringLiteral("end"),
                         QStringLiteral("value")},
                        {QStringLiteral("unknown")})) {
                    return reject(QStringLiteral("Malformed lane segment"));
                }
                qint64 start = 0;
                qint64 end = 0;
                const QString value =
                    segment.value(QStringLiteral("value")).toString();
                if (!jsonInteger(segment.value(QStringLiteral("start")),
                                 &start)
                    || !jsonInteger(segment.value(QStringLiteral("end")), &end)
                    || start < timeStart || end > timeEnd || end <= start
                    || start < previousEnd || value.isEmpty()
                    || value.size() > 128
                    || (segment.contains(QStringLiteral("unknown"))
                        && !segment.value(QStringLiteral("unknown")).isBool())) {
                    return reject(QStringLiteral(
                        "Lane segments overlap or violate the timebase"));
                }
                previousEnd = end;
            }
        }
        if (nextGeneration)
            *nextGeneration = candidateGeneration;
        if (nextMode)
            *nextMode = candidateMode;
        if (nextSources)
            *nextSources = std::move(candidateSources);
        hasAcceptedPayload = true;
        return true;
    }

    qulonglong generation = 0;
    QString mode;
    QString state = QStringLiteral("empty");
    QString error;
    QString theme = QStringLiteral("system");
    QString statusMessage;
    bool compactDensity = false;
    bool hasAcceptedPayload = false;
    QHash<QString, SourceLocation> sources;
    QString selectedLane;
};

extern "C" Q_DECL_EXPORT int wavewidgets_abi_version() noexcept
{
    return 1;
}

extern "C" Q_DECL_EXPORT int wavewidgets_create_simulation_workspace_v1(
    const char* projectPathUtf8,
    QWidget* parent,
    QWidget** workspace,
    char* errorUtf8,
    const std::size_t errorCapacity) noexcept
{
    if (workspace) *workspace = nullptr;
    const QString projectPath = QString::fromUtf8(projectPathUtf8);
    if (!workspace || !QFileInfo::exists(projectPath)) {
        writeFixtureError(errorUtf8,
                          errorCapacity,
                          QByteArrayLiteral("fixture project is missing"));
        return 2;
    }
    auto* page = new FixtureWaveWorkspace(parent);
    page->setObjectName(QStringLiteral("FixtureWaveWorkspace"));
    page->setProperty(
        "wavewidgets.contract",
        QStringLiteral("wave-workbench.simulation-workspace/v1"));
    page->setProperty("wavewidgets.abiVersion", 1);
    page->setProperty(
        "wavewidgets.capabilities",
        QStringList{QStringLiteral("result-source-navigation/v1")});
    page->setProperty(
        "wavewidgets.projectPath", QFileInfo(projectPath).absoluteFilePath());
    *workspace = page;
    writeFixtureError(errorUtf8, errorCapacity, {});
    return 0;
}

extern "C" Q_DECL_EXPORT int wavewidgets_create_waveform_view_v1(
    QWidget* parent,
    QWidget** view,
    char* errorUtf8,
    const std::size_t errorCapacity) noexcept
{
    if (view)
        *view = nullptr;
    if (!view) {
        writeFixtureError(errorUtf8,
                          errorCapacity,
                          QByteArrayLiteral("fixture view output is null"));
        return 2;
    }
    auto* page = new FixtureWaveformView(parent);
    page->setProperty("wavewidgets.abiVersion", 1);
    page->setProperty(
        "wavewidgets.contract",
        QStringLiteral("wave-workbench.waveform-view/v1"));
    page->setProperty(
        "wavewidgets.previewContract",
        QStringLiteral("wave-preview/v1"));
    page->setProperty(
        "wavewidgets.capabilities",
        QStringList{
            QStringLiteral("wave-preview/v1"),
            QStringLiteral("generation-replace/v1"),
            QStringLiteral("waveform-theme/v1"),
            QStringLiteral("compact-density/v1"),
            QStringLiteral("source-navigation/v1")});
    *view = page;
    writeFixtureError(errorUtf8, errorCapacity, {});
    return 0;
}

extern "C" Q_DECL_EXPORT int wavewidgets_set_waveform_preview_v1(
    QWidget* view,
    const char* payloadUtf8,
    const std::size_t payloadSize,
    char* errorUtf8,
    const std::size_t errorCapacity) noexcept
{
    if (!view || !payloadUtf8 || payloadSize == 0
        || payloadSize > 8 * 1024 * 1024) {
        writeFixtureError(errorUtf8,
                          errorCapacity,
                          QByteArrayLiteral("invalid fixture payload buffer"));
        return 2;
    }
    auto* waveformView = qobject_cast<FixtureWaveformView*>(view);
    if (!waveformView
        || view->property("wavewidgets.contract").toString()
            != QStringLiteral("wave-workbench.waveform-view/v1")) {
        writeFixtureError(errorUtf8,
                          errorCapacity,
                          QByteArrayLiteral("widget contract mismatch"));
        return 3;
    }
    const QByteArray payload(
        payloadUtf8, static_cast<qsizetype>(payloadSize));
    if (!waveformView->replacePreviewPayload(payload)) {
        writeFixtureError(errorUtf8,
                          errorCapacity,
                          waveformView->lastError().toUtf8());
        return 4;
    }
    writeFixtureError(errorUtf8, errorCapacity, {});
    return 0;
}

#include "wavewidgets_fixture.moc"
