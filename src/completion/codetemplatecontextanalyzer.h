#ifndef CODETEMPLATECONTEXTANALYZER_H
#define CODETEMPLATECONTEXTANALYZER_H

#include <QString>

struct CodeTemplateSignalContext {
    QString clockName;
    QString resetName;
    bool currentModuleFound = false;
};

class CodeTemplateContextAnalyzer
{
public:
    static CodeTemplateSignalContext analyze(
        const QString& documentText,
        int cursorPosition);
};

#endif // CODETEMPLATECONTEXTANALYZER_H
