#include "pdfdocumentpage.h"
#include "webeditorcontainer.h"
#include "uicontrols.h"
#include "roundedicons.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <cmath>

PdfDocumentPage::PdfDocumentPage(QWidget* parent, const QString& restoredViewId) : QWidget(parent), viewId(restoredViewId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : restoredViewId) {
    setObjectName(QStringLiteral("pdfDocumentPage"));
    setProperty("documentViewId", viewId); setProperty("editorViewId", viewId);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto button = [this](RoundedIcons::Kind icon, const QString& tooltip) {
        auto* value = UiControls::toolButton(this); value->setIcon(RoundedIcons::icon(icon)); value->setToolTip(tooltip); return value;
    };
    auto* navigation = new QHBoxLayout; navigation->setContentsMargins(4, 3, 4, 2); navigation->setSpacing(2);
    auto* previous = button(RoundedIcons::Left, QStringLiteral("Previous Page"));
    auto* next = button(RoundedIcons::Right, QStringLiteral("Next Page"));
    auto* original = button(RoundedIcons::OpenProject, QStringLiteral("Open Original"));
    pageNumber = UiControls::spinBox(this); pageNumber->setObjectName(QStringLiteral("pdfPageNumber")); pageNumber->setRange(1, 1); pageNumber->setMinimumWidth(48); pageNumber->setToolTip(QStringLiteral("Page"));
    pageTotal = UiControls::label(QStringLiteral("/ —"), this);
    navigation->addWidget(previous); navigation->addWidget(pageNumber); navigation->addWidget(pageTotal); navigation->addWidget(next); navigation->addStretch(); navigation->addWidget(original);
    layout->addLayout(navigation);
    auto* reading = new QHBoxLayout; reading->setContentsMargins(4, 0, 4, 3); reading->setSpacing(2);
    zoom = UiControls::comboBox(this); zoom->setObjectName(QStringLiteral("pdfZoom")); zoom->setMinimumWidth(0); zoom->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); zoom->setMinimumContentsLength(4);
    zoom->addItem(QStringLiteral("Fit Width"), QStringLiteral("page-width")); zoom->addItem(QStringLiteral("Fit Page"), QStringLiteral("page-fit"));
    for (int percent : {50, 75, 100, 125, 150, 200, 300}) zoom->addItem(QString::number(percent) + QLatin1Char('%'), QString::number(percent / 100.0));
    search = UiControls::lineEdit(this); search->setObjectName(QStringLiteral("pdfSearch")); search->setPlaceholderText(QStringLiteral("Find in PDF")); search->setMinimumWidth(30);
    auto* findPrevious = button(RoundedIcons::Up, QStringLiteral("Previous Match"));
    auto* findNext = button(RoundedIcons::Down, QStringLiteral("Next Match"));
    reading->addWidget(zoom); reading->addWidget(search, 1); reading->addWidget(findPrevious); reading->addWidget(findNext); layout->addLayout(reading);
    auto* noticeRow = new QHBoxLayout; noticeRow->setContentsMargins(5, 0, 5, 0);
    notice = UiControls::label(QStringLiteral("Loading PDF…"), this); notice->setWordWrap(true);
    reload = button(RoundedIcons::Refresh, QStringLiteral("Reload PDF")); reload->hide(); noticeRow->addWidget(notice, 1); noticeRow->addWidget(reload); layout->addLayout(noticeRow);
    const QString profileRoot = qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT");
    const QString profile = (profileRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) : profileRoot) + QStringLiteral("/document-webview");
    web = new WebEditorContainer(QCoreApplication::applicationDirPath() + QStringLiteral("/resources/document-editors"), profile, QStringLiteral("pdf"), this);
    layout->addWidget(web, 1); setFocusProxy(web);
    rangeTimer = new QTimer(this); rangeTimer->setSingleShot(true);
    startupTimer = new QTimer(this); startupTimer->setSingleShot(true); startupTimer->setInterval(30000);
    watcher = new QFileSystemWatcher(this);
    connect(rangeTimer, &QTimer::timeout, this, &PdfDocumentPage::pumpRange);
    connect(startupTimer, &QTimer::timeout, this, [this] { invalidate(QStringLiteral("The PDF reader did not finish loading. Reload to retry.")); });
    connect(watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& changed) { if (sourceAvailable && changed == path && !sourceUnchanged()) invalidate(QStringLiteral("The PDF changed on disk. Reload to read the current file.")); });
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this] { if (sourceAvailable && !sourceUnchanged()) invalidate(QStringLiteral("The PDF was replaced or removed. Reload to retry.")); });
    connect(web, &WebEditorContainer::messageReceived, this, &PdfDocumentPage::receive);
    connect(web, &WebEditorContainer::initializationFailed, this, &PdfDocumentPage::invalidate);
    connect(web, &WebEditorContainer::activated, this, &PdfDocumentPage::activated);
    connect(web, &WebEditorContainer::commandRequested, this, &PdfDocumentPage::executeCommand);
    connect(web, &WebEditorContainer::externalLinkRequested, this, [](const QUrl& url) { QDesktopServices::openUrl(url); });
    connect(previous, &QToolButton::clicked, this, [this] { setPage(pageNumber->value() - 1); });
    connect(next, &QToolButton::clicked, this, [this] { setPage(pageNumber->value() + 1); });
    connect(pageNumber, &QSpinBox::valueChanged, this, &PdfDocumentPage::setPage);
    connect(zoom, &QComboBox::activated, this, [this](int index) { setScale(zoom->itemData(index).toString()); });
    connect(search, &QLineEdit::returnPressed, this, [this] { findText(search->text()); });
    connect(findPrevious, &QToolButton::clicked, this, [this] { findText(search->text(), true); });
    connect(findNext, &QToolButton::clicked, this, [this] { findText(search->text()); });
    connect(original, &QToolButton::clicked, this, [this] { if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    connect(reload, &QToolButton::clicked, this, [this] { openFile(path); });
}
PdfDocumentPage::~PdfDocumentPage() { releaseView(); }
bool PdfDocumentPage::supportsFile(const QString& path) { return QFileInfo(path).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0; }
QString PdfDocumentPage::title() const { return QFileInfo(path).fileName(); }
bool PdfDocumentPage::openFile(const QString& value, QString* error) {
    if (released) return false;
    if (!watcher->files().isEmpty()) watcher->removePaths(watcher->files());
    if (!watcher->directories().isEmpty()) watcher->removePaths(watcher->directories());
    rangeTimer->stop(); ranges.clear(); sourceAvailable = false;
    if (!session.isEmpty()) web->postMessage({{"type", "stale"}, {"session", session}});
    session = QUuid::createUuid().toString(QUuid::WithoutBraces); path = QFileInfo(value).absoluteFilePath();
    initialized = false; pages = renderedPage = matches = 0; renderedPages.clear(); readBytes = 0; failure.clear(); reload->hide();
    QFileInfo info(path); physicalPath = info.canonicalFilePath(); size = info.size(); modified = info.lastModified();
    QFile probe(physicalPath);
    if (!info.isFile() || size <= 0 || size > 9007199254740991LL || !probe.open(QIODevice::ReadOnly)) {
        invalidate(QStringLiteral("Cannot read this PDF file.")); if (error) *error = failure; return false;
    }
    probe.close(); sourceAvailable = true;
    watcher->addPath(path); watcher->addPath(info.absolutePath()); notice->setText(QStringLiteral("Loading PDF…")); notice->show();
    startupTimer->start(); if (hello) initialize(); emit statusChanged(); return true;
}
bool PdfDocumentPage::sourceUnchanged() const {
    const QFileInfo info(path);
    return sourceAvailable && info.isFile() && info.canonicalFilePath() == physicalPath && info.size() == size && info.lastModified() == modified;
}
QByteArray PdfDocumentPage::readRange(qint64 begin, qint64 count) {
    if (!sourceUnchanged()) { invalidate(QStringLiteral("The PDF changed while it was being read.")); return {}; }
    QFile source(physicalPath);
    if (!source.open(QIODevice::ReadOnly) || !source.seek(begin)) { invalidate(source.errorString()); return {}; }
    const auto bytes = source.read(count); source.close(); readBytes += bytes.size();
    if (!sourceUnchanged()) { invalidate(QStringLiteral("The PDF changed while it was being read.")); return {}; }
    if (bytes.size() != count) { invalidate(QStringLiteral("Cannot read the requested PDF data.")); return {}; }
    return bytes;
}
void PdfDocumentPage::initialize() {
    const auto initial = readRange(0, qMin<qint64>(size, 65536));
    if (initial.isEmpty()) return;
    web->postMessage({{"type", "open"}, {"session", session}, {"size", static_cast<double>(size)}, {"initial", QString::fromLatin1(initial.toBase64())}, {"name", title()}, {"state", state}, {"dark", palette().color(QPalette::Window).lightness() < 128}});
}
void PdfDocumentPage::receive(const QJsonObject& value) {
    if (released) return;
    const auto type = value.value("type").toString();
    if (type == QStringLiteral("hello")) { hello = true; if (sourceAvailable) initialize(); return; }
    if (value.value("session").toString() != session || !sourceAvailable) return;
    if (type == QStringLiteral("range")) {
        const double begin = value.value("begin").toDouble(-1), end = value.value("end").toDouble(-1);
        const QString request = value.value("request").toString();
        if (!std::isfinite(begin) || !std::isfinite(end) || std::floor(begin) != begin || std::floor(end) != end || begin < 0 || end <= begin || end > size || request.isEmpty() || request.size() > 80 || ranges.size() >= 64) { invalidate(QStringLiteral("The PDF reader requested an invalid file range.")); return; }
        ranges.enqueue({request, static_cast<qint64>(begin), static_cast<qint64>(end)}); if (!rangeTimer->isActive()) rangeTimer->start(0);
    } else if (type == QStringLiteral("ready")) {
        pages = value.value("pages").toInt(); if (pages < 1) { invalidate(QStringLiteral("This PDF has no readable pages.")); return; }
        initialized = true; startupTimer->stop(); notice->hide(); pageNumber->setMaximum(pages); pageTotal->setText(QStringLiteral("/ %1").arg(pages)); emit statusChanged();
    } else if (type == QStringLiteral("state")) {
        state = value.value("state").toObject(); const QSignalBlocker blocker(pageNumber), zoomBlocker(zoom); pageNumber->setValue(state.value("page").toInt(1));
        const auto scale = state.value("scale");
        const QString key = scale.isString() ? scale.toString() : QString::number(scale.toDouble() / 100.0);
        int index = zoom->findData(key);
        if (index < 0 && scale.isDouble() && scale.toDouble() > 0) {
            index = zoom->property("customZoomIndex").isValid() ? zoom->property("customZoomIndex").toInt() : zoom->count();
            if (index == zoom->count()) zoom->addItem(QString(), key);
            zoom->setItemText(index, QString::number(scale.toDouble(), 'g', 4) + QLatin1Char('%')); zoom->setItemData(index, key); zoom->setProperty("customZoomIndex", index);
        }
        if (index >= 0) zoom->setCurrentIndex(index);
        emit viewStateChanged();
    } else if (type == QStringLiteral("rendered")) {
        renderedPage = value.value("page").toInt();
        if (renderedPage > 0 && renderedPage <= pages) renderedPages.insert(renderedPage);
        emit statusChanged();
    }
    else if (type == QStringLiteral("matches")) { matches = value.value("total").toInt(); search->setToolTip(QStringLiteral("%1 matches").arg(matches)); emit statusChanged(); }
    else if (type == QStringLiteral("copy")) QApplication::clipboard()->setText(value.value("text").toString());
    else if (type == QStringLiteral("error")) invalidate(value.value("message").toString());
}
void PdfDocumentPage::pumpRange() {
    if (released || ranges.isEmpty()) return;
    if (!sourceUnchanged()) { invalidate(QStringLiteral("The PDF changed while it was being read.")); return; }
    auto& range = ranges.head(); const auto count = qMin<qint64>(262144, range.end - range.position);
    const auto bytes = readRange(range.position, count);
    if (bytes.isEmpty()) return;
    web->postMessage({{"type", "range"}, {"session", session}, {"request", range.request}, {"begin", static_cast<double>(range.position)}, {"data", QString::fromLatin1(bytes.toBase64())}});
    range.position += count; if (range.position == range.end) ranges.dequeue(); if (!ranges.isEmpty()) rangeTimer->start(0);
}
void PdfDocumentPage::invalidate(const QString& reason) {
    initialized = false; failure = reason; rangeTimer->stop(); ranges.clear(); startupTimer->stop(); sourceAvailable = false;
    if (!session.isEmpty()) web->postMessage({{"type", "stale"}, {"session", session}});
    notice->setText(reason); notice->show(); reload->show(); emit statusChanged();
}
void PdfDocumentPage::command(QJsonObject value) { if (!initialized) return; value.insert("type", "command"); value.insert("session", session); web->postMessage(value); }
void PdfDocumentPage::executeCommand(const QString& value) {
    if (value == QStringLiteral("new") || value == QStringLiteral("open")) emit fileCommandRequested(value);
    else if (value == QStringLiteral("find")) { search->setFocus(); search->selectAll(); }
    else if (value == QStringLiteral("copy") || value == QStringLiteral("selectAll")) command({{"command", value}});
}
void PdfDocumentPage::setPage(int value) { command({{"command", "page"}, {"page", qBound(1, value, qMax(1, pages))}}); }
void PdfDocumentPage::setScale(const QString& value) { command({{"command", "scale"}, {"scale", value}}); }
void PdfDocumentPage::findText(const QString& value, bool previous) { search->setText(value); command({{"command", "find"}, {"query", value}, {"previous", previous}}); }
void PdfDocumentPage::restoreViewState(const QJsonObject& value) { state = value; command({{"command", "restore"}, {"state", state}}); }
void PdfDocumentPage::releaseView() { if (released) return; released = true; rangeTimer->stop(); startupTimer->stop(); ranges.clear(); sourceAvailable = false; web->shutdown(); }
void PdfDocumentPage::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && initialized) web->postMessage({{"type", "theme"}, {"session", session}, {"dark", palette().color(QPalette::Window).lightness() < 128}});
}
