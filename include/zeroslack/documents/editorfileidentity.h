#ifndef EDITORFILEIDENTITY_H
#define EDITORFILEIDENTITY_H

#include <QString>

class EditorFileIdentity
{
public:
    static QString normalized(QString fileName);
    static QString lookupKey(QString fileName);
    static bool same(const QString& lhs, const QString& rhs);

    bool set(QString nextFileName);
    QString current() const;

private:
    QString fileName;
};

#endif // EDITORFILEIDENTITY_H
