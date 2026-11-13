#pragma once
#include "zeroslackexport.h"
#include <QWidget>
#include <QPointer>
class QLabel;
class QTabWidget;
class DocumentBlockModel;

class ZEROSLACK_API WaveBlockContent final : public QWidget {
    Q_OBJECT
public:
    WaveBlockContent(DocumentBlockModel* model,const QString& id,QWidget* parent);
    void present(const QString& payload,bool readOnly);
    bool flush(QString* error);
    void focusContent();
    static QString initialProject();
    static bool validate(const QByteArray& payload,QString* error);
protected:
    bool eventFilter(QObject*,QEvent*) override;
private slots:
    void edited();
private:
    QPointer<DocumentBlockModel> model;
    QString id,accepted,nativeBaseline;
    quint64 previewGeneration=0;
    QTabWidget* tabs;
    QLabel* status;
    QWidget* editor=nullptr;
    QWidget* preview=nullptr;
    bool updating=false,readOnly=false;
    void refreshPreview(const QByteArray& payload);
};
