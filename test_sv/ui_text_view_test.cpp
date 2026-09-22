#include "activitylogpanelcoordinator.h"
#include "activitylogservice.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "testuistyle.h"
#include "uicontrols.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStyleOptionFrame>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaEventBus.h"
#endif

namespace {
void settle() { QApplication::processEvents(); QTest::qWait(20); }
bool usesEla() { return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela; }
QStringList lines() {
    QStringList result;
    for (int i = 0; i < 160; ++i)
        result.append(QStringLiteral("line %1: ").arg(i) + QString(180, QLatin1Char('x')));
    return result;
}
}

class UiTextViewTest final : public QObject {
    Q_OBJECT
private slots:
    void documentFontSelectionAndTheme() {
        std::unique_ptr<QPlainTextEdit> text(UiControls::readOnlyText(lines().join('\n')));
        QCOMPARE(text->inherits("ElaPlainTextEdit"), usesEla());
        QVERIFY(text->isReadOnly());
        const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        text->setFont(mono);
        text->setLineWrapMode(QPlainTextEdit::NoWrap);
        text->resize(380, 220); text->show(); text->activateWindow(); settle();
        const auto resolvedFont = text->font();
        QCOMPARE(resolvedFont.family(), mono.family());
        QCOMPARE(resolvedFont.pointSizeF(), mono.pointSizeF());
        auto* document = text->document();
        const auto documentFont = document->defaultFont();
        const auto value = text->toPlainText();
        QTextCursor cursor(document);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, 60);
        cursor.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
        text->setTextCursor(cursor);
        text->ensureCursorVisible();
        text->horizontalScrollBar()->setValue(70);
        const auto selection = text->textCursor().selectedText();
        const int vertical = text->verticalScrollBar()->value();
        for (auto mode : {ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinLatte, ThemeMode::CatppuccinMocha}) {
            ApplicationThemeManager::instance().setMode(mode); settle();
            QCOMPARE(text->document(), document);
            QCOMPARE(text->font(), resolvedFont);
            QCOMPARE(document->defaultFont(), documentFont);
            QCOMPARE(text->toPlainText(), value);
            QCOMPARE(text->textCursor().selectedText(), selection);
            QCOMPARE(text->verticalScrollBar()->value(), vertical);
            QCOMPARE(text->horizontalScrollBar()->value(), 70);
            QCOMPARE(text->palette().color(QPalette::Text), usesEla()
                ? qApp->palette().color(QPalette::Text) : InsightVisualStyle::theme().textPrimary);
            QVERIFY(!text->grab().isNull());
        }
        QTest::keyClicks(text.get(), "must not alter readonly content");
        QCOMPARE(text->toPlainText(), value);
        QTest::keyClick(text.get(), Qt::Key_End, Qt::ControlModifier);
        QVERIFY(text->verticalScrollBar()->value() > vertical);
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }

    void localizedCopyMenuAndLifetime() {
        auto text = std::unique_ptr<QPlainTextEdit>(UiControls::readOnlyText("alpha\nbeta"));
        text->resize(350, 200); text->show(); text->activateWindow(); text->setFocus(); settle();
        for (const bool select : {false, true}) {
            if (select) text->selectAll();
            std::unique_ptr<QMenu> standard(text->createStandardContextMenu());
            QStringList labels; QList<bool> enabled;
            for (auto* action : standard->actions()) { labels << action->text(); enabled << action->isEnabled(); }
            bool observed = false;
            QTimer::singleShot(0, text.get(), [&] {
                auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (!popup) return;
                const auto dismiss = qScopeGuard([guard = QPointer<QMenu>(popup)] { if (guard) guard->close(); });
                observed = true;
                const auto actions = popup->actions();
                QCOMPARE(actions.size(), labels.size());
                QCOMPARE(popup->inherits("ElaMenu"), usesEla());
                for (int i = 0; i < actions.size(); ++i) {
                    QCOMPARE(actions[i]->text(), labels[i]);
                    QCOMPARE(actions[i]->isEnabled(), enabled[i]);
                }
                auto* copy = popup->findChild<QAction*>(QStringLiteral("edit-copy"));
                QVERIFY(copy);
                QCOMPARE(copy->isEnabled(), select);
                if (select) {
                    popup->setActiveAction(copy);
                    QTest::keyClick(popup, Qt::Key_Return);
                } else QTest::keyClick(popup, Qt::Key_Escape);
            });
            const QPoint at(30, 30);
            QContextMenuEvent event(QContextMenuEvent::Mouse, at, text->viewport()->mapToGlobal(at));
            QApplication::sendEvent(text->viewport(), &event);
            settle();
            QVERIFY(observed);
            QVERIFY(!QApplication::activePopupWidget());
        }
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("alpha\nbeta"));
        QCOMPARE(text->toPlainText(), QStringLiteral("alpha\nbeta"));

        if (usesEla()) {
            const QPoint at(30, 30);
            QContextMenuEvent event(QContextMenuEvent::Mouse, at, text->viewport()->mapToGlobal(at));
            QApplication::sendEvent(text->viewport(), &event); settle();
            QPointer<QMenu> popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            QVERIFY(popup);
            text.reset();
            QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); settle();
            QVERIFY(popup.isNull());
        }
    }

    void nativeFocusCustomPaletteAndTeardown() {
        if (!usesEla()) QSKIP("Ela-specific style ownership and focus-bus contract");
        auto countStyles = [] {
            int count = 0;
            for (auto* object : qApp->children()) if (object->inherits("ElaPlainTextEditStyle")) ++count;
            return count;
        };
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const int before = countStyles();
        for (int i = 0; i < 8; ++i) {
            auto host = std::make_unique<QWidget>();
            auto* layout = new QVBoxLayout(host.get());
            auto* text = UiControls::readOnlyText("symbol detail", host.get()); layout->addWidget(text);
            auto* other = UiControls::pushButton("Other", host.get()); layout->addWidget(other);
            QPalette palette = text->palette();
            palette.setColor(QPalette::Text, QColor("#238577"));
            palette.setColor(QPalette::Base, QColor("#e8f4ec"));
            text->setPalette(palette);
            host->resize(380, 250); host->show(); host->activateWindow();
            text->setFocus(Qt::TabFocusReason); settle();
            QVERIFY(text->hasFocus());
#ifdef ZEROSLACK_ENABLE_ELA
            ElaEventBus::getInstance()->post("WMWindowClicked", {
                {"WMClickType", QVariant::fromValue(ElaAppBarType::WMLBUTTONDOWN)}});
            QVERIFY(text->hasFocus());
#endif
            text->grab();
            QCOMPARE(text->palette().color(QPalette::Text), QColor("#238577"));
            ApplicationThemeManager::instance().setMode(i % 2 ? ThemeMode::Light : ThemeMode::Dark);
            text->grab();
            QCOMPARE(text->palette().color(QPalette::Text), QColor("#238577"));
            QCOMPARE(text->palette().color(QPalette::Base), QColor("#e8f4ec"));
            QTest::keyClick(text, Qt::Key_Tab);
            QVERIFY(other->hasFocus());
            text->setFocus(Qt::TabFocusReason); settle();
            // Destroy while the focus mark is still moving.
            host.reset();
            QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); settle();
            QCOMPARE(countStyles(), before);
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);

        std::unique_ptr<QPlainTextEdit> frameless(UiControls::readOnlyText());
        frameless->setFrameShape(QFrame::NoFrame);
        QImage image(100, 80, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        QStyleOptionFrame option; option.initFrom(frameless.get());
        option.rect = image.rect(); option.frameShape = QFrame::NoFrame;
        QPainter painter(&image);
        frameless->style()->drawControl(QStyle::CE_ShapedFrame, &option, &painter, frameless.get());
        painter.end();
        QImage empty(image.size(), image.format()); empty.fill(Qt::transparent);
        QCOMPARE(image, empty);
    }

    void scrollAreaReachabilityAndOwnership() {
        std::unique_ptr<QScrollArea> area(UiControls::scrollArea());
        QCOMPARE(area->inherits("ElaScrollArea"), usesEla());
        QCOMPARE(area->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
        QCOMPARE(area->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
        auto* content = new QWidget; content->resize(1400, 1400);
        auto* target = new QPushButton("Reachable", content); target->setGeometry(1100, 1150, 160, 50);
        QSignalSpy clicked(target, &QPushButton::clicked);
        area->setWidget(content); area->resize(330, 220); area->show(); settle();
        for (auto* bar : {area->horizontalScrollBar(), area->verticalScrollBar()}) {
            QCOMPARE(bar->inherits("ElaScrollBar"), usesEla());
            QVERIFY(bar->isVisible()); QVERIFY(bar->maximum() > 0);
            QVERIFY(!bar->property("smoothWheelEnabled").toBool());
        }
        area->ensureWidgetVisible(target); settle();
        QVERIFY(area->viewport()->rect().contains(QRect(target->mapTo(area->viewport(), QPoint()), target->size())));
        QTest::mouseClick(target, Qt::LeftButton); QCOMPARE(clicked.count(), 1);
        area->horizontalScrollBar()->setValue(0);
        area->verticalScrollBar()->setValue(0);
        content->resize(100, 100); settle();
        QVERIFY(!area->horizontalScrollBar()->isVisible());
        QVERIFY(!area->verticalScrollBar()->isVisible());
        QPointer<QWidget> ownedContent(content);
        area.reset(); settle(); QVERIFY(ownedContent.isNull());
    }

    void activityAppendTailAndHiddenRefresh() {
        auto* service = ActivityLogService::getInstance(); service->clear();
        QMainWindow host;
        ActivityLogPanelCoordinator coordinator(&host);
        host.addDockWidget(Qt::BottomDockWidgetArea, coordinator.dock());
        host.resize(500, 300); host.show(); settle();
        auto* text = host.findChild<QPlainTextEdit*>("activityOutputText"); QVERIFY(text);
        QCOMPARE(text->inherits("ElaPlainTextEdit"), usesEla());
        for (int i = 0; i < 80; ++i) service->append("Test", ActivityLogLevel::Info, QString::number(i));
        settle();
        auto* bar = text->verticalScrollBar(); QVERIFY(bar->maximum() > 0);
        QCOMPARE(bar->value(), bar->maximum());
        bar->setValue(0);
        service->append("Test", ActivityLogLevel::Info, "Preserve position"); settle();
        QCOMPARE(bar->value(), 0);
        QVERIFY(text->toPlainText().contains("Preserve position"));
        coordinator.dock()->hide(); settle();
        service->append("Test", ActivityLogLevel::Warning, "Hidden event", -1, {}, true); settle();
        QVERIFY(!text->toPlainText().contains("Hidden event"));
        QVERIFY(service->unreadCount() > 0);
        coordinator.dock()->show(); settle();
        QVERIFY(text->toPlainText().contains("Hidden event"));
        QCOMPARE(service->unreadCount(), 0);
        service->clear(); settle(); QVERIFY(text->toPlainText().isEmpty());
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    QTemporaryDir profile; if (!profile.isValid()) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
    if (!initializeUiStyleForTest()) return 3;
    ApplicationThemeManager::instance().applyToApplication();
    UiTextViewTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_text_view_test.moc"
