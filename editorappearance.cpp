#include "editorappearance.h"

#include "mycodeeditor.h"

#include <QFont>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QtGlobal>

namespace {
void setLigatureFeatures(QFont& font, bool enabled)
{
    const quint32 value = enabled ? 1U : 0U;
    font.setFeature(QFont::Tag("liga"), value);
    font.setFeature(QFont::Tag("calt"), value);
}

void applyLineHeight(MyCodeEditor* editor, double lineHeight)
{
    if (!editor || !editor->document())
        return;

    QTextDocument* document = editor->document();
    const bool wasModified = document->isModified();
    const QSignalBlocker editorBlocker(editor);
    const QSignalBlocker documentBlocker(document);

    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);

    QTextBlockFormat format;
    format.setLineHeight(qRound(lineHeight * 100.0),
                         QTextBlockFormat::ProportionalHeight);
    cursor.mergeBlockFormat(format);
    cursor.endEditBlock();
    document->setModified(wasModified);
}
}

QStringList EditorAppearance::recommendedFontFamilies()
{
    return {
        QStringLiteral("JetBrains Mono"),
        QStringLiteral("Cascadia Code"),
        QStringLiteral("Maple Mono"),
        QStringLiteral("Iosevka"),
        QStringLiteral("Commit Mono"),
        QStringLiteral("IBM Plex Mono"),
        QStringLiteral("Fira Code"),
        QStringLiteral("Consolas"),
        QStringLiteral("Courier New"),
    };
}

QString EditorAppearance::fallbackFontFamily()
{
    const QFontDatabase database;
    const QStringList families = database.families();
    for (const QString& family : recommendedFontFamilies()) {
        if (families.contains(family, Qt::CaseInsensitive))
            return family;
    }

    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!fixedFont.family().isEmpty())
        return fixedFont.family();

    return QStringLiteral("Consolas");
}

QString EditorAppearance::resolveFontFamily(const QString& preferredFamily)
{
    if (!preferredFamily.trimmed().isEmpty()) {
        const QFontDatabase database;
        if (database.families().contains(preferredFamily, Qt::CaseInsensitive))
            return preferredFamily;
    }
    return fallbackFontFamily();
}

EditorAppearanceOptions EditorAppearance::defaultOptions()
{
    EditorAppearanceOptions options;
    options.fontFamily = fallbackFontFamily();
    options.fontSizePt = 12;
    options.lineHeight = 1.4;
    options.ligaturesEnabled = false;
    return options;
}

void EditorAppearance::apply(MyCodeEditor* editor) const
{
    apply(editor, defaultOptions());
}

void EditorAppearance::apply(
    MyCodeEditor* editor,
    const EditorAppearanceOptions& options) const
{
    if (!editor)
        return;

    QFont font(resolveFontFamily(options.fontFamily),
               qBound(8, options.fontSizePt, 32));
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    setLigatureFeatures(font, options.ligaturesEnabled);
    editor->setFont(font);
    applyLineHeight(editor, qBound(1.0, options.lineHeight, 2.0));

    const int tabWidth =
        editor->fontMetrics().horizontalAdvance(' ') * 4;
    editor->setTabStopDistance(tabWidth);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->updateGeometry();
    editor->viewport()->update();
}
