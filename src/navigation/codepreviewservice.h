#ifndef CODEPREVIEWSERVICE_H
#define CODEPREVIEWSERVICE_H

#include "rtlinsightlink.h"
#include "semanticsourcerange.h"

#include <QString>
#include <QStringList>
#include <memory>
#include <QPointer>

class DocumentModel;
class SemanticIndexSnapshot;

struct CodePreviewQuery {
    RtlInsightCodeLink codeLink;
    SemanticSourceRange sourceRange;
    QString title;
    QString detail;
    int contextBefore = 4;
    int contextAfter = 7;
    std::shared_ptr<const SemanticIndexSnapshot> locationSnapshot;
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
    bool stale = false;
    QString sourceDescription;
    QString documentId;
    int documentRevision = 0;
};

class CodePreviewService
{
public:
    static CodePreviewService* getInstance();

    CodePreviewService();
    ~CodePreviewService();

    void setDocumentModel(DocumentModel* documentModel);

    CodePreviewReport previewForCodeLink(
        const CodePreviewQuery& query) const;

private:
    QPointer<DocumentModel> documents;
    static std::unique_ptr<CodePreviewService> instance;

};

#endif // CODEPREVIEWSERVICE_H
