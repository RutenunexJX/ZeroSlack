#include "blocknotestyle.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "uitypography.h"
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QSignalBlocker>
#include <QtMath>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace {
const QJsonObject& presetConfiguration() {
    static const auto configuration=[] {
        QFile file(":/blocks/presets-v1.json");
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    }();
    return configuration;
}
}
QStringList BlockNoteStyle::presets() { return {"Plain","Paper","Outline","Technical"}; }
QStringList BlockNoteStyle::fonts() { return {"UI Sans","Reading Serif","Engineering Mono"}; }
QStringList BlockNoteStyle::tones() { return {"Default","Neutral","Sand","Mist","Sage"}; }
QVariantMap BlockNoteStyle::defaults(const QString& type) {
    const auto preset=type=="text" ? "Paper" : type=="image" ? "Plain" : type=="wave" || type=="code" ? "Technical" : "Outline";
    return {{"preset",preset},{"presetVersion","1"},{"fontSet",presetConfiguration().value("presets").toObject().value(preset).toObject().value("font").toString()},{"tone","Default"}};
}
bool BlockNoteStyle::validPreset(const QVariantMap& appearance) {
    return appearance.size()==4 && appearance.value("presetVersion")=="1"
        && presets().contains(appearance.value("preset").toString())
        && fonts().contains(appearance.value("fontSet").toString()) && tones().contains(appearance.value("tone").toString());
}

BlockNoteStyle::BlockNoteStyle()
{
    connect(&ApplicationThemeManager::instance(), &ApplicationThemeManager::themeChanged, this, [this] {
        ++generation; emit changed();
    });
}
BlockNoteStyle& BlockNoteStyle::instance() { static BlockNoteStyle style; return style; }
void BlockNoteStyle::apply(const QVariantMap& effectiveValues)
{
    QVariantMap next;
    for (auto it = effectiveValues.cbegin(); it != effectiveValues.cend(); ++it)
        if (it.key().startsWith(QStringLiteral("blockNotes."))) next.insert(it.key(), it.value());
    if (values == next) return;
    values = next; ++generation; emit changed();
}
BlockNoteAppearance BlockNoteStyle::appearance(const QVariantMap& overrides) const { return resolve(values, overrides); }
BlockNoteAppearance BlockNoteStyle::resolve(const QVariantMap& settings, const QVariantMap& overrides)
{
    const auto& theme = InsightVisualStyle::theme();
    BlockNoteAppearance result;
    result.font = UiTypography::font();
    const QString family = settings.value(QStringLiteral("blockNotes.fontFamily")).toString().trimmed();
    if (!family.isEmpty()) { auto families = result.font.families(); families.prepend(family); result.font.setFamilies(families); }
    result.font.setPixelSize(qBound(8, settings.value(QStringLiteral("blockNotes.sizePx"), 15).toInt(), 48));
    result.lineHeight = qBound(1.0, settings.value(QStringLiteral("blockNotes.lineHeight"), 1.6).toDouble(), 3.0);
    if (validPreset(overrides)) {
        const auto& configuration=presetConfiguration();
        const auto preset=configuration.value("presets").toObject().value(overrides.value("preset").toString()).toObject();
        const bool dark=theme.textPrimary.lightness()>theme.panelBackground.lightness();
        const auto mode=dark ? "dark" : "light";
        const auto tone=configuration.value("tones").toObject().value(overrides.value("tone").toString()).toObject();
        QStringList families;
        for (const auto& family:configuration.value("fonts").toObject().value(overrides.value("fontSet").toString()).toArray()) families.append(family.toString());
        result.font.setFamilies(families);
        result.style=overrides.value("preset").toString(); result.text=theme.textPrimary;
        result.background=QColor(tone.value(mode).toString(preset.value(mode).toString("transparent")));
        result.border=QColor(preset.value(dark ? "borderDark" : "borderLight").toString("transparent"));
        return result;
    }
    const auto value = [&](const QString& key) {
        return overrides.value(key, settings.value(QStringLiteral("blockNotes.") + key)).toString();
    };
    const auto color = [&](const QString& key, const QColor& fallback) {
        const QColor candidate(value(key)); return candidate.isValid() ? candidate : fallback;
    };
    result.style = value(QStringLiteral("style"));
    if (!QStringList{"Plain", "Outlined", "Soft"}.contains(result.style)) result.style = QStringLiteral("Soft");
    result.background = color(QStringLiteral("background"), theme.surface.panel);
    result.text = color(QStringLiteral("textColor"), theme.textPrimary);
    result.border = color(QStringLiteral("borderColor"), theme.border);
    if (result.style == QStringLiteral("Soft")) result.background = color(QStringLiteral("background"), theme.panelSubtle);
    if (result.style == QStringLiteral("Plain") && value(QStringLiteral("background")).isEmpty()) result.background = Qt::transparent;
    if (result.style != QStringLiteral("Outlined")) result.border = Qt::transparent;
    return result;
}
void BlockNoteStyle::format(QTextDocument* document, const BlockNoteAppearance& appearance)
{
    const QSignalBlocker guard(document);
    document->setDefaultFont(appearance.font);
    document->setDocumentMargin(0);
    QTextCursor cursor(document); cursor.select(QTextCursor::Document);
    QTextBlockFormat block; block.setLineHeight(appearance.lineHeight * 100, QTextBlockFormat::ProportionalHeight);
    cursor.mergeBlockFormat(block);
}
qreal BlockNoteStyle::measure(const QString& text, qreal width, const BlockNoteAppearance& appearance)
{
    QTextDocument document;
    document.setPlainText(text);
    format(&document, appearance);
    document.setTextWidth(qMax<qreal>(1, width));
    return qCeil(document.size().height());
}
