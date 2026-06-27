#ifndef GLOBALCONTROLSERVICE_H
#define GLOBALCONTROLSERVICE_H

#include <QString>
#include <QList>

class ProjectModel;
class SemanticIndex;

enum class GlobalControlItemKind {
    Domain,
    Command,
    File,
    Symbol,
    Template,
    RtlInsight,
    Setting
};

struct GlobalControlItem {
    GlobalControlItemKind kind = GlobalControlItemKind::Command;
    QString id;
    QString title;
    QString subtitle;
    QString filePath;
    int line = -1;
    int column = -1;
};

class GlobalControlService
{
public:
    QList<GlobalControlItem> query(const QString& text,
                                   ProjectModel* projectModel,
                                   SemanticIndex* semanticIndex) const;

    QList<GlobalControlItem> commandItems() const;
    QList<GlobalControlItem> templateItems() const;
    QList<GlobalControlItem> rtlInsightItems() const;
    QList<GlobalControlItem> fileItems(ProjectModel* projectModel) const;
    QList<GlobalControlItem> symbolItems(SemanticIndex* semanticIndex) const;
};

#endif // GLOBALCONTROLSERVICE_H
