#pragma once

#include "semanticindex.h"
#include "zeroslackexport.h"
class EffectiveValueService;

// Rules describe a conversion, never the current configuration. They live in
// an adjacent source comment and participate in the document's undo history.
struct TbEngineeringRule {
    QString kind = QStringLiteral("reciprocal");
    QString scale, offset, unit, source;
    bool bound = false;
    QString error;
};

struct TbInputAssist {
    QString signal, module, expression, status;
    int start = -1, length = 0, line = -1, statementStart = -1;
    int ruleStart = -1, ruleLength = 0;
    quint64 bitWidth = 0;
    bool signedIntegral = false, current = false;
    QList<SemanticStructuredEnumValueFact> enumValues;
    TbEngineeringRule rule;
    QString annotation;
};

struct TbEngineeringPreview {
    bool valid = false;
    QString expression, actual, error, difference;
    bool quantized = false;
};

class ZEROSLACK_API TbInputAssistService {
public:
    // Product-level pause; shared semantic facts and stored rules remain intact.
    static constexpr bool interactiveEnabled() { return false; }
    static quint64 analysisCountForTesting();
    static QList<TbInputAssist> analyze(
        const QString& file, const QString& text, SemanticIndex& index,
        const std::function<bool()>& cancelled = {},
        const EffectiveValueService* values = nullptr, quint64 documentRevision = 0);
    static QString ruleComment(const TbInputAssist& input, const TbEngineeringRule& rule);
    static QString validateRule(const TbEngineeringRule& rule);
    static QString engineeringValue(const TbInputAssist& input, QString* error);
    static TbEngineeringPreview preview(const TbInputAssist& input, const QString& target);
};
