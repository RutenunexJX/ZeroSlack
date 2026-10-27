#pragma once
#include <zeroslack/semantic/signaltraceservice.h>

enum class SignalFeedbackKind { Combinational, Sequential, Uncertain };
struct SignalFeedbackEdge {
    QString id;
    SignalTraceObject from, to;
    SignalTraceEvidence evidence;
};
struct SignalFeedbackFinding {
    QString id;
    SignalFeedbackKind kind=SignalFeedbackKind::Uncertain;
    QList<SignalFeedbackEdge> edges;
    QStringList limitations;
};
struct SignalFeedbackReport {
    SignalTraceContext context;
    SignalTraceStatus status=SignalTraceStatus::Unavailable;
    QList<SignalFeedbackFinding> findings;
    QStringList limitations;
    int nodeCount=0, edgeCount=0, operations=0;
};
class ZEROSLACK_SEMANTIC_API SignalFeedbackService {
public:
    explicit SignalFeedbackService(SignalTraceContext context) : m_context(std::move(context)) {}
    SignalFeedbackReport analyze(const QString& instancePath,
        const std::function<bool()>& cancelled={}) const;
    static QString kindName(SignalFeedbackKind kind);
private:
    SignalTraceContext m_context;
};
