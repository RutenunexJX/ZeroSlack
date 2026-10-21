#include "cliautomation.h"
#include "automationservice.h"
#ifdef ZEROSLACK_AUTOMATION_TRANSPORT
#include "automationhost.h"
#endif
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

void configureZeroSlackCliProfile(const QString& requested) {
    auto profile = requested;
    if (profile.isEmpty()) profile = qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT");
    if (profile.isEmpty()) {
        const auto name = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName("ZeroSlack-Ela");
        profile = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QCoreApplication::setApplicationName(name);
    }
    profile = QDir(profile).absolutePath();
    qputenv("ZEROSLACK_PROFILE_ROOT", profile.toUtf8());
    qputenv("ZEROSLACK_SESSION_STORAGE_PATH", (profile + "/workspace-sessions.ini").toUtf8());
    qputenv("ZEROSLACK_SIMULATION_CACHE_ROOT", (profile + "/simulation").toUtf8());
}

int runZeroSlackAutomationCli(const QStringList& arguments) {
    QString source, session, profile;
    const auto command = arguments.value(0);
    const auto print = [](const QJsonObject& result) {
        QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
        if (result.value("ok").toBool()) return 0;
        const auto code = result.value("error").toObject().value("code").toString();
        return code.contains("conflict") || code == "session_required" ? 3 : 2;
    };
    for (int i = 1; i < arguments.size(); ++i) {
        const auto arg = arguments[i];
        if (arg == "--session" || arg == "--profile") {
            if (i + 1 == arguments.size()) return print(ZeroSlackAutomationService::failure("invalid_arguments", "Option requires a value."));
            auto& target = arg == "--session" ? session : profile;
            if (!target.isEmpty()) return print(ZeroSlackAutomationService::failure("invalid_arguments", "Duplicate option."));
            target = arguments[++i];
        } else if (command == "api" && source.isEmpty() && (!arg.startsWith('-') || arg == "-")) source = arg;
        else return print(ZeroSlackAutomationService::failure("invalid_arguments", "Use api <request.json|-> [--session id] [--profile directory], sessions, capabilities, or serve [--profile directory]."));
    }
    if (!profile.isEmpty()) {
        profile = QDir(profile).absolutePath();
        if (!QDir().mkpath(profile)) return print(ZeroSlackAutomationService::failure("invalid_profile", "Cannot create profile directory."));
    }
    configureZeroSlackCliProfile(profile);
    if (command == "capabilities") return print(ZeroSlackAutomationService::capabilities());
#ifdef ZEROSLACK_AUTOMATION_TRANSPORT
    if (command == "sessions") return print(ZeroSlackAutomationService::success({{"sessions", ZeroSlackAutomationHost::sessions()}}));
    if (command == "serve") {
        ZeroSlackAutomationHost host;
        QString error;
        if (!host.start(&error)) return print(ZeroSlackAutomationService::failure("listen_failed", error));
        print(ZeroSlackAutomationService::success(host.descriptor()));
        return QCoreApplication::exec();
    }
#endif
    if (command != "api" || source.isEmpty()) return print(ZeroSlackAutomationService::failure("invalid_arguments", "A JSON request file or - for standard input is required."));
    QFile input;
    if (source == "-") input.open(stdin, QIODevice::ReadOnly);
    else { input.setFileName(source); input.open(QIODevice::ReadOnly); }
    if (!input.isOpen()) return print(ZeroSlackAutomationService::failure("read_error", input.errorString()));
    const auto bytes = input.read(1024*1024 + 1);
    if (bytes.size() > 1024*1024) return print(ZeroSlackAutomationService::failure("input_limit", "The request exceeds 1 MiB."));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return print(ZeroSlackAutomationService::failure("invalid_json", "Expected one JSON request object: " + error.errorString()));
    const auto request = document.object();
#ifdef ZEROSLACK_AUTOMATION_TRANSPORT
    if (!session.isEmpty()) return print(ZeroSlackAutomationHost::send(session, request));
    const auto conflict = ZeroSlackAutomationHost::offlineConflict(request);
    if (!conflict.isEmpty()) return print(conflict);
#else
    if (!session.isEmpty()) return print(ZeroSlackAutomationService::failure("transport_unavailable", "This build has no SuiteApp transport."));
#endif
    if (request.value("command") == "simulation.run" || request.value("command") == "simulation.cancel" || request.value("command") == "simulation.status")
        return print(ZeroSlackAutomationService::failure("session_required", "Start a headless serve session or select a GUI session to retain and query the run."));
    ZeroSlackAutomationService service;
    return print(service.execute(request));
}
