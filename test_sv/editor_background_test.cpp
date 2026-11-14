#include "editorappearance.h"
#include "editorappearancesettings.h"
#include "editorbackground.h"
#include "mycodeeditor.h"
#include "documentblockmodel.h"
#include "settingscenterpanel.h"
#include "settingscenterservice.h"
#include "testuistyle.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QPainter>
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
    void semanticTextMatchesQtPaint();
    void resizeKeepsCaretOnItsTextLine_data();
    void resizeKeepsCaretOnItsTextLine();
    void wheelInputHasNoTrailingMotion_data();
    void wheelInputHasNoTrailingMotion();
    void backgroundPaintCoversFractionalPixelEdges();
};

void EditorBackgroundTest::backgroundPaintCoversFractionalPixelEdges()
{
    EditorBackground background;
    const QRgb sentinel=qRgb(255,0,255);
    for(const auto* preset:{"Resting","Peekaboo","Balancing"}){
    background.setOptions(preset,{},55);
    for(qreal dpr:{1.0,1.25,1.5,1.75,2.0})for(int offset:{0,1})for(int width=1200;width<1220;++width){
        const QSize logical(width,703+(width%3));
        QImage image(QSize(qCeil((logical.width()+2*offset)*dpr),qCeil((logical.height()+2*offset)*dpr)),QImage::Format_RGB32);
        image.setDevicePixelRatio(dpr);image.fill(sentinel);
        {QPainter painter(&image);painter.translate(offset,offset);background.paint(painter,QRect(QPoint(),logical),QRect(QPoint(),logical),dpr,QColor(246,248,251));}
        int remaining=0,black=0;QPoint first;
        for(int y=qCeil(offset*dpr);y<qFloor((logical.height()+offset)*dpr);++y){const auto* row=reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for(int x=qCeil(offset*dpr);x<qFloor((logical.width()+offset)*dpr);++x){
                if(row[x]==sentinel){if(!remaining)first=QPoint(x,y);++remaining;}
                if(qRed(row[x])<5 && qGreen(row[x])<5 && qBlue(row[x])<5)++black;}}
        if(remaining || black){saveReview(image,QString("uncovered-%1-%2-%3-%4").arg(preset).arg(dpr).arg(width).arg(offset));qInfo()<<"uncovered background"<<preset<<dpr<<logical<<offset<<first<<remaining<<black;}
        QVERIFY2(!remaining,"Opaque viewport background left pixels unpainted at the wallpaper margin");
        QVERIFY2(!black,"Light background acquired black pixels while painting its scaled edge");
    }
    }
}

void EditorBackgroundTest::resizeKeepsCaretOnItsTextLine_data()
{
    QTest::addColumn<bool>("associated");
    QTest::addColumn<QString>("background");
    for(bool associated:{false,true})for(const auto& background:{"None","Resting"})
        QTest::newRow(qPrintable(QString("%1-%2").arg(associated?"block":"plain",background)))<<associated<<QString(background);
}

void EditorBackgroundTest::resizeKeepsCaretOnItsTextLine()
{
    QFETCH(bool,associated);QFETCH(QString,background);
    MyCodeEditor editor;
    const QString source="`timescale 1ns / 1ps\n"+QString("// resize review\n").repeated(150);
    editor.setPlainText(source);editor.setEditorBackground(background,{},55);
    if(associated){EditorAssociatedBlock note;note.type="text";note.text="Preserved note";note.autoHeight=false;note.height=160;
        QTextCursor anchor(editor.document()->findBlockByNumber(3));QVERIFY(!editor.blockModel()->insert(note,anchor).isEmpty());}
    editor.resize(1300,900);editor.show();editor.activateWindow();editor.setFocus();
    editor.moveCursor(QTextCursor::Start);editor.moveCursor(QTextCursor::EndOfBlock);
    const int position=editor.textCursor().position();
    for(const QSize size:{QSize(900,680),QSize(1450,900),QSize(760,580),QSize(1300,900)}){
        editor.resize(size);QTest::qWait(25);
        const auto caret=editor.cursorRect();
        qInfo()<<"resize caret"<<associated<<background<<size<<caret<<"line"<<editor.document()->begin().layout()->boundingRect()<<"dpr"<<editor.devicePixelRatioF();
        QVERIFY(caret.height()>0);QVERIFY(caret.height()<=editor.fontMetrics().height()*2);
        const auto frame=capture(editor);saveReview(frame,QString("resize-%1-%2-%3").arg(associated).arg(background).arg(size.width()));
        const int x=qBound(0,caret.center().x(),frame.width()-1);int run=0,longest=0;
        for(int y=caret.bottom()+3;y<frame.height();++y){const auto pixel=frame.pixelColor(x,y);
            run=pixel.lightness()<45?run+1:0;longest=qMax(longest,run);}
        QVERIFY2(longest<editor.fontMetrics().height()*2,"A dark vertical stroke extends below the caret line");
        QCOMPARE(editor.textCursor().position(),position);QCOMPARE(editor.toPlainText(),source);
    }
}

void EditorBackgroundTest::wheelInputHasNoTrailingMotion_data()
{
    QTest::addColumn<bool>("associated");
    QTest::newRow("plain")<<false;QTest::newRow("block")<<true;
}

