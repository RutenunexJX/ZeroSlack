#pragma once

#include "zeroslackexport.h"
#include <QJsonObject>
#include <QString>

// Full native board state. Pinloom source content remains transient and is not copied here.
class ZEROSLACK_API LinkedContextState {
public:
    bool load(const QString& workspace, const QString& fileName);
    bool save(const QJsonObject& state);
    bool writeCopy(const QString& destination, const QJsonObject& state, QString* failure) const;
    static bool validate(const QJsonObject& state, QString* error = nullptr);
    QJsonObject value;
    QString error;
    QString path;
private:
    QByteArray revision;
    QString identity;
    bool writable = false;
};
