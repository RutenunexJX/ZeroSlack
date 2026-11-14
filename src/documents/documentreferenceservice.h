#pragma once
#include "documentblockmodel.h"
#include "pinloomcodelinkstore.h"
class PinloomHostClient;
class QWidget;
class QMenu;

// One local relation owns source binding and display. Pinloom owns the target.
class ZEROSLACK_API DocumentReferenceService final {
public:
    struct Selection { QString uri, caption; bool accepted = false; };
    static Selection pick(PinloomHostClient* client, QWidget* parent, const QString& initialUri = {});
    static QString insertSemantic(DocumentBlockModel* model, const PinloomSourceSelection& source,
        const QString& text, const Selection& target, QString* error = nullptr);
    static QString convertLegacy(DocumentBlockModel* model, const ResolvedPinloomCodeLink& anchor,
        const QString& linkId, const QString& display, QString* error = nullptr);
    static bool hasLegacy(const DocumentBlockModel* model, const QString& anchorId, const QString& linkId);
    static bool setDisplay(DocumentBlockModel* model, const QString& id, const QString& display);
    static void refresh(DocumentBlockModel* model, const QString& file, const QString& text,
        const TSDocument* syntax, const PinloomCodeLinkStore* legacy);
    static void addMenu(QMenu* menu, DocumentBlockModel* model, const QString& id,
        QWidget* host, std::function<bool()> readOnly);
};
