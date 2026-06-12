#ifndef SEMANTICRUNTIMECOORDINATOR_H
#define SEMANTICRUNTIMECOORDINATOR_H

#include <QObject>
#include <memory>

class SlangManager;
class SmartRelationshipBuilder;
class AnalysisScheduler;
class SemanticIndex;
class SymbolAnalyzer;
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
    void configureScheduler(AnalysisScheduler* scheduler) const;

private:
    void configureQueryServices(SemanticIndex* semanticIndex) const;

    std::unique_ptr<SymbolRelationshipEngine> relationshipEngineInstance;
    std::unique_ptr<SymbolAnalyzer> symbolAnalyzerInstance;
    std::unique_ptr<SlangManager> slangManagerInstance;
    std::unique_ptr<SmartRelationshipBuilder> relationshipBuilderInstance;
};

#endif // SEMANTICRUNTIMECOORDINATOR_H
