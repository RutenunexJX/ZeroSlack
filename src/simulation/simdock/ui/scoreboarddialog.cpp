#include "scoreboarddialog.h"
#include "uistyle.h"
#include <ElaCheckBox.h>
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaSpinBox.h>
#include <ElaText.h>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

namespace simdock {
ScoreboardDialog::ScoreboardDialog(const QList<StimulusSignal> &ports, const TbOptions &timing,
                                 const QJsonObject &saved, QWidget *parent)
    : ElaDialog(parent), m_ports(ports), m_timing(timing)
{
    setObjectName(QStringLiteral("scoreboardDialog"));
    setWindowTitle(QStringLiteral("Check plan"));
    setIsDefaultClosed(true); setIsStayTop(false); setAppBarHeight(36);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
    resize(710, 700);
    auto values = defaultScoreboard();
    for (auto i = saved.begin(); i != saved.end(); ++i) values.insert(i.key(), i.value());
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    auto *top = new QHBoxLayout;
    top->addWidget(Ui::label(QStringLiteral("Check plan"), this, Ui::Role::PanelTitle));
    m_kind = new ElaComboBox(this); m_kind->setObjectName(QStringLiteral("scoreboardKind"));
    m_kind->addItem(QStringLiteral("None"), QStringLiteral("none"));
    m_kind->addItem(QStringLiteral("UART transmit"), QStringLiteral("uart_tx"));
    m_kind->addItem(QStringLiteral("FIFO / pass-through"), QStringLiteral("stream"));
    m_kind->addItem(QStringLiteral("Expected values"), QStringLiteral("values"));
    m_kind->setCurrentIndex(qMax(0, m_kind->findData(values.value(QStringLiteral("kind")).toString())));
    top->addWidget(m_kind, 1); layout->addLayout(top);
    m_hint = Ui::label(QString(), this, Ui::Role::Metadata);
    m_hint->setWordWrap(true); m_hint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    layout->addWidget(m_hint);
    auto *scroll = new QScrollArea(this);
    scroll->setProperty("simdockDialogSurface", true);
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    m_settings = new QWidget(scroll);
    auto *body = new QVBoxLayout(m_settings); body->setContentsMargins(0, 4, 12, 4);
    body->addWidget(Ui::label(QStringLiteral("Sample on %1 rising edge · Reset: %2")
        .arg(timing.clock.isEmpty() ? QStringLiteral("(select a clock in Timing)") : timing.clock,
             timing.reset.isEmpty() ? QStringLiteral("None") : timing.reset), this, Ui::Role::Metadata));
    const auto makeForm = [&](QWidget *owner) {
        auto *form = new QFormLayout(owner); form->setContentsMargins(0, 0, 0, 0);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); return form;
    };
    const auto binding = [&](QFormLayout *form, const QString &label, const QString &key,
                             const QString &direction, bool scalar, bool optional,
                             const QStringList &suggestions) {
        auto *box = new ElaComboBox(this); box->setObjectName(QStringLiteral("scoreboard_") + key);
        box->addItem(optional ? QStringLiteral("Always ready") : QStringLiteral("Choose signal"), QString());
        for (const auto &p : ports)
            if (p.direction == direction && (!scalar || p.width == 1) && p.name != timing.clock && p.name != timing.reset)
                box->addItem(p.name + QStringLiteral("  [%1]").arg(p.width), p.name);
        const auto selected = values.value(key).toString();
        int index = box->findData(selected);
        if (!selected.isEmpty() && index < 0) {
            box->addItem(selected + QStringLiteral(" (missing)"), selected); index = box->count() - 1;
        }
        if (selected.isEmpty() && !saved.contains(key))
            for (const auto &name : suggestions) if (box->findData(name) > 0) { index = box->findData(name); break; }
        box->setCurrentIndex(qMax(0, index));
        m_bindings.insert(key, box); Ui::formRow(form, label, box);
    };
    m_input = new QWidget(this); auto *inputForm = makeForm(m_input);
    binding(inputForm, QStringLiteral("Input data"), QStringLiteral("data"), QStringLiteral("input"), false, false,
            {QStringLiteral("byte_data"), QStringLiteral("data"), QStringLiteral("in_data"), QStringLiteral("din")});
    binding(inputForm, QStringLiteral("Input valid"), QStringLiteral("valid"), QStringLiteral("input"), true, false,
            {QStringLiteral("byte_valid"), QStringLiteral("valid"), QStringLiteral("in_valid")});
    binding(inputForm, QStringLiteral("Input ready"), QStringLiteral("ready"), QStringLiteral("output"), true, true,
            {QStringLiteral("byte_ready"), QStringLiteral("ready"), QStringLiteral("in_ready")});
    body->addWidget(m_input);
    auto *output = new QWidget(this); auto *outputForm = makeForm(output);
    binding(outputForm, QStringLiteral("Observed output"), QStringLiteral("output"), QStringLiteral("output"), false, false,
            {QStringLiteral("uart_tx"), QStringLiteral("tx"), QStringLiteral("out_data"), QStringLiteral("dout")});
    body->addWidget(output);
    m_outputHandshake = new QWidget(this); auto *outputHandshake = makeForm(m_outputHandshake);
    binding(outputHandshake, QStringLiteral("Output valid"), QStringLiteral("outputValid"), QStringLiteral("output"), true, false,
            {QStringLiteral("out_valid"), QStringLiteral("valid_out")});
    binding(outputHandshake, QStringLiteral("Output ready"), QStringLiteral("outputReady"), QStringLiteral("input"), true, true,
            {QStringLiteral("out_ready"), QStringLiteral("ready_out")});
    body->addWidget(m_outputHandshake);
    const auto spin = [&](const QString &key, int low, int high) {
        auto *box = Ui::spinBox(this); box->setObjectName(QStringLiteral("scoreboard_") + key);
        box->setRange(low, high); box->setValue(values.value(key).toInt()); return box;
    };
    m_uart = new QWidget(this); auto *uartForm = makeForm(m_uart);
    auto *format = new QHBoxLayout;
    m_baud = spin(QStringLiteral("baud"), 1, 100000000); m_baud->setSuffix(QStringLiteral(" baud"));
    m_bits = spin(QStringLiteral("dataBits"), 5, 9); m_bits->setSuffix(QStringLiteral(" data bits"));
    format->addWidget(m_baud); format->addWidget(m_bits); Ui::formRow(uartForm, QStringLiteral("Expected format"), format);
    m_parity = new ElaComboBox(this); m_parity->setObjectName(QStringLiteral("scoreboard_parity"));
    for (const auto &p : {QStringLiteral("none"), QStringLiteral("even"), QStringLiteral("odd")}) m_parity->addItem(p, p);
    m_parity->setCurrentIndex(qMax(0, m_parity->findData(values.value(QStringLiteral("parity")))));
    m_stop = new ElaComboBox(this); m_stop->setObjectName(QStringLiteral("scoreboard_stop"));
    m_stop->addItem(QStringLiteral("1 stop bit"), 2); m_stop->addItem(QStringLiteral("1.5 stop bits"), 3); m_stop->addItem(QStringLiteral("2 stop bits"), 4);
    m_stop->setCurrentIndex(qMax(0, m_stop->findData(values.value(QStringLiteral("stopHalfBits")).toInt())));
    auto *parityRow = new QHBoxLayout; parityRow->addWidget(m_parity); parityRow->addWidget(m_stop);
    Ui::formRow(uartForm, QStringLiteral("Parity / stop"), parityRow); body->addWidget(m_uart);
    m_expected = new QWidget(this); auto *expectedLayout = new QVBoxLayout(m_expected); expectedLayout->setContentsMargins(0, 0, 0, 0);
    expectedLayout->addWidget(Ui::label(QStringLiteral("Expected output sequence (hexadecimal, one value per row)"), this));
    m_values = new QTableWidget(0, 1, this); m_values->setObjectName(QStringLiteral("scoreboard_values"));
    m_values->setHorizontalHeaderLabels({QStringLiteral("Expected value (hex)")});
    m_values->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_values->setMinimumHeight(130); m_values->setMaximumHeight(200);
    m_values->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_values->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const auto &v : values.value(QStringLiteral("values")).toArray()) {
        int row = m_values->rowCount(); m_values->insertRow(row); m_values->setItem(row, 0, new QTableWidgetItem(v.toString()));
    }
    expectedLayout->addWidget(m_values);
    auto *valueButtons = new QHBoxLayout;
    auto *add = Ui::button(QStringLiteral("Add value"), this); add->setObjectName(QStringLiteral("scoreboardAddValue"));
    auto *remove = Ui::button(QStringLiteral("Remove value"), this);
    valueButtons->addWidget(add); valueButtons->addWidget(remove); valueButtons->addStretch(); expectedLayout->addLayout(valueButtons);
    connect(add, &QPushButton::clicked, this, [this] {
        if (m_values->rowCount() >= 4096) return;
        int row = m_values->rowCount(); m_values->insertRow(row);
        auto *item = new QTableWidgetItem(QStringLiteral("00")); m_values->setItem(row, 0, item);
        m_values->setCurrentItem(item); m_values->editItem(item);
    });
    connect(remove, &QPushButton::clicked, this, [this] { if (m_values->currentRow() >= 0) m_values->removeRow(m_values->currentRow()); });
    body->addWidget(m_expected);
    auto *checkRow = new QHBoxLayout;
    for (const auto &[key, label] : QList<QPair<QString, QString>>{
        {QStringLiteral("checkData"), QStringLiteral("Data")}, {QStringLiteral("checkCount"), QStringLiteral("No loss / extra items")},
        {QStringLiteral("checkFormat"), QStringLiteral("Frame format")}, {QStringLiteral("checkTimeout"), QStringLiteral("Timeout")}}) {
        auto *box = new ElaCheckBox(label, this); box->setObjectName(QStringLiteral("scoreboard_") + key);
        box->setChecked(values.value(key).toBool()); m_checks.insert(key, box); checkRow->addWidget(box);
    }
    body->addLayout(checkRow);
    auto *limits = new QWidget(this); auto *limitsForm = makeForm(limits);
    m_timeout = spin(QStringLiteral("timeoutNs"), 1, 2000000000); m_timeout->setSuffix(QStringLiteral(" ns"));
    Ui::formRow(limitsForm, QStringLiteral("Output deadline"), m_timeout);
    m_reset = new ElaComboBox(this); m_reset->setObjectName(QStringLiteral("scoreboard_resetPolicy"));
    m_reset->addItem(QStringLiteral("Discard unfinished items"), QStringLiteral("discard"));
    m_reset->addItem(QStringLiteral("Keep unfinished items"), QStringLiteral("keep"));
    m_reset->setCurrentIndex(qMax(0, m_reset->findData(values.value(QStringLiteral("resetPolicy")).toString())));
    Ui::formRow(limitsForm, QStringLiteral("On reset"), m_reset); body->addWidget(limits); body->addStretch();
    scroll->setWidget(m_settings); Ui::smoothScrolling(scroll); layout->addWidget(scroll, 1);
    m_error = Ui::label(QString(), this, Ui::Role::Metadata); m_error->setObjectName(QStringLiteral("scoreboardError"));
    m_error->setProperty("error", true); m_error->setWordWrap(true); m_error->hide(); layout->addWidget(m_error);
    auto *actions = new QHBoxLayout; actions->addStretch();
    auto *cancel = Ui::button(QStringLiteral("Cancel"), this);
    auto *apply = Ui::button(QStringLiteral("Apply"), this, true); apply->setObjectName(QStringLiteral("applyScoreboard"));
    actions->addWidget(cancel); actions->addWidget(apply); layout->addLayout(actions);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(apply, &QPushButton::clicked, this, [this] {
        // Commit the last edited numeric table cell before taking the snapshot.
        if (auto *focused = focusWidget()) focused->clearFocus();
        QString error; scoreboardCode(m_ports, m_timing, plan(), &error);
        m_error->setText(error); m_error->setVisible(!error.isEmpty());
        if (error.isEmpty()) accept();
    });
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] { updateKind(); });
    connect(m_checks.value(QStringLiteral("checkTimeout")), &QCheckBox::toggled, m_timeout, &QWidget::setEnabled);
    m_timeout->setEnabled(m_checks.value(QStringLiteral("checkTimeout"))->isChecked());
    Ui::normalizeControls(this); updateKind();
    Ui::constrainDialog(this);
}
QJsonObject ScoreboardDialog::plan() const
{
    auto p = defaultScoreboard(); p[QStringLiteral("kind")] = m_kind->currentData().toString();
    for (auto i = m_bindings.begin(); i != m_bindings.end(); ++i) p[i.key()] = i.value()->currentData().toString();
    for (auto i = m_checks.begin(); i != m_checks.end(); ++i) p[i.key()] = i.value()->isChecked();
    p[QStringLiteral("baud")] = m_baud->value(); p[QStringLiteral("dataBits")] = m_bits->value();
    p[QStringLiteral("parity")] = m_parity->currentData().toString(); p[QStringLiteral("stopHalfBits")] = m_stop->currentData().toInt();
    p[QStringLiteral("timeoutNs")] = m_timeout->value(); p[QStringLiteral("resetPolicy")] = m_reset->currentData().toString();
    QJsonArray values;
    for (int row = 0; row < m_values->rowCount(); ++row)
        values << (m_values->item(row, 0) ? m_values->item(row, 0)->text().trimmed() : QString());
    p[QStringLiteral("values")] = values; return p;
}
void ScoreboardDialog::updateKind()
{
    const auto kind = m_kind->currentData().toString();
    const bool uart = kind == QStringLiteral("uart_tx"), expected = kind == QStringLiteral("values");
    m_settings->setVisible(kind != QStringLiteral("none"));
    m_input->setVisible(!expected); m_outputHandshake->setVisible(!uart);
    m_uart->setVisible(uart); m_expected->setVisible(expected);
    m_checks.value(QStringLiteral("checkFormat"))->setVisible(uart);
    m_hint->setText(kind == QStringLiteral("none") ? QStringLiteral("Run input waveforms without checking output behavior.")
        : uart ? QStringLiteral("Compare accepted input data with decoded UART frames. Confirm the bindings and expected frame format; the plan does not drive or reconfigure the DUT.")
        : expected ? QStringLiteral("Compare each accepted output with the next value in your table. An empty comparison is never reported as a pass.")
        : QStringLiteral("Compare accepted input and output items in order. Select Always ready only for an interface without backpressure."));
    m_error->hide();
}
}
