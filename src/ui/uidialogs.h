#pragma once

#include "zeroslackexport.h"
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>

#ifdef ZEROSLACK_ENABLE_ELA
#include "ElaDialog.h"
#include "ElaContentDialog.h"
using UiDialogBase = ElaDialog;
using UiMessageDialogBase = ElaContentDialog;
#else
using UiDialogBase = QDialog;
using UiMessageDialogBase = QDialog;
#endif

class QLabel;
class QPlainTextEdit;

class ZEROSLACK_API UiDialog : public UiDialogBase {
    Q_OBJECT
public:
    explicit UiDialog(QWidget* parent = nullptr, Qt::WindowFlags flags = {});
};

class ZEROSLACK_API UiMessageDialog final : public UiMessageDialogBase {
    Q_OBJECT
public:
    explicit UiMessageDialog(QWidget* parent = nullptr);
    void setText(const QString& text);
    QString text() const;
    void setDetailedText(const QString& text);
    void setIcon(QMessageBox::Icon icon);
    QPushButton* addButton(const QString& text, QMessageBox::ButtonRole role);
    QList<QAbstractButton*> buttons() const;
    QMessageBox::ButtonRole buttonRole(QAbstractButton* button) const;
    void setDefaultButton(QPushButton* button);
    QPushButton* defaultButton() const { return defaultAction; }
    void setEscapeButton(QPushButton* button) { escapeAction = button; }
    QAbstractButton* escapeButton() const;
    QAbstractButton* clickedButton() const;
    void reject() override;
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
private:
    QLabel* titleLabel;
    QLabel* messageLabel;
    QLabel* iconLabel;
    QPlainTextEdit* details;
    QPushButton* detailsToggle;
    QDialogButtonBox* actions;
    QPointer<QPushButton> defaultAction;
    QPointer<QPushButton> escapeAction;
    QPointer<QAbstractButton> chosen;
};

// Business decisions retain Qt button roles and synchronous accept/reject semantics.
namespace UiDialogs {
// Role-driven boxes: callers retain returned buttons or use buttonRole().
ZEROSLACK_API QDialogButtonBox* buttonBox(QDialogButtonBox::StandardButtons buttons,
                                        QWidget* parent = nullptr);
ZEROSLACK_API QPushButton* addButton(QDialogButtonBox* box, const QString& text,
                                   QDialogButtonBox::ButtonRole role);
ZEROSLACK_API QPushButton* addButton(QDialogButtonBox* box,
                                   QDialogButtonBox::StandardButton button);
ZEROSLACK_API QPushButton* addButton(QMessageBox* box, const QString& text,
                                   QMessageBox::ButtonRole role);
ZEROSLACK_API QPushButton* addButton(QMessageBox* box, QMessageBox::StandardButton button);
ZEROSLACK_API QPushButton* addButton(UiMessageDialog* box, const QString& text, QMessageBox::ButtonRole role);
ZEROSLACK_API QPushButton* addButton(UiMessageDialog* box, QMessageBox::StandardButton button);
ZEROSLACK_API void warning(QWidget* parent, const QString& title, const QString& text);
ZEROSLACK_API void information(QWidget* parent, const QString& title, const QString& text);
ZEROSLACK_API QString getText(QWidget* parent, const QString& title, const QString& label,
                             QLineEdit::EchoMode mode = QLineEdit::Normal,
                             const QString& text = {}, bool* accepted = nullptr);
ZEROSLACK_API QString getItem(QWidget* parent, const QString& title, const QString& label,
                             const QStringList& items, int current = 0, bool editable = true,
                             bool* accepted = nullptr);
ZEROSLACK_API int getInt(QWidget* parent, const QString& title, const QString& label,
                        int value = 0, int minimum = -2147483647, int maximum = 2147483647,
                        int step = 1, bool* accepted = nullptr);
}
