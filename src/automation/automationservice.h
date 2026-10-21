#pragma once
#include "../app/zeroslackexport.h"
#include <QObject>
#include <QJsonObject>
#include <QPointer>
#include <memory>

class MainWindow;
class ZEROSLACK_API ZeroSlackAutomationService : public QObject {
public:
    explicit ZeroSlackAutomationService(MainWindow* window = nullptr, QObject* parent = nullptr);
    ~ZeroSlackAutomationService() override;
    QJsonObject execute(const QJsonObject& request);
    static QJsonObject capabilities();
    static QJsonObject success(const QJsonObject& data);
    static QJsonObject failure(const QString& code, const QString& message, const QJsonObject& data = {});
    static QString digest(const QByteArray& bytes);
    static QString diskRevision(const QString& path);
private:
    struct Run;
    QPointer<MainWindow> window;
    std::unique_ptr<Run> run;
    QJsonObject templates(const QJsonObject& request, const QString& root);
    QJsonObject document(const QJsonObject& request, const QString& root);
    QJsonObject simulation(const QJsonObject& request, const QString& root);
};
