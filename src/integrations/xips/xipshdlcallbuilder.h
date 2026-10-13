#pragma once
#include "globalcontrolservice.h"
#include "slangmanager.h"
#include "semanticanalysisinput.h"
#include <atomic>
#include <memory>

struct XipsHdlCallRequest {
    GlobalControlQueryContext context;
    QVariantMap entry;
    QString sourceFile;
    QHash<QString, QString> contents;
    QStringList orderedFiles, includeDirectories;
    QHash<QString, QString> defines;
    QString target, clock;
    bool rising = true;
    bool asserted = true;
    std::shared_ptr<std::atomic_bool> cancelled;
};

struct XipsHdlCallInspection {
    QString error;
    SemanticSymbolRecord callable;
    QString declaration, callName, bindingName;
    int declarationPosition = -1;
    QList<SemanticDiagnostic> diagnostics;
    std::shared_ptr<const SemanticAnalysisInput> input;
    bool needsSignalBinding = false;
    QList<SemanticSymbolRecord> bindingCandidates;
};

struct XipsHdlCallPlan {
    QString error;
    int start = -1, end = -1;
    QString text;
    CodeTemplateSlotList placeholders;
};

class XipsHdlCallBuilder {
public:
    static bool proceduralSite(const GlobalControlQueryContext& context, bool allowFunctions = false);
    static XipsHdlCallInspection inspect(const XipsHdlCallRequest& request);
    static XipsHdlCallPlan build(const XipsHdlCallRequest& request,
                               const XipsHdlCallInspection& inspection,
                               const QHash<QString, QString>& arguments);
};
