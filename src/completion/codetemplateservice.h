#ifndef CODETEMPLATESERVICE_H
#define CODETEMPLATESERVICE_H

#include "codetemplatecontextanalyzer.h"
#include "completiontypes.h"

class CodeTemplateService
{
public:
    static CodeTemplateService* getInstance();

    QList<CodeTemplateItem> catalog() const;
    QList<CodeTemplateItem> matchingTemplates(
        const QString& commandToken,
        const QString& seedText = QString(),
        const CodeTemplateSignalContext& signalContext =
            CodeTemplateSignalContext()) const;
    CodeTemplateItem templateForCommand(
        const QString& commandToken,
        const QString& seedText = QString(),
        const CodeTemplateSignalContext& signalContext =
            CodeTemplateSignalContext()) const;

private:
    QString seededName(const QString& seedText, const QString& fallback) const;
    QString expandTemplate(const QString& commandToken,
                           const QString& seedText) const;
};

#endif // CODETEMPLATESERVICE_H
