#pragma once

#include "linkedconnectorpath.h"
#include "zeroslackexport.h"
#include <QPointer>
#include <QTextDocument>
#include <QWidget>

class ZEROSLACK_API LinkedConnectorOverlay final : public QWidget {
    Q_OBJECT
public:
    struct Identity {
        QString session;
        QPointer<QTextDocument> document;
        quint64 generation = 0, requestSerial = 0, layoutSerial = 0, presentationSerial = 0;
        qint64 contentRevision = -1;
        int documentRevision = -1;
    };
    struct Stroke { LinkedConnectorPath::Input input; QRectF clip; };
    struct Scene {
        Identity identity;
        QRectF central, codeViewport;
        QList<QRectF> occlusions;
        QList<Stroke> strokes;
        bool valid = false;
    };
    struct Completion {
        quint64 paintSerial = 0, geometrySerial = 0;
        Identity identity;
        int paths = 0;
        bool valid = false;
    };

    explicit LinkedConnectorOverlay(QWidget* host);
    void setScene(const Scene& scene);
    const Scene& scene() const { return m_scene; }
    const QList<LinkedConnectorPath::Route>& routes() const { return m_routes; }
    quint64 geometrySerial() const { return m_geometrySerial; }
    const Completion& completion() const { return m_completion; }

signals:
    void presentationRequested();
    void geometryApplied(quint64 serial);
    void paintCompleted(quint64 paintSerial, quint64 geometrySerial);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    Scene m_scene;
    QList<LinkedConnectorPath::Route> m_routes;
    quint64 m_geometrySerial = 0;
    Completion m_completion;
};
