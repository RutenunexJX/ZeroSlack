#ifndef EXPOSESIGNALTOTOPDIALOG_H
#define EXPOSESIGNALTOTOPDIALOG_H

#include "exposesignaltotopservice.h"

#include <QDialog>

#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class ExposeSignalToTopDialog : public QDialog
{
public:
    using ReplanFunction =
        std::function<ExposeSignalToTopReport(const QString&)>;

    explicit ExposeSignalToTopDialog(
        const ExposeSignalToTopReport& initialReport,
        ReplanFunction replan = {},
        QWidget* parent = nullptr);

    const ExposeSignalToTopReport& reportForApply() const;
    QString exportedPortName() const;

private:
    void updateReport(const ExposeSignalToTopReport& report);
    static QString summaryText(
        const ExposeSignalToTopReport& report);

    ExposeSignalToTopReport currentReport;
    ReplanFunction replanFunction;
    QLineEdit* portNameEdit = nullptr;
    QLabel* stateLabel = nullptr;
    QPlainTextEdit* summaryView = nullptr;
    QPlainTextEdit* diffView = nullptr;
    QPushButton* applyButton = nullptr;
};

#endif // EXPOSESIGNALTOTOPDIALOG_H
