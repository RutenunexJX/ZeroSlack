#ifndef TEMPORARYEDITORSEARCHPOPUP_H
#define TEMPORARYEDITORSEARCHPOPUP_H

#include "editorsearchcandidate.h"

#include <QFrame>
#include <QPointer>

#include <functional>

class QListWidget;
class QLineEdit;

class TemporaryEditorSearchPopup final : public QFrame
{
public:
    using ActivationHandler =
        std::function<void(const EditorSearchCandidate&)>;

    explicit TemporaryEditorSearchPopup(QWidget* drawerParent);
    ~TemporaryEditorSearchPopup() override;

    void attachSearchField(QLineEdit* field);
    void setCandidates(const EditorSearchCandidates& candidates,
                       const QString& query);
    void clearCandidates();
    void setActivationHandler(ActivationHandler handler);
    void refreshTheme();
    void synchronizeGeometry();

    QListWidget* resultsList() const;
    EditorSearchCandidates visibleCandidates() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QLineEdit> searchField;
    QListWidget* list = nullptr;
    EditorSearchCandidates candidatesValue;
    QString queryValue;
    ActivationHandler activationHandler;

    void repositionBelowSearchField();
    void moveSelection(int delta);
    bool activateCurrentOrUniqueExact();
    void activateRow(int row);
};

#endif // TEMPORARYEDITORSEARCHPOPUP_H
