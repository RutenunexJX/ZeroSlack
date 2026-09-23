#include "../src/integrations/xips/xipscontextprovider.h"
#include "testuistyle.h"
#include "workspacemanager.h"
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>

namespace
{
bool put(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray get(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace

class XipsContextProviderTest final : public QObject
{
    Q_OBJECT
    QTemporaryDir settings;
  private slots:
    void initTestCase()
    {
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/segoeui.ttf"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QVERIFY(initializeUiStyleForTest());
    }
    void validatesDestinationsAndPreservesInvalidReferences()
    {
        QTemporaryDir tmp;
        const auto root = tmp.filePath("project");
        QVERIFY(QDir().mkpath(root));
        WorkspaceManager workspaces;
        workspaces.setRecentWorkspacePersistenceEnabledForTesting(false);
        XipsHostBridge bridge(nullptr, &workspaces, nullptr);
        QVERIFY(!bridge.destinationError(root + "/uart.sv").isEmpty());
        QVERIFY(workspaces.openWorkspace(root));
        QVERIFY(bridge.destinationError(root + "/uart.sv").isEmpty());
        QVERIFY(!bridge.destinationError(tmp.filePath("outside.sv")).isEmpty());
        QVERIFY(!bridge.destinationError(root + "/.zeroslack/source.sv").isEmpty());
        const auto file = root + "/uart.sv";
        QVERIFY(put(file, "pinned revision"));
        QVERIFY(!bridge.destinationError(file).isEmpty());
        QVariantMap receipt{{"schema", "xips.use/v1"},
                            {"assetId", "uart-id"},
                            {"name", "UART"},
                            {"revision", "1"},
                            {"contentHash", "sha256:example"},
                            {"workspace", root},
                            {"path", file},
                            {"files", QStringList{"uart.sv"}}};
        QVERIFY(bridge.exportCompleted(receipt).isEmpty());
        const auto references = root + "/.zeroslack/xips-references.json";
        const auto recorded =
            QJsonDocument::fromJson(get(references)).object().value("assets").toArray();
        QCOMPARE(recorded.size(), 1);
        QCOMPARE(recorded.first().toObject().value("revision").toString(), QString("1"));
        QCOMPARE(recorded.first().toObject().value("path").toString(), QString("uart.sv"));
        const QByteArray invalid =
            R"({"schema":"zeroslack.xips-references/v1","assets":"damaged"})";
        QVERIFY(put(references, invalid));
        QVERIFY(!bridge.exportCompleted(receipt).isEmpty());
        QCOMPARE(get(references), invalid);
        QCOMPARE(get(file), QByteArray("pinned revision"));
    }
    void nativePanelCollectsAndUsesInWorkspace()
    {
        if (qEnvironmentVariableIsEmpty("XIPS_BROWSER_LIBRARY"))
            QSKIP("Set XIPS_BROWSER_LIBRARY to test the installed native component.");
        QTemporaryDir tmp;
        const auto root = tmp.filePath("project"), library = tmp.filePath("library");
        QVERIFY(QDir().mkpath(root));
        QVERIFY(QDir().mkpath(library));
        const auto previousLibrary = qgetenv("XIPS_LIBRARY");
        qputenv("XIPS_LIBRARY", library.toUtf8());
        const auto restoreEnvironment = qScopeGuard([previousLibrary] {
            if (previousLibrary.isNull()) qunsetenv("XIPS_LIBRARY");
            else qputenv("XIPS_LIBRARY", previousLibrary);
        });
        const auto source = tmp.filePath("uart.sv");
        QVERIFY(put(source, "module uart; endmodule"));
        WorkspaceManager workspaces;
        workspaces.setRecentWorkspacePersistenceEnabledForTesting(false);
        QVERIFY(workspaces.openWorkspace(root));
        XipsContextProvider provider(nullptr, &workspaces);
        QVERIFY(!QIcon(provider.iconKey()).isNull());
        const auto resource = provider.activationResource(root);
        std::unique_ptr<QWidget> view(provider.createView(resource, nullptr));
        view->resize(480, 620);
        view->show();
        auto *panel = view->findChild<QWidget *>("xipsBrowser");
        QVERIFY(panel);
        auto *collect = panel->findChild<QPushButton *>("collectButton");
        auto *take = panel->findChild<QPushButton *>("takeButton");
        auto *versions = panel->findChild<QComboBox *>("versionCombo");
        QVERIFY(collect);
        QVERIFY(take);
        QVERIFY(versions);
        QTRY_VERIFY(collect->isEnabled());
        const auto accept = [&]
        {
            auto *form = view->findChild<QWidget *>("xipsForm");
            QVERIFY(form);
            form->findChild<QPushButton *>("formAccept")->click();
        };
        QTimer::singleShot(0, view.get(), accept);
        QVERIFY(QMetaObject::invokeMethod(panel, "collectPaths",
                                          Q_ARG(QStringList, QStringList{source})));
        QTRY_COMPARE(versions->count(), 1);
        QTRY_VERIFY(take->isEnabled());
        QTimer::singleShot(0, view.get(), accept);
        take->click();
        const auto references = root + "/.zeroslack/xips-references.json";
        QTRY_VERIFY(QFileInfo::exists(references));
        QCOMPARE(get(root + "/uart.sv"), get(source));
        const auto entry = QJsonDocument::fromJson(get(references))
                               .object()
                               .value("assets")
                               .toArray()
                               .first()
                               .toObject();
        QCOMPARE(entry.value("revision").toString(), QString("1"));
        QCOMPARE(entry.value("path").toString(), QString("uart.sv"));
        QVERIFY(entry.value("contentHash").toString().startsWith("sha256:"));
        const auto screenshots = qEnvironmentVariable("XIPS_SCREENSHOT_DIR");
        if (!screenshots.isEmpty())
            view->grab().save(screenshots + "/zeroslack-xips.png");
        workspaces.closeWorkspace();
        QCOMPARE(take->text(), QString("Use…"));
    }
};
QTEST_MAIN(XipsContextProviderTest)
#include "xips_context_provider_test.moc"
