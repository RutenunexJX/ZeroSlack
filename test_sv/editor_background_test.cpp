#include "editorappearance.h"
#include "editorappearancesettings.h"
#include "mycodeeditor.h"
#include "settingscenterpanel.h"
#include "settingscenterservice.h"
#include "testuistyle.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QLineEdit>
#include <QPlainTextDocumentLayout>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QtTest>

namespace {
QImage capture(MyCodeEditor& editor)
{
    QApplication::processEvents();
    return editor.viewport()->grab().toImage().scaled(editor.viewport()->size());
}

QImage corner(const QImage& image)
{
    return image.copy(QRect(image.width() * 3 / 4, image.height() * 3 / 4,
                            image.width() / 8, image.height() / 8));
}

void saveReview(const QImage& image, const QString& name)
{
    const QString directory = qEnvironmentVariable("ZEROSLACK_UI_REVIEW_DIR");
    if (!directory.isEmpty()) {
        QDir().mkpath(directory);
        image.save(QDir(directory).filePath(name + QStringLiteral(".png")));
    }
}
}

class EditorBackgroundTest : public QObject
{
    Q_OBJECT
private slots:
    void presetsAndSettingsPersist();
    void backgroundStaysFixedWhenScrollingAndFolding();
    void backgroundChangesPreserveEditingAndOtherViews();
    void customImageFallbackAndDarkTheme();
};

void EditorBackgroundTest::presetsAndSettingsPersist()
{
    MyCodeEditor editor;
    for (const QString& name : {QStringLiteral("resting"), QStringLiteral("peekaboo"), QStringLiteral("balancing")})
        QVERIFY(!QImage(QStringLiteral(":/backgrounds/%1.png").arg(name)).isNull());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsPath = directory.filePath(QStringLiteral("global.ini"));
    SettingsCenterService service(settingsPath);
    QCOMPARE(service.load().value(QStringLiteral("appearance.editorBackground")).toString(), QStringLiteral("Resting"));
    SettingsCenterPanel panel(&service);
    auto* preset = qobject_cast<QComboBox*>(panel.fieldEditor(QStringLiteral("appearance.editorBackground")));
    auto* opacity = qobject_cast<QSlider*>(panel.fieldEditor(QStringLiteral("appearance.editorBackgroundOpacity")));
    auto* path = panel.findChild<QLineEdit*>(QStringLiteral("settingsCenterFilePathEdit.appearance.editorBackgroundImagePath"));
    QVERIFY(preset && opacity && path);
    QVERIFY(!path->isEnabled());
    preset->setCurrentText(QStringLiteral("Balancing"));
    opacity->setValue(63);
    panel.applyCurrentScope();
    SettingsCenterService reloaded(settingsPath);
    QCOMPARE(reloaded.load().value(QStringLiteral("appearance.editorBackground")).toString(), QStringLiteral("Balancing"));
    QCOMPARE(reloaded.load().value(QStringLiteral("appearance.editorBackgroundOpacity")).toInt(), 63);
    EditorAppearanceSettings appearance(std::make_unique<QSettings>(settingsPath, QSettings::IniFormat));
    QCOMPARE(appearance.options().backgroundPreset, QStringLiteral("Balancing"));
    QCOMPARE(appearance.options().backgroundOpacity, 63);
    preset->setCurrentText(QStringLiteral("Custom image"));
    QVERIFY(path->isEnabled());
    path->setText(directory.filePath(QStringLiteral("custom.png")));
    panel.applyCurrentScope();
    QCOMPARE(reloaded.load().value(QStringLiteral("appearance.editorBackgroundImagePath")).toString(), path->text());
    const auto workspaceValues = SettingsCenterSchema::validateLayer(
        {{QStringLiteral("appearance.editorBackground"), QStringLiteral("None")}}, SettingsCenterScope::Workspace);
    QVERIFY(!workspaceValues.values.contains(QStringLiteral("appearance.editorBackground")));
}

