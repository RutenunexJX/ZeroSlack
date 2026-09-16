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

bool containsCjkCharacter(const QString& family)
{
    for (const QChar ch : family) {
        const uint code = ch.unicode();
        if ((code >= 0x2E80 && code <= 0x2EFF)   // CJK radicals
            || (code >= 0x3000 && code <= 0x303F) // CJK punctuation
            || (code >= 0x3040 && code <= 0x30FF) // Hiragana/Katakana
            || (code >= 0x3100 && code <= 0x312F) // Bopomofo
            || (code >= 0x3130 && code <= 0x318F) // Hangul compatibility
            || (code >= 0x31F0 && code <= 0x31FF) // Katakana extensions
            || (code >= 0x3400 && code <= 0x4DBF) // CJK extension A
            || (code >= 0x4E00 && code <= 0x9FFF) // CJK unified
            || (code >= 0xAC00 && code <= 0xD7AF) // Hangul syllables
            || (code >= 0xF900 && code <= 0xFAFF) // CJK compatibility
            || (code >= 0xFF00 && code <= 0xFFEF)) { // fullwidth forms
            return true;
        }
    }
    return false;
}

QString normalizedFamilyName(const QString& family)
{
    QString normalized = family.toLower();
    normalized.remove(QLatin1Char(' '));
    normalized.remove(QLatin1Char('-'));
    normalized.remove(QLatin1Char('_'));
    return normalized;
}

bool containsKnownCjkFontAlias(const QString& family)
{
    static const QStringList aliases = {
        QStringLiteral("simsun"),
        QStringLiteral("nsimsun"),
        QStringLiteral("simhei"),
        QStringLiteral("microsoftyahei"),
        QStringLiteral("microsoftjhenghei"),
        QStringLiteral("kaiti"),
        QStringLiteral("fangsong"),
        QStringLiteral("dengxian"),
        QStringLiteral("meiryo"),
        QStringLiteral("yugothic"),
        QStringLiteral("msgothic"),
        QStringLiteral("malgungothic"),
        QStringLiteral("mingliu"),
        QStringLiteral("pmingliu"),
        QStringLiteral("dfkai"),
        QStringLiteral("pingfang"),
        QStringLiteral("hiragino"),
        QStringLiteral("heiti"),
        QStringLiteral("sourcehan"),
        QStringLiteral("notocjk"),
        QStringLiteral("notosanscjk"),
        QStringLiteral("notoserifcjk"),
        QStringLiteral("batang"),
        QStringLiteral("gulim"),
        QStringLiteral("dotum"),
    };

    const QString normalized = normalizedFamilyName(family);
    for (const QString& alias : aliases) {
        if (normalized.contains(alias))
            return true;
    }
    return false;
}

