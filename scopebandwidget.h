#ifndef SCOPEBANDWIDGET_H
#define SCOPEBANDWIDGET_H

#include "scopeitem.h"
#include "mycodeeditor.h"
#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPointer>
#include <QString>

/**
 */
class ScopeBandWidget : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kBandWidth = 14;

    explicit ScopeBandWidget(QWidget* parent = nullptr);
    ~ScopeBandWidget();

    void setEditor(MyCodeEditor* editor);
    void setDocumentFileName(const QString& fileName);
    MyCodeEditor* editor() const { return m_editor.data(); }

    void refresh();

private slots:
    void onEditorUpdateRequest(const QRect& rect, int dy);
    void onEditorScrollValueChanged(int value);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void connectEditor();
    void disconnectEditor();
    void syncScrollFromEditor();

    QPointer<MyCodeEditor> m_editor;
    QString m_fileName;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsView* m_view = nullptr;
    bool m_refreshScheduled = false;
    QMetaObject::Connection m_blockCountConnection;
};

#endif // SCOPEBANDWIDGET_H
