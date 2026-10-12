#pragma once
#include "tbinputassist.h"
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QHash>
#include <QTimer>

class MyCodeEditor;
class QFrame;
class QMenu;
class QTextDocument;

class TbInputAssistController : public QObject {
public:
    explicit TbInputAssistController(MyCodeEditor* editor);
    void publish(const QList<TbInputAssist>& inputs);
    void invalidate();
    void closePopup();
    void beginPaint();
    void painted(const QString& key, const QRect& rect);
    bool click(const QPoint& position);
    bool hover(const QPoint& position);
    void addContextAction(QMenu* menu, int position);
    QList<TbInputAssist> inputs() const { return items; }
    QHash<QString, QRect> hitRegions() const { return regions; }
    bool open(int index);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    MyCodeEditor* editor;
    QList<TbInputAssist> items;
    QHash<QString, QRect> regions;
    QPointer<QFrame> popup;
    QPointer<QTextDocument> document;
    QString file;
    quint64 revision = 0;
    SemanticSnapshotToken token;
    QTimer refreshTimer;
    bool hasInputContext = false;
    bool valid(const TbInputAssist& input) const;
    void apply(const TbInputAssist& input, int start, int length, const QString& expected, const QString& replacement);
    void showRule(const TbInputAssist& input, bool binding);
    QFrame* makePopup(const TbInputAssist& input, const QString& title);
    void placePopup(const TbInputAssist& input);
};
