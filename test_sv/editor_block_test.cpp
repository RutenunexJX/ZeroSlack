#include "mycodeeditor.h"
#include "documentblockmodel.h"
#include "shareddocument.h"
#include "formatterservice.h"
#include "documentblockstore.h"
#include "tabmanager.h"
#include "tsdocument.h"
#include "pinloompreviewimage.h"
#include "blocknotestyle.h"
#include <QPlainTextDocumentLayout>
#include <QtTest>
#include <QtWidgets>

class EditorBlockTest : public QObject {
    Q_OBJECT
    std::unique_ptr<MyCodeEditor> editor;
    DocumentBlockModel* model() { return editor->blockModel(); }
    QString insert(int position = 0) {
        QTextCursor cursor(editor->document()); cursor.setPosition(position);
        EditorAssociatedBlock fixed; fixed.type = "text"; fixed.text = "note";
        return model()->insert(fixed, cursor);
    }
private slots:
    void init() {
        editor = std::make_unique<MyCodeEditor>(); editor->resize(800, 500);
        editor->setPlainText("alpha\nbeta\ngamma\n"); editor->document()->setModified(false);
        editor->show(); editor->setFocus(); QCoreApplication::processEvents();
        const auto expected=qEnvironmentVariable("ZEROSLACK_EXPECTED_DPR");
        if (!expected.isEmpty()) QCOMPARE(editor->devicePixelRatioF(),expected.toDouble());
    }
    void cleanup() { editor.reset(); BlockNoteStyle::instance().apply({}); }
    void independentSourceAndSharedAuthority() {
        const auto source = editor->toPlainText(); const int count = editor->blockCount();
        const auto id = insert(3); QVERIFY(!id.isEmpty()); QVERIFY(model()->dirty());
        QVERIFY(!editor->document()->isModified()); QCOMPARE(editor->toPlainText(), source); QCOMPARE(editor->blockCount(), count);
        auto* shared = editor->document(); MyCodeEditor other; other.attachSharedDocument(shared);
        QCOMPARE(other.blockModel(), model()); model()->editText(id, "other view"); QCOMPARE(other.blockModel()->find(id)->text, "other view");
        auto* detached = new QTextDocument(&other); detached->setDocumentLayout(new QPlainTextDocumentLayout(detached)); other.attachSharedDocument(detached);
    }
    void mixedUndoRedo() {
        QTextCursor cursor(editor->document()); cursor.setPosition(2); editor->setTextCursor(cursor);
        QTest::keyClicks(editor.get(), "1"); const auto withFirst = editor->toPlainText();
        const auto id = insert(2);
        QTest::keyClicks(editor.get(), "2"); const auto withSecond = editor->toPlainText();
        model()->beginTextSession(id); model()->editText(id, "edited"); model()->editText(id, "edited note"); model()->endTextSession();
        QVERIFY(model()->remove(id));
        editor->undo(); QVERIFY(model()->find(id)); QCOMPARE(model()->find(id)->text, "edited note");
        editor->undo(); QCOMPARE(model()->find(id)->text, "note");
        editor->undo(); QCOMPARE(editor->toPlainText(), withFirst); QVERIFY(model()->find(id));
        editor->undo(); QVERIFY(!model()->find(id)); QCOMPARE(editor->toPlainText(), withFirst);
        editor->undo(); QCOMPARE(editor->toPlainText(), "alpha\nbeta\ngamma\n");
        editor->redo(); QCOMPARE(editor->toPlainText(), withFirst);
        editor->redo(); QVERIFY(model()->find(id));
        editor->redo(); QCOMPARE(editor->toPlainText(), withSecond);
        editor->redo(); QCOMPARE(model()->find(id)->text, "edited note");
        editor->redo(); QVERIFY(!model()->find(id));
    }
    void missingAnchorRetainsContentAndUndo() {
        const auto id = insert(1);
        QTextCursor cursor(editor->document()); cursor.setPosition(0); cursor.setPosition(6, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor); QTest::keyClick(editor.get(), Qt::Key_Delete);
        QVERIFY(model()->find(id)->missing); QCOMPARE(model()->find(id)->text, "note");
        editor->undo(); QVERIFY(!model()->find(id)->missing); QCOMPARE(editor->toPlainText(), "alpha\nbeta\ngamma\n");
        cursor = editor->textCursor(); cursor.setPosition(8); QVERIFY(model()->reattach(id, cursor));
        QVERIFY(model()->find(id)->start >= 6);
        editor->undo(); QCOMPARE(model()->find(id)->start, 0);
    }
    void shellFocusAndDelete() {
        const auto id = insert(3); editor->selectAssociatedBlock(id, true);
        auto* text = editor->findChild<QTextEdit*>("editorBlockText"); QVERIFY(text); QVERIFY(text->hasFocus());
        QTest::keyClicks(text, "more"); QVERIFY(model()->find(id)->text.contains("more"));
        QTest::keyClick(text, Qt::Key_Escape);
        auto* shell = editor->findChild<QFrame*>("editorBlockShell"); QVERIFY(shell && shell->hasFocus());
        QTest::keyClick(shell, Qt::Key_Escape); QVERIFY(editor->hasFocus()); QCOMPARE(editor->textCursor().position(), 3);
        QTest::keyClick(editor.get(), Qt::Key_Down); QVERIFY(shell->hasFocus());
        QTest::keyClick(shell, Qt::Key_Delete); QVERIFY(!model()->find(id));
        QTest::keyClick(editor.get(), Qt::Key_Z, Qt::ControlModifier); QVERIFY(model()->find(id));
    }
    void geometryAndVirtualization_data() {
        QTest::addColumn<int>("blocks"); QTest::newRow("none") << 0; QTest::newRow("ten") << 10; QTest::newRow("hundred") << 100;
    }
    void evictedTextLayoutIsRestored() {
        editor->setPlainText("first_line\nrendered_second_line\nlast_line\n");
        insert(editor->document()->findBlockByNumber(2).position());
        QCoreApplication::processEvents(); editor->viewport()->repaint();
        const auto block = editor->document()->findBlockByNumber(1);
        auto* layout = block.layout(); QVERIFY(layout && layout->lineCount() > 0);
        const auto revision = block.revision();
        const auto before = editor->viewport()->grab().toImage();
        const auto geometry = editor->blockGeometry(1);
        const auto scale = before.devicePixelRatio();
        const QRect row(0, qCeil(geometry.top * scale), before.width(), qFloor(geometry.height * scale));
        // Qt may evict shaped lines without changing the document's text revision.
        layout->clearLayout(); QCOMPARE(block.revision(), revision); QCOMPARE(layout->lineCount(), 0);
        editor->viewport()->repaint();
        QVERIFY2(layout->lineCount() > 0, "Cached row height must not suppress rebuilding evicted text layout");
        const auto after = editor->viewport()->grab().toImage();
        QCOMPARE(after.copy(row), before.copy(row));
    }
    void gutterAndImageUseProjectedSpace() {
        auto palette=editor->palette();
        palette.setColor(QPalette::AlternateBase,QColor(219,228,234));
        editor->setPalette(palette);
        insert(); QCoreApplication::processEvents(); editor->repaint();
        auto* gutter=editor->findChild<QWidget*>("editorLineNumberGutter"); QVERIFY(gutter);
        const auto capture=gutter->grab().toImage(); const auto scale=capture.devicePixelRatio();
        const int from=qCeil((editor->blockGeometry(0).top+editor->blockGeometry(0).height+3)*scale);
        const int to=qFloor((editor->blockGeometry(1).top-3)*scale);
        QVERIFY(to>from+50);
        const auto background=editor->palette().color(QPalette::AlternateBase).rgb();
        for(int y=from;y<to;++y) for(int x=qCeil(18*scale);x<qFloor((gutter->width()-20)*scale);++x) {
            if (capture.pixel(x,y)!=background) {
                qWarning()<<"gutter pixel"<<QPoint(x,y)<<Qt::hex<<capture.pixel(x,y)<<background
                    <<"gap"<<from<<to<<"gutter"<<gutter->geometry()<<"viewport"<<editor->viewport()->geometry();
                const auto artifacts=qEnvironmentVariable("ZEROSLACK_TEST_ARTIFACT_DIR");
                if (!artifacts.isEmpty()) capture.save(artifacts+"/gutter-gap.png");
            }
            QVERIFY2(capture.pixel(x,y)==background,"Line numbers must not appear in association space");
        }
        QTemporaryDir root; QVERIFY(root.isValid()); QImage original(240,120,QImage::Format_RGB32); original.fill(Qt::blue);
        const auto path=root.filePath("preview.png"); QVERIFY(original.save(path)); QString error;
        EditorAssociatedBlock block; block.type="image"; block.height=150; block.width=360;
        block.asset=DocumentBlockStore::get(model())->importImage(path,&error); QVERIFY2(!block.asset.isEmpty(),qPrintable(error));
        const auto id=model()->insert(block,QTextCursor(editor->document())); QVERIFY(!id.isEmpty());
        PinloomPreviewImageLabel* preview=nullptr;
        const auto imageReady=[&] {
            for(auto* label:editor->findChildren<QLabel*>())
                if(auto* image=dynamic_cast<PinloomPreviewImageLabel*>(label)) preview=image;
            return preview && !preview->sourcePixmap().isNull();
        };
        QTRY_VERIFY(imageReady());
        QCoreApplication::processEvents();
        QVERIFY(preview->width()>=200); QVERIFY(preview->height()>=80);
        QVERIFY(preview->pixmap().width()>=100);
    }
    void geometryAndVirtualization() {
        QFETCH(int, blocks);
        QString source; source.reserve(320000); for (int i = 0; i < 20000; ++i) source += "wire value_" + QString::number(i) + ";\n";
        QElapsedTimer timer; timer.start(); editor->setPlainText(source); const auto opening = timer.nsecsElapsed();
        const auto before = editor->viewProjectionMetricsForTest();
        for (int i = 0; i < blocks; ++i) insert(editor->document()->findBlockByNumber(i * 150).position());
        editor->viewport()->update(); QTest::qWait(10);
        const auto metrics = editor->viewProjectionMetricsForTest();
        QCOMPARE(metrics.sourceRowsVisitedDuringRebuild, before.sourceRowsVisitedDuringRebuild);
        QVERIFY(metrics.sourceRowsMeasured < 250); QVERIFY(editor->associatedBlockViewCount() < 10);
        QCOMPARE(editor->toPlainText(), source);
        if (blocks) {
            QVERIFY(editor->blockGeometry(1).top - editor->blockGeometry(0).top > 150);
            const auto block = editor->document()->findBlockByNumber(1); QTextCursor cursor(block);
            const auto rect = editor->cursorRect(cursor);
            QCOMPARE(editor->cursorForPosition(rect.center()).blockNumber(), 1);
        }
        timer.restart(); QTest::keyClicks(editor.get(), "x"); const auto input = timer.nsecsElapsed();
        timer.restart(); editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum() / 2); QTest::qWait(10); const auto scroll = timer.nsecsElapsed();
        qInfo().noquote() << QString("PERF blocks=%1 open_ns=%2 input_ns=%3 scroll_ns=%4 measured_rows=%5 views=%6")
            .arg(blocks).arg(opening).arg(input).arg(scroll).arg(editor->viewProjectionMetricsForTest().sourceRowsMeasured).arg(editor->associatedBlockViewCount());
    }
    void modelPersistenceValidation() {
        const auto id = insert(2); const auto json = model()->serialize();
        MyCodeEditor other; other.setPlainText(editor->toPlainText()); QString error;
        QVERIFY(other.blockModel()->restore(json, &error)); QCOMPARE(other.blockModel()->find(id)->text, "note");
        auto future = json; future["version"] = 9; QVERIFY(!other.blockModel()->restore(future, &error));
        QCOMPARE(other.blockModel()->find(id)->text, "note");
    }
    void formattingMapsAnchorsAndUndo() {
        const QString source="module m;\nlogic a;\nalways_comb begin\na=1;\nend\nendmodule\n";
        editor->setPlainText(source); const auto id=insert(source.indexOf("a=1"));
        const auto before=*model()->find(id); const auto report=editor->formatDocument(); QVERIFY2(report.accepted(),qPrintable(report.diagnostic)); QVERIFY(report.changed);
        const auto formatted=*model()->find(id); QVERIFY(!formatted.missing);
        QCOMPARE(editor->document()->findBlock(formatted.start).text().trimmed(),QString("a=1;"));
        editor->undo(); QCOMPARE(editor->toPlainText(),source); QVERIFY(!model()->find(id)->missing); QCOMPARE(model()->find(id)->start,before.start);
        editor->redo(); QCOMPARE(editor->toPlainText(),report.formattedText); QVERIFY(!model()->find(id)->missing); QCOMPARE(model()->find(id)->start,formatted.start);
    }
    void emptyEndsAndOrderedBlocks() {
        editor->setPlainText({}); const auto empty=insert(); QCoreApplication::processEvents();
        QCOMPARE(editor->blockCount(),1); QCOMPARE(editor->toPlainText(),QString()); QVERIFY(model()->find(empty));
        editor->undo(); QVERIFY(model()->items().isEmpty());
        editor->setPlainText("first\nlast"); const auto first=insert(2); const auto second=insert(3); const auto last=insert(10);
        QCOMPARE(model()->find(first)->end,5); QCOMPARE(model()->find(last)->end,10);
        QVERIFY(model()->find(second)->order>model()->find(first)->order); QVERIFY(model()->reorder(second,-1));
        QVERIFY(model()->find(second)->order<model()->find(first)->order);
        auto value=*model()->find(first); value.collapsed=true; QVERIFY(model()->update(value));
        editor->undo(); QVERIFY(!model()->find(first)->collapsed); editor->redo(); QVERIFY(model()->find(first)->collapsed);
        QCOMPARE(editor->toPlainText(),QString("first\nlast"));
    }
    void wrappedGeometryAndSelectionReturn() {
        QString source="module m;\n"+QString("long_identifier + ").repeated(24)+"0;\nendmodule\n";
        editor->setPlainText(source); editor->setLineWrapMode(QPlainTextEdit::WidgetWidth); editor->resize(360,640);
        QTextCursor selection(editor->document()); selection.setPosition(12); selection.setPosition(50,QTextCursor::KeepAnchor); editor->setTextCursor(selection);
        const auto id=model()->insert("text",selection,"selected range"); editor->selectAssociatedBlock(id,true);
        auto* text=editor->findChild<QTextEdit*>("editorBlockText"); QVERIFY(text);
        QTest::keyClick(text,Qt::Key_Escape); auto* shell=editor->findChild<QFrame*>("editorBlockShell"); QVERIFY(shell);
        QTest::keyClick(shell,Qt::Key_Escape); QCOMPARE(editor->textCursor(),selection);
        for (int width : {360,900,360}) {
            editor->resize(width,640); QCoreApplication::processEvents(); editor->viewport()->repaint();
            QTextCursor cursor(editor->document()); cursor.setPosition(45); editor->setTextCursor(cursor); editor->ensureCursorVisible();
            const auto rect=editor->cursorRect(cursor); QCOMPARE(editor->cursorForPosition(rect.center()).position(),cursor.position());
            QVERIFY(editor->blockGeometry(2).top>editor->blockGeometry(1).top+100);
        }
        QCOMPARE(editor->toPlainText(),source);
    }
    void persistedPerformance_data() { geometryAndVirtualization_data(); }
    void automaticNotesAreViewMetrics() {
        const auto code = editor->toPlainText(); const auto codeFont = editor->font();
        const auto id = model()->insert("text", QTextCursor(editor->document()), QString("A readable note with context. ").repeated(12));
        QVERIFY(model()->find(id)->autoHeight); model()->markSaved();
        editor->selectAssociatedBlock(id, true); QCoreApplication::processEvents();
        auto* shell = editor->findChild<QFrame*>("editorBlockShell"); QVERIFY(shell);
        const auto original = model()->serialize(); const auto generation = model()->revision();
        const int wideHeight = shell->height();
        editor->resize(340, 700); QCoreApplication::processEvents();
        QVERIFY(shell->height() > wideHeight);
        const int narrowHeight = shell->height();
        BlockNoteStyle::instance().apply({{"blockNotes.sizePx", 22}, {"blockNotes.lineHeight", 2.2}});
        QCoreApplication::processEvents(); QVERIFY(shell->height() > narrowHeight);
        QCOMPARE(model()->serialize(), original); QCOMPARE(model()->revision(), generation); QVERIFY(!model()->dirty());
        QCOMPARE(editor->font(), codeFont); QCOMPARE(editor->toPlainText(), code); QVERIFY(!editor->document()->isModified());
        auto* text = shell->findChild<QTextEdit*>("editorBlockText"); QVERIFY(text);
        QCOMPARE(text->document()->defaultFont().pixelSize(), 22);
        QVERIFY(text->verticalScrollBar()->maximum() <= 1);
        auto overridden = *model()->find(id); overridden.appearance = {{"style", "Outlined"}, {"background", "#fff4cc"}};
        QVERIFY(model()->update(overridden)); QVERIFY(model()->dirty());
        editor->undo(); QCOMPARE(model()->serialize(), original); QVERIFY(!model()->dirty());
    }
    void legacyHeightAndAppearanceValidation() {
        const auto id = insert(); auto legacy = model()->serialize();
        auto entries = legacy["blocks"].toArray(); auto item = entries[0].toObject();
        item.remove("heightMode"); item.remove("appearance"); item.remove("detached"); entries[0] = item; legacy["blocks"] = entries;
        QString error; QVERIFY(model()->restore(legacy, &error)); QVERIFY(!model()->find(id)->autoHeight);
        QCOMPARE(model()->find(id)->height, 160);
        editor->selectAssociatedBlock(id); QCoreApplication::processEvents();
        auto* shell = editor->findChild<QFrame*>("editorBlockShell"); QVERIFY(shell); QCOMPARE(shell->height(),196);
        BlockNoteStyle::instance().apply({{"blockNotes.sizePx", 32}}); QCoreApplication::processEvents(); QCOMPARE(shell->height(),196);
        item["appearance"] = QJsonObject{{"background", "invalid-color"}}; entries[0] = item; legacy["blocks"] = entries;
        QVERIFY(!model()->restore(legacy, &error)); QVERIFY(model()->find(id)); QCOMPARE(model()->find(id)->height,160);
    }
    void linkOnlyReferencesUseTextGeometryWithoutPreviewErrors() {
        EditorAssociatedBlock link; link.type="reference"; link.autoHeight=true;
        link.uri="https://example.invalid/reference/"+QString("long-path/").repeated(12);
        const auto source=editor->toPlainText();
        const auto id=model()->insert(link,editor->textCursor()); model()->markSaved();
        editor->selectAssociatedBlock(id); QCoreApplication::processEvents();
        auto* shell=editor->findChild<QFrame*>("editorBlockShell"); QVERIFY(shell);
        auto* text=shell->findChild<QTextEdit*>("editorBlockReferenceText"); QVERIFY(text);
        QTRY_VERIFY(text->isVisible()); QVERIFY(text->toPlainText().contains(link.uri));
        QVERIFY(text->toPlainText().contains("Link only")); QVERIFY(text->isReadOnly());
        const auto saved=model()->serialize(); const auto revision=model()->revision();
        const int wide=shell->height();
        editor->resize(340,700); QCoreApplication::processEvents(); QVERIFY(shell->height()>wide);
        QVERIFY(text->verticalScrollBar()->maximum()<=1);
        const int narrow=shell->height();
        BlockNoteStyle::instance().apply({{"blockNotes.sizePx",22},{"blockNotes.lineHeight",2.0}});
        QCoreApplication::processEvents(); QVERIFY(shell->height()>narrow);
        qInfo()<<"LINK_GEOMETRY"<<editor->devicePixelRatioF()<<shell->size()<<text->size()<<text->viewport()->size()
               <<text->document()->size()<<text->verticalScrollBar()->maximum()<<text->document()->defaultFont();
        QVERIFY(text->verticalScrollBar()->maximum()<=1);
        QVERIFY(text->document()->size().height()<=text->viewport()->height()+1);
        QCOMPARE(model()->serialize(),saved); QCOMPARE(model()->revision(),revision); QVERIFY(!model()->dirty());
        QCOMPARE(editor->toPlainText(),source); QVERIFY(!editor->document()->isModified());
    }
    void foldedAnchorsKeepKeyboardOrder() {
        const QString source="module m;\ninitial begin\n    a = 1;\n    b = 2;\nend\nendmodule\n";
        editor->setPlainText(source); const auto first=insert(source.indexOf("a =")); const auto second=insert(source.indexOf("b ="));
        QTRY_VERIFY(!editor->syntaxDocument()->hasPendingEdits());
        QVERIFY(editor->toggleFoldAtLineForTest(1)); QCoreApplication::processEvents();
        QVERIFY(!editor->foldLineVisibleForTest(2)); QVERIFY(!model()->find(first)->missing);
        QTextCursor cursor(editor->document()); cursor.setPosition(source.indexOf("initial")); editor->setTextCursor(cursor); editor->setFocus();
        QTest::keyClick(editor.get(),Qt::Key_Down); auto* shell=qobject_cast<QFrame*>(QApplication::focusWidget()); QVERIFY(shell); QCOMPARE(shell->property("blockId").toString(),first);
        QTest::keyClick(shell,Qt::Key_Down); shell=qobject_cast<QFrame*>(QApplication::focusWidget()); QVERIFY(shell); QCOMPARE(shell->property("blockId").toString(),second);
        QTest::keyClick(shell,Qt::Key_Down); QVERIFY(editor->hasFocus()); QVERIFY(editor->textCursor().blockNumber()>=4);
        QTest::keyClick(editor.get(),Qt::Key_Up); shell=qobject_cast<QFrame*>(QApplication::focusWidget()); QVERIFY(shell); QCOMPARE(shell->property("blockId").toString(),second);
        QVERIFY(editor->toggleFoldAtLineForTest(1)); QCOMPARE(editor->toPlainText(),source);
    }
    void persistedPerformance() {
        QFETCH(int,blocks); QTemporaryDir root; QVERIFY(root.isValid()); const auto path=root.filePath("large.sv");
        QString source="module large;\n"; for(int i=0;i<20000;++i) source+="wire value_"+QString::number(i)+";\n"; source+="endmodule\n";
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(source.toUtf8()),source.toUtf8().size()); file.close();
        editor->hide(); editor->setPlainText(source); auto* store=DocumentBlockStore::get(model()); QString error; QVERIFY(store->bind(path,root.path(),&error));
        for(int i=0;i<blocks;++i) insert(editor->document()->findBlockByNumber(i*150).position());
        const auto hash=DocumentBlockStore::fileRevision(path); if(blocks) QVERIFY2(store->save(path,root.path(),hash,hash,false,{}, {},&error),qPrintable(error));
        QTabWidget tabs; tabs.resize(800,500); tabs.show(); TabManager manager(&tabs); manager.setWorkspaceScope({root.path()},root.path());
        QElapsedTimer timer; timer.start(); QVERIFY(manager.openFileInTab(path)); QCoreApplication::processEvents();
        auto* page=manager.getCurrentEditor(); QVERIFY(page); page->viewport()->repaint(); const auto opening=timer.nsecsElapsed();
        const auto startMetrics=page->viewProjectionMetricsForTest();
        QCOMPARE(page->blockModel()->items().size(),blocks); page->setFocus();
        timer.restart(); QTest::keyClicks(page,"x"); QCoreApplication::processEvents(); page->viewport()->repaint(); const auto input=timer.nsecsElapsed();
        timer.restart(); page->verticalScrollBar()->setValue(page->verticalScrollBar()->maximum()/2); QCoreApplication::processEvents(); page->viewport()->repaint(); const auto scroll=timer.nsecsElapsed();
        const auto metrics=page->viewProjectionMetricsForTest(); QCOMPARE(metrics.sourceRowsVisitedDuringRebuild,quint64(0)); QVERIFY(metrics.sourceRowsMeasured<250); QVERIFY(page->associatedBlockViewCount()<10);
        qInfo().noquote()<<QString("PERSISTED_PERF blocks=%1 open_ns=%2 input_ns=%3 scroll_ns=%4 rebuilds=%5 measured_rows=%6 views=%7 first_measured=%8")
            .arg(blocks).arg(opening).arg(input).arg(scroll).arg(metrics.rebuilds).arg(metrics.sourceRowsMeasured).arg(page->associatedBlockViewCount()).arg(startMetrics.sourceRowsMeasured);
        manager.unsavedDocumentManagerForTesting()->setDecisionProvider([](const auto&,QWidget*){return UnsavedDocumentBatchDecision::DiscardAll;}); QVERIFY(manager.closeAllTabs());
    }
};
QTEST_MAIN(EditorBlockTest)
#include "editor_block_test.moc"
