#pragma once
#include <QJsonObject>
#include <QString>

namespace simdock {
// One revision-checked, atomically replaced workspace record. Legacy project
// manifests are separate inputs and are never rewritten by this store.
class WorkspaceStateFile {
public:
    bool open(const QString& root, const QString& name, const QString& schema, QString* error);
    bool save(const QJsonObject& value, QString* error);
    bool exists() const { return m_exists; }
    QJsonObject value() const { return m_value; }
private:
    QString m_root, m_path, m_schema;
    QByteArray m_revision;
    QJsonObject m_value;
    bool m_exists = false;
};
}
