#ifndef EDITORINSERTPALETTESERVICE_H
#define EDITORINSERTPALETTESERVICE_H

#include "globalcontrolservice.h"
#include "packagetoolservice.h"
#include <optional>

class EditorInsertPaletteService
{
public:
    void invalidate() { captured.reset(); }
    quint64 contextAnalysesForTesting() const { return contextAnalyses; }
    QList<GlobalControlItem> query(
        GlobalControlCategory category,
        const QString& text,
        const GlobalControlQueryContext& context) const;
private:
    struct CapturedFacts {
        GlobalControlQueryContext context;
        quint64 catalogRevision = 0;
        QList<CodeTemplateItem> templates;
        PackageImportSite importSite;
    };
    mutable std::optional<CapturedFacts> captured;
    mutable quint64 contextAnalyses = 0;
};

#endif // EDITORINSERTPALETTESERVICE_H
