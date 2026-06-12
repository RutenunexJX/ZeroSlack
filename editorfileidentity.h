#ifndef EDITORFILEIDENTITY_H
#define EDITORFILEIDENTITY_H

#include <QString>

class EditorFileIdentity
{
public:
    static QString normalized(QString fileName);

    bool set(QString nextFileName);
    QString current() const;

private:
    QString fileName;
};

#endif // EDITORFILEIDENTITY_H
