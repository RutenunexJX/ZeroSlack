#pragma once
#include "../app/zeroslackexport.h"
#include "automationservice.h"
#include <QJsonArray>
#include <memory>
class QMainWindow;
class QLockFile;
namespace SuiteApp { class JsonServer; }

class ZEROSLACK_API ZeroSlackAutomationHost : public QObject {
public:
    explicit ZeroSlackAutomationHost(QMainWindow* window = nullptr, QObject* parent = nullptr);
    ~ZeroSlackAutomationHost() override;
    bool start(QString* error);
    QJsonObject descriptor() const;
    static QJsonArray sessions();
    static QJsonObject send(const QString& sessionId, const QJsonObject& request);
    static QJsonObject offlineConflict(const QJsonObject& request);
private:
    ZeroSlackAutomationService service;
    std::unique_ptr<SuiteApp::JsonServer> server;
    std::unique_ptr<QLockFile> lease;
    QString id, endpoint, recordPath;
    bool gui = false;
};
