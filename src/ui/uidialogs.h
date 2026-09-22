#pragma once

#include "zeroslackexport.h"
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>

// Dialog lifetime and button roles remain Qt-owned. Ela supplies the controls.
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
