#include "zeroslackcli.h"

#include "version.h"

#include <QCoreApplication>
#include <QSet>
#include <QJsonDocument>
#include <QTextStream>

#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif
#ifdef ZEROSLACK_CLI_AUTOMATION
#include "cliautomation.h"
#include <QApplication>
#endif

namespace {

bool takeValue(const QStringList& arguments,
               int* index,
               QString* value,
               QString* failureReason)
{
    if (!index || !value || *index + 1 >= arguments.size()) {
        if (failureReason)
            *failureReason = QStringLiteral("Option requires a value.");
        return false;
    }
    *value = arguments.at(++(*index));
    return true;
}

bool takeInteger(const QStringList& arguments,
                 int* index,
                 int* value,
                 QString* failureReason)
{
    QString text;
    if (!takeValue(arguments, index, &text, failureReason))
        return false;
    bool ok = false;
    const int parsed = text.toInt(&ok);
    if (!ok) {
        if (failureReason)
            *failureReason = QStringLiteral("Option value must be an integer.");
        return false;
    }
    *value = parsed;
    return true;
}

bool parseArguments(const QStringList& arguments,
                    ZeroSlackCliRequest* request,
                    QString* failureReason)
{
    if (request && !arguments.isEmpty()) request->command = arguments.first().trimmed().toLower();
    if (!request || arguments.size() < 2 || arguments.at(1).startsWith(QStringLiteral("--"))) {
        if (failureReason)
            *failureReason = QStringLiteral("A command and its workspace or explicit file are required.");
        return false;
    }
    request->command = arguments.at(0).trimmed().toLower();
    if (request->command == QStringLiteral("format")) request->filePath = arguments.at(1);
    else request->workspaceRoot = arguments.at(1);
    QStringList positional;
    bool includeSpecified = false;
    for (int index = 2; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (request->command == QStringLiteral("format")
            && argument != "--format" && argument != "--write" && argument != "--check") {
            *failureReason = QStringLiteral("format accepts only one file, --check or --write, and --format.");
            return false;
        }
        if (argument == "--write" || argument == "--check") {
            if (request->command != "format" || request->formatWrite || request->formatCheck) {
                *failureReason = QStringLiteral("--write and --check are mutually exclusive and only valid for format.");
                return false;
            }
            request->formatWrite = argument == "--write";
            request->formatCheck = argument == "--check";
        } else if (argument == QStringLiteral("--format")) {
            if (!takeValue(arguments, &index, &request->format, failureReason))
                return false;
        } else if (argument == QStringLiteral("--cache-dir")) {
            if (!takeValue(arguments, &index, &request->cacheDirectory,
                           failureReason)) {
                return false;
            }
        } else if (argument == QStringLiteral("--file")) {
            if (!takeValue(arguments, &index, &request->filePath,
                           failureReason)) {
                return false;
            }
        } else if (argument == QStringLiteral("--symbol")) {
            if (!takeValue(arguments, &index, &request->symbol,
                           failureReason)) {
                return false;
            }
        } else if (argument == QStringLiteral("--query")) {
            if (!takeValue(arguments, &index, &request->query,
                           failureReason)) {
                return false;
            }
        } else if (argument == QStringLiteral("--base")) {
            if (!takeValue(arguments, &index, &request->baseRef,
                           failureReason)) {
                return false;
            }
        } else if (argument == QStringLiteral("--line")) {
            if (!takeInteger(arguments, &index, &request->line,
                             failureReason)) {
                return false;
            }
            request->lineSpecified = true;
        } else if (argument == QStringLiteral("--depth")) {
            if (!takeInteger(arguments, &index, &request->depth,
                             failureReason)) {
                return false;
            }
        } else if (argument == QStringLiteral("--max-tokens")) {
            if (!takeInteger(arguments, &index, &request->maxTokens,
                             failureReason)) {
                return false;
            }
        } else if (argument == QStringLiteral("--include")) {
            includeSpecified = true;
            QString value;
            if (!takeValue(arguments, &index, &value, failureReason))
                return false;
            const int providerCountBefore =
                request->includedProviders.size();
            for (const QString& provider : value.split(
                     QLatin1Char(','), Qt::SkipEmptyParts)) {
                const QString normalized = provider.trimmed().toLower();
                if (!normalized.isEmpty())
                    request->includedProviders.append(normalized);
            }
            if (request->includedProviders.size()
                == providerCountBefore) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "--include requires at least one provider.");
                }
                return false;
            }
        } else if (argument == QStringLiteral("--refresh")) {
            request->forceRefresh = true;
        } else if (argument == QStringLiteral("--no-refresh")) {
            request->allowRefresh = false;
        } else if (argument == QStringLiteral("--require-current")) {
            request->requireCurrent = true;
        } else if (argument.startsWith(QStringLiteral("--"))) {
            if (failureReason)
                *failureReason = QStringLiteral("Unknown option: %1").arg(argument);
            return false;
        } else {
            positional.append(argument);
        }
    }

    request->format = request->format.trimmed().toLower();
    if (request->format != QStringLiteral("json")
        && request->format != QStringLiteral("jsonl")
        && request->format != QStringLiteral("markdown")) {
        if (failureReason)
            *failureReason = QStringLiteral("Unsupported output format.");
        return false;
    }
    if (!request->includedProviders.isEmpty()) {
        const QSet<QString> allowed{
            QStringLiteral("all"), QStringLiteral("pinloom"),
            QStringLiteral("wave"), QStringLiteral("regmap")};
        for (const QString& provider :
             std::as_const(request->includedProviders)) {
            if (!allowed.contains(provider)) {
                if (failureReason) {
                    *failureReason = QStringLiteral(
                        "Unsupported --include provider: %1").arg(provider);
                }
                return false;
            }
        }
        if (request->includedProviders.contains(QStringLiteral("all")))
            request->includedProviders.clear();
        else
            request->includedProviders.removeDuplicates();
    }
    if (includeSpecified
        && request->command != QStringLiteral("suite-context")) {
        if (failureReason) {
            *failureReason = QStringLiteral(
                "--include is only valid for suite-context.");
        }
        return false;
    }
    if (request->command == QStringLiteral("symbol")
        && request->symbol.isEmpty() && !positional.isEmpty()) {
        request->symbol = positional.takeFirst();
    }
    if (!positional.isEmpty()) {
        if (failureReason)
            *failureReason = QStringLiteral("Unexpected positional argument: %1")
                .arg(positional.constFirst());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    // Qt consumes its own --session option. Preserve the public CLI arguments
    // before constructing QApplication, including Windows' Unicode paths.
    QStringList allArguments;
#ifdef Q_OS_WIN
    int argumentCount = 0;
    auto** wideArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    for (int i = 1; wideArguments && i < argumentCount; ++i)
        allArguments << QString::fromWCharArray(wideArguments[i]);
    if (wideArguments) LocalFree(wideArguments);
#else
    for (int i = 1; i < argc; ++i) allArguments << QString::fromLocal8Bit(argv[i]);
#endif
#ifdef ZEROSLACK_CLI_AUTOMATION
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
#else
    QCoreApplication application(argc, argv);
#endif
    QCoreApplication::setApplicationName(QStringLiteral("zeroslack-cli"));
    QCoreApplication::setOrganizationName(QStringLiteral("ZeroSlack"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION));

    QTextStream output(stdout);
#ifdef ZEROSLACK_CLI_AUTOMATION
    configureZeroSlackCliProfile();
    if (QStringList{"api", "capabilities", "sessions", "serve"}.contains(allArguments.value(0)))
        return runZeroSlackAutomationCli(allArguments);
#endif
    if (allArguments.isEmpty()
        || allArguments.contains(QStringLiteral("--help"))
        || allArguments.contains(QStringLiteral("-h"))) {
        output << ZeroSlackCliService::usageText();
        return allArguments.isEmpty() ? 2 : 0;
    }
    if (allArguments.size() == 1
        && (allArguments.constFirst() == QStringLiteral("--version")
            || allArguments.constFirst() == QStringLiteral("-v"))) {
        output << QStringLiteral("zeroslack-cli %1\n")
                      .arg(QCoreApplication::applicationVersion());
        return 0;
    }

    ZeroSlackCliRequest request;
    QString failure;
    if (!parseArguments(allArguments, &request, &failure)) {
        if (request.command == "format") {
            const QJsonObject envelope{{"schema", "zeroslack.cli/v1"}, {"schemaVersion", 1}, {"command", "format"},
                {"ok", false}, {"error", QJsonObject{{"code", "invalid_arguments"}, {"message", failure}}},
                {"data", QJsonObject{{"file", request.filePath}, {"changed", false}, {"written", false},
                    {"outcome", "Rejected"}, {"diagnostic", failure}, {"formattedLines", 0}}}};
            output << ZeroSlackCliService::render(envelope, request.format);
        } else {
            output << failure << QStringLiteral("\n\n") << ZeroSlackCliService::usageText();
        }
        return 2;
    }

    const ZeroSlackCliResult result = ZeroSlackCliService().execute(request);
    output << result.rendered;
    if (!result.rendered.endsWith('\n'))
        output << '\n';
    return result.exitCode;
}
