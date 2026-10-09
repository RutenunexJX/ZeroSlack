#pragma once
#include <QJsonObject>
#include <QObject>
#include <functional>
#include <memory>
#include <suiteapp/runtime.h>
namespace SuiteApp { class Provider; }

// One compatibility provider in the ZeroSlack process. Resolve and describe
// remain read-only; navigation is supplied by the existing host owner.
class SimDockSuiteCompatibility final : public QObject
{
public:
    using OpenTarget = std::function<bool(const QString&, const QString&, QString*)>;
    explicit SimDockSuiteCompatibility(OpenTarget open, QObject* parent = nullptr);
    ~SimDockSuiteCompatibility() override;
    bool start(QString* error, const SuiteApp::RuntimeStartOptions& options = {});
    bool isRegistered() const;
    static QJsonObject descriptor(const QString& endpoint = {});
    QJsonObject handle(const QJsonObject& request);
private:
    OpenTarget openTarget;
    std::unique_ptr<SuiteApp::Provider> provider;
    QString runtimeEndpoint;
};
