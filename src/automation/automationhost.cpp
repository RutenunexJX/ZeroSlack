#include "automationhost.h"
#include "mainwindow.h"
#include "editorfileidentity.h"
#include "../simulation/simdock/core/workspace.h"
#include <suiteapp/transport.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

namespace {
QString registryDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)).filePath("ZeroSlack/automation-sessions");
}
}
ZeroSlackAutomationHost::ZeroSlackAutomationHost(QMainWindow* window, QObject* parent)
    : QObject(parent), service(qobject_cast<MainWindow*>(window)), gui(window != nullptr) {}
ZeroSlackAutomationHost::~ZeroSlackAutomationHost() {
    server.reset();
    if (lease && lease->isLocked()) QFile::remove(recordPath);
}
QJsonObject ZeroSlackAutomationHost::descriptor() const {
    return {{"sessionId", id}, {"endpoint", endpoint}, {"kind", gui ? "gui" : "headless"},
        {"processId", double(QCoreApplication::applicationPid())}, {"profile", qEnvironmentVariable("ZEROSLACK_PROFILE_ROOT")}};
}
bool ZeroSlackAutomationHost::start(QString* error) {
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    endpoint = "zeroslack.automation." + id;
    server = std::make_unique<SuiteApp::JsonServer>(endpoint, [this](const QJsonObject& envelope) {
        if (envelope.value("method") != "automation.execute") return SuiteApp::errorResponse(envelope, "method_not_supported", "Use automation.execute.");
        const auto params = envelope.value("params").toObject();
        if (params.value("sessionId") != id) return SuiteApp::errorResponse(envelope, "session_conflict", "Session identity mismatch.");
        const auto request = params.value("request").toObject();
        if (request.value("command") == "session.shutdown") {
            if (gui) return SuiteApp::errorResponse(envelope, "not_supported", "GUI sessions cannot be closed through automation.");
            QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
            return SuiteApp::successResponse(envelope, ZeroSlackAutomationService::success({{"closing", true}}));
        }
        if (!gui) {
            const auto conflict = offlineConflict(request);
            if (!conflict.isEmpty()) return SuiteApp::successResponse(envelope, conflict);
        }
        return SuiteApp::successResponse(envelope, service.execute(request));
    });
    if (!server->start(error)) return false;
    const auto directory = registryDirectory();
    if (!QDir().mkpath(directory)) { *error = "Cannot create the local session registry."; server.reset(); return false; }
    recordPath = QDir(directory).filePath(id + ".json");
    lease = std::make_unique<QLockFile>(recordPath + ".lease");
    if (!lease->tryLock(0)) { *error = "Cannot acquire session lease."; server.reset(); return false; }
    QSaveFile output(recordPath); output.setDirectWriteFallback(false);
    const auto bytes = QJsonDocument(descriptor()).toJson(QJsonDocument::Compact);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        *error = output.errorString(); server.reset(); lease.reset(); return false;
    }
    return true;
}
QJsonArray ZeroSlackAutomationHost::sessions() {
    QJsonArray result;
    const QDir directory(registryDirectory());
    for (const auto& name : directory.entryList({"*.json"}, QDir::Files, QDir::Name)) {
        const auto path = directory.filePath(name);
        QLockFile lease(path + ".lease");
        if (lease.tryLock(0)) continue;
        QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) continue;
        const auto record = QJsonDocument::fromJson(file.readAll()).object();
        if (record.value("sessionId").toString() + ".json" == name && record.value("endpoint").toString() == "zeroslack.automation." + record.value("sessionId").toString()) result << record;
    }
    return result;
}
QJsonObject ZeroSlackAutomationHost::send(const QString& sessionId, const QJsonObject& request) {
    for (const auto& value : sessions()) {
        const auto record = value.toObject();
        if (record.value("sessionId").toString() != sessionId) continue;
        const auto response = SuiteApp::sendRequest(record.value("endpoint").toString(), SuiteApp::makeRequest("automation.execute",
            {{"sessionId", sessionId}, {"request", request}}), 120000, 8*1024*1024);
        if (!response.hasResponse()) return ZeroSlackAutomationService::failure("session_unavailable", response.errorMessage);
        if (!response.response.value("ok").toBool()) return ZeroSlackAutomationService::failure("transport_error", response.response.value("error").toObject().value("message").toString());
        return response.response.value("result").toObject();
    }
    return ZeroSlackAutomationService::failure("session_not_found", "Unknown active sessionId. Use sessions; no implicit fallback is performed.");
}
QJsonObject ZeroSlackAutomationHost::offlineConflict(const QJsonObject& request) {
    const auto command = request.value("command").toString();
    const bool documentAccess = command.startsWith("document.") || command == "dut.inspect";
    const bool storageWrite = !request.value("dryRun").toBool() && (command == "templates.put"
        || command == "templates.delete" || command == "simulation.put" || command == "simulation.run");
    if (!documentAccess && !storageWrite) return {};
    const auto root = request.value("workspace").toString();
    for (const auto& value : sessions()) {
        const auto record = value.toObject();
        if (record.value("kind") != "gui") continue;
        const auto response = send(record.value("sessionId").toString(), {{"command", "session.describe"}});
        if (!response.value("ok").toBool()) return ZeroSlackAutomationService::failure("session_unavailable", "An active GUI session could not be inspected; retry or explicitly select its session.", record);
        const auto data = response.value("data").toObject();
        if (!data.value("workspace").toString().isEmpty() && EditorFileIdentity::same(root, data.value("workspace").toString()))
            return ZeroSlackAutomationService::failure("session_required", "The workspace is open in a GUI session.", record);
        for (const auto& document : data.value("documents").toArray()) {
            const auto file = document.toObject().value("file").toString();
            if (!file.isEmpty() && simdock::insideWorkspace(root, file))
                return ZeroSlackAutomationService::failure("session_required", "This workspace has an open GUI document. Select its session to adopt and version changes explicitly.", record);
        }
        if (!data.value("simulationWorkspace").toString().isEmpty() && EditorFileIdentity::same(root, data.value("simulationWorkspace").toString()))
            return ZeroSlackAutomationService::failure("session_required", "The simulation workspace is open in a GUI session.", record);
    }
    return {};
}
