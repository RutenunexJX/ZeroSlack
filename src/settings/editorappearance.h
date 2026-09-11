#ifndef EDITORAPPEARANCE_H
#define EDITORAPPEARANCE_H

#include <QString>
#include <QStringList>

class MyCodeEditor;

struct EditorAppearanceOptions
{
    QString fontFamily;
    int fontSizePt = 12;
    double lineHeight = 1.4;
    bool ligaturesEnabled = false;
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
};

#endif // EDITORAPPEARANCE_H
