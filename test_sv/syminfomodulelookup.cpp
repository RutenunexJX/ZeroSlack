#include "syminfo.h"

#include <QRegularExpression>

bool sym_list::isValidModuleName(const QString& name)
{
    if (name.isEmpty())
        return false;
    static const QRegularExpression svIdentifier(QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_]*$"));
    return svIdentifier.match(name).hasMatch();
}

QString sym_list::getCachedFileContent(const QString& fileName) const
{
    return previousFileContents.value(fileName, QString());
}
