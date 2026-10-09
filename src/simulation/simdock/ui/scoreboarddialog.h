#pragma once
#include "zeroslackexport.h"
#include "../core/scoreboard.h"
#include <ElaDialog.h>
#include <QMap>
class ElaComboBox;
class ElaSpinBox;
class ElaCheckBox;
class ElaText;
class QTableWidget;

namespace simdock {
class ZEROSLACK_API ScoreboardDialog : public ElaDialog {
    Q_OBJECT
public:
    ScoreboardDialog(const QList<StimulusSignal> &, const TbOptions &, const QJsonObject &,
                     QWidget *parent = nullptr);
    QJsonObject plan() const;
private:
    void updateKind();
    QList<StimulusSignal> m_ports;
    TbOptions m_timing;
    ElaComboBox *m_kind, *m_parity, *m_stop, *m_reset;
    ElaSpinBox *m_baud, *m_bits, *m_timeout;
    QMap<QString, ElaComboBox *> m_bindings;
    QMap<QString, ElaCheckBox *> m_checks;
    QWidget *m_settings, *m_input, *m_outputHandshake, *m_uart, *m_expected;
    QTableWidget *m_values;
    ElaText *m_error, *m_hint;
};
}
