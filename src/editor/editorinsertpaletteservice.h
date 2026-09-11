#ifndef EDITORINSERTPALETTESERVICE_H
#define EDITORINSERTPALETTESERVICE_H

#include "globalcontrolservice.h"

class EditorInsertPaletteService
{
public:
    QList<GlobalControlItem> query(
        GlobalControlCategory category,
        const QString& text,
        const GlobalControlQueryContext& context) const;
};

#endif // EDITORINSERTPALETTESERVICE_H
