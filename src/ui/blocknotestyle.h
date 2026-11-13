#pragma once
#include "zeroslackexport.h"
#include <QColor>
#include <QFont>
#include <QObject>
#include <QVariantMap>

class QTextDocument;

struct BlockNoteAppearance {
    QFont font;
    qreal lineHeight = 1.6;
    QColor background, text, border;
    QString style;
};

// A projection of Settings Center values, with no independent storage.
class ZEROSLACK_API BlockNoteStyle final : public QObject {
    Q_OBJECT
public:
    static BlockNoteStyle& instance();
    void apply(const QVariantMap& effectiveValues);
    BlockNoteAppearance appearance(const QVariantMap& overrides = {}) const;
    static BlockNoteAppearance resolve(const QVariantMap& settings, const QVariantMap& overrides = {});
    static QVariantMap defaults(const QString& type);
    static bool validPreset(const QVariantMap& appearance);
    static QStringList presets();
    static QStringList fonts();
    static QStringList tones();
    static void format(QTextDocument* document, const BlockNoteAppearance& appearance);
    static qreal measure(const QString& text, qreal width, const BlockNoteAppearance& appearance);
    quint64 revision() const { return generation; }
signals:
    void changed();
private:
    BlockNoteStyle();
    QVariantMap values;
    quint64 generation = 0;
};