QStringList bundledFontResourcePaths()
{
    return {
        QStringLiteral(":/fonts/resources/fonts/maple-mono/MapleMono-Regular.ttf"),
        QStringLiteral(":/fonts/resources/fonts/maple-mono/MapleMono-Bold.ttf"),
        QStringLiteral(":/fonts/resources/fonts/maple-mono/MapleMono-Italic.ttf"),
        QStringLiteral(":/fonts/resources/fonts/maple-mono/MapleMono-BoldItalic.ttf"),
        QStringLiteral(":/fonts/resources/fonts/iosevka/Iosevka-Regular.ttf"),
        QStringLiteral(":/fonts/resources/fonts/iosevka/Iosevka-Bold.ttf"),
        QStringLiteral(":/fonts/resources/fonts/iosevka/Iosevka-Italic.ttf"),
        QStringLiteral(":/fonts/resources/fonts/iosevka/Iosevka-BoldItalic.ttf"),
        QStringLiteral(":/fonts/resources/fonts/monaspace-neon/MonaspaceNeon-Regular.otf"),
        QStringLiteral(":/fonts/resources/fonts/monaspace-neon/MonaspaceNeon-Bold.otf"),
        QStringLiteral(":/fonts/resources/fonts/monaspace-neon/MonaspaceNeon-Italic.otf"),
        QStringLiteral(":/fonts/resources/fonts/monaspace-neon/MonaspaceNeon-BoldItalic.otf"),
        QStringLiteral(":/fonts/resources/fonts/intel-one-mono/IntelOneMono-Regular.ttf"),
        QStringLiteral(":/fonts/resources/fonts/intel-one-mono/IntelOneMono-Bold.ttf"),
        QStringLiteral(":/fonts/resources/fonts/intel-one-mono/IntelOneMono-Italic.ttf"),
        QStringLiteral(":/fonts/resources/fonts/intel-one-mono/IntelOneMono-BoldItalic.ttf"),
        QStringLiteral(":/fonts/resources/fonts/geist-mono/GeistMono-Regular.ttf"),
        QStringLiteral(":/fonts/resources/fonts/geist-mono/GeistMono-Bold.ttf"),
        QStringLiteral(":/fonts/resources/fonts/geist-mono/GeistMono-Italic.ttf"),
        QStringLiteral(":/fonts/resources/fonts/geist-mono/GeistMono-BoldItalic.ttf"),
        QStringLiteral(":/fonts/resources/fonts/0xproto/0xProto-Regular.ttf"),
        QStringLiteral(":/fonts/resources/fonts/0xproto/0xProto-Bold.ttf"),
        QStringLiteral(":/fonts/resources/fonts/0xproto/0xProto-Italic.ttf"),
    };
}
}

QStringList EditorAppearance::recommendedFontFamilies()
{
    ensureApplicationFontsLoaded();
    return {
        QStringLiteral("Cascadia Code"),
        QStringLiteral("Maple Mono"),
        QStringLiteral("Iosevka"),
        QStringLiteral("Monaspace Neon"),
        QStringLiteral("Intel One Mono"),
        QStringLiteral("Geist Mono"),
        QStringLiteral("0xProto"),
    };
}

bool EditorAppearance::isCjkFontFamily(const QString& family)
{
    return containsCjkCharacter(family)
        || containsKnownCjkFontAlias(family);
}

bool EditorAppearance::ensureApplicationFontsLoaded()
{
    static bool attempted = false;
    static bool loaded = false;
    if (attempted)
        return loaded;

    attempted = true;
    Q_INIT_RESOURCE(code);
    for (const QString& path : bundledFontResourcePaths()) {
        const int fontId = QFontDatabase::addApplicationFont(path);
        if (fontId >= 0)
            loaded = true;
    }
    return loaded;
}

QString EditorAppearance::fallbackFontFamily()
{
    return QStringLiteral("Maple Mono");
}

QString EditorAppearance::resolveFontFamily(const QString& preferredFamily)
{
    ensureApplicationFontsLoaded();
    for (const QString& family : recommendedFontFamilies()) {
        if (preferredFamily.compare(family, Qt::CaseInsensitive) == 0)
            return family;
    }
    return fallbackFontFamily();
}

EditorAppearanceOptions EditorAppearance::defaultOptions()
{
    EditorAppearanceOptions options;
    options.fontFamily = fallbackFontFamily();
    options.fontSizePt = 15;
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

    ensureApplicationFontsLoaded();
    QFont font(resolveFontFamily(options.fontFamily),
               qBound(8, options.fontSizePt, 32));
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    setLigatureFeatures(font, options.ligaturesEnabled);
    editor->setFont(font);
    // A shared QTextDocument is bound before the coordinator applies the
    // editor appearance. QPlainTextEdit::setFont does not update that
    // document's default font, so text layout and painted annotations can
    // otherwise use different metrics.
    if (editor->document())
        editor->document()->setDefaultFont(font);
    applyLineHeight(editor, qBound(1.0, options.lineHeight, 2.0));

    const int tabWidth =
        editor->fontMetrics().horizontalAdvance(' ') * 4;
    editor->setTabStopDistance(tabWidth);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor->updateGeometry();
    editor->viewport()->update();
}
