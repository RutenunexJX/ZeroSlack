#include "suiteappintegration.h"

#include <suiteapp/protocol.h>

#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <QtTest>

class ZeroSlackSuiteAppIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void publishesStableContract()
    {
        const QJsonObject descriptor =
            ZeroSlackSuiteIntegration::appDescriptor(
                QStringLiteral("1.2.3"),
                QStringLiteral("test.zeroslack"));
        QString reason;
        QVERIFY2(SuiteApp::validateAppDescriptor(descriptor, &reason),
                 qPrintable(reason));
        QCOMPARE(SuiteApp::descriptorAppId(descriptor),
                 QStringLiteral("zeroslack"));
        QVERIFY(SuiteApp::descriptorOwnsScheme(
            descriptor, QStringLiteral("zeroslack")));
        QVERIFY(SuiteApp::descriptorOwnsAction(
            descriptor, QStringLiteral("zeroslack.source.reveal")));
        QVERIFY(SuiteApp::descriptorOwnsSurface(
            descriptor, QStringLiteral("zeroslack.source.preview")));
    }

    void resolvesSourceResourceWithoutUi()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write("module demo;\nendmodule\n");
        file.flush();

        QUrl uri;
        uri.setScheme(QStringLiteral("zeroslack"));
        uri.setHost(QStringLiteral("source"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("file"), file.fileName());
        query.addQueryItem(QStringLiteral("line"), QStringLiteral("1"));
        uri.setQuery(query);

        ZeroSlackSuiteIntegration integration(nullptr);
        const QJsonObject response = integration.processRequestForTesting(
            SuiteApp::makeRequest(
                QStringLiteral("resource.resolve"),
                {{QStringLiteral("uri"), uri.toString()}}));
        QVERIFY(response.value(QStringLiteral("ok")).toBool());
        const QJsonObject result =
            response.value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("source"));
        QVERIFY(result.value(QStringLiteral("snippet")).toString()
                    .contains(QStringLiteral("module demo")));
    }
};

QTEST_APPLESS_MAIN(ZeroSlackSuiteAppIntegrationTest)

#include "suiteapp_integration_test.moc"
