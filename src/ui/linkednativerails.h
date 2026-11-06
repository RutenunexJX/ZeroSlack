#pragma once

#include "zeroslackexport.h"
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QSplitter>
#include <QStringList>
#include <memory>

class ZEROSLACK_API LinkedNativeRails final : public QSplitter {
    Q_OBJECT
public:
    struct Reference { QString id, source, title; };
    struct Position { int y = 0; qint64 contentY = 0; int viewportY = 0; };
    struct Group {
        QString anchor, title;
        int y = 0;
        QList<Reference> references;
        qint64 contentY = 0;
        int viewportY = 0;
        int sourceLine = -1, foldHead = -1, visibleRow = -1;
        bool positioned = true;
        QStringList members;
    };
    struct Preview { QString title, text, location, error; QImage image; bool complete = false; };
    struct ConnectorEndpoint {
        QString id;
        QStringList members;
        int side = 0, sourceLine = -1;
        bool positioned = false, visible = false;
        QPointF header;
        QRectF groupRect;
    };
    struct ConnectorSnapshot {
        QList<ConnectorEndpoint> endpoints;
        QRectF viewports[2];
        QList<QRectF> occlusions;
        quint64 layoutSerial = 0, presentationSerial = 0;
        qint64 contentRevision = -1;
    };

    explicit LinkedNativeRails(QWidget* editorPage, QWidget* parent = nullptr);
    ~LinkedNativeRails() override;
    void setGroups(const QList<Group>& groups, qint64 contentRevision = -1);
    void resetSession();
    void updateAnchorPositions(const QMap<QString, Position>& positions);
    int railViewportTop() const;
    void setGroupExpanded(const QString& anchor, bool expanded);
    void followAnchor(const QString& anchor, const QString& reason);
    void setPreview(const QString& source, const Preview& preview);
    void clearPreview(const QString& source);
    void setInitialSizes(const QList<int>& saved, double codeRatio);
    QJsonObject snapshot() const;
    ConnectorSnapshot connectorSnapshot(const QWidget* coordinates) const;
    void setActiveAnchors(const QSet<QString>& ids);

signals:
    void visibleReferencesChanged(const QStringList& ids);
    void locateCodeRequested(const QString& id);
    void openOriginalRequested(const QString& id);
    void codeRatioChanged(double ratio);
    void layoutApplied(quint64 serial, qint64 contentRevision);
    void expandFoldedRequested(const QStringList& anchors, qint64 contentRevision);
    void presentationChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};
