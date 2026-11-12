#include "blocknotestyle.h"
#include "applicationthememanager.h"
#include "insightvisualstyle.h"
#include "uitypography.h"
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QSignalBlocker>
#include <QtMath>

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
