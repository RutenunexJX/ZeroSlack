#ifndef COMPLETIONSERVICE_H
#define COMPLETIONSERVICE_H

#include "semanticindex.h"

#include <QString>
#include <QStringList>
#include <memory>

struct CompletionQuery {
    QString prefix;
    QString fileName;
    QString moduleName;
    int cursorLine = -1;
    int cursorPosition = -1;
};

class CompletionService
{
public:
    static CompletionService* getInstance();

    explicit CompletionService(SemanticIndex* semanticIndex = nullptr);
    ~CompletionService();

    void setSemanticIndex(SemanticIndex* semanticIndex);

    QStringList findCompletions(const CompletionQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<CompletionService> instance;

    SemanticIndex* semanticIndex() const;
};

#endif // COMPLETIONSERVICE_H
