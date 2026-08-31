#ifndef SEMANTICSTATEVIEW_H
#define SEMANTICSTATEVIEW_H

#include "zeroslackexport.h"

#include <QWidget>

class QLabel;
class QPushButton;

enum class SemanticStateKind {
    Empty,
    Loading,
    Stale,
    Warning,
    Error
};

class ZEROSLACK_API SemanticStateView final : public QWidget
{
    Q_OBJECT

public:
    explicit SemanticStateView(QWidget* parent = nullptr);

    void setState(SemanticStateKind kind,
                  const QString& title,
                  const QString& detail = {},
                  const QString& actionText = {});
    SemanticStateKind stateKind() const;
    QString titleText() const;
    QString detailText() const;
    QPushButton* actionButton() const;

signals:
    void actionRequested();

private:
    SemanticStateKind kindValue = SemanticStateKind::Empty;
    QLabel* marker = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* detailLabel = nullptr;
    QPushButton* action = nullptr;

    void refreshStyle();
};

#endif // SEMANTICSTATEVIEW_H
