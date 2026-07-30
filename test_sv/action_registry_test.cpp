#include "actionregistry.h"
#include "codetemplateservice.h"
#include "commandlayercommandregistry.h"
#include "globalcontrolservice.h"
#include "inlinecommandmode.h"
#include "packagetoolservice.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {
int checks = 0;
int failures = 0;

void expect(const char* name, bool value)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("[%s] %s\n", value ? "PASS" : "FAIL", name);
}

bool descriptorHasAlias(const ActionDescriptor& descriptor,
                        ActionSurface surface,
                        const QString& token)
{
    const ActionAliasDescriptor* alias =
        findActionAlias(descriptor, surface, token);
    return alias && alias->token == token;
}
}

int main()
{
    QString registryReason;
    expect("unified action registry validates",
           actionRegistryIsValid(&registryReason));
    bool invalidIdsRejected = true;
    const QStringList invalidIds = {
        QStringLiteral("Navigation.goLine"),
        QStringLiteral("navigation"),
        QStringLiteral("navigation."),
        QStringLiteral("navigation..goLine"),
        QStringLiteral("navigation.go_line"),
        QStringLiteral("navigation.göLine"),
    };
    for (const QString& invalidId : invalidIds) {
        QList<ActionDescriptor> invalidRegistry = actionRegistry();
        invalidRegistry.front().id = invalidId;
        invalidIdsRejected =
            invalidIdsRejected
            && !validateActionRegistry(invalidRegistry);
    }
    expect("action ids retain the ASCII dotted identifier policy",
           invalidIdsRejected);
    expect("unified catalog covers all trigger families",
           actionRegistry().size() >= 50
               && !actionDescriptorsForSurface(
                       ActionSurface::CommandLayer).isEmpty()
               && !actionDescriptorsForSurface(
                       ActionSurface::InlineSemantic).isEmpty()
               && !actionDescriptorsForSurface(
                       ActionSurface::InlineTemplate).isEmpty()
               && !actionDescriptorsForSurface(
                       ActionSurface::GlobalControl).isEmpty()
               && !actionDescriptorsForSurface(
                       ActionSurface::PackageTools).isEmpty()
               && !actionDescriptorsForSurface(
                       ActionSurface::ContextMenu).isEmpty());
    const QList<ActionCatalogEntry> catalog =
        unifiedActionCatalog();
    bool catalogHasHelpAliases = false;
    bool catalogHasGlobalSession = false;
    bool catalogHasExpose = false;
    for (const ActionCatalogEntry& entry : catalog) {
        if (entry.actionId == QStringLiteral("help.actionCatalog")) {
            catalogHasHelpAliases =
                entry.displayText.contains(
                    QStringLiteral("F24 Command Layer: help"))
                && entry.displayText.contains(
                    QStringLiteral("Inline semantic command: ;?"))
                && entry.displayText.contains(
                    QStringLiteral("Inline template command: ;;?"));
        } else if (entry.actionId
                   == QStringLiteral("workspace.session.save")) {
            catalogHasGlobalSession =
                entry.displayText.contains(
                    QStringLiteral("Global Control: ow s save"));
        } else if (entry.actionId
                   == QStringLiteral("refactor.exposeSignalToTop")) {
            catalogHasExpose =
                entry.displayText.contains(
                    QStringLiteral("Context menu"));
        }
    }
    expect("unified user-visible catalog exposes every action and alias",
           catalog.size() == actionRegistry().size()
               && catalogHasHelpAliases
               && catalogHasGlobalSession
               && catalogHasExpose);

    bool everyDescriptorComplete = true;
    for (const ActionDescriptor& descriptor : actionRegistry()) {
        everyDescriptorComplete =
            everyDescriptorComplete
            && !descriptor.id.trimmed().isEmpty()
            && !descriptor.canonicalName.trimmed().isEmpty()
            && !descriptor.description.trimmed().isEmpty()
            && descriptor.category != ActionCategory::Unknown
            && descriptor.scope != ActionScope::Unknown
            && !descriptor.executionRoute.trimmed().isEmpty()
            && !descriptor.aliases.isEmpty();
        if (descriptor.requirementMask != 0) {
            everyDescriptorComplete =
                everyDescriptorComplete
                && !descriptor.unavailableReason.trimmed().isEmpty();
        }
    }
    expect("every descriptor has complete canonical metadata",
           everyDescriptorComplete);

    const ActionDescriptor* saveLocalSession =
        findActionById(
            QStringLiteral("workspace.session.save"));
    const ActionDescriptor* restoreLocalSession =
        findActionById(
            QStringLiteral("workspace.session.restore"));
    const ActionDescriptor* clearLocalSession =
        findActionById(
            QStringLiteral("workspace.session.clean"));
    expect("ow s names local state and preserves portable project config",
           saveLocalSession
               && restoreLocalSession
               && clearLocalSession
               && saveLocalSession->canonicalName
                      == QStringLiteral(
                          "Save Local Workspace Session")
               && saveLocalSession->description.contains(
                      QStringLiteral("local AppData"))
               && saveLocalSession->description.contains(
                      QStringLiteral(
                          "project configuration is unchanged"))
               && restoreLocalSession->description.contains(
                      QStringLiteral("read-only"))
               && clearLocalSession->canonicalName
                      == QStringLiteral(
                          "Clear Local Workspace Session")
               && clearLocalSession->description.contains(
                      QStringLiteral("project.json"))
               && clearLocalSession->description.contains(
                      QStringLiteral("legacy .zs")));

    bool commandLayerAdapterUsesRegistry = true;
    for (const CommandLayerCommandMetadata& command :
         commandLayerCommandRegistry()) {
        const ActionDescriptor* descriptor =
            findActionById(command.actionId);
        commandLayerAdapterUsesRegistry =
            commandLayerAdapterUsesRegistry
            && descriptor
            && command.name == descriptor
                                   ->aliasForSurface(
                                       ActionSurface::CommandLayer)
                                   .token
            && command.description == descriptor->description
            && command.executionRoute == descriptor->executionRoute;
    }
    expect("all F24 commands are registry adapters",
           commandLayerCommandRegistry().size() == 11
               && commandLayerAdapterUsesRegistry);

    bool inlineAdaptersUseRegistry = true;
    for (const InlineCommandDescriptor& inlineDescriptor :
         InlineCommandMode::descriptors()) {
        const ActionSurface surface =
            inlineDescriptor.intent == InlineCommandIntent::CodeTemplate
            ? ActionSurface::InlineTemplate
            : ActionSurface::InlineSemantic;
        const ActionDescriptor* descriptor =
            findActionById(inlineDescriptor.actionId);
        inlineAdaptersUseRegistry =
            inlineAdaptersUseRegistry
            && descriptor
            && inlineDescriptor.executionRoute
                   == descriptor->executionRoute
            && descriptorHasAlias(*descriptor,
                                  surface,
                                  inlineDescriptor.label);
    }
    expect("all built-in inline commands are registry adapters",
           InlineCommandMode::descriptors().size() >= 44
               && inlineAdaptersUseRegistry);

    const ActionDescriptor* semanticParameter =
        findActionByAlias(ActionSurface::InlineSemantic,
                          QStringLiteral(";p"));
    const ActionDescriptor* templateParameter =
        findActionByAlias(ActionSurface::InlineTemplate,
                          QStringLiteral(";;p"));
    expect("semantic search and template insertion remain distinct intents",
           semanticParameter
               && templateParameter
               && semanticParameter->id != templateParameter->id
               && semanticParameter->executionRoute
                      != templateParameter->executionRoute);

    const ActionDescriptor* visibleSymbols =
        findActionByAlias(ActionSurface::InlineSemantic,
                          QStringLiteral(";v"));
    const ActionDescriptor* visibleSymbolsTemplate =
        findActionByAlias(ActionSurface::InlineTemplate,
                          QStringLiteral(";;v"));
    expect("visible-symbol token is a unique semantic-only action",
           visibleSymbols
               && visibleSymbols->id
                      == QStringLiteral("completion.visibleSymbols")
               && visibleSymbols->canonicalName
                      == QStringLiteral("Search Visible Symbols")
               && visibleSymbols->executionRoute
                      == QStringLiteral("completion.semantic")
               && !visibleSymbolsTemplate);

    bool templatesUseRegistry = true;
    for (const CodeTemplateItem& item :
         CodeTemplateService::getInstance()->catalog()) {
        const ActionDescriptor* descriptor =
            findActionById(item.actionId);
        templatesUseRegistry =
            templatesUseRegistry
            && descriptor
            && item.executionRoute == descriptor->executionRoute
            && descriptorHasAlias(*descriptor,
                                  ActionSurface::InlineTemplate,
                                  item.commandToken);
    }
    expect("built-in template catalog uses registry metadata",
           CodeTemplateService::getInstance()->catalog().size() == 15
               && templatesUseRegistry);

    const PackageToolService packageTools;
    bool packageToolsUseRegistry = true;
    for (const PackageToolKind kind : PackageToolService::toolOrder()) {
        const QString actionId =
            PackageToolService::actionIdForKind(kind);
        const ActionDescriptor* descriptor =
            findActionById(actionId);
        const CodeTemplateItem item =
            packageTools.templateForKind(kind);
        packageToolsUseRegistry =
            packageToolsUseRegistry
            && descriptor
            && item.actionId == actionId
            && item.executionRoute == descriptor->executionRoute
            && descriptorHasAlias(
                *descriptor,
                ActionSurface::PackageTools,
                PackageToolService::idForKind(kind));
    }
    expect("all Package Tools are registry adapters",
           PackageToolService::toolOrder().size() == 6
               && packageToolsUseRegistry);

    const QStringList sourceActionIds = {
        QStringLiteral("source.goToDefinition"),
        QStringLiteral("source.findReferences"),
        QStringLiteral("source.showRelationships"),
        QStringLiteral("insight.signalKernelGraph"),
        QStringLiteral("insight.signalUsageHotspot"),
        QStringLiteral("insight.stateTransitionGraph"),
        QStringLiteral("insight.moduleBlockDiagram"),
        QStringLiteral("refactor.exposeSignalToTop"),
    };
    bool sourceActionsComplete = true;
    for (const QString& id : sourceActionIds) {
        const ActionDescriptor* descriptor = findActionById(id);
        sourceActionsComplete =
            sourceActionsComplete
            && descriptor
            && descriptor->hasSurface(ActionSurface::ContextMenu);
    }
    const ActionDescriptor* expose =
        findActionById(QStringLiteral("refactor.exposeSignalToTop"));
    ActionAvailabilityContext exposeContext;
    exposeContext.editorAvailable = true;
    exposeContext.workspaceAvailable = true;
    exposeContext.semanticCurrent = true;
    exposeContext.symbolAvailable = true;
    const ActionAvailabilityState exposeAvailability =
        expose ? evaluateActionAvailability(*expose, exposeContext)
               : ActionAvailabilityState();
    expect("source actions and Expose have canonical context-menu descriptors",
           sourceActionsComplete
               && expose
               && expose->executionRoute
                      == QStringLiteral(
                          "rtledit.signal.exposeToTop")
               && !exposeAvailability.executable
               && exposeAvailability.enterable()
               && !exposeAvailability.reason.isEmpty());

    bool globalControlUsesRegistry = true;
    const GlobalControlService globalControl;
    for (const QString& query :
         {QStringLiteral("fd"),
          QStringLiteral("ow"),
          QStringLiteral("ow s")}) {
        for (const GlobalControlItem& item :
             globalControl.query(query)) {
            if (item.kind != GlobalControlItemKind::Command)
                continue;
            const ActionDescriptor* descriptor =
                findActionById(item.actionId);
            globalControlUsesRegistry =
                globalControlUsesRegistry
                && descriptor
                && item.executionRoute
                       == descriptor->executionRoute
                && descriptorHasAlias(*descriptor,
                                      ActionSurface::GlobalControl,
                                      item.id);
        }
    }
    expect("static Global Control commands use registry metadata",
           globalControlUsesRegistry);

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
