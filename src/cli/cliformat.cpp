#include "formatterservice.h"
#include "structuredwhitespaceformatter.h"
#include "zeroslackcli.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <zeroslack/documents/documentfileread.h>
#ifdef ZEROSLACK_CLI_AUTOMATION
#include "automationservice.h"
#include "documenttemplatemodel.h"
#include "formattercursoranchor.h"
#include "workspacesessionstateservice.h"
#include <QDir>
#include <QTextDocument>
#ifdef ZEROSLACK_AUTOMATION_TRANSPORT
#include "automationhost.h"
#endif
#endif

namespace
{
QString outcomeName(FormatterOutcome outcome)
{
    switch (outcome)
    {
    case FormatterOutcome::Applied:
        return QStringLiteral("Applied");
    case FormatterOutcome::Unchanged:
        return QStringLiteral("Unchanged");
    case FormatterOutcome::ConservativeFallback:
        return QStringLiteral("ConservativeFallback");
    case FormatterOutcome::Rejected:
        return QStringLiteral("Rejected");
    }
    return QStringLiteral("Rejected");
}
} // namespace

ZeroSlackCliResult ZeroSlackCliService::formatFile(const ZeroSlackCliRequest &request) const
{
    ZeroSlackCliResult result;
    const QString path = QFileInfo(request.filePath).absoluteFilePath();
    QJsonObject data{{"file", path},          {"changed", false}, {"written", false},
                     {"outcome", "Rejected"}, {"diagnostic", ""}, {"formattedLines", 0}};
    result.envelope = {{"schema", "zeroslack.cli/v1"},
                       {"schemaVersion", kSchemaVersion},
                       {"command", "format"},
                       {"ok", true},
                       {"appVersion", QCoreApplication::applicationVersion()},
                       {"generatedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    const auto finish = [&](int exitCode, const QString &code = {}, const QString &message = {})
    {
        result.exitCode = exitCode;
        if (!message.isEmpty())
        {
            result.envelope.insert("ok", false);
            result.envelope.insert("error", QJsonObject{{"code", code}, {"message", message}});
            const auto diagnostic = data.value("diagnostic").toString();
            data.insert("diagnostic", diagnostic.isEmpty() ? message : diagnostic + ' ' + message);
        }
        result.envelope.insert("data", data);
        result.rendered = render(result.envelope, request.format);
        return result;
    };
    if (request.filePath.isEmpty() || (request.formatWrite && request.formatCheck))
        return finish(2, "invalid_arguments", "Specify one file and at most one of --check or --write.");
    const QFileInfo identity(path);
    if (!identity.isFile() || identity.isSymLink() || identity.isJunction())
        return finish(2, "read_error", "The input must be an existing regular file, without a file link.");
#if defined(ZEROSLACK_CLI_AUTOMATION) && defined(ZEROSLACK_AUTOMATION_TRANSPORT)
    const auto conflict = ZeroSlackAutomationHost::offlineConflict({{"command", "document.format"},
        {"workspace", QFileInfo(path).absolutePath()}, {"file", path}});
    if (!conflict.isEmpty()) return finish(2, conflict.value("error").toObject().value("code").toString(),
        conflict.value("error").toObject().value("message").toString());
#endif
    const auto original = readDocumentFile(path);
    if (!original.available)
        return finish(2, "read_error", original.failureReason);
    if (original.text.contains(QChar::Null) ||
        encodeDocumentText(original.text, original.format) != original.rawBytes)
        return finish(2, "unsupported_encoding",
                      "The input cannot be decoded and re-encoded losslessly; no file was written.");
    if (observationHook)
        observationHook(ObservationStage::FormatCaptured);
    const auto report = FormatterService::getInstance()->formatDocument(original.text);
    data.insert("changed", report.changed);
    data.insert("outcome", outcomeName(report.outcome));
    data.insert("diagnostic", report.diagnostic);
    data.insert("formattedLines", report.formattedLines);
    if (!report.accepted() ||
        !StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(original.text, report.formattedText))
    {
        data.insert("outcome", "Rejected");
        return finish(2, "format_rejected",
                      "The formatter could not preserve immutable tokens, comments and strings.");
    }
#ifdef ZEROSLACK_CLI_AUTOMATION
    QString metadataError;
    const auto metadata = WorkspaceSessionStateService().loadDocumentTemplates(path, &metadataError);
    QTextDocument preview; preview.setPlainText(original.text);
    auto* templates = DocumentTemplateModel::get(&preview);
    if (!metadataError.isEmpty() || (!metadata.isEmpty() && !templates->restore(metadata, path)))
        return finish(2, "metadata_conflict", metadataError.isEmpty() ? templates->issue() : metadataError);
    FormatterTriviaPositionMapper mapper(original.text, report.formattedText);
    if (!templates->applyFormatting(report.formattedText, mapper, &metadataError))
        return finish(2, "metadata_conflict", metadataError);
    data.insert("templateCount", templates->instances().size());
    data.insert("text", report.formattedText);
#endif
    const auto unchangedSource = [&]
    {
        const QFileInfo currentIdentity(path);
        const auto current = readDocumentFile(path);
        return !currentIdentity.isSymLink() && !currentIdentity.isJunction() &&
               currentIdentity.canonicalFilePath() == identity.canonicalFilePath() && current.available &&
               current.rawBytes == original.rawBytes && current.modifiedUtc == original.modifiedUtc;
    };
    if (!unchangedSource())
        return finish(2, "source_changed",
                      "The file changed during formatting; retry with the current file.");
    if (!report.changed)
        return finish(0);
    if (!request.formatWrite)
        return finish(1);
    if (original.readOnly)
        return finish(2, "write_error", "The input file is read-only.");
#ifdef ZEROSLACK_CLI_AUTOMATION
    if (observationHook) observationHook(ObservationStage::BeforeFormatWrite);
    if (!unchangedSource()) return finish(2, "source_changed", "The file changed before atomic replacement; no formatting was written.");
    if (!WorkspaceSessionStateService().saveDocumentWithTemplates(path, encodeDocumentText(report.formattedText, original.format),
            ZeroSlackAutomationService::digest(original.rawBytes), templates->serialize(path), &metadataError))
        return finish(2, "write_error", metadataError);
    data.insert("written", true);
    return finish(0);
#else
    QLockFile lock(path + QStringLiteral(".zeroslack-format.lock"));
    if (!lock.tryLock(0))
        return finish(2, "write_error", "Another formatter is writing this file.");
    const auto bytes = encodeDocumentText(report.formattedText, original.format);
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size())
        return finish(2, "write_error", output.errorString());
    if (observationHook)
        observationHook(ObservationStage::BeforeFormatWrite);
    if (!unchangedSource())
    {
        output.cancelWriting();
        return finish(2, "source_changed",
                      "The file changed before atomic replacement; no formatting was written.");
    }
    if (!output.commit())
        return finish(2, "write_error", output.errorString());
    data.insert("written", true);
    return finish(0);
#endif
}
