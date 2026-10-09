#pragma once

#include <QJsonObject>
#include <QObject>

#include <memory>

class MainWindow;
class QMainWindow;
class SimDockSuiteCompatibility;

namespace SuiteApp {
class Provider;
struct RuntimeStartOptions;
}

class ZeroSlackSuiteIntegration final : public QObject
{
public:
    explicit ZeroSlackSuiteIntegration(QMainWindow* window,
                                       QObject* parent = nullptr);
    ~ZeroSlackSuiteIntegration() override;

    bool start(QString* failureReason = nullptr);
    bool start(const SuiteApp::RuntimeStartOptions& options,
               QString* failureReason = nullptr);
    bool isRegistered() const;

    static QJsonObject appDescriptor(const QString& version,
                                     const QString& endpoint = {});
    QJsonObject processRequestForTesting(const QJsonObject& request);

private:
    QJsonObject processRequest(const QJsonObject& request);

    MainWindow* window = nullptr;
    std::unique_ptr<SuiteApp::Provider> provider;
    std::unique_ptr<SimDockSuiteCompatibility> simdock;
    QString runtimeEndpoint;
};