void EditorBackgroundTest::wheelInputHasNoTrailingMotion()
{
    QFETCH(bool,associated);
    MyCodeEditor editor;editor.resize(1000,700);
    editor.setPlainText(QString("logic scroll_review;\n").repeated(250));editor.setEditorBackground("Resting",{},55);
    if(associated){EditorAssociatedBlock note;note.type="text";note.text="Scroll note";
        QVERIFY(!editor.blockModel()->insert(note,editor.textCursor()).isEmpty());}
    editor.show();QTest::qWait(30);auto* bar=editor.verticalScrollBar();bar->setValue(bar->maximum()/2);
    QElapsedTimer clock;clock.start();QJsonArray values;QList<bool> immediate;
    const auto connection=connect(bar,&QScrollBar::valueChanged,&editor,[&](int value){values.append(QJsonObject{{"ms",clock.elapsed()},{"value",value}});});
    for(int angle:{-120,-120,-120,120}){
        const int before=bar->value();const QPoint point(800,500);
        QWheelEvent wheel(point,editor.viewport()->mapToGlobal(point),{},QPoint(0,angle),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(editor.viewport(),&wheel);immediate<<((bar->value()-before)*(angle<0?1:-1)>0);QTest::qWait(20);
    }
    const int stopped=bar->value();QTest::qWait(220);disconnect(connection);
    qInfo().noquote()<<QJsonDocument(QJsonObject{{"associated",associated},{"dpr",editor.devicePixelRatioF()},{"stoppedValue",stopped},{"settledValue",bar->value()},{"values",values}}).toJson(QJsonDocument::Compact);
    for(bool response:immediate)QVERIFY2(response,"Wheel input must change the scroll position in the requested direction immediately");
    QCOMPARE(bar->value(),stopped);
}

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

void EditorBackgroundTest::semanticTextMatchesQtPaint()
{
    const int flashTime = QApplication::cursorFlashTime();
    QApplication::setCursorFlashTime(0);
    MyCodeEditor editor;
    QString source = QStringLiteral("module colored;\n");
    QList<SemanticDecoration> decorations;
    for (int i = 0; i < 60; ++i) {
        const QString name = QStringLiteral("signal_%1").arg(i);
        const QString line = QStringLiteral("  logic [31:0] %1; // 保持文字清晰\n").arg(name);
        SemanticDecoration decoration;
        decoration.role = SemanticDecorationRole::ActualSignal;
        decoration.text = name;
        decoration.startPosition = source.size() + line.indexOf(name);
        decoration.length = name.size();
        decorations.append(decoration);
        source += line;
    }
    source += QStringLiteral("endmodule\n");
    editor.setPlainText(source);
    editor.setSemanticDecorations(decorations);
    editor.resize(1040, 700);
    editor.show();
    QTest::qWait(30);
    const auto checkPixels = [&](MyCodeEditor& view, const QString& label) {
        qputenv("ZEROSLACK_NATIVE_TEXT_PAINT", "1");
        view.viewport()->update();
        const QImage native = view.viewport()->grab().toImage();
        qunsetenv("ZEROSLACK_NATIVE_TEXT_PAINT");
        view.viewport()->update();
        const QImage optimized = view.viewport()->grab().toImage();
        saveReview(native, label + QStringLiteral("-native"));
        saveReview(optimized, label + QStringLiteral("-optimized"));
        if (optimized != native) {
            QRect differences;
            int count = 0;
            for (int y = 0; y < native.height(); ++y)
                for (int x = 0; x < native.width(); ++x)
                    if (native.pixel(x, y) != optimized.pixel(x, y)) {
                        differences |= QRect(x, y, 1, 1);
                        ++count;
                    }
            qInfo() << label << "different pixels" << count << differences;
        }
        QCOMPARE(optimized, native);
    };
    checkPixels(editor, QStringLiteral("semantic-light"));
    auto selections = editor.extraSelections();
    QTextEdit::ExtraSelection diagnostic;
    diagnostic.cursor = QTextCursor(editor.document());
    diagnostic.cursor.setPosition(decorations[2].startPosition);
    diagnostic.cursor.setPosition(decorations[2].startPosition + decorations[2].length, QTextCursor::KeepAnchor);
    diagnostic.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    diagnostic.format.setUnderlineColor(Qt::red);
    selections.append(diagnostic);
    editor.setExtraSelections(selections);
    checkPixels(editor, QStringLiteral("semantic-diagnostic"));
    selections.removeLast();
    editor.setExtraSelections(selections);
    {
        MyCodeEditor second;
        second.attachSharedDocument(editor.document());
        second.resize(700, 550);
        auto alternateColors = decorations;
        for (auto& decoration : alternateColors) decoration.role = SemanticDecorationRole::FormalPort;
        second.setSemanticDecorations(alternateColors);
        second.show();
        checkPixels(editor, QStringLiteral("semantic-shared-first"));
        checkPixels(second, QStringLiteral("semantic-shared-second"));
    }
    editor.moveCursor(QTextCursor::End);
    editor.insertPlainText(QStringLiteral("// edit remains live"));
    checkPixels(editor, QStringLiteral("semantic-edit"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), source);
    editor.moveCursor(QTextCursor::Start);
    editor.resize(620, 700);
    checkPixels(editor, QStringLiteral("semantic-narrow"));
    editor.horizontalScrollBar()->setValue(80);
    checkPixels(editor, QStringLiteral("semantic-horizontal"));
    editor.verticalScrollBar()->setValue(12);
    checkPixels(editor, QStringLiteral("semantic-scroll"));
    ApplicationThemeManager::instance().setMode(ThemeMode::Dark);
    checkPixels(editor, QStringLiteral("semantic-dark"));
    editor.moveCursor(QTextCursor::NextWord, QTextCursor::KeepAnchor);
    checkPixels(editor, QStringLiteral("semantic-selection"));
    QCOMPARE(editor.toPlainText(), source);
    ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    QApplication::setCursorFlashTime(flashTime);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (!initializeUiStyleForTest()) return 3;
    EditorBackgroundTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "editor_background_test.moc"
