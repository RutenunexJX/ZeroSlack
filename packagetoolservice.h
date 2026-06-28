#ifndef PACKAGETOOLSERVICE_H
#define PACKAGETOOLSERVICE_H

#include "completiontypes.h"

#include <QList>
#include <QString>

enum class PackageToolKind {
    Parameter,
    Localparam,
    TypedefEnum,
    TypedefStruct,
    TypedefStructPacked,
    Function
};

struct EditorPackageToolAvailability {
    bool available = false;
    QString packageName;
    QString failureMessage;

    bool ok() const { return available; }
};

class PackageToolService
{
public:
    static QList<PackageToolKind> toolOrder();
    static QString idForKind(PackageToolKind kind);
    static QString labelForKind(PackageToolKind kind);

    CodeTemplateItem templateForKind(PackageToolKind kind) const;
    CodeTemplateItem templateForInsertion(PackageToolKind kind,
                                          const QString& lineIndent,
                                          bool insertAfterLine) const;
};

#endif // PACKAGETOOLSERVICE_H
