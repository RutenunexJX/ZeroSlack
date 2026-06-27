#ifndef COMMODESERVICE_H
#define COMMODESERVICE_H

#include "projectmodel.h"
#include "semanticindexsnapshot.h"

#include <QList>
#include <QString>
#include <memory>

enum class ComModePickerItemKind {
    Module,
    Package,
    Parameter,
    Localparam,
    Signal
};

struct ComModePickerItem {
    ComModePickerItemKind kind = ComModePickerItemKind::Module;
    QString name;
    QString kindLabel;
    QString detailLabel;
    QString scopeName;
    QString filePath;
    QString displayPath;
    int line = -1;
    int column = -1;
    int score = 0;
    SemanticSymbolRecord symbolRecord;
};

struct ComModePickerQuery {
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    ProjectSnapshot project;
    QString filter;
    int maxResults = 200;
};

struct ComModeScopedPickerQuery : ComModePickerQuery {
    QString fileName;
    QString currentModuleName;
    int currentLine = -1;
};

struct ComModeScopedPickerResult {
    bool hasScope = false;
    QString message;
    QString scopeName;
    QList<ComModePickerItem> items;
};

struct ComModeRelativeLineQuery {
    std::shared_ptr<const SemanticIndexSnapshot> snapshot;
    QString fileName;
    QString currentModuleName;
    int currentLine = -1;
    int requestedModuleLine = -1;
};

struct ComModeRelativeLineResult {
    bool ok = false;
    QString message;
    QString filePath;
    int line = -1;
    int column = -1;
    int moduleLineCount = 0;
    SemanticSymbolRecord moduleRecord;
};

class ComModeService
{
public:
    QList<ComModePickerItem> moduleItems(
        const ComModePickerQuery& query) const;
    QList<ComModePickerItem> packageItems(
        const ComModePickerQuery& query) const;
    ComModeScopedPickerResult parameterItems(
        const ComModeScopedPickerQuery& query) const;
    ComModeScopedPickerResult signalItems(
        const ComModeScopedPickerQuery& query) const;
    ComModeRelativeLineResult relativeLineTarget(
        const ComModeRelativeLineQuery& query) const;
};

#endif // COMMODESERVICE_H
