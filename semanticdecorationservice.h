#ifndef SEMANTICDECORATIONSERVICE_H
#define SEMANTICDECORATIONSERVICE_H

#include "semanticindex.h"

#include <QHash>
#include <QList>
#include <QString>
#include <memory>

enum class SemanticDecorationRole {
    ModuleInterface,
    PackageClassType,
    InstanceName,
    FormalPort,
    ActualSignal,
    Parameter,
    Macro,
    SystemTask,
    EnumValue,
    TypeAlias,
    ModulePort,
    InactivePreprocessorBranch
};

struct SemanticDecoration {
    SemanticDecorationRole role = SemanticDecorationRole::ActualSignal;
    QString text;
    int startPosition = -1;
    int length = 0;
    SemanticSymbolRecord symbolRecord;

    bool isValid() const { return startPosition >= 0 && length > 0; }
};

struct SemanticDecorationQuery {
    QString fileName;
    QString documentText;
    QHash<QString, QString> configuredDefines;
};

struct SemanticDecorationReport {
    QList<SemanticDecoration> decorations;
};

class SemanticDecorationService
{
public:
    static SemanticDecorationService* getInstance();

    explicit SemanticDecorationService(SemanticIndex* semanticIndex = nullptr);
    ~SemanticDecorationService();

    void setSemanticIndex(SemanticIndex* semanticIndex);
    SemanticDecorationReport decorationsForDocument(
        const SemanticDecorationQuery& query) const;

private:
    SemanticIndex* index = nullptr;
    static std::unique_ptr<SemanticDecorationService> instance;

    SemanticIndex* semanticIndex() const;
};

#endif // SEMANTICDECORATIONSERVICE_H
