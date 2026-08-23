#ifndef PINLOOMHOSTCLIENT_H
#define PINLOOMHOSTCLIENT_H

#include "zeroslackexport.h"

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <functional>

struct ZEROSLACK_API PinloomHostIdentity {
    QString entryId;
    QString resourceId;
    QString anchorId;
    QString clipId;

    bool isValid() const;
    QVariantMap toVariantMap() const;
    QJsonObject toJson() const;
    QUrl toUri() const;

    static PinloomHostIdentity fromVariantMap(const QVariantMap& map);
    static PinloomHostIdentity fromJson(const QJsonObject& object);
    static PinloomHostIdentity fromUri(const QUrl& uri);
};

struct ZEROSLACK_API PinloomHostEntry {
    PinloomHostIdentity identity;
    QUrl uri;
    QString type;
    QString title;
    QStringList aliases;
    QStringList tags;
    QString summary;
    QString location;
    QString matchedField;
    QString matchSummary;
    bool pinned = false;
    bool deleted = false;
    QVariantMap metadata;

    bool isValid() const;
    QVariantMap toVariantMap() const;

    static PinloomHostEntry fromJson(const QJsonObject& object);
    static PinloomHostEntry fromVariantMap(const QVariantMap& map);
};

struct ZEROSLACK_API PinloomHostDocument {
    PinloomHostEntry entry;
    QString content;
    QString contentType;
    QVariantMap details;

    bool isValid() const;
    static PinloomHostDocument fromJson(const QJsonObject& object);
};

class ZEROSLACK_API PinloomHostClient final : public QObject
{
    Q_OBJECT

public:
    using RawReplyHandler =
        std::function<void(const QJsonObject&, const QString&)>;
    using RequestTransport =
        std::function<void(const QJsonObject&, RawReplyHandler)>;
    using SearchHandler =
        std::function<void(const QList<PinloomHostEntry>&,
                           const QString&)>;
    using ResolveHandler =
        std::function<void(const PinloomHostDocument&,
                           const QString&)>;
    using CompletionHandler =
        std::function<void(bool, const QString&)>;

    explicit PinloomHostClient(QObject* parent = nullptr);
    explicit PinloomHostClient(RequestTransport transport,
                               QObject* parent = nullptr);

    QString executablePath() const;
    void setExecutablePath(const QString& path);

    void search(const QString& query,
                int limit,
                SearchHandler handler);
    void resolve(const PinloomHostIdentity& identity,
                 ResolveHandler handler);
    void open(const PinloomHostIdentity& identity,
              CompletionHandler handler);
    void capabilities(RawReplyHandler handler);

    static QString protocolName();
    static QString defaultServerName();
    static QString discoverExecutablePath(
        const QString& configuredPath = QString());

private:
    void request(const QString& method,
                 const QJsonObject& params,
                 RawReplyHandler handler);

    RequestTransport customTransport;
    QString configuredExecutablePath;
};

Q_DECLARE_METATYPE(PinloomHostIdentity)
Q_DECLARE_METATYPE(PinloomHostEntry)
Q_DECLARE_METATYPE(PinloomHostDocument)

#endif // PINLOOMHOSTCLIENT_H
