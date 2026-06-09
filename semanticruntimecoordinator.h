#ifndef SEMANTICRUNTIMECOORDINATOR_H
#define SEMANTICRUNTIMECOORDINATOR_H

#include <QObject>
#include <memory>

class SlangManager;
class SmartRelationshipBuilder;
class SymbolRelationshipEngine;

class SemanticRuntimeCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit SemanticRuntimeCoordinator(QObject* parent = nullptr);
    ~SemanticRuntimeCoordinator() override;

    SymbolRelationshipEngine* relationshipEngine() const;
    SmartRelationshipBuilder* relationshipBuilder() const;
    SlangManager* slangManager() const;

private:
    std::unique_ptr<SymbolRelationshipEngine> relationshipEngineInstance;
    std::unique_ptr<SlangManager> slangManagerInstance;
    std::unique_ptr<SmartRelationshipBuilder> relationshipBuilderInstance;
};

#endif // SEMANTICRUNTIMECOORDINATOR_H
