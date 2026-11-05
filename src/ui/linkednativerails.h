#pragma once

#include "zeroslackexport.h"
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QSplitter>
#include <QStringList>
#include <memory>

class ZEROSLACK_API LinkedNativeRails final : public QSplitter {
    Q_OBJECT
public:
    struct Reference { QString id, source, title; };
    struct Group { QString anchor, title; int y = 0; QList<Reference> references; };
    struct Preview { QString title, text, location, error; QImage image; bool complete = false; };

    explicit LinkedNativeRails(QWidget* editorPage, QWidget* parent = nullptr);
    ~LinkedNativeRails() override;
    void setGroups(const QList<Group>& groups);
    void updateAnchorPositions(const QMap<QString, int>& positions);
    void setPreview(const QString& source, const Preview& preview);
    void clearPreview(const QString& source);
    void setInitialSizes(const QList<int>& saved, double codeRatio);
    QJsonObject snapshot() const;

signals:
    void visibleReferencesChanged(const QStringList& ids);
    void locateCodeRequested(const QString& id);
    void openOriginalRequested(const QString& id);
    void codeRatioChanged(double ratio);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};
