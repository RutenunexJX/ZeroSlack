#include "uidialogs.h"
#include "applicationthememanager.h"
#include "uicontrols.h"
#include "uitypography.h"

#include <QComboBox>
#include <QDialog>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
bool usesEla()
{
#ifdef ZEROSLACK_ENABLE_ELA
    return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela;
#else
    return false;
#endif
}

class InputDialog final : public QDialog {
public:
    InputDialog(QWidget* parent, const QString& title, const QString& prompt, QWidget* input)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("uiInputDialog"));
        setWindowTitle(title);
        setFont(UiTypography::font());
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 18, 20, 18);
        layout->setSpacing(12);
        auto* label = UiControls::label(prompt, this);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setBuddy(input);
        layout->addWidget(label);
        input->setObjectName(QStringLiteral("uiDialogInput"));
        layout->addWidget(input);
        auto* buttons = UiDialogs::buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        const int available = screen()->availableGeometry().width() - 40;
        resize(qMin(available, qMax(sizeHint().width(), fontMetrics().averageCharWidth() * 46)),
               sizeHint().height());
        input->setFocus(Qt::OtherFocusReason);
    }
};

void message(QWidget* parent, const QString& title, const QString& text, QMessageBox::Icon icon)
{
    QMessageBox box(parent);
    box.setWindowTitle(title);
    box.setIcon(icon);
    box.setText(text);
    auto* ok = UiDialogs::addButton(&box, QMessageBox::Ok);
    box.setDefaultButton(ok);
    box.setEscapeButton(ok);
    box.exec();
}
}

QPushButton* UiDialogs::addButton(QDialogButtonBox* box, const QString& text,
                                 QDialogButtonBox::ButtonRole role)
{
    auto* button = UiControls::pushButton(text, box);
    box->addButton(button, role);
    return button;
}

QPushButton* UiDialogs::addButton(QDialogButtonBox* box, QDialogButtonBox::StandardButton standard)
{
    if (!usesEla())
        return box->addButton(standard);
    QDialogButtonBox prototype(standard);
    auto* native = prototype.button(standard);
    if (!native)
        return nullptr;
    auto* button = addButton(box, native->text(), prototype.buttonRole(native));
    button->setObjectName(QStringLiteral("uiDialogButton_%1").arg(int(standard)));
    button->setProperty("uiDialogStandardButton", int(standard));
    return button;
}

QDialogButtonBox* UiDialogs::buttonBox(QDialogButtonBox::StandardButtons buttons, QWidget* parent)
{
    if (!usesEla())
        return new QDialogButtonBox(buttons, parent);
    auto* box = new QDialogButtonBox(parent);
    for (int value = QDialogButtonBox::FirstButton; value <= QDialogButtonBox::LastButton; value <<= 1) {
        const auto standard = static_cast<QDialogButtonBox::StandardButton>(value);
        if (buttons.testFlag(standard))
            addButton(box, standard);
    }
    return box;
}

QPushButton* UiDialogs::addButton(QMessageBox* box, const QString& text, QMessageBox::ButtonRole role)
{
    auto* button = UiControls::pushButton(text, box);
    box->addButton(button, role);
    return button;
}

QPushButton* UiDialogs::addButton(QMessageBox* box, QMessageBox::StandardButton standard)
{
    if (!usesEla())
        return box->addButton(standard);
    QMessageBox prototype;
    auto* native = prototype.addButton(standard);
    auto* button = addButton(box, native->text(), prototype.buttonRole(native));
    button->setProperty("uiDialogStandardButton", int(standard));
    return button;
}

void UiDialogs::warning(QWidget* parent, const QString& title, const QString& text)
{
    if (!usesEla()) { QMessageBox::warning(parent, title, text); return; }
    message(parent, title, text, QMessageBox::Warning);
}

void UiDialogs::information(QWidget* parent, const QString& title, const QString& text)
{
    if (!usesEla()) { QMessageBox::information(parent, title, text); return; }
    message(parent, title, text, QMessageBox::Information);
}

QString UiDialogs::getText(QWidget* parent, const QString& title, const QString& label,
                           QLineEdit::EchoMode mode, const QString& text, bool* accepted)
{
    if (!usesEla())
        return QInputDialog::getText(parent, title, label, mode, text, accepted);
    auto* edit = UiControls::lineEdit(text);
    edit->setEchoMode(mode);
    InputDialog dialog(parent, title, label, edit);
    edit->selectAll();
    const bool result = dialog.exec() == QDialog::Accepted;
    if (accepted) *accepted = result;
    return result ? edit->text() : QString();
}

QString UiDialogs::getItem(QWidget* parent, const QString& title, const QString& label,
                           const QStringList& items, int current, bool editable, bool* accepted)
{
    if (!usesEla())
        return QInputDialog::getItem(parent, title, label, items, current, editable, accepted);
    auto* combo = UiControls::comboBox();
    combo->addItems(items);
    combo->setEditable(editable);
    combo->setCurrentIndex(qBound(0, current, qMax(0, int(items.size()) - 1)));
    const QString initial = combo->currentText();
    InputDialog dialog(parent, title, label, combo);
    if (combo->lineEdit()) combo->lineEdit()->selectAll();
    const bool result = dialog.exec() == QDialog::Accepted;
    if (accepted) *accepted = result;
    return result ? combo->currentText() : initial;
}

int UiDialogs::getInt(QWidget* parent, const QString& title, const QString& label,
                      int value, int minimum, int maximum, int step, bool* accepted)
{
    if (!usesEla())
        return QInputDialog::getInt(parent, title, label, value, minimum, maximum, step, accepted);
    auto* spin = UiControls::spinBox();
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setValue(value);
    InputDialog dialog(parent, title, label, spin);
    spin->selectAll();
    const bool result = dialog.exec() == QDialog::Accepted;
    if (accepted) *accepted = result;
    return result ? spin->value() : value;
}
