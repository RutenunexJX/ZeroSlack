#ifndef SCOPEBANDSERVICE_H
#define SCOPEBANDSERVICE_H

#include "semanticindex.h"
#include "syminfo.h"

#include <QList>
#include <QString>
#include <memory>

struct ScopeBandQuery {
    QString fileName;
};

struct ScopeBandSymbolRange {
    sym_list::SymbolInfo symbol;
    SemanticSymbolRecord symbolRecord;
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
