#ifndef INCLUDENAVIGATIONSERVICE_H
#define INCLUDENAVIGATIONSERVICE_H

#include <QString>
#include <memory>

struct IncludeDirectiveTarget {
    bool matched = false;
    QString includePath;
    int startColumn = -1;
    int endColumn = -1;
};

class IncludeNavigationService
{
public:
    static IncludeNavigationService* getInstance();

    IncludeNavigationService();
    ~IncludeNavigationService();

    IncludeDirectiveTarget includeAtColumn(const QString& lineText,
                                           int column) const;

private:
    static std::unique_ptr<IncludeNavigationService> instance;
};

#endif // INCLUDENAVIGATIONSERVICE_H
