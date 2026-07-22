#ifndef COMMANDLAYERSERVICE_H
#define COMMANDLAYERSERVICE_H

#include "projectmodel.h"
#include "semanticindexsnapshot.h"

#include <QList>
#include <QString>
#include <memory>

enum class CommandLayerPickerItemKind {
    Module,
    Package
};

struct CommandLayerPickerItem {
    CommandLayerPickerItemKind kind = CommandLayerPickerItemKind::Module;
    QString name;
    QString kindLabel;
    QString scopeName;
    QString filePath;
    QString displayPath;
    int line = -1;
    int column = -1;
    int score = 0;
    SemanticSymbolRecord symbolRecord;
};

struct CommandLayerPickerQuery {
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    ProjectSnapshot project;
    QString filter;
    int maxResults = 200;
};

struct CommandLayerRelativeLineQuery {
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    QString fileName;
    QString currentModuleName;
    int currentLine = -1;
    int requestedModuleLine = -1;
};

struct CommandLayerRelativeLineResult {
    bool ok = false;
    QString message;
    QString filePath;
    int line = -1;
    int column = -1;
    int moduleLineCount = 0;
    SemanticSymbolRecord moduleRecord;
};

class CommandLayerService
{
public:
    QList<CommandLayerPickerItem> moduleItems(
        const CommandLayerPickerQuery& query) const;
    QList<CommandLayerPickerItem> packageItems(
        const CommandLayerPickerQuery& query) const;
    CommandLayerRelativeLineResult relativeLineTarget(
        const CommandLayerRelativeLineQuery& query) const;
};

#endif // COMMANDLAYERSERVICE_H
