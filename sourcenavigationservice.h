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

struct SourceIdentifierTarget {
    bool matched = false;
    QString identifier;
    int startColumn = -1;
    int endColumn = -1;
};

enum class SourceNavigationTargetKind {
    None,
    IncludeDirective,
    PackageImport,
    Identifier
};

struct SourceNavigationTarget {
    bool matched = false;
    SourceNavigationTargetKind kind = SourceNavigationTargetKind::None;
    QString text;
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
    SourceIdentifierTarget identifierAtColumn(const QString& lineText,
                                              int column) const;
    SourceNavigationTarget targetAtColumn(const QString& lineText,
                                          int column) const;

private:
    static std::unique_ptr<SourceNavigationService> instance;

    static bool isIdentifierStart(QChar ch);
    static bool isIdentifierPart(QChar ch);
};

#endif // SOURCENAVIGATIONSERVICE_H
