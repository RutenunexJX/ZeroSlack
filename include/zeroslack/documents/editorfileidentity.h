#ifndef EDITORFILEIDENTITY_H
#define EDITORFILEIDENTITY_H

#include <QString>

class EditorFileIdentity
{
public:
    static QString normalized(QString fileName);
    // Resolve once at a document binding boundary; retain the returned path
    // as the document ID even if the lexical alias later changes target.
    static QString physicalPath(QString fileName);
    static QString lookupKey(QString fileName);
    static bool same(const QString& lhs, const QString& rhs);

    bool set(QString nextFileName);
    QString current() const;

private:
    QString fileName;
};

#endif // EDITORFILEIDENTITY_H
