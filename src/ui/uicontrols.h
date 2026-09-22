#pragma once

#include "zeroslackexport.h"
#include <QString>
#include <Qt>
#include <QTabWidget>

class QWidget;
class QPushButton;
class QToolButton;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QRadioButton;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class QTreeView;
class QTreeWidget;
class QTabBar;
class QListWidget;
class QTableWidget;
class QTableView;
class QMenu;
class QMenuBar;
class QAbstractScrollArea;
class QScrollArea;
class QPlainTextEdit;
class QLabel;
class QFormLayout;

// Also used by Designer for the initial editor group.
class ZEROSLACK_API UiTabWidget : public QTabWidget {
public:
    explicit UiTabWidget(QWidget* parent = nullptr);
};

// Backend choice is fixed before widget creation. Consumers keep Qt contracts;
// specialized editors, graph canvases and native window chrome do not use this factory.
namespace UiControls {
ZEROSLACK_API QLabel* label(QWidget* parent = nullptr);
ZEROSLACK_API QLabel* label(const QString& text, QWidget* parent = nullptr);
ZEROSLACK_API void addFormRow(QFormLayout* form, const QString& text, QWidget* field);
ZEROSLACK_API QPushButton* pushButton(QWidget* parent = nullptr);
ZEROSLACK_API QPushButton* pushButton(const QString& text, QWidget* parent = nullptr);
ZEROSLACK_API QToolButton* toolButton(QWidget* parent = nullptr);
ZEROSLACK_API QToolButton* railButton(QWidget* parent = nullptr);
ZEROSLACK_API QToolButton* badgedToolButton(QWidget* parent = nullptr);
ZEROSLACK_API void setToolButtonBadge(QToolButton* button, const QString& text, const QString& tone);
ZEROSLACK_API QLineEdit* lineEdit(QWidget* parent = nullptr);
ZEROSLACK_API QLineEdit* lineEdit(const QString& text, QWidget* parent = nullptr);
ZEROSLACK_API QComboBox* comboBox(QWidget* parent = nullptr);
ZEROSLACK_API QCheckBox* checkBox(QWidget* parent = nullptr);
ZEROSLACK_API QCheckBox* checkBox(const QString& text, QWidget* parent = nullptr);
ZEROSLACK_API QRadioButton* radioButton(const QString& text, QWidget* parent = nullptr);
ZEROSLACK_API QSpinBox* spinBox(QWidget* parent = nullptr);
ZEROSLACK_API QDoubleSpinBox* doubleSpinBox(QWidget* parent = nullptr);
ZEROSLACK_API QSlider* slider(Qt::Orientation orientation, QWidget* parent = nullptr);
ZEROSLACK_API QTreeView* treeView(QWidget* parent = nullptr);
ZEROSLACK_API QTreeWidget* treeWidget(QWidget* parent = nullptr);
ZEROSLACK_API QTabBar* tabBar(QWidget* parent = nullptr);
ZEROSLACK_API QTabWidget* tabWidget(QWidget* parent = nullptr);
ZEROSLACK_API QTabWidget* editorTabWidget(QWidget* parent = nullptr);
ZEROSLACK_API QListWidget* listWidget(QWidget* parent = nullptr);
ZEROSLACK_API QTableWidget* tableWidget(QWidget* parent = nullptr);
ZEROSLACK_API QTableWidget* tableWidget(int rows, int columns, QWidget* parent = nullptr);
ZEROSLACK_API QTableView* tableView(QWidget* parent = nullptr);
ZEROSLACK_API QScrollArea* scrollArea(QWidget* parent = nullptr);
ZEROSLACK_API QPlainTextEdit* readOnlyText(QWidget* parent = nullptr);
ZEROSLACK_API QPlainTextEdit* readOnlyText(const QString& text, QWidget* parent = nullptr);
ZEROSLACK_API QMenu* menu(QWidget* parent = nullptr);
ZEROSLACK_API QMenu* addMenu(QMenu* parent, const QString& title);
ZEROSLACK_API QMenu* addMenu(QMenuBar* parent, const QString& title);
ZEROSLACK_API void enableSmoothScrolling(QAbstractScrollArea* area);
ZEROSLACK_API void enableTreeTransitions(QTreeView* tree);
ZEROSLACK_API void refreshRole(QWidget* widget);
}
