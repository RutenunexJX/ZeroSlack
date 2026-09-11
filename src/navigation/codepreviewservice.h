#ifndef CODEPREVIEWSERVICE_H
#define CODEPREVIEWSERVICE_H

#include "rtlinsightlink.h"
#include "semanticsourcerange.h"

#include <QString>
#include <QStringList>
#include <memory>

class DocumentModel;
class SemanticIndex;

struct CodePreviewQuery {
    RtlInsightCodeLink codeLink;
    SemanticSourceRange sourceRange;
    QString title;
    QString detail;
    int contextBefore = 4;
    int contextAfter = 7;
};

struct CodePreviewReport {
    bool available = false;
    bool preciseRange = false;
    QString title;
    QString detail;
    QString fileName;
    QString fileDisplayName;
    QString locationDisplayName;
    int targetLine = 0;
    int targetColumn = 0;
    int targetEndLine = 0;
    int targetEndColumn = 0;
    int firstLineNumber = 0;
    int highlightedLine = 0;
    int caretLineNumber = 0;
    QStringList codeLines;
    QString caretLine;
    QString unavailableReason;
};

class CodePreviewService
{
public:
    static CodePreviewService* getInstance();

    explicit CodePreviewService(SemanticIndex* semanticIndex = nullptr);
    ~CodePreviewService();

    void setSemanticIndex(SemanticIndex* semanticIndex);
    void setDocumentModel(DocumentModel* documentModel);

    CodePreviewReport previewForCodeLink(
        const CodePreviewQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    DocumentModel* documents = nullptr;
    static std::unique_ptr<CodePreviewService> instance;

    QString previewTextForFile(const QString& fileName) const;
};

#endif // CODEPREVIEWSERVICE_H
