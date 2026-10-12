#pragma once
#include <QStringList>
#include <memory>
class QLockFile;

namespace simdock {
class SessionFiles {
public:
    SessionFiles();
    ~SessionFiles();
    bool create(QString* error, QStringList* notices);
    QString path() const { return m_path; }
    bool setChild(qint64 pid, QString* error);
    bool prune(const QStringList& retained, QString* error);
    bool release(QString* error);
    void preserveForRecovery();
    static QString cacheRoot();
    static QStringList recover(const QString& root);
private:
    QString m_root, m_path;
    qint64 m_child = 0;
    std::unique_ptr<QLockFile> m_lock;
};
}
