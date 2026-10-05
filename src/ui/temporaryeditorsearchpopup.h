#ifndef TEMPORARYEDITORSEARCHPOPUP_H
#define TEMPORARYEDITORSEARCHPOPUP_H

#include "editorsearchcandidate.h"

#include <QFrame>
#include <QPointer>

#include <functional>

class QListView;
class TemporaryEditorSearchModel;
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
    void setCancellationHandler(std::function<void()> handler);
    void refreshTheme();
    void synchronizeGeometry();

    QListView* resultsList() const;
    EditorSearchCandidates visibleCandidates() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QLineEdit> searchField;
    QListView* list = nullptr;
    TemporaryEditorSearchModel* model = nullptr;
    QString queryValue;
    ActivationHandler activationHandler;
    std::function<void()> cancellationHandler;

    void repositionBelowSearchField();
    void moveSelection(int delta);
    bool activateCurrentOrUniqueExact();
    void activateRow(int row);
};

#endif // TEMPORARYEDITORSEARCHPOPUP_H
