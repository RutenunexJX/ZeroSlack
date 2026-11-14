#pragma once
#include "zeroslackexport.h"
#include <QWidget>
#include <QPointer>
#include <QJsonObject>
class QLabel;
class DocumentBlockModel;
class ZEROSLACK_API WaveBlockContent final : public QWidget {
    Q_OBJECT
public:
    WaveBlockContent(DocumentBlockModel*,const QString& id,QWidget* parent);
    ~WaveBlockContent() override;
    bool present(const QString& payload,bool readOnly,bool editing=false);
    bool flush(QString* error);
    void rebind(DocumentBlockModel* value);
    void focusContent();
    static QString initialProject();
    static bool validate(const QByteArray&,QString* error);
private:
    QPointer<DocumentBlockModel> model;
    QString id,accepted;
    QLabel* status;
    QWidget* editor=nullptr;
    QWidget* preview=nullptr;
    bool updating=false,publishing=false,readOnly=false,editing=false,scheduled=false;
    QList<QByteArray> pendingProjects;
    QJsonObject savedView;
    static void callback(void*,const char*,size_t);
    void publishPending();
    void publishProject(const QByteArray&);
    void updateState();
    void updateHistory();
    void replay(bool redo);
    void saveView();
    QString viewKey() const;
    bool fail(const QString&,QString* error=nullptr);
    void bindModel();
};
