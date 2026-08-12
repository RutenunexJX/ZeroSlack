#include "applicationthememanager.h"
#include "mycodeeditor.h"
#include "symbolanalyzer.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

class DerivedEditor final : public MyCodeEditor
{
public:
    using MyCodeEditor::MyCodeEditor;
};

} // namespace

class SharedCoreRuntimeTest final : public QObject
{
    Q_OBJECT

private slots:
    void exportedQtMetaObjectsAndSingletonStateAreUsable()
    {
        ApplicationThemeManager& manager =
            ApplicationThemeManager::instance();
        QCOMPARE(&manager, &ApplicationThemeManager::instance());

        QSignalSpy themeSpy(&manager,
                            &ApplicationThemeManager::themeChanged);
        manager.setMode(ThemeMode::Dark);
        QCOMPARE(manager.mode(), ThemeMode::Dark);
        QCOMPARE(themeSpy.count(), 1);
        manager.setMode(ThemeMode::Light);
        QCOMPARE(manager.mode(), ThemeMode::Light);
        QCOMPARE(themeSpy.count(), 2);

        DerivedEditor editor;
        MyCodeEditor* base = qobject_cast<MyCodeEditor*>(&editor);
        QVERIFY(base != nullptr);
        base->setPlainText(QStringLiteral("module dll_boundary; endmodule\n"));
        QCOMPARE(base->document()->toPlainText(),
                 QStringLiteral("module dll_boundary; endmodule\n"));

        SymbolAnalyzer analyzer;
        QSignalSpy analysisSpy(&analyzer,
                               &SymbolAnalyzer::analysisCompleted);
        analyzer.analyzeFileContent(
            QStringLiteral("dll_boundary.sv"),
            QStringLiteral("module dll_boundary; endmodule\n"));
        QCOMPARE(analysisSpy.count(), 1);
        const QList<QVariant> arguments = analysisSpy.takeFirst();
        QCOMPARE(arguments.at(0).toString(),
                 QStringLiteral("dll_boundary.sv"));
        QVERIFY(arguments.at(1).toInt() >= 1);
    }
};

QTEST_MAIN(SharedCoreRuntimeTest)

#include "shared_core_runtime_test.moc"
