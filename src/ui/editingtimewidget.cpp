#include "editingtimewidget.h"
#include "editingtimeservice.h"
#include "uicontrols.h"
#include "uitypography.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QToolButton>

EditingTimeWidget::EditingTimeWidget(EditingTimeService* service, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("editingTimeWidget"));
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 0, 4, 0);
    row->setSpacing(8);
    auto* label = UiControls::label(this);
    label->setObjectName(QStringLiteral("editingTimeLabel"));
    label->setTextFormat(Qt::PlainText);
    UiTypography::apply(label, UiTypography::Role::Body);
    row->addWidget(label);
    auto* reset = UiControls::toolButton(this);
    reset->setObjectName(QStringLiteral("resetEditingTimeButton"));
    reset->setText(tr("Reset"));
    reset->setToolButtonStyle(Qt::ToolButtonTextOnly);
    reset->setAccessibleName(tr("Reset editing time"));
    reset->setToolTip(tr("Clear accumulated editing time"));
    connect(reset, &QToolButton::clicked, service, &EditingTimeService::reset);
    row->addWidget(reset);

    const auto refresh = [this, service = QPointer<EditingTimeService>(service), label] {
        if (!service) return;
        const qint64 tenths = service->totalNanoseconds() / 100000000;
        const QString duration = QStringLiteral("%1:%2:%3.%4")
            .arg(tenths / 36000, 2, 10, QLatin1Char('0'))
            .arg((tenths / 600) % 60, 2, 10, QLatin1Char('0'))
            .arg((tenths / 10) % 60, 2, 10, QLatin1Char('0')).arg(tenths % 10);
        label->setText(tr("Editing %1").arg(duration)
            + (service->persistenceError().isEmpty() ? QString() : tr(" · Save failed")));
        const QString explanation = tr("Accumulated actual input time. Pauses on key release. "
            "IME commits and menu edits count only their synchronous edit duration.");
        label->setToolTip(service->persistenceError().isEmpty() ? explanation
            : explanation + QLatin1Char('\n') + service->persistenceError());
        label->setAccessibleName(label->text());
    };
    auto* refreshTimer = new QTimer(this);
    refreshTimer->setInterval(100);
    connect(refreshTimer, &QTimer::timeout, this, refresh);
    connect(service, &EditingTimeService::changed, this, refresh);
    connect(service, &QObject::destroyed, refreshTimer, &QTimer::stop);
    refreshTimer->start();
    refresh();
}
