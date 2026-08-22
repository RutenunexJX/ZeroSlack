#ifndef FORMATTERSERVICE_H
#define FORMATTERSERVICE_H

#include <QString>
#include <memory>

enum class FormatterOutcome {
    Applied,
    Unchanged,
    ConservativeFallback,
    Rejected
};

struct FormatterOptions {
    int indentWidth = 4;
    bool preservePreprocessorIndent = true;
    bool indentContinuationLines = true;
    bool indentAssignmentRhsContinuations = true;
    bool indentSingleStatementBodies = true;
    bool indentCaseItemBodies = true;
    bool alignDeclarationBlocks = true;
    bool alignPortLists = true;
    bool alignInstanceMaps = true;
    bool alignCaseItems = true;
    bool alignEnumItems = true;
    bool alignAssignments = true;
    bool alignContinuationOperators = true;
    bool alignCallArgumentContinuations = true;
};

struct FormatterReport {
    QString formattedText;
    bool changed = false;
    int formattedLines = 0;
    FormatterOutcome outcome = FormatterOutcome::Unchanged;
    QString diagnostic;

    bool accepted() const
    {
        return outcome != FormatterOutcome::Rejected;
    }
};

class FormatterService
{
public:
    static FormatterService* getInstance();

    FormatterReport formatDocument(
        const QString& text,
        const FormatterOptions& options = FormatterOptions()) const;
    FormatterReport formatSnippet(
        const QString& text,
        const FormatterOptions& options = FormatterOptions()) const;

private:
    static std::unique_ptr<FormatterService> instance;
};

#endif // FORMATTERSERVICE_H
