#include "../src/integrations/xips/xipscontextprovider.h"
#include "testuistyle.h"
#include "workspacemanager.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QDialog>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <functional>
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
void whenVisible(QWidget *owner, const QString &name, const std::function<void(QWidget *)> &action)
{
    auto *timer = new QTimer(owner);
    QElapsedTimer elapsed; elapsed.start();
    QObject::connect(timer, &QTimer::timeout, owner, [owner, name, timer, elapsed, action] {
        for (auto *form : owner->findChildren<QWidget *>(name)) {
            if (!form->isVisible()) continue;
            timer->stop(); timer->deleteLater();
            action(form); return;
        }
        if (elapsed.elapsed() > 10000) {
            timer->stop(); timer->deleteLater();
            QTest::qFail(qPrintable("Dialog not shown: " + name), __FILE__, __LINE__);
            for (auto *dialog : owner->findChildren<QDialog *>()) if (dialog->isVisible()) dialog->reject();
        }
    });
    timer->start(10);
}
void acceptWhenVisible(QWidget *owner, const QString &name)
{
    whenVisible(owner, name, [](QWidget *form) {
        auto *accept = form->window()->findChild<QAbstractButton *>("formAccept");
        QVERIFY(accept); accept->click();
    });
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
        const auto hostFont = QApplication::font();
        const auto hostPalette = QApplication::palette();
        std::unique_ptr<QWidget> view(provider.createView(resource, nullptr));
        QVERIFY2(view->property("nativeComponentReady").toBool(),
                 qPrintable(view->property("nativeComponentError").toString()));
        QCOMPARE(QApplication::font(), hostFont);
        QCOMPARE(QApplication::palette(), hostPalette);
        view->resize(480, 620);
        view->show();
        auto *panel = view->findChild<QWidget *>("xipsBrowser");
        QVERIFY(panel);
        auto *collect = panel->findChild<QAbstractButton *>("collectButton");
        auto *take = panel->findChild<QAbstractButton *>("takeButton");
        auto *versions = panel->findChild<QAbstractItemView *>("revisionTable");
        QVERIFY(collect);
        QVERIFY(take);
        QVERIFY(versions);
        QTRY_VERIFY(collect->isEnabled());
        acceptWhenVisible(view.get(), "collectName");
        acceptWhenVisible(view.get(), "payloadReviewForm");
        QVERIFY(QMetaObject::invokeMethod(panel, "collectPaths",
                                          Q_ARG(QStringList, QStringList{source})));
        QTRY_COMPARE(versions->model()->rowCount(), 1);
        QTRY_VERIFY(take->isEnabled());
        QVariantMap state;
        QVERIFY(QMetaObject::invokeMethod(panel, "saveState", Q_RETURN_ARG(QVariantMap, state)));
        QVERIFY(!state.value("revision").toString().isEmpty());
        acceptWhenVisible(view.get(), "exportDestination");
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
        QCOMPARE(entry.value("revision").toString(), state.value("revision").toString());
        QCOMPARE(entry.value("path").toString(), QString("uart.sv"));
        QVERIFY(entry.value("contentHash").toString().startsWith("sha256:"));
        const auto screenshots = qEnvironmentVariable("XIPS_SCREENSHOT_DIR");
        if (!screenshots.isEmpty())
            view->grab().save(screenshots + "/zeroslack-xips.png");
        const auto catalogBusy = [panel] {
            bool busy = true;
            QMetaObject::invokeMethod(panel, "isCatalogBusy", Q_RETURN_ARG(bool, busy));
            return busy;
        };
        QTRY_VERIFY(!catalogBusy());
        workspaces.closeWorkspace();
        QVERIFY(view->property("nativeComponentReady").toBool());
        bool destinationInspected = false;
        QString observedDestination;
        whenVisible(view.get(), "exportDestination", [&](QWidget *form) {
            auto *destination = qobject_cast<QLineEdit *>(form);
            QVERIFY(destination);
            observedDestination = destination->text();
            destinationInspected = true;
            auto *dialog = qobject_cast<QDialog *>(destination->window());
            QVERIFY(dialog); dialog->reject();
        });
        take->click();
        QTRY_VERIFY(destinationInspected);
        QVERIFY2(observedDestination.isEmpty(), qPrintable(observedDestination));
        view.reset();
        QCOMPARE(QApplication::font(), hostFont);
        QCOMPARE(QApplication::palette(), hostPalette);
    }
};
QTEST_MAIN(XipsContextProviderTest)
#include "xips_context_provider_test.moc"
