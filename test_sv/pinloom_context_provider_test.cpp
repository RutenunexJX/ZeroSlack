#include "contextworkspacecontroller.h"
#include "contextdockhost.h"
#include "contextpeekhost.h"
#include "contextrail.h"
#include "pinloomcodelinkcoordinator.h"
#include "pinloomcontextprovider.h"
#include "pinloomcontextview.h"
#include "pinloomhostclient.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QDialog>
#include <QDockWidget>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QVector>

#include <iostream>
#include <utility>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

QJsonObject entryJson()
{
    return {
        {QStringLiteral("identity"),
         QJsonObject{{QStringLiteral("entryId"),
                      QStringLiteral("anchor:clock-reset")},
                     {QStringLiteral("resourceId"),
                      QStringLiteral("resource-clock")},
                     {QStringLiteral("anchorId"),
                      QStringLiteral("clock-reset")},
                     {QStringLiteral("clipId"), QString()}}},
        {QStringLiteral("uri"),
         QStringLiteral(
             "pinloom://entry/anchor:clock-reset?resource=resource-clock&anchor=clock-reset")},
        {QStringLiteral("type"), QStringLiteral("anchor")},
        {QStringLiteral("title"), QStringLiteral("Clock reset note")},
        {QStringLiteral("aliases"), QJsonArray{QStringLiteral("cdc")}},
        {QStringLiteral("tags"), QJsonArray{QStringLiteral("rtl")}},
        {QStringLiteral("summary"), QStringLiteral("clock.md")},
        {QStringLiteral("location"), QStringLiteral("E:/notes/clock.md")},
        {QStringLiteral("matchedField"), QStringLiteral("anchor_name")},
        {QStringLiteral("matchSummary"), QStringLiteral("Match: anchor_name")},
        {QStringLiteral("pinned"), true},
        {QStringLiteral("deleted"), false},
        {QStringLiteral("metadata"), QJsonObject{}},
    };
}

QJsonObject rectangleEntryJson(const QString& suffix,
                               const QString& title)
{
    QJsonObject entry = entryJson();
    const QString entryId = QStringLiteral("anchor:pdf-%1").arg(suffix);
    const QString resourceId = QStringLiteral("resource-pdf-%1").arg(suffix);
    const QString anchorId = QStringLiteral("pdf-%1").arg(suffix);
    entry.insert(
        QStringLiteral("identity"),
        QJsonObject{{QStringLiteral("entryId"), entryId},
                    {QStringLiteral("resourceId"), resourceId},
                    {QStringLiteral("anchorId"), anchorId},
                    {QStringLiteral("clipId"), QString()}});
    entry.insert(
        QStringLiteral("uri"),
        QStringLiteral("pinloom://entry/%1?resource=%2&anchor=%3")
            .arg(entryId, resourceId, anchorId));
    entry.insert(QStringLiteral("title"), title);
    entry.insert(QStringLiteral("aliases"),
                 QJsonArray{QStringLiteral("reset figure")});
    entry.insert(QStringLiteral("tags"),
                 QJsonArray{QStringLiteral("timing")});
    entry.insert(QStringLiteral("summary"), QStringLiteral("clock.pdf"));
    entry.insert(QStringLiteral("location"),
                 QStringLiteral("E:/docs/clock.pdf"));
    return entry;
}

QJsonObject readyPreviewJson(const QString& filePath,
                             const QSize& size,
                             int page)
{
    return {{QStringLiteral("kind"), QStringLiteral("image")},
            {QStringLiteral("state"), QStringLiteral("ready")},
            {QStringLiteral("mimeType"), QStringLiteral("image/png")},
            {QStringLiteral("uri"),
             QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded)},
            {QStringLiteral("filePath"),
             QFileInfo(filePath).absoluteFilePath()},
            {QStringLiteral("byteSize"), QFileInfo(filePath).size()},
            {QStringLiteral("pixelWidth"), size.width()},
            {QStringLiteral("pixelHeight"), size.height()},
            {QStringLiteral("page"), page},
            {QStringLiteral("cropped"), true},
            {QStringLiteral("altText"),
             QStringLiteral("Reset crossing | clock.pdf | Page %1").arg(page)}};
}

QJsonObject unavailablePreviewJson(const QString& error, int page)
{
    return {{QStringLiteral("kind"), QStringLiteral("image")},
            {QStringLiteral("state"), QStringLiteral("unavailable")},
            {QStringLiteral("page"), page},
            {QStringLiteral("cropped"), false},
            {QStringLiteral("error"), error}};
}

