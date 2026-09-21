#pragma once

#include "zeroslackexport.h"
#include <QString>
#include <Qt>

class QWidget;
class QPushButton;
class QToolButton;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;

// Backend choice is fixed before widget creation. Consumers keep Qt contracts;
// specialized editors, graph canvases and native window chrome do not use this factory.
namespace UiControls {
ZEROSLACK_API QPushButton* pushButton(QWidget* parent = nullptr);
ZEROSLACK_API QPushButton* pushButton(const QString& text, QWidget* parent = nullptr);
ZEROSLACK_API QToolButton* toolButton(QWidget* parent = nullptr);
ZEROSLACK_API QLineEdit* lineEdit(QWidget* parent = nullptr);
ZEROSLACK_API QLineEdit* lineEdit(const QString& text, QWidget* parent = nullptr);
ZEROSLACK_API QComboBox* comboBox(QWidget* parent = nullptr);
ZEROSLACK_API QCheckBox* checkBox(QWidget* parent = nullptr);
ZEROSLACK_API QCheckBox* checkBox(const QString& text, QWidget* parent = nullptr);
ZEROSLACK_API QSpinBox* spinBox(QWidget* parent = nullptr);
ZEROSLACK_API QDoubleSpinBox* doubleSpinBox(QWidget* parent = nullptr);
ZEROSLACK_API QSlider* slider(Qt::Orientation orientation, QWidget* parent = nullptr);
ZEROSLACK_API void refreshRole(QWidget* widget);
}
