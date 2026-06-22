#ifndef FOLDBLOCKSHELFMODEL_H
#define FOLDBLOCKSHELFMODEL_H

#include <QObject>
#include <QList>
#include <QString>

enum class FoldShelfOriginKind {
    Moved,
    Copied
};

struct FoldShelfItem {
    QString id;
    QString alias;
    QString text;
    QString sourceFile;
    QString sourceModule;
    int sourceStartLine = -1;
    int sourceEndLine = -1;
    int lineCount = 0;
    FoldShelfOriginKind originKind = FoldShelfOriginKind::Copied;
    bool consumed = false;
    bool stale = false;
};

class FoldBlockShelfModel : public QObject
{
    Q_OBJECT

public:
    explicit FoldBlockShelfModel(QObject* parent = nullptr);

    QList<FoldShelfItem> items() const;
    FoldShelfItem item(const QString& id) const;
    QString addItem(FoldShelfItem item);
    bool consumeItem(const QString& id);
    bool removeItem(const QString& id);
    void clear();

signals:
    void changed();

private:
    QList<FoldShelfItem> shelfItems;
    int nextId = 1;
};

QString foldShelfBlockMimeType();
QString foldShelfItemMimeType();
QByteArray encodeFoldShelfItem(const FoldShelfItem& item);
FoldShelfItem decodeFoldShelfItem(const QByteArray& payload);

#endif // FOLDBLOCKSHELFMODEL_H
