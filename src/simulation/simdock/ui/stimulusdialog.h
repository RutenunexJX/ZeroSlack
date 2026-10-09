#pragma once
#include "../core/stimulus.h"
#include <ElaDialog.h>
#include <functional>
class ElaComboBox;
class ElaSpinBox;
class ElaText;
class ElaPushButton;
class QVBoxLayout;

namespace simdock
{
class StimulusDialog : public ElaDialog
{
  public:
    using SaveHandler = std::function<bool(const QJsonObject &, QString *)>;
    using GeneratedSaveHandler = std::function<bool(const QJsonObject &, const QString &, QString *)>;
    using SaveCompletion = std::function<void(const QString &, const QString &)>;
    using SavePreparer = std::function<void(const QJsonObject &, SaveCompletion)>;
    void setSavePreparer(SavePreparer prepare) { m_prepareSave = std::move(prepare); }
    StimulusDialog(const Module &, const Scan &, const QJsonObject &saved, qint64 durationNs,
                   SaveHandler save, QWidget *parent = nullptr);
    StimulusDialog(const Module &, const StimulusSemantics &, const QJsonObject &saved, qint64 durationNs,
                   GeneratedSaveHandler save, QWidget *parent = nullptr);
    ~StimulusDialog() override;
    QJsonObject drawing(QString *error) const;
    bool ready() const
    {
        return m_editor != nullptr;
    }
    bool runRequested() const
    {
        return m_runRequested;
    }

  private:
    bool eventFilter(QObject *, QEvent *) override;
    void syncTiming(const QJsonObject &);
    bool replaceEditor(const QJsonObject &drawing);
    bool applyTiming(bool reset);
    void save(bool run);
    void finishSave(const QJsonObject &, const QString &, QString error, bool run);
    void showError(const QString &error);
    void editChecks();
    Module m_module;
    StimulusSemantics m_semantics;
    QJsonObject m_drawing;
    GeneratedSaveHandler m_save;
    SavePreparer m_prepareSave;
    bool m_saving = false;
    QList<QWidget *> m_saveLockedControls;
    QWidget *m_editor = nullptr;
    QVBoxLayout *m_canvasLayout;
    ElaComboBox *m_clock, *m_reset, *m_polarity, *m_units;
    ElaSpinBox *m_period, *m_cycles, *m_duration;
    ElaText *m_error;
    ElaPushButton *m_checks;
    bool m_pendingTiming = false, m_runRequested = false;
};
} // namespace simdock
