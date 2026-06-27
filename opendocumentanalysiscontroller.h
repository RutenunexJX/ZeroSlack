#ifndef OPENDOCUMENTANALYSISCONTROLLER_H
#define OPENDOCUMENTANALYSISCONTROLLER_H

#include "documentsnapshot.h"

#include <QMap>
#include <QObject>
#include <QString>
#include <functional>

class DocumentModel;
class QTimer;
class SymbolAnalyzer;

class OpenDocumentAnalysisController : public QObject
{
    Q_OBJECT

public:
    explicit OpenDocumentAnalysisController(QObject* parent = nullptr);
    ~OpenDocumentAnalysisController() override;

    void setDocumentModel(DocumentModel* model);
    void setSymbolAnalyzer(SymbolAnalyzer* analyzer);
    void setOpenFileContentProvider(std::function<QString(const QString&)> provider);
    void setWorkspaceOpenProvider(std::function<bool()> provider);

    void scheduleOpenFileAnalysis(const QString& fileName, int delayMs);
    void cancelScheduledOpenFileAnalysis(const QString& fileName);
    void handleExternalFileChanged(const QString& fileName, int debounceMs);
    void handleDocumentClosed(const QString& fileName);
    void handleDocumentEdited(const DocumentSnapshot& snapshot, int relationshipDelayMs);
    void analyzeOpenDocumentNow(const DocumentSnapshot& snapshot,
                                bool skipUnchanged,
                                bool requestRelationships = true);
    void analyzeOpenDocumentsNow();
    QString contentForOpenFile(const QString& fileName) const;
    bool isWorkspaceOpen() const;

signals:
    void documentRefreshRequested(const QString& fileName);
    void relationshipAnalysisRequested(const QString& fileName, const QString& content);
    void relationshipAnalysisScheduled(const QString& fileName,
                                       const QString& content,
                                       int delayMs);

private:
    DocumentModel* documentModel = nullptr;
    SymbolAnalyzer* symbolAnalyzer = nullptr;
    std::function<QString(const QString&)> openFileContentProvider;
    std::function<bool()> workspaceOpenProvider;
    QMap<QString, QTimer*> openFileAnalysisTimers;
    QMap<QString, QTimer*> fileChangeDebounceTimers;

    bool isDirtyOpenDocument(const QString& fileName) const;
    bool lineContainsStructuralKeyword(const QString& content, int oneBasedLine) const;
    bool hasNonWhitespaceChange(const QString& oldContent,
                                const QString& newContent) const;
};

#endif // OPENDOCUMENTANALYSISCONTROLLER_H
