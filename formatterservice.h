#ifndef FORMATTERSERVICE_H
#define FORMATTERSERVICE_H

#include <QString>
#include <memory>

struct FormatterOptions {
    int indentWidth = 4;
    bool preservePreprocessorIndent = true;
    bool alignDeclarationBlocks = true;
    bool alignPortLists = true;
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

    FormatterReport formatDocument(
        const QString& text,
        const FormatterOptions& options = FormatterOptions()) const;

private:
    static std::unique_ptr<FormatterService> instance;
};

#endif // FORMATTERSERVICE_H
