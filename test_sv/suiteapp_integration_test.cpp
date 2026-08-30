#include "suiteappintegration.h"

#include "semanticindex.h"
#include "semanticindexsnapshot.h"
#include "semanticstableidentity.h"
#include "slangmanager.h"

#include <suiteapp/protocol.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTemporaryDir>
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
        QVERIFY(SuiteApp::descriptorOwnsAction(
            descriptor, QStringLiteral("zeroslack.symbol.reveal")));
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

    void resolvesStableSymbolResourceWithoutUi()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString filePath = QDir(workspace.path()).filePath(
            QStringLiteral("symbol.sv"));
        const QString content = QStringLiteral(
            "module demo; logic payload; endmodule\n");
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QCOMPARE(file.write(content.toUtf8()), content.toUtf8().size());
        file.close();

        SlangManager slang;
        const QList<SemanticSymbolRecord> records =
            slang.extractSymbolRecords(filePath, content);
        SemanticSymbolRecord payload;
        for (const SemanticSymbolRecord& record : records) {
            if (record.name == QStringLiteral("payload")) {
                payload = record;
                break;
            }
        }
        QVERIFY(payload.isValid());
        SemanticIndex::getInstance()->setSnapshot(
            std::make_shared<const SemanticIndexSnapshot>(
                SemanticIndexSnapshot::fromSymbolRecords(
                    records, {}, {}, {{filePath, content}})));

        const QString uri = semanticStableSymbolUri(
            payload, workspace.path());
        QVERIFY(uri.startsWith(QStringLiteral("zeroslack://symbol/")));
        ZeroSlackSuiteIntegration integration(nullptr);
        const QJsonObject response = integration.processRequestForTesting(
            SuiteApp::makeRequest(
                QStringLiteral("resource.resolve"),
                {{QStringLiteral("uri"), uri}}));
        QVERIFY(response.value(QStringLiteral("ok")).toBool());
        const QJsonObject result = response.value(
            QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("kind")).toString(),
                 QStringLiteral("symbol"));
        QCOMPARE(result.value(QStringLiteral("filePath")).toString(),
                 QFileInfo(filePath).absoluteFilePath());
        QCOMPARE(result.value(QStringLiteral("line")).toInt(),
                 payload.location.startLine);
        QVERIFY(result.value(QStringLiteral("snippet")).toString()
                    .contains(QStringLiteral("payload")));
        SemanticIndex::getInstance()->clearSemanticState();
    }
};

QTEST_APPLESS_MAIN(ZeroSlackSuiteAppIntegrationTest)

#include "suiteapp_integration_test.moc"
