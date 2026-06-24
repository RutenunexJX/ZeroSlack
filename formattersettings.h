#ifndef FORMATTERSETTINGS_H
#define FORMATTERSETTINGS_H

#include "formatterservice.h"

#include <QObject>
#include <QSettings>
#include <memory>

class FormatterSettings : public QObject
{
    Q_OBJECT

public:
    explicit FormatterSettings(QObject* parent = nullptr);
    explicit FormatterSettings(std::unique_ptr<QSettings> settings,
                               QObject* parent = nullptr);

    FormatterProfile profile() const;
    bool formatOnSaveEnabled() const;
    void setProfile(FormatterProfile profile);
    void setFormatOnSaveEnabled(bool enabled);

signals:
    void settingsChanged(FormatterProfile profile);
    void formatOnSaveChanged(bool enabled);

private:
    void load();
    void save() const;
    static QString profileKey(FormatterProfile profile);
    static FormatterProfile profileFromKey(const QString& key);

    std::unique_ptr<QSettings> settings;
    FormatterProfile currentProfile = FormatterProfile::Structured;
    bool currentFormatOnSaveEnabled = false;
};

#endif // FORMATTERSETTINGS_H
