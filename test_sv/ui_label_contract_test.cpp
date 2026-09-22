#include "applicationthememanager.h"
#include "testuistyle.h"
#include "uicontrols.h"
#include "uitypography.h"
#include "workspaceconfigurationdialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <memory>

namespace {
void settle() { QApplication::processEvents(); QTest::qWait(30); }
bool usesEla() { return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela; }
const auto modes = {ThemeMode::Light, ThemeMode::Dark, ThemeMode::CatppuccinLatte, ThemeMode::CatppuccinMocha};
}

class UiLabelContractTest final : public QObject {
    Q_OBJECT
private slots:
    void textSizingFontsAndInteraction() {
        std::unique_ptr<QLabel> label;
        const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        for (auto font : {UiTypography::font(UiTypography::Role::Body),
                          UiTypography::font(UiTypography::Role::Metadata),
                          UiTypography::font(UiTypography::Role::PageTitle), mono}) {
            label.reset(UiControls::label("alpha beta"));
            QCOMPARE(label->inherits("ElaText"), usesEla());
            QVERIFY(!label->wordWrap());
            QCOMPARE(label->textFormat(), Qt::AutoText);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
            label->setFont(font);
            label->resize(400, 90); label->show(); label->activateWindow(); settle();
            label->setSelection(6, 4);
            const auto resolved = label->font();
            for (auto mode : modes) {
                ApplicationThemeManager::instance().setMode(mode); settle();
                QCOMPARE(label->font(), resolved);
                QCOMPARE(label->selectedText(), QStringLiteral("beta"));
                QLabel reference(label->text()); reference.setFont(resolved);
                reference.setTextInteractionFlags(label->textInteractionFlags()); reference.ensurePolished();
                QCOMPARE(label->sizeHint(), reference.sizeHint());
            }
        }
        label->setText(QStringLiteral("A long explanation with several words. ").repeated(12));
        label->setWordWrap(true);
        QLabel reference(label->text()); reference.setFont(label->font()); reference.setWordWrap(true);
        reference.setTextInteractionFlags(label->textInteractionFlags()); reference.ensurePolished();
        QCOMPARE(label->heightForWidth(200), reference.heightForWidth(200));
        QVERIFY(label->heightForWidth(200) > label->heightForWidth(400));

        label->setText("<a href=\"symbol:WIDTH\">WIDTH</a>");
        label->setTextInteractionFlags(Qt::LinksAccessibleByKeyboard | Qt::LinksAccessibleByMouse);
        label->setOpenExternalLinks(false);
        label->setAlignment(Qt::AlignCenter);
        QSignalSpy activated(label.get(), &QLabel::linkActivated);
        settle();
        QTest::mouseClick(label.get(), Qt::LeftButton, Qt::NoModifier, label->rect().center());
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().toString(), QStringLiteral("symbol:WIDTH"));
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }

    void semanticAndDisabledColors() {
        QWidget host; host.resize(500, 150);
        auto* label = UiControls::label("Warning: unresolved symbol", &host);
        QLabel reference(label->text(), &host);
        reference.setFont(label->font());
        label->setGeometry(10, 10, 450, 45); reference.setGeometry(10, 70, 450, 45);
        QPalette palette;
        palette.setColor(QPalette::Active, QPalette::WindowText, QColor("#ba6246"));
        palette.setColor(QPalette::Inactive, QPalette::WindowText, QColor("#ba6246"));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#727987"));
        label->setPalette(palette); reference.setPalette(palette);
        host.show(); host.activateWindow(); settle();
        for (const auto& css : {QString(), QStringLiteral("QLabel { color: #389687; } QLabel:disabled { color: #8a8196; }")}) {
            label->setStyleSheet(css); reference.setStyleSheet(css);
            for (auto mode : modes) {
                ApplicationThemeManager::instance().setMode(mode); settle();
                for (bool enabled : {true, false}) {
                    label->setEnabled(enabled); reference.setEnabled(enabled); settle();
                    const auto expected = reference.grab().toImage();
                    const auto actual = label->grab().toImage();
                    QCOMPARE(label->palette().color(label->foregroundRole()),
                             reference.palette().color(reference.foregroundRole()));
                    QCOMPARE(actual, expected);
                }
            }
        }
        ApplicationThemeManager::instance().setMode(ThemeMode::Light);
    }

    void formBuddiesEmptyRowsAndOwnership() {
        auto host = std::make_unique<QWidget>();
        auto* form = new QFormLayout(host.get());
        auto* field = UiControls::lineEdit(host.get());
        auto* other = UiControls::lineEdit(host.get());
        UiControls::addFormRow(form, "&Name", field);
        UiControls::addFormRow(form, QString(), other);
        auto* caption = qobject_cast<QLabel*>(form->labelForField(field));
        QVERIFY(caption); QCOMPARE(caption->inherits("ElaText"), usesEla());
        QCOMPARE(caption->buddy(), field);
        QWidget referenceHost;
        QFormLayout reference(&referenceHost);
        auto* referenceField = new QLineEdit;
        reference.addRow(QString(), referenceField);
        QCOMPARE(form->labelForField(other) == nullptr, reference.labelForField(referenceField) == nullptr);
        host->resize(450, 160); host->show(); host->activateWindow(); other->setFocus(); settle();
        QTest::keyClick(other, Qt::Key_N, Qt::AltModifier); settle();
        QVERIFY(field->hasFocus());
        QVERIFY(caption->width() >= caption->minimumSizeHint().width());
        QPointer<QLabel> ownedCaption(caption); QPointer<QLineEdit> ownedField(field);
        host.reset(); settle();
        QVERIFY(ownedCaption.isNull()); QVERIFY(ownedField.isNull());
    }

    void workspaceConfigurationScrollReachability() {
        auto dialog = std::make_unique<WorkspaceConfigurationDialog>();
        WorkspaceConfiguration config;
        config.topModule = QStringLiteral("counter");
        dialog->setConfiguration(config);
        auto* scroll = dialog->findChild<QScrollArea*>("workspaceConfigurationScroll");
        QVERIFY(scroll); QCOMPARE(scroll->inherits("ElaScrollArea"), usesEla());
        for (const QSize size : {QSize(800, 620), QSize(1100, 760), QSize(800, 620)}) {
            dialog->resize(size); dialog->show(); settle();
            int views = 0;
            for (auto* view : scroll->widget()->findChildren<QAbstractItemView*>()) {
                if (qobject_cast<QHeaderView*>(view)) continue;
                ++views;
                QVERIFY(view->height() <= qMax(view->minimumSizeHint().height(), scroll->viewport()->height()));
                scroll->ensureWidgetVisible(view, 0, 0); settle();
                const QRect rect(view->mapTo(scroll->viewport(), QPoint()), view->size());
                QVERIFY2(scroll->viewport()->rect().contains(rect), qPrintable(view->objectName()));
            }
            QCOMPARE(views, 5);
            QCOMPARE(dialog->configuration().topModule, config.topModule);
        }
        QPointer<QScrollArea> ownedScroll(scroll);
        dialog->resize(900, 680);
        // Destruction must cancel the queued viewport-size adjustment.
        dialog.reset(); settle();
        QVERIFY(ownedScroll.isNull());
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
    UiLabelContractTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_label_contract_test.moc"
