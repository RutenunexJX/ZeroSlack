#include "zeroslackcli.h"

#include "version.h"

#include <QCoreApplication>
#include <QSet>
#include <QTextStream>

#include <utility>

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
    if (!request || arguments.size() < 2) {
        if (failureReason)
            *failureReason = QStringLiteral("Command and workspace are required.");
        return false;
    }
    request->command = arguments.at(0).trimmed().toLower();
    request->workspaceRoot = arguments.at(1);
    QStringList positional;
    bool includeSpecified = false;
    for (int index = 2; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument == QStringLiteral("--format")) {
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
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("zeroslack-cli"));
    QCoreApplication::setOrganizationName(QStringLiteral("ZeroSlack"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION));

    QTextStream output(stdout);
    const QStringList allArguments = application.arguments().mid(1);
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
        output << failure << QStringLiteral("\n\n")
               << ZeroSlackCliService::usageText();
        return 2;
    }

    const ZeroSlackCliResult result = ZeroSlackCliService().execute(request);
    output << result.rendered;
    if (!result.rendered.endsWith('\n'))
        output << '\n';
    return result.exitCode;
}
