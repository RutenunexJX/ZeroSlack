#ifndef FORMATTERSERVICE_H
#define FORMATTERSERVICE_H

#include <QString>
#include <memory>

enum class FormatterProfile {
    IndentOnly,
    Structured
};

struct FormatterOptions {
    int indentWidth = 4;
    bool preservePreprocessorIndent = true;
    bool alignDeclarationBlocks = true;
    bool alignPortLists = true;
    bool alignInstanceMaps = true;
};

struct FormatterReport {
    QString formattedText;
    bool changed = false;
    int formattedLines = 0;
};

class FormatterService
{
public:
    static FormatterService* getInstance();
    static FormatterOptions optionsForProfile(FormatterProfile profile);
    static QString profileDisplayName(FormatterProfile profile);

    FormatterReport formatDocument(
        const QString& text,
        const FormatterOptions& options = FormatterOptions()) const;
    FormatterReport formatDocument(
        const QString& text,
        FormatterProfile profile) const;
    FormatterReport formatSelection(
        const QString& text,
        const FormatterOptions& options = FormatterOptions()) const;
    FormatterReport formatSelection(
        const QString& text,
        FormatterProfile profile) const;

private:
    static std::unique_ptr<FormatterService> instance;
};

#endif // FORMATTERSERVICE_H
