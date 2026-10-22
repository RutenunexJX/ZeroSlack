#pragma once
#include <zeroslack/semantic/semanticanalysisinput.h>
#include <QMap>

struct CompilationOrderRequest {
    QString workspace;
    QStringList files;
    QStringList includeDirectories;
    QHash<QString, QString> defines;
    QHash<QString, QString> overrides;
};
struct CompilationOrderResult {
    QStringList files;
    QStringList headers;
    QStringList diagnostics;
    QMap<QString, QStringList> dependencies;
    std::shared_ptr<const SemanticAnalysisInput> input;
    bool valid() const { return diagnostics.isEmpty(); }
};

// One preprocessor per compilation unit, matching separate Questa vlog calls.
class CompilationOrderService {
public:
    static CompilationOrderResult plan(const CompilationOrderRequest& request,
                                      const std::function<bool()>& cancelled = {});
};
