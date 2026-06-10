#include "sourcenavigationservice.h"

std::unique_ptr<SourceNavigationService> SourceNavigationService::instance = nullptr;

SourceNavigationService* SourceNavigationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<SourceNavigationService>();
    return instance.get();
}

SourceNavigationService::SourceNavigationService() = default;

SourceNavigationService::~SourceNavigationService() = default;

IncludeDirectiveTarget SourceNavigationService::includeAtColumn(
    const QString& lineText,
    int column) const
{
    IncludeDirectiveTarget target;
    if (lineText.isEmpty() || column < 0)
        return target;

    const int keywordPos = lineText.indexOf(QStringLiteral("`include"));
    if (keywordPos < 0)
        return target;

    const int firstQuote = lineText.indexOf(QLatin1Char('"'), keywordPos);
    if (firstQuote < 0)
        return target;

    const int secondQuote = lineText.indexOf(QLatin1Char('"'), firstQuote + 1);
    if (secondQuote < 0)
        return target;

    if (column <= firstQuote || column >= secondQuote)
        return target;

    const QString includePath =
        lineText.mid(firstQuote + 1, secondQuote - firstQuote - 1).trimmed();
    if (includePath.isEmpty())
        return target;

    target.matched = true;
    target.includePath = includePath;
    target.startColumn = firstQuote + 1;
    target.endColumn = secondQuote;
    return target;
}

PackageImportTarget SourceNavigationService::packageImportAtColumn(
    const QString& lineText,
    int column) const
{
    PackageImportTarget target;
    if (lineText.isEmpty() || column < 0)
        return target;

    const int importPos = lineText.indexOf(QStringLiteral("import"));
    if (importPos < 0)
        return target;

    int packageStart = importPos + QStringLiteral("import").size();
    while (packageStart < lineText.size()
           && lineText.at(packageStart).isSpace()) {
        ++packageStart;
    }

    if (packageStart >= lineText.size())
        return target;

    const QChar firstPackageChar = lineText.at(packageStart);
    if (!firstPackageChar.isLetter() && firstPackageChar != QLatin1Char('_'))
        return target;

    int packageEnd = packageStart + 1;
    while (packageEnd < lineText.size()) {
        const QChar current = lineText.at(packageEnd);
        if (!current.isLetterOrNumber() && current != QLatin1Char('_'))
            break;
        ++packageEnd;
    }

    int scopePos = packageEnd;
    while (scopePos < lineText.size()
           && lineText.at(scopePos).isSpace()) {
        ++scopePos;
    }
    if (lineText.mid(scopePos, 2) != QStringLiteral("::"))
        return target;

    if (column < packageStart || column >= packageEnd)
        return target;

    target.matched = true;
    target.packageName = lineText.mid(packageStart, packageEnd - packageStart);
    target.startColumn = packageStart;
    target.endColumn = packageEnd;
    return target;
}
