#ifndef SCOPEBANDSERVICE_H
#define SCOPEBANDSERVICE_H

#include "rtlinsightlink.h"
#include "semanticindex.h"

#include <QList>
#include <QString>
#include <memory>

struct ScopeBandQuery {
    QString fileName;
};

struct ScopeBandSymbolRange {
    SemanticSymbolRecord symbolRecord;
    SymbolStableKey symbolStableKey;
    RtlInsightCodeLink codeLink;
    QString symbolDisplayName;
    QString symbolTypeDisplayName;
    QString sourceRoleDisplayName;
    int startLine = -1;
    int endLine = -1;
};

struct ScopeBandReport {
    QList<ScopeBandSymbolRange> modules;
    QList<ScopeBandSymbolRange> logics;
};

class ScopeBandService
{
public:
    static ScopeBandService* getInstance();

    explicit ScopeBandService(SemanticIndex* semanticIndex = nullptr);
    ~ScopeBandService();

    void setSemanticIndex(SemanticIndex* semanticIndex);
    ScopeBandReport scopeBands(const ScopeBandQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<ScopeBandService> instance;

    SemanticIndex* semanticIndex() const;
};

#endif // SCOPEBANDSERVICE_H
