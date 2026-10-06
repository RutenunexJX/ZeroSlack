#ifndef EDITORFILEIDENTITY_H
#define EDITORFILEIDENTITY_H

#include <QString>
#include <QStringList>
#include <QHash>

class EditorFileIdentity
{
public:
    static QString normalized(QString fileName);
    // Resolve once at a document binding boundary; retain the returned path
    // as the document ID even if the lexical alias later changes target.
    static QString physicalPath(QString fileName);
    static QString lookupKey(QString fileName);
    // Bind a publication's inputs together. Regular files share one resolved
    // parent per lexical directory; links, missing and short-name paths keep
    // the full resolver. The temporary parent cache ends with this call.
    static QHash<QString, QString> lookupKeys(const QStringList& fileNames);
    static bool same(const QString& lhs, const QString& rhs);

    bool set(QString nextFileName);
    QString current() const;

private:
    QString fileName;
};

#endif // EDITORFILEIDENTITY_H
