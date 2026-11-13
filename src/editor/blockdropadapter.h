#pragma once
#include "documentblockmodel.h"
class QMimeData;
class DocumentBlockStore;
class ZEROSLACK_API BlockDropAdapter {
public:
    static bool accepts(const QMimeData* data);
    static QList<EditorAssociatedBlock> decode(const QMimeData* data,DocumentBlockStore* store,QString* error);
};
