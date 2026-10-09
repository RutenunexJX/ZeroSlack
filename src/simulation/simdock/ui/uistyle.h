#pragma once
#include <QFont>
#include <QString>
class QWidget;
class QDialog;
class ElaText;
class ElaPushButton;
class ElaPlainTextEdit;
class ElaSpinBox;
class ElaToolButton;
class QAbstractScrollArea;
class QFormLayout;
class QLayout;

namespace simdock::Ui {
enum class Role { PageTitle, PanelTitle, Section, Body, Metadata };
QFont font(Role role = Role::Body);
QFont codeFont();
void initialize();
void initializeComponent();
void applyTheme(QWidget* root = nullptr);
void normalizeControls(QWidget* root);
ElaText* label(const QString& text, QWidget* parent, Role role = Role::Body);
ElaPushButton* button(const QString& text, QWidget* parent, bool primary = false);
ElaPlainTextEdit* textView(QWidget* parent);
ElaSpinBox* spinBox(QWidget* parent);
ElaToolButton* disclosure(const QString& text, QWidget* parent);
void smoothScrolling(QAbstractScrollArea* area);
// Keep dialogs usable on the host's current screen, including high DPI screens.
void constrainDialog(QDialog* dialog);
void enableToolTip(QWidget* widget);
void formRow(QFormLayout* form, const QString& text, QWidget* field);
void formRow(QFormLayout* form, const QString& text, QLayout* fields);
}
