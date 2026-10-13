#ifndef CODETEMPLATECONTEXTANALYZER_H
#define CODETEMPLATECONTEXTANALYZER_H

#include <QString>

class TSDocument;

struct CodeTemplateSignalContext {
    QString clockName;
    QString resetName;
    bool currentModuleFound = false;
};

class CodeTemplateContextAnalyzer
{
public:
    // Borrowed only for this synchronous call; no syntax/text is retained.
    static CodeTemplateSignalContext analyze(const TSDocument& document,
                                              int cursorPosition,
                                              const QString& fileName = {});
    static CodeTemplateSignalContext analyze(
        const QString& documentText,
        int cursorPosition,
        const QString& fileName = {});
};

#endif // CODETEMPLATECONTEXTANALYZER_H
