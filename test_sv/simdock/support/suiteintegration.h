#pragma once
#include "../../../src/integrations/simdock/simdocksuitecompatibility.h"
#include "mainwindow.h"
namespace simdock {
class SuiteIntegration final {
public:
    explicit SuiteIntegration(MainWindow* window)
        : compatibility([window](const QString& root, const QString& id, QString* error) {
            return window->openSuiteTarget(root, id, error);
        }) {}
    bool start(QString* error, const SuiteApp::RuntimeStartOptions& options = {}) { return compatibility.start(error, options); }
    static QJsonObject descriptor() { return SimDockSuiteCompatibility::descriptor(); }
    QJsonObject handle(const QJsonObject& request) { return compatibility.handle(request); }
private:
    SimDockSuiteCompatibility compatibility;
};
}
