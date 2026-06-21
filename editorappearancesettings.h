#ifndef EDITORAPPEARANCESETTINGS_H
#define EDITORAPPEARANCESETTINGS_H

#include "editorappearance.h"

#include <QObject>
#include <QSettings>
#include <memory>

class EditorAppearanceSettings : public QObject
{
    Q_OBJECT

public:
    explicit EditorAppearanceSettings(QObject* parent = nullptr);
    explicit EditorAppearanceSettings(std::unique_ptr<QSettings> settings,
                                      QObject* parent = nullptr);

    EditorAppearanceOptions options() const;

    void setOptions(const EditorAppearanceOptions& options);
    void setFontFamily(const QString& family);
    void setFontSizePt(int sizePt);
    void setLineHeight(double lineHeight);
    void setLigaturesEnabled(bool enabled);
    void resetToDefaults();

signals:
    void settingsChanged(const EditorAppearanceOptions& options);

private:
    void load();
    void save() const;
    static EditorAppearanceOptions normalized(
        const EditorAppearanceOptions& options);

    std::unique_ptr<QSettings> settings;
    EditorAppearanceOptions currentOptions;
};

#endif // EDITORAPPEARANCESETTINGS_H
