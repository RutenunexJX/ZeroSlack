#ifndef EXPOSESIGNALTOTOPPREVIEW_H
#define EXPOSESIGNALTOTOPPREVIEW_H

#include "exposesignaltotopservice.h"

#include <QPointer>

#include <functional>

class QWidget;

class ExposeSignalToTopPreview
{
public:
    enum class Mode {
        ReviewAndApply,
        PreviewOnly
    };

    enum class Result {
        Rejected,
        Accepted
    };

    using ReplanFunction =
        std::function<ExposeSignalToTopReport(const QString&)>;

    explicit ExposeSignalToTopPreview(
        const ExposeSignalToTopReport& initialReport,
        ReplanFunction replan = {},
        QWidget* host = nullptr,
        Mode mode = Mode::ReviewAndApply);

    Result exec();
    Result result() const;
    const ExposeSignalToTopReport& reportForApply() const;
    QString exportedPortName() const;

private:
    ExposeSignalToTopReport currentReport;
    ReplanFunction replanFunction;
    QPointer<QWidget> hostWidget;
    Mode previewMode = Mode::ReviewAndApply;
    Result currentResult = Result::Rejected;
};

#endif // EXPOSESIGNALTOTOPPREVIEW_H