QJsonObject rectangleDocumentJson(const QJsonObject& entry,
                                  const QJsonObject& preview,
                                  int page)
{
    const QString anchorId = entry.value(QStringLiteral("identity"))
                                 .toObject()
                                 .value(QStringLiteral("anchorId"))
                                 .toString();
    return {{QStringLiteral("entry"), entry},
            {QStringLiteral("content"),
             QStringLiteral("Fallback PDF anchor text")},
            {QStringLiteral("contentType"),
             QStringLiteral("text/plain")},
            {QStringLiteral("details"),
             QJsonObject{
                 {QStringLiteral("title"),
                  entry.value(QStringLiteral("title"))},
                 {QStringLiteral("fileName"), QStringLiteral("clock.pdf")},
                 {QStringLiteral("page"), page},
                 {QStringLiteral("aliases"),
                  entry.value(QStringLiteral("aliases"))},
                 {QStringLiteral("tags"),
                  entry.value(QStringLiteral("tags"))},
                 {QStringLiteral("anchorId"), anchorId},
                 {QStringLiteral("locatorType"),
                  QStringLiteral("sumatrapdf.rect")},
                 {QStringLiteral("locatorJson"),
                  QStringLiteral(
                      "{\"type\":\"sumatrapdf.rect\",\"page\":%1,\"rect\":[72,144,252,252]}")
                      .arg(page)}}},
            {QStringLiteral("preview"), preview}};
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    const PinloomHostIdentity parsedIdentity =
        PinloomHostIdentity::fromUri(
            QUrl(entryJson().value(QStringLiteral("uri")).toString()));
    check(parsedIdentity.entryId == QStringLiteral("anchor:clock-reset")
              && parsedIdentity.resourceId == QStringLiteral("resource-clock")
              && parsedIdentity.anchorId == QStringLiteral("clock-reset")
              && parsedIdentity.toUri().scheme() == QStringLiteral("pinloom"),
          "stable Pinloom URI round-trips without cached content");

    QTemporaryDir previewDirectory;
    check(previewDirectory.isValid(),
          "image preview tests have a private temporary directory");
    const QString mainPreviewPath =
        previewDirectory.filePath(QStringLiteral("main-preview.png"));
    QImage mainPreviewImage(640, 320, QImage::Format_ARGB32_Premultiplied);
    mainPreviewImage.fill(QColor(QStringLiteral("#c08a16")));
    check(mainPreviewImage.save(mainPreviewPath, "PNG"),
          "main PNG preview fixture is created");
    const QString oldPreviewPath =
        previewDirectory.filePath(QStringLiteral("old-preview.png"));
    QImage oldPreviewImage(320, 160, QImage::Format_ARGB32_Premultiplied);
    oldPreviewImage.fill(QColor(QStringLiteral("#d02020")));
    check(oldPreviewImage.save(oldPreviewPath, "PNG"),
          "old-generation PNG preview fixture is created");
    const QString latestPreviewPath =
        previewDirectory.filePath(QStringLiteral("latest-preview.png"));
    QImage latestPreviewImage(300, 300, QImage::Format_ARGB32_Premultiplied);
    latestPreviewImage.fill(QColor(QStringLiteral("#2050d0")));
    check(latestPreviewImage.save(latestPreviewPath, "PNG"),
          "latest-generation PNG preview fixture is created");

    const QJsonObject readyRectangleEntry =
        rectangleEntryJson(QStringLiteral("ready"),
                           QStringLiteral("Reset crossing diagram"));
    const PinloomHostDocument parsedReadyDocument =
        PinloomHostDocument::fromJson(rectangleDocumentJson(
            readyRectangleEntry,
            readyPreviewJson(mainPreviewPath, QSize(640, 320), 4),
            4));
    check(parsedReadyDocument.preview.present
              && parsedReadyDocument.preview.isImage()
              && parsedReadyDocument.preview.isReady()
              && parsedReadyDocument.preview.mimeType
                     == QStringLiteral("image/png")
              && parsedReadyDocument.preview.uri.isLocalFile()
              && parsedReadyDocument.preview.filePath
                     == QFileInfo(mainPreviewPath).absoluteFilePath()
              && parsedReadyDocument.preview.byteSize > 0
              && parsedReadyDocument.preview.pixelWidth == 640
              && parsedReadyDocument.preview.pixelHeight == 320
              && parsedReadyDocument.preview.page == 4
              && parsedReadyDocument.preview.cropped,
          "host document parser accepts the complete ready image descriptor");

    const QString rendererUnavailable =
        QStringLiteral("PDF preview renderer executable was not found");
    const PinloomHostDocument parsedUnavailableDocument =
        PinloomHostDocument::fromJson(rectangleDocumentJson(
            readyRectangleEntry,
            unavailablePreviewJson(rendererUnavailable, 4),
            4));
    check(parsedUnavailableDocument.preview.present
              && parsedUnavailableDocument.preview.isImage()
              && !parsedUnavailableDocument.preview.isReady()
              && parsedUnavailableDocument.preview.state
                     == QStringLiteral("unavailable")
              && parsedUnavailableDocument.preview.error
                     == rendererUnavailable,
          "host document parser preserves an unavailable image reason");

    QJsonObject compatiblePreview{
        {QStringLiteral("state"), QStringLiteral("ready")},
        {QStringLiteral("filePath"),
         QFileInfo(mainPreviewPath).absoluteFilePath()},
    };
    const PinloomHostPreview parsedCompatiblePreview =
        PinloomHostPreview::fromJson(compatiblePreview);
    check(parsedCompatiblePreview.isReady()
              && parsedCompatiblePreview.kind
                     == QStringLiteral("image")
              && parsedCompatiblePreview.mimeType.isEmpty(),
          "preview parser tolerates omitted optional ready fields");

    QJsonObject remotePreview =
        readyPreviewJson(mainPreviewPath, QSize(640, 320), 4);
    remotePreview.insert(
        QStringLiteral("uri"),
        QStringLiteral("https://example.invalid/preview.png"));
    check(!PinloomHostPreview::fromJson(remotePreview)
               .validationError.isEmpty(),
          "preview parser rejects non-local image URIs");
    QJsonObject oversizedPreview =
        readyPreviewJson(mainPreviewPath, QSize(640, 320), 4);
    oversizedPreview.insert(QStringLiteral("altText"),
                            QString(17 * 1024, QLatin1Char('x')));
    check(PinloomHostPreview::fromJson(oversizedPreview)
              .validationError.contains(QStringLiteral("16 KiB")),
          "preview parser enforces the 16 KiB descriptor limit");
    QJsonObject oversizedDimensions =
        readyPreviewJson(mainPreviewPath, QSize(640, 320), 4);
    oversizedDimensions.insert(QStringLiteral("pixelWidth"), 20000);
    check(!PinloomHostPreview::fromJson(oversizedDimensions)
               .validationError.isEmpty(),
          "preview parser rejects unsafe declared dimensions");

    int searchRequests = 0;
    int resolveRequests = 0;
    int openRequests = 0;
    int createRequests = 0;
    PinloomHostClient client(
        [&searchRequests,
         &resolveRequests,
         &openRequests,
         &createRequests](
            const QJsonObject& request,
            PinloomHostClient::RawReplyHandler reply) {
            const QString method =
                request.value(QStringLiteral("method")).toString();
            if (method == QStringLiteral("search")) {
                ++searchRequests;
                reply({{QStringLiteral("entries"),
                        QJsonArray{entryJson()}}}, {});
                return;
            }
            if (method == QStringLiteral("resolve")) {
                ++resolveRequests;
                reply({{QStringLiteral("entry"), entryJson()},
                       {QStringLiteral("content"),
                        QStringLiteral("Reset deassertion is synchronized.")},
                       {QStringLiteral("contentType"),
                        QStringLiteral("text/plain")},
                       {QStringLiteral("details"),
                        QJsonObject{{QStringLiteral("locatorType"),
                                     QStringLiteral("text.line")}}}},
                      {});
                return;
            }
            if (method == QStringLiteral("open")) {
                ++openRequests;
                reply({{QStringLiteral("message"),
                        QStringLiteral("Opened anchor")}}, {});
                return;
            }
            if (method == QStringLiteral("createSourceAnchor")) {
                ++createRequests;
                check(request.value(QStringLiteral("params"))
                          .toObject()
                          .value(QStringLiteral("content"))
                          .toString()
                          == QStringLiteral("always_ff @(posedge clk)"),
                      "createSourceAnchor maps the selected source text to the host content field");
                reply({{QStringLiteral("entry"), entryJson()}}, {});
                return;
            }
            reply({}, QStringLiteral("unexpected method"));
        });
    client.setExecutablePath(QStringLiteral("   "));
    check(client.executablePath().isEmpty(),
          "empty Pinloom executable setting keeps automatic discovery active");

    QMainWindow window;
    auto* editorRegion = new QWidget(&window);
    window.setCentralWidget(editorRegion);
    window.resize(1000, 700);
    window.show();
    QApplication::processEvents();

    ContextWorkspaceController controller(&window, editorRegion, &window);
    int linkCalls = 0;
    auto provider = std::make_unique<PinloomContextProvider>(&client);
    provider->setLinkHandler(
        [&linkCalls](const QVariantMap& source,
                     const PinloomHostEntry& entry,
                     QString* failureReason) {
            if (failureReason)
                failureReason->clear();
            ++linkCalls;
            return source.value(QStringLiteral("selectedText")).toString()
                       == QStringLiteral("always_ff @(posedge clk)")
                && entry.identity.anchorId
                       == QStringLiteral("clock-reset");
        });
    check(controller.registerProvider(std::move(provider)),
          "Pinloom provider registers in Context Workspace");
    check(controller.providerIds().contains(QStringLiteral("pinloom"))
              && controller.rail()->entryIds().contains(
                     QStringLiteral("pinloom")),
          "Pinloom provider exposes a Context Rail entry");

    controller.setWorkspaceRoot(QStringLiteral("workspace-a"));
    QString failureReason;
    QAction* pinloomAction = window.findChild<QAction*>(
        QStringLiteral("contextRail.pinloom"));
    if (pinloomAction)
        pinloomAction->trigger();
    QApplication::processEvents();
    check(pinloomAction && controller.peekHost()->hasResource(),
          "Pinloom rail activation opens its provider resource in Peek");
    auto* view = qobject_cast<PinloomContextView*>(
        controller.peekHost()->view());
    check(view && searchRequests == 1
              && view->resultList()->count() == 1,
          "opening Pinloom performs unified search through the host bridge");

    view->resultList()->setCurrentRow(0);
    QApplication::processEvents();
    check(resolveRequests >= 1
              && view->currentEntry().identity.anchorId
                     == QStringLiteral("clock-reset")
              && view->previewEditor()->toPlainText().contains(
                     QStringLiteral("synchronized")),
          "selecting a result resolves authoritative Pinloom content");
    check(controller.peekHost()->resource().resourceId
              == QStringLiteral("library")
              && controller.peekHost()->resource().uri.scheme()
                     == QStringLiteral("pinloom"),
          "selection updates the portable Pinloom URI without duplicating content");

    const ContextResource linkedResource =
        PinloomContextProvider::resourceForUri(
            view->currentEntry().uri,
            QStringLiteral("workspace-a"));
    check(linkedResource.isValid()
              && linkedResource.state
                     .value(QStringLiteral("identity"))
                     .toMap()
                     .value(QStringLiteral("anchorId"))
                     .toString()
                     == QStringLiteral("clock-reset"),
          "a code-held Pinloom URI reconstructs an openable context resource");

    view->copyLinkButton()->click();
    check(QApplication::clipboard()->text().startsWith(
              QStringLiteral("pinloom://entry/")),
          "Copy Link exposes the stable Pinloom URI");
    view->openButton()->click();
    check(openRequests == 1
              && view->statusText() == QStringLiteral("Opened anchor"),
          "Open delegates to Pinloom's authoritative primary action");

    int imageOpenRequests = 0;
    PinloomHostClient imageClient(
        [&readyRectangleEntry,
         &mainPreviewPath,
         &imageOpenRequests](
            const QJsonObject& request,
            PinloomHostClient::RawReplyHandler reply) {
            const QString method =
                request.value(QStringLiteral("method")).toString();
            if (method == QStringLiteral("resolve")) {
                reply(rectangleDocumentJson(
                          readyRectangleEntry,
                          readyPreviewJson(
                              mainPreviewPath, QSize(640, 320), 4),
                          4),
                      {});
                return;
            }
            if (method == QStringLiteral("open")) {
                ++imageOpenRequests;
                reply({{QStringLiteral("message"),
                        QStringLiteral("Opened original PDF")}}, {});
                return;
            }
            reply({}, QStringLiteral("unexpected image preview method"));
        });
    PinloomContextView imageView(&imageClient);
    imageView.resize(360, 700);
    imageView.restoreState(
        {{QStringLiteral("boundEntries"),
          QVariantList{PinloomHostEntry::fromJson(readyRectangleEntry)
                           .toVariantMap()}}});
    imageView.show();
    QApplication::processEvents();

    QLabel* imageLabel = imageView.imagePreviewLabel();
    QLabel* imageSummary = imageView.findChild<QLabel*>(
        QStringLiteral("pinloomContextDetails"));
    QLabel* technicalDetails = imageView.findChild<QLabel*>(
        QStringLiteral("pinloomContextTechnicalDetails"));
    QToolButton* technicalToggle = imageView.technicalDetailsToggle();
    const QPixmap narrowPixmap = imageLabel
        ? imageLabel->pixmap(Qt::ReturnByValue)
        : QPixmap();
    check(imageLabel && imageLabel->isVisible()
              && !narrowPixmap.isNull(),
          "sumatrapdf.rect displays its cropped PNG in the side-view main area");
    check(imageSummary
              && imageSummary->text().contains(QStringLiteral("clock.pdf"))
              && imageSummary->text().contains(QStringLiteral("Page 4"))
              && imageSummary->text().contains(QStringLiteral("reset figure"))
              && imageSummary->text().contains(QStringLiteral("timing"))
              && !imageSummary->text().contains(QStringLiteral("anchorId"))
              && !imageSummary->text().contains(QStringLiteral("locatorJson")),
          "default image metadata is limited to file, page, aliases, and tags");
    check(technicalToggle && !technicalToggle->isChecked()
              && technicalDetails && !technicalDetails->isVisible()
              && technicalDetails->text().contains(
                     QStringLiteral("anchorId"))
              && technicalDetails->text().contains(
                     QStringLiteral("locatorJson")),
          "technical identity and locator fields are collapsed by default");
    if (technicalToggle) {
        technicalToggle->click();
        QApplication::processEvents();
    }
    check(technicalDetails && technicalDetails->isVisible(),
          "technical details can be expanded explicitly");
    if (technicalToggle)
        technicalToggle->click();

    imageView.resize(760, 700);
    QApplication::processEvents();
    const QPixmap widePixmap = imageLabel
        ? imageLabel->pixmap(Qt::ReturnByValue)
        : QPixmap();
    check(!narrowPixmap.isNull() && !widePixmap.isNull()
              && widePixmap.width() > narrowPixmap.width()
              && qAbs(widePixmap.width() * 320
                      - widePixmap.height() * 640) <= 640,
          "image preview scales with side-view width while preserving aspect ratio");
    if (imageLabel)
        QTest::mouseClick(imageLabel, Qt::LeftButton);
    QApplication::processEvents();
    QDialog* imageDialog = imageView.findChild<QDialog*>(
        QStringLiteral("pinloomContextImageDialog"));
    check(imageDialog && imageDialog->isVisible()
              && imageDialog->findChild<QLabel*>(
                     QStringLiteral("pinloomContextLargeImage")),
          "clicking the scaled image opens a larger preview");
    if (imageDialog)
        imageDialog->close();
    QApplication::processEvents();

    const QJsonObject unavailableRectangleEntry =
        rectangleEntryJson(QStringLiteral("unavailable"),
                           QStringLiteral("Unavailable PDF crop"));
    int unavailableImageOpenRequests = 0;
    PinloomHostClient unavailableImageClient(
        [&unavailableRectangleEntry,
         &rendererUnavailable,
         &unavailableImageOpenRequests](
            const QJsonObject& request,
            PinloomHostClient::RawReplyHandler reply) {
            const QString method =
                request.value(QStringLiteral("method")).toString();
            if (method == QStringLiteral("resolve")) {
                reply(rectangleDocumentJson(
                          unavailableRectangleEntry,
                          unavailablePreviewJson(rendererUnavailable, 5),
                          5),
                      {});
                return;
            }
            if (method == QStringLiteral("open")) {
                ++unavailableImageOpenRequests;
                reply({{QStringLiteral("message"),
                        QStringLiteral("Opened unavailable preview target")}},
                      {});
                return;
            }
            reply({}, QStringLiteral("unexpected unavailable preview method"));
        });
    PinloomContextView unavailableImageView(&unavailableImageClient);
    unavailableImageView.resize(480, 600);
    unavailableImageView.restoreState(
        {{QStringLiteral("boundEntries"),
          QVariantList{PinloomHostEntry::fromJson(unavailableRectangleEntry)
                           .toVariantMap()}}});
    unavailableImageView.show();
    QApplication::processEvents();
    check(unavailableImageView.previewEditor()->isVisible()
              && unavailableImageView.previewEditor()->toPlainText().contains(
                     rendererUnavailable)
              && unavailableImageView.statusText() == rendererUnavailable
              && unavailableImageView.openButton()->text()
                     == QStringLiteral("Open original location"),
          "image rendering failure shows its concrete reason and original-location action");
    unavailableImageView.openButton()->click();
    check(unavailableImageOpenRequests == 1,
          "image failure original-location action delegates to Pinloom open");

    QJsonObject clipEntry = entryJson();
    clipEntry.insert(QStringLiteral("type"), QStringLiteral("clip"));
    clipEntry.insert(QStringLiteral("title"), QStringLiteral("Saved reset clip"));
    clipEntry.insert(
        QStringLiteral("identity"),
        QJsonObject{{QStringLiteral("entryId"), QStringLiteral("clip:reset")},
                    {QStringLiteral("resourceId"), QString()},
                    {QStringLiteral("anchorId"), QString()},
                    {QStringLiteral("clipId"), QStringLiteral("reset")}});
    clipEntry.insert(
        QStringLiteral("uri"),
        QStringLiteral("pinloom://entry/clip:reset?clip=reset"));
    PinloomHostClient clipClient(
        [&clipEntry](const QJsonObject& request,
                     PinloomHostClient::RawReplyHandler reply) {
            if (request.value(QStringLiteral("method")).toString()
                == QStringLiteral("resolve")) {
                reply({{QStringLiteral("entry"), clipEntry},
                       {QStringLiteral("content"),
                        QStringLiteral("Saved clip text remains selectable.")},
                       {QStringLiteral("contentType"),
                        QStringLiteral("text/plain")},
                       {QStringLiteral("details"), QJsonObject{}}},
                      {});
                return;
            }
            reply({}, QStringLiteral("unexpected clip method"));
        });
    PinloomContextView clipView(&clipClient);
    clipView.resize(480, 600);
    clipView.restoreState(
        {{QStringLiteral("boundEntries"),
          QVariantList{PinloomHostEntry::fromJson(clipEntry).toVariantMap()}}});
    clipView.show();
    QApplication::processEvents();
    check(clipView.previewEditor()->isVisible()
              && clipView.previewEditor()->toPlainText().contains(
                     QStringLiteral("selectable"))
              && !clipView.imagePreviewLabel()->isVisible(),
          "Saved Clip and text-only anchors continue to use the text preview");

    struct PendingImageResolve {
        QString entryId;
        PinloomHostClient::RawReplyHandler reply;
    };
    QVector<PendingImageResolve> pendingImageResolves;
    PinloomHostClient delayedImageClient(
        [&pendingImageResolves](
            const QJsonObject& request,
            PinloomHostClient::RawReplyHandler reply) {
            if (request.value(QStringLiteral("method")).toString()
                != QStringLiteral("resolve")) {
                reply({}, QStringLiteral("unexpected delayed image method"));
                return;
            }
            const QString entryId =
                request.value(QStringLiteral("params")).toObject()
                    .value(QStringLiteral("identity")).toObject()
                    .value(QStringLiteral("entryId")).toString();
            pendingImageResolves.append(
                PendingImageResolve{entryId, std::move(reply)});
        });
    const QJsonObject oldRectangleEntry =
        rectangleEntryJson(QStringLiteral("old"),
                           QStringLiteral("Old rectangle"));
    const QJsonObject latestRectangleEntry =
        rectangleEntryJson(QStringLiteral("latest"),
                           QStringLiteral("Latest rectangle"));
    PinloomContextView delayedImageView(&delayedImageClient);
    delayedImageView.resize(560, 650);
    delayedImageView.restoreState(
        {{QStringLiteral("boundEntries"),
          QVariantList{PinloomHostEntry::fromJson(oldRectangleEntry)
                           .toVariantMap(),
                       PinloomHostEntry::fromJson(latestRectangleEntry)
                           .toVariantMap()}}});
    delayedImageView.show();
    QApplication::processEvents();
    check(pendingImageResolves.size() == 1,
          "first binding starts one image resolve generation");
    delayedImageView.resultList()->setCurrentRow(1);
    QApplication::processEvents();
    check(pendingImageResolves.size() == 2,
          "switching bindings starts a new image resolve generation");
    if (pendingImageResolves.size() == 2) {
        pendingImageResolves[1].reply(
            rectangleDocumentJson(
                latestRectangleEntry,
                readyPreviewJson(latestPreviewPath, QSize(300, 300), 7),
                7),
            {});
        QApplication::processEvents();
        const QPixmap latestPixmap =
            delayedImageView.imagePreviewLabel()->pixmap(Qt::ReturnByValue);
        const QColor latestColor = latestPixmap.isNull()
            ? QColor()
            : latestPixmap.toImage().pixelColor(
                  latestPixmap.width() / 2, latestPixmap.height() / 2);
        check(delayedImageView.currentEntry().identity.entryId
                  == QStringLiteral("anchor:pdf-latest")
                  && latestColor.blue() > latestColor.red(),
              "new binding response installs the matching image preview");

        pendingImageResolves[0].reply(
            rectangleDocumentJson(
                oldRectangleEntry,
                readyPreviewJson(oldPreviewPath, QSize(320, 160), 6),
                6),
            {});
        QApplication::processEvents();
        const QPixmap retainedPixmap =
            delayedImageView.imagePreviewLabel()->pixmap(Qt::ReturnByValue);
        const QColor retainedColor = retainedPixmap.isNull()
            ? QColor()
            : retainedPixmap.toImage().pixelColor(
                  retainedPixmap.width() / 2, retainedPixmap.height() / 2);
        check(delayedImageView.currentEntry().identity.entryId
                  == QStringLiteral("anchor:pdf-latest")
                  && delayedImageView.findChild<QLabel*>(
                         QStringLiteral("pinloomContextTitle"))->text()
                         == QStringLiteral("Latest rectangle")
                  && retainedColor.blue() > retainedColor.red(),
              "stale asynchronous resolve cannot overwrite the newer binding preview");
    }
    imageView.close();
    unavailableImageView.close();
    clipView.close();
    delayedImageView.close();

    const QVariantMap linkSource{
        {QStringLiteral("anchorKind"), QStringLiteral("always")},
        {QStringLiteral("logicalKey"),
         QStringLiteral("syntax|always|top|always_ff|always_ff@(posedgeclk)")},
        {QStringLiteral("structuralFingerprint"),
         QStringLiteral("process-fingerprint")},
        {QStringLiteral("syntaxKind"), QStringLiteral("always_ff")},
        {QStringLiteral("workspaceRoot"),
         QStringLiteral("workspace-a")},
        {QStringLiteral("selectedText"),
         QStringLiteral("always_ff @(posedge clk)")},
        {QStringLiteral("relativeFilePath"),
         QStringLiteral("rtl/top.sv")},
        {QStringLiteral("absoluteFilePath"),
         QStringLiteral("workspace-a/rtl/top.sv")},
        {QStringLiteral("selectedTextHash"),
         QStringLiteral("source-hash")},
        {QStringLiteral("startPosition"), 0},
        {QStringLiteral("endPosition"), 24},
        {QStringLiteral("startLine"), 12},
        {QStringLiteral("startColumn"), 1},
        {QStringLiteral("endLine"), 12},
        {QStringLiteral("endColumn"), 25},
        {QStringLiteral("suggestedTitle"),
         QStringLiteral("Clocked process")},
    };

    PinloomCodeLinkAnchorRecord bindingAnchor;
    bindingAnchor.id = QStringLiteral("source-anchor");
    bindingAnchor.source =
        PinloomSourceSelection::fromVariantMap(linkSource);
    bindingAnchor.source.anchorId = bindingAnchor.id;
    bindingAnchor.createdAtUtc = QStringLiteral("2026-01-01T00:00:00Z");
    PinloomCodeLinkRecord firstBinding;
    firstBinding.id = QStringLiteral("binding-one");
    firstBinding.title = QStringLiteral("Clock reset note");
    firstBinding.uri = QUrl(
        entryJson().value(QStringLiteral("uri")).toString());
    firstBinding.identity = entryJson()
        .value(QStringLiteral("identity")).toObject().toVariantMap();
    firstBinding.source = bindingAnchor.source;
    firstBinding.createdAtUtc = bindingAnchor.createdAtUtc;
    PinloomCodeLinkRecord secondBinding = firstBinding;
    secondBinding.id = QStringLiteral("binding-two");
    secondBinding.title = QStringLiteral("Clock reset review");
    bindingAnchor.links = {firstBinding, secondBinding};
    const ContextResource bindingsResource =
        PinloomContextProvider::resourceForBindings(
            bindingAnchor, QStringLiteral("workspace-a"));
    check(bindingsResource.isValid()
              && bindingsResource.resourceId
                     == QStringLiteral("bindings/source-anchor"),
          "one code anchor produces a grouped multi-target resource");
    check(controller.openResource(
              bindingsResource,
              ContextOpenMode::TransientDock,
              &failureReason),
          "a binding marker opens grouped Pinloom content in the real side dock");
    auto* boundView = qobject_cast<PinloomContextView*>(
        controller.dockHost()->viewForResource(
            bindingsResource.stableKey()));
    check(controller.dockWidget()->isVisible()
              && boundView && boundView->boundModeActive()
              && boundView->resultList()->count() == 2,
          "the transient side dock lists every Pinloom target for the anchor");
    check(controller.captureState().pinnedResources.isEmpty(),
          "transient binding content is excluded from workspace persistence");
    check(controller.openResource(
              bindingsResource,
              ContextOpenMode::TransientDock,
              &failureReason)
              && !controller.dockHost()->containsResource(
                  bindingsResource.stableKey()),
          "activating the same binding marker closes its transient side dock tab");

    PinloomCodeLinkCoordinator codeLinks(&controller);
    codeLinks.setWorkspaceRoot(QStringLiteral("workspace-a"));
    ActionInvocation linkInvocation;
    linkInvocation.parameters.insert(
        QStringLiteral("linkSource"), linkSource);
    const ActionDescriptor* linkDescriptor = findActionById(
        QString::fromLatin1(ActionIds::PinloomLinkSelection));
    const ActionExecutionResult linkResult = linkDescriptor
        ? codeLinks.execute(*linkDescriptor, linkInvocation)
        : ActionExecutionResult{};
    check(linkResult.succeeded && view->linkModeActive(),
          "the registered source-link route opens Pinloom in explicit link mode");
    auto* attachButton = view->findChild<QToolButton*>(
        QStringLiteral("pinloomContextAttachEntry"));
    check(attachButton && attachButton->isEnabled(),
          "link mode can attach the selected existing Pinloom entry");
    attachButton->click();
    check(linkCalls == 1 && !view->linkModeActive(),
          "attaching an existing entry persists the code link and closes link mode");

    const ActionExecutionResult secondLinkResult = linkDescriptor
        ? codeLinks.execute(*linkDescriptor, linkInvocation)
        : ActionExecutionResult{};
    check(secondLinkResult.succeeded && view->linkModeActive(),
          "link mode can be reopened for another source anchor");
    auto* createButton = view->findChild<QToolButton*>(
        QStringLiteral("pinloomContextCreateAnchor"));
    check(createButton && createButton->isEnabled(),
          "link mode exposes source-anchor creation");
    createButton->click();
    check(createRequests == 1 && linkCalls == 2
              && !view->linkModeActive(),
          "creating a Pinloom source anchor immediately persists its code link");

    check(controller.pinPeek(&failureReason),
          "resolved Pinloom view can be pinned without recreation");
    const ContextWorkspaceState state = controller.captureState();
    check(state.pinnedResources.size() == 1
              && !state.pinnedResources.constFirst()
                      .value(QStringLiteral("state")).toMap()
                      .contains(QStringLiteral("content")),
          "workspace persistence stores identity and query but no Pinloom content");

    controller.clearResources();
    controller.setWorkspaceRoot(QStringLiteral("workspace-a"));
    const ContextWorkspaceRestoreResult restored =
        controller.restoreState(state);
    check(restored.restoredResources == 1
              && controller.dockHost()->resourceCount() == 1,
          "pinned Pinloom identity restores through the provider");

    PinloomHostClient unavailable(
        [](const QJsonObject&,
           PinloomHostClient::RawReplyHandler reply) {
            reply({}, QStringLiteral("Pinloom unavailable"));
        });
    PinloomContextView unavailableView(&unavailable);
    unavailableView.restoreState({});
    check(unavailableView.statusText()
              == QStringLiteral("Pinloom unavailable"),
          "unavailable Pinloom state is explicit and non-fatal");

    QVector<PinloomHostClient::RawReplyHandler> delayedReplies;
    PinloomHostClient delayed(
        [&delayedReplies](
            const QJsonObject&,
            PinloomHostClient::RawReplyHandler reply) {
            delayedReplies.append(std::move(reply));
        });
    auto* closingView = new PinloomContextView(&delayed);
    closingView->restoreState({});
    check(delayedReplies.size() == 1,
          "Pinloom view can have an in-flight search");
    delete closingView;
    delayedReplies.takeFirst()(
        {{QStringLiteral("entries"), QJsonArray{entryJson()}}}, {});
    check(true,
          "closing Pinloom view invalidates in-flight callbacks safely");

    if (failures == 0) {
        std::cout << "pinloom_context_provider_test: "
                  << checks << " checks passed\n";
        return 0;
    }
    std::cerr << "pinloom_context_provider_test: "
              << failures << " of " << checks
              << " checks failed\n";
    return 1;
}
