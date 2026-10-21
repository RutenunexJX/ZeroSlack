#include "workspacesessionstateservice.h"
#include "tabfileio.h"
#include "../src/simulation/simdock/core/simulationevents.h"
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class AutomationServiceTest : public QObject {
    Q_OBJECT
private slots:
    void nativeSourcePathInStructuredEvent() {
        simdock::SimulationEventDecoder decoder;
        decoder.reset("run");
        const QString path = QString::fromUtf8("工程/调用 test.sv");
        const auto events = decoder.feed("ZS_RUN_V1|run\nZS_EVT_V1|1.000|Info||7462|"
            + path.toLocal8Bit().toHex() + "|1|6f6b|\n");
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().sourceFile, path);
    }
    void metadataGenerationTracksCommittedSource() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto file = root.filePath("source.sv");
        WorkspaceSessionStateService storage(root.filePath("session.ini"));
        const auto revision = [](const QByteArray& bytes) {
            return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        };
        const QJsonObject oldMetadata{{"generation", "old"}}, newMetadata{{"generation", "new"}};
        QString error;
        QVERIFY2(storage.saveDocumentWithTemplates(file, "old source", "missing", oldMetadata, &error), qPrintable(error));
        int calls = 0;
        const auto rejectCommit = [&](QString* reason) {
            ++calls;
            if (storage.loadDocumentTemplates(file) != oldMetadata) qFatal("Uncommitted metadata became visible");
            if (calls == 2) { *reason = "Injected final guard rejection"; return false; }
            return true;
        };
        QVERIFY(!storage.saveDocumentWithTemplates(file, "new source", revision("old source"), newMetadata, &error, rejectCommit));
        QCOMPARE(calls, 2);
        QFile source(file); QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(), QByteArray("old source")); source.close();
        QCOMPARE(storage.loadDocumentTemplates(file), oldMetadata);
        QVERIFY2(storage.saveDocumentWithTemplates(file, "new source", revision("old source"), newMetadata, &error), qPrintable(error));
        QCOMPARE(storage.loadDocumentTemplates(file), newMetadata);
        QVERIFY(!storage.saveDocumentWithTemplates(file, "stale overwrite", revision("old source"), oldMetadata, &error));
        QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(), QByteArray("new source")); source.close();
        QCOMPARE(storage.loadDocumentTemplates(file), newMetadata);
    }
    void normalSaveDoesNotPublishSourceWhenMetadataFails() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto file = root.filePath("source.sv");
        QFile source(file); QVERIFY(source.open(QIODevice::WriteOnly)); source.write("old source"); source.close();
        const auto previous = qgetenv("ZEROSLACK_SESSION_STORAGE_PATH");
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", root.path().toUtf8());
        QString error;
        const QJsonObject metadata{{"generation", "new"}};
        const bool saved = TabFileIo().writeTextFile(nullptr, file, "new source", &error, nullptr, nullptr, {}, {}, &metadata);
        qputenv("ZEROSLACK_SESSION_STORAGE_PATH", previous);
        QVERIFY(!saved); QVERIFY(!error.isEmpty());
        QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(), QByteArray("old source"));
    }
};
QTEST_MAIN(AutomationServiceTest)
#include "automation_service_test.moc"
