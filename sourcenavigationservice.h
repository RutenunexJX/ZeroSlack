#ifndef SOURCENAVIGATIONSERVICE_H
#define SOURCENAVIGATIONSERVICE_H

#include <QString>
#include <memory>

struct IncludeDirectiveTarget {
    bool matched = false;
    QString includePath;
    int startColumn = -1;
    int endColumn = -1;
};

struct PackageImportTarget {
    bool matched = false;
    QString packageName;
    int startColumn = -1;
    int endColumn = -1;
};

class SourceNavigationService
{
public:
    static SourceNavigationService* getInstance();

    SourceNavigationService();
    ~SourceNavigationService();

    IncludeDirectiveTarget includeAtColumn(const QString& lineText,
                                           int column) const;
    PackageImportTarget packageImportAtColumn(const QString& lineText,
                                              int column) const;

private:
    static std::unique_ptr<SourceNavigationService> instance;
};

#endif // SOURCENAVIGATIONSERVICE_H
