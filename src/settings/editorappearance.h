#ifndef EDITORAPPEARANCE_H
#define EDITORAPPEARANCE_H

#include <QString>
#include <QStringList>
#include <QPointer>

class MyCodeEditor;
class QTextDocument;

struct EditorAppearanceOptions
{
    QString fontFamily;
    int fontSizePt = 12;
    double lineHeight = 1.4;
    bool ligaturesEnabled = false;
    QString backgroundPreset = QStringLiteral("Resting");
    QString backgroundImagePath;
    int backgroundOpacity = 55;
};

class EditorAppearance
{
public:
    static QStringList recommendedFontFamilies();
    static bool isCjkFontFamily(const QString& family);
    static bool ensureApplicationFontsLoaded();
    static QString fallbackFontFamily();
    static QString resolveFontFamily(const QString& preferredFamily);
    static EditorAppearanceOptions defaultOptions();

    void apply(MyCodeEditor* editor) const;
    void apply(MyCodeEditor* editor,
               const EditorAppearanceOptions& options) const;

private:
    mutable QPointer<QTextDocument> appliedDocument;
    mutable double appliedLineHeight = 0;
};

#endif // EDITORAPPEARANCE_H
