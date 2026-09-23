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
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QShowEvent>
#include <QApplication>

UiDialog::UiDialog(QWidget* parent, Qt::WindowFlags flags)
    : UiDialogBase(nullptr)
{
#ifdef ZEROSLACK_ENABLE_ELA
    setParent(parent, (flags.testFlag(Qt::Window) ? Qt::Window : Qt::Dialog)
        | Qt::FramelessWindowHint | (windowFlags() & ~Qt::WindowType_Mask) | flags);
    setIsStayTop(false);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
    setAppBarHeight(36);
    if (auto* bar = findChild<ElaAppBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        bar->setWindowIconVisible(false);
        bar->titleLabel()->setTextFormat(Qt::PlainText);
    }
#else
    setParent(parent, Qt::Dialog | flags);
#endif
    setFont(UiTypography::font());
}

UiMessageDialog::UiMessageDialog(QWidget* parent)
    : UiMessageDialogBase(parent ? parent->window() : nullptr)
{
    setObjectName(QStringLiteral("uiMessageDialog"));
    setFont(UiTypography::font());
#ifdef ZEROSLACK_ENABLE_ELA
    setStandardButtonsVisible(false);
#endif
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    auto* heading = new QHBoxLayout;
    iconLabel = UiControls::label(body);
    iconLabel->hide();
    heading->addWidget(iconLabel);
    titleLabel = UiControls::label(body);
    titleLabel->setTextFormat(Qt::PlainText);
    titleLabel->setWordWrap(true);
    auto titleFont = titleLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(titleFont);
    heading->addWidget(titleLabel, 1);
    layout->addLayout(heading);
    connect(this, &QWidget::windowTitleChanged, titleLabel, &QLabel::setText);
    messageLabel = UiControls::label(body);
    messageLabel->setObjectName(QStringLiteral("uiMessageText"));
    messageLabel->setTextFormat(Qt::PlainText);
    messageLabel->setWordWrap(true);
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(messageLabel);
    detailsToggle = UiControls::pushButton(tr("Show details"), body);
    detailsToggle->setObjectName(QStringLiteral("uiMessageDetailsToggle"));
    detailsToggle->setCheckable(true);
    detailsToggle->setAutoDefault(false);
    detailsToggle->hide();
    layout->addWidget(detailsToggle, 0, Qt::AlignLeft);
    details = UiControls::readOnlyText(body);
    details->setObjectName(QStringLiteral("uiMessageDetails"));
    details->setMaximumHeight(180);
    details->hide();
    layout->addWidget(details);
    connect(detailsToggle, &QPushButton::toggled, this, [this](bool expanded) {
        details->setVisible(expanded);
        detailsToggle->setText(expanded ? tr("Hide details") : tr("Show details"));
        adjustSize();
    });
    actions = new QDialogButtonBox(body);
    actions->setObjectName(QStringLiteral("uiMessageActions"));
    layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::clicked, this, [this](QAbstractButton* button) {
        chosen = button;
        done(actions->buttonRole(button) == QDialogButtonBox::AcceptRole ? Accepted : Rejected);
    });
#ifdef ZEROSLACK_ENABLE_ELA
    setCentralWidget(body);
#else
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(body);
#endif
    setModal(true);
}

void UiMessageDialog::setText(const QString& text) { messageLabel->setText(text); }
QString UiMessageDialog::text() const { return messageLabel->text(); }
void UiMessageDialog::setDetailedText(const QString& text) {
    details->setPlainText(text);
    detailsToggle->setVisible(!text.isEmpty());
    if (text.isEmpty()) { detailsToggle->setChecked(false); details->hide(); }
}
void UiMessageDialog::setIcon(QMessageBox::Icon icon) {
    const auto standard = icon == QMessageBox::Warning ? QStyle::SP_MessageBoxWarning
        : icon == QMessageBox::Critical ? QStyle::SP_MessageBoxCritical
        : icon == QMessageBox::Question ? QStyle::SP_MessageBoxQuestion : QStyle::SP_MessageBoxInformation;
    iconLabel->setPixmap(style()->standardIcon(standard).pixmap(22, 22));
    iconLabel->setVisible(icon != QMessageBox::NoIcon);
}
QPushButton* UiMessageDialog::addButton(const QString& text, QMessageBox::ButtonRole role) {
    auto* button = UiDialogs::addButton(actions, text, static_cast<QDialogButtonBox::ButtonRole>(role));
    button->setAutoDefault(false);
    return button;
}
QList<QAbstractButton*> UiMessageDialog::buttons() const { return actions->buttons(); }
QMessageBox::ButtonRole UiMessageDialog::buttonRole(QAbstractButton* button) const {
    return static_cast<QMessageBox::ButtonRole>(actions->buttonRole(button));
}
void UiMessageDialog::setDefaultButton(QPushButton* button) {
    if (defaultAction) defaultAction->setDefault(false);
    defaultAction = button;
    if (button) { button->setDefault(true); button->setFocus(); }
}
QAbstractButton* UiMessageDialog::escapeButton() const { return escapeAction; }
QAbstractButton* UiMessageDialog::clickedButton() const { return chosen; }
void UiMessageDialog::reject() {
    chosen = escapeAction && escapeAction->isEnabled() ? escapeAction.data() : nullptr;
    done(Rejected);
}
void UiMessageDialog::keyPressEvent(QKeyEvent* event) { QDialog::keyPressEvent(event); }
void UiMessageDialog::showEvent(QShowEvent* event) {
    chosen.clear();
    const auto available = screen()->availableGeometry().adjusted(16, 16, -16, -16);
    setMaximumWidth(available.width());
    resize(qMin(available.width(), qMax(440, minimumSizeHint().width())), sizeHint().height());
    UiMessageDialogBase::showEvent(event);
}

namespace {
bool usesEla()
{
#ifdef ZEROSLACK_ENABLE_ELA
    return ApplicationThemeManager::instance().backend() == UiStyleBackend::Ela;
#else
    return false;
#endif
}

class InputDialog final : public UiDialog {
public:
    InputDialog(QWidget* parent, const QString& title, const QString& prompt, QWidget* input)
        : UiDialog(parent)
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
    UiMessageDialog box(parent);
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

QPushButton* UiDialogs::addButton(UiMessageDialog* box, const QString& text, QMessageBox::ButtonRole role)
{
    return box->addButton(text, role);
}

QPushButton* UiDialogs::addButton(UiMessageDialog* box, QMessageBox::StandardButton standard)
{
    QMessageBox prototype;
    auto* native = prototype.addButton(standard);
    auto* button = box->addButton(native->text(), prototype.buttonRole(native));
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
