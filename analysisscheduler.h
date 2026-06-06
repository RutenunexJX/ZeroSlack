#ifndef ANALYSISSCHEDULER_H
#define ANALYSISSCHEDULER_H

#include "documentmodel.h"

#include <QObject>
#include <QMap>
#include <QString>
#include <functional>

class SymbolAnalyzer;
class QTimer;

class AnalysisScheduler : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisScheduler(QObject* parent = nullptr);
    ~AnalysisScheduler() override;

    void setDocumentModel(DocumentModel* model);
    void setSymbolAnalyzer(SymbolAnalyzer* analyzer);
    void setOpenFileContentProvider(std::function<QString(const QString&)> provider);
    void setWorkspaceOpenProvider(std::function<bool()> provider);
    void setRelationshipAnalysisCallback(std::function<void(const QString&, const QString&)> callback);

    void scheduleOpenFileAnalysis(const QString& fileName, int delayMs);
    void cancelScheduledOpenFileAnalysis(const QString& fileName);
    void requestRelationshipAnalysis(const QString& fileName, const QString& content);
    void handleExternalFileChanged(const QString& fileName, int debounceMs);

signals:
    void documentRefreshRequested(const QString& fileName);

private:
    DocumentModel* documentModel = nullptr;
    SymbolAnalyzer* symbolAnalyzer = nullptr;

    std::function<QString(const QString&)> openFileContentProvider;
    std::function<bool()> workspaceOpenProvider;
    std::function<void(const QString&, const QString&)> relationshipAnalysisCallback;

    QMap<QString, QTimer*> openFileAnalysisTimers;
    QMap<QString, QTimer*> fileChangeDebounceTimers;
    QMap<QString, QString> lastRelationshipAnalysisContent;

    void onDocumentOpened(const DocumentSnapshot& snapshot);
    void onDocumentEdited(const DocumentSnapshot& snapshot);
    void onDocumentSaved(const DocumentSnapshot& snapshot);
    void analyzeOpenDocumentNow(const DocumentSnapshot& snapshot, bool skipUnchanged);

    QString contentForOpenFile(const QString& fileName) const;
    bool isWorkspaceOpen() const;
    bool lineContainsStructuralKeyword(const QString& content, int oneBasedLine) const;
};

#endif // ANALYSISSCHEDULER_H
