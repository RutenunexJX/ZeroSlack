#include "semanticruntimecoordinator.h"

#include "semanticindex.h"
#include "activitylogservice.h"
#include <QPointer>
#include "slangmanager.h"
#include "smartrelationshipbuilder.h"
#include "symbolanalyzer.h"
#include "symbolrelationshipengine.h"

SemanticRuntimeCoordinator::SemanticRuntimeCoordinator(QObject* parent)
    : QObject(parent)
{
    relationshipEngineInstance = std::make_unique<SymbolRelationshipEngine>(this);
    symbolAnalyzerInstance = std::make_unique<SymbolAnalyzer>(this);

    SemanticIndex* semanticIndex = SemanticIndex::getInstance();
    semanticIndex->setPublicationObserver([owner = QPointer<SemanticRuntimeCoordinator>(this)](
        std::uint64_t revision, int symbols, int relationships, const QStringList& changedFiles) {
        if (owner) ActivityLogService::getInstance()->append(QStringLiteral("SemanticIndex"), ActivityLogLevel::Info,
            QStringLiteral("Published snapshot gen=%1 symbols=%2 relationships=%3 changedFiles=%4")
                .arg(revision).arg(symbols).arg(relationships).arg(changedFiles.join(QLatin1Char(','))));
    });
    configureQueryServices(semanticIndex);
    semanticIndex->attachRelationshipEngine(relationshipEngineInstance.get());

    slangManagerInstance = std::make_unique<SlangManager>();

    relationshipBuilderInstance = semanticIndex->createRelationshipBuilder(
        relationshipEngineInstance.get(), slangManagerInstance.get(), this);
}
SemanticRuntimeCoordinator::~SemanticRuntimeCoordinator()
{
    if (relationshipEngineInstance) {
        relationshipEngineInstance->clearAllRelationships();
        SemanticIndex* semanticIndex = SemanticIndex::getInstance();
        if (semanticIndex->relationshipEngine()
            == relationshipEngineInstance.get()) {
            semanticIndex->attachRelationshipEngine(nullptr);
            semanticIndex->setPublicationObserver({});
        }
    }
}

SymbolRelationshipEngine* SemanticRuntimeCoordinator::relationshipEngine() const
{
    return relationshipEngineInstance.get();
}

SmartRelationshipBuilder* SemanticRuntimeCoordinator::relationshipBuilder() const
{
    return relationshipBuilderInstance.get();
}
