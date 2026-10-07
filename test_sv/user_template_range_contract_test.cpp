#include "completionservice.h"
#include "usertemplateservice.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

namespace {
const QString body = QString::fromUtf8("logic 数据; // 😀\n");
bool write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

class UserTemplateRangeContractTest final : public QObject {
    Q_OBJECT
private slots:
    void loadRanges_data()
    {
        QTest::addColumn<bool>("slot");
        QTest::addColumn<int>("start");
        QTest::addColumn<int>("length");
        QTest::addColumn<bool>("valid");
        const int end = static_cast<int>(body.size());
        const int maximum = std::numeric_limits<int>::max();
        QTest::newRow("full-slot") << true << 0 << end << true;
        QTest::newRow("empty-end-slot") << true << end << 0 << true;
        QTest::newRow("full-selection") << false << 0 << end << true;
        QTest::newRow("no-selection") << false << -1 << 0 << true;
        QTest::newRow("negative-slot") << true << -1 << 1 << false;
        QTest::newRow("past-end-slot") << true << end << 1 << false;
        QTest::newRow("past-end-selection") << false << end << 1 << false;
        QTest::newRow("overflow-slot-start") << true << maximum << 1 << false;
        QTest::newRow("overflow-slot-length") << true << 1 << maximum << false;
        QTest::newRow("overflow-selection-start") << false << maximum << 1 << false;
        QTest::newRow("overflow-selection-length") << false << 1 << maximum << false;
    }
    void loadRanges()
    {
        QFETCH(bool, slot); QFETCH(int, start); QFETCH(int, length); QFETCH(bool, valid);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path = directory.filePath("global.json");
        QJsonObject entry{{"command", ";;rangeprobe"}, {"body", body}};
        if (slot) entry.insert("slots", QJsonArray{QJsonObject{{"name", "probe"}, {"start", start}, {"length", length}}});
        else { entry.insert("selectionStart", start); entry.insert("selectionLength", length); }
        const auto bytes = QJsonDocument(QJsonObject{{"templates", QJsonArray{entry}}}).toJson();
        QVERIFY(write(path, bytes));
        UserTemplateService templates(path, directory.filePath("workspace.json"));
        const auto loaded = templates.reload();
        CompletionService completion;
        completion.setUserTemplateService(&templates);
        const auto state = completion.commandModeCompletionState(CommandModeCompletionQuery{QStringLiteral(";;rangeprobe ")});
        qInfo("valid=%d issues=%lld records=%lld completionItems=%lld", loaded.valid,
              static_cast<long long>(loaded.issues.size()), static_cast<long long>(loaded.records.size()),
              static_cast<long long>(state.templateItems.size()));
        QCOMPARE(loaded.valid, valid);
        QCOMPARE(loaded.records.size(), valid ? 1 : 0);
        QCOMPARE(state.templateItems.size(), valid ? 1 : 0);
        QCOMPARE(loaded.issues.isEmpty(), valid);
        QCOMPARE(read(path), bytes);
        if (!valid) {
            UserTemplateRecord next; next.commandToken = ";;validnext"; next.insertText = "logic next;";
            QVERIFY(!templates.addOrUpdateRecord(next).valid);
            QVERIFY(!templates.removeRecord(";;rangeprobe"));
            QCOMPARE(read(path), bytes);
        }
    }
    void saveRanges_data() { loadRanges_data(); }
    void saveRanges()
    {
        QFETCH(bool, slot); QFETCH(int, start); QFETCH(int, length); QFETCH(bool, valid);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path = directory.filePath("global.json");
        const QByteArray initial = "{\"templates\": []}\n";
        QVERIFY(write(path, initial));
        UserTemplateService templates(path, directory.filePath("workspace.json"));
        UserTemplateRecord record; record.commandToken = ";;rangeprobe"; record.insertText = body;
        if (slot) {
            CodeTemplateSlot range; range.name = "probe"; range.start = start; range.length = length;
            record.templateSlots.append(range);
        } else { record.selectionStart = start; record.selectionLength = length; }
        const auto saved = templates.addOrUpdateRecord(record);
        QCOMPARE(saved.valid, valid);
        QCOMPARE(saved.issues.isEmpty(), valid);
        if (!valid) QCOMPARE(read(path), initial);
        else {
            const auto reloaded = templates.reload();
            QVERIFY(reloaded.valid); QCOMPARE(reloaded.records.size(), 1);
            QCOMPARE(reloaded.records.first().insertText, body);
            if (slot) {
                QCOMPARE(reloaded.records.first().templateSlots.size(), 1);
                QCOMPARE(reloaded.records.first().templateSlots.first().start, start);
                QCOMPARE(reloaded.records.first().templateSlots.first().length, length);
            }
        }
    }
};

QTEST_GUILESS_MAIN(UserTemplateRangeContractTest)
#include "user_template_range_contract_test.moc"
