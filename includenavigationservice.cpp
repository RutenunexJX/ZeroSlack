#include "includenavigationservice.h"

std::unique_ptr<IncludeNavigationService> IncludeNavigationService::instance = nullptr;

IncludeNavigationService* IncludeNavigationService::getInstance()
{
    if (!instance)
        instance = std::make_unique<IncludeNavigationService>();
    return instance.get();
}

IncludeNavigationService::IncludeNavigationService() = default;

IncludeNavigationService::~IncludeNavigationService() = default;

IncludeDirectiveTarget IncludeNavigationService::includeAtColumn(
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
