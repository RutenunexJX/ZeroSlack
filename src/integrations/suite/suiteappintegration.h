#pragma once

#include <QJsonObject>
#include <QObject>

#include <memory>

class MainWindow;

namespace SuiteApp {
class Provider;
}

class ZeroSlackSuiteIntegration final : public QObject
{
public:
    explicit ZeroSlackSuiteIntegration(MainWindow* window,
                                       QObject* parent = nullptr);
    ~ZeroSlackSuiteIntegration() override;

    bool start(QString* failureReason = nullptr);
    bool isRegistered() const;

    static QJsonObject appDescriptor(const QString& version,
                                     const QString& endpoint = {});
    QJsonObject processRequestForTesting(const QJsonObject& request);

private:
    QJsonObject processRequest(const QJsonObject& request);

    MainWindow* window = nullptr;
    std::unique_ptr<SuiteApp::Provider> provider;
};