void EditorBackgroundTest::backgroundStaysFixedWhenScrollingAndFolding()
{
    MyCodeEditor editor;
    editor.resize(1040, 700);
    editor.setPlainText(QStringLiteral("module sample;\ninitial begin\n  logic a;\n  logic b;\nend\nendmodule\n")
        + QStringLiteral("// long ") + QString(320, QLatin1Char('x')) + QString(150, QLatin1Char('\n')));
    editor.show();
    QTest::qWait(30);
    editor.setEditorBackground(QStringLiteral("None"), {}, 55);
    const QImage plain = capture(editor);
    editor.setEditorBackground(QStringLiteral("Resting"), {}, 55);
    const QImage initial = capture(editor);
    QVERIFY(corner(initial) != corner(plain));
    saveReview(initial, QStringLiteral("editor-resting-light"));
    QVERIFY(editor.verticalScrollBar()->maximum() > 0);
    editor.verticalScrollBar()->setValue(35);
    QCOMPARE(corner(capture(editor)), corner(initial));
    editor.verticalScrollBar()->setValue(0);
    QVERIFY(editor.horizontalScrollBar()->maximum() > 0);
    editor.horizontalScrollBar()->setValue(70);
    QCOMPARE(corner(capture(editor)), corner(initial));
    editor.horizontalScrollBar()->setValue(0);
    QVERIFY(editor.toggleFoldAtLineForTest(1));
    QVERIFY(editor.viewProjectionActive());
    QCOMPARE(corner(capture(editor)), corner(initial));
    editor.resize(560, 700);
    const QImage narrow = capture(editor);
    saveReview(narrow, QStringLiteral("editor-resting-narrow"));
    editor.setEditorBackground(QStringLiteral("None"), {}, 55);
    QVERIFY(corner(narrow) != corner(capture(editor)));
}

void EditorBackgroundTest::backgroundChangesPreserveEditingAndOtherViews()
{
    QTextDocument document;
    document.setDocumentLayout(new QPlainTextDocumentLayout(&document));
    document.setPlainText(QStringLiteral("module original;\nendmodule\n"));
    MyCodeEditor first;
    MyCodeEditor second;
    first.attachSharedDocument(&document);
    second.attachSharedDocument(&document);
    first.resize(940, 640);
    second.resize(620, 640);
    first.show();
    second.show();
    auto options = EditorAppearance::defaultOptions();
    first.applyAppearanceSettings(options);
    first.moveCursor(QTextCursor::End);
    first.insertPlainText(QStringLiteral("// edited"));
    const QString contents = document.toPlainText();
    const int undoSteps = document.availableUndoSteps();
    const int cursor = first.textCursor().position();
    const QFont font = first.font();
    const QImage otherBefore = capture(second);
    options.backgroundPreset = QStringLiteral("Peekaboo");
    first.applyAppearanceSettings(options);
    QCOMPARE(document.toPlainText(), contents);
    QCOMPARE(document.availableUndoSteps(), undoSteps);
    QVERIFY(document.isModified());
    QCOMPARE(first.textCursor().position(), cursor);
    QCOMPARE(first.font(), font);
    QCOMPARE(corner(capture(second)), corner(otherBefore));
    saveReview(capture(first), QStringLiteral("editor-peekaboo"));
    first.undo();
    QVERIFY(!document.toPlainText().contains(QStringLiteral("// edited")));
}

void EditorBackgroundTest::customImageFallbackAndDarkTheme()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("custom.png"));
    QImage image(80, 80, QImage::Format_RGB32);
    image.fill(QColor(210, 120, 100));
    QVERIFY(image.save(path));
    MyCodeEditor editor;
    editor.resize(900, 650);
    editor.setPlainText(QStringLiteral("module sample;\n  logic ready;\nendmodule\n"));
    editor.show();
    editor.setEditorBackground(QStringLiteral("None"), {}, 55);
    const QImage none = capture(editor);
    editor.setEditorBackground(QStringLiteral("Custom image"), path, 100);
    const QImage custom = capture(editor);
    QCOMPARE(custom.pixelColor(custom.width() - 40, custom.height() - 40), QColor(210, 120, 100));
    editor.setEditorBackground(QStringLiteral("Custom image"), directory.filePath(QStringLiteral("missing.png")), 100);
    QCOMPARE(corner(capture(editor)), corner(none));
    editor.setEditorBackground(QStringLiteral("Resting"), {}, 0);
    QCOMPARE(corner(capture(editor)), corner(none));
    ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
    editor.setEditorBackground(QStringLiteral("Resting"), {}, 55);
    const QImage dark = capture(editor);
    QVERIFY(dark.pixelColor(dark.width() - 40, dark.height() - 40).lightness() < 110);
    const QColor upperBlank = dark.pixelColor(dark.width() / 3, dark.height() / 5);
    const QColor lowerBlank = dark.pixelColor(dark.width() / 3, dark.height() * 3 / 5);
    QVERIFY(qAbs(upperBlank.lightness() - lowerBlank.lightness()) < 5);
    saveReview(dark, QStringLiteral("editor-resting-dark"));
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    editor.setEditorBackground(QStringLiteral("Balancing"), {}, 55);
    saveReview(capture(editor), QStringLiteral("editor-balancing"));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 3;
    EditorBackgroundTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "editor_background_test.moc"
