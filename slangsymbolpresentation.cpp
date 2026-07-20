#include "slangsymbolpresentation.h"
#include "slangsymbolcollectorhelpers.h"

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/expressions/LiteralExpressions.h>
#include <slang/ast/expressions/OperatorExpressions.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/syntax/AllSyntax.h>
#include <slang/syntax/SyntaxPrinter.h>
#include <slang/syntax/SyntaxTree.h>
#include <slang/syntax/SyntaxVisitor.h>
#include <slang/text/SourceManager.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStringList>

#include <limits>
#include <utility>

namespace {
using namespace slang::ast;
using namespace slang::syntax;

QString normalizedFileName(const QString& fileName)
{
    if (fileName.isEmpty())
        return QString();
    QString result = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(fileName).absoluteFilePath()));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

QString flattenSyntaxText(QString text)
{
    QString result;
    result.reserve(text.size());
    bool inString = false;
    bool escaped = false;
    bool pendingSpace = false;
    for (const QChar ch : std::as_const(text)) {
        if (inString) {
            result.append(ch);
            if (escaped)
                escaped = false;
            else if (ch == QLatin1Char('\\'))
                escaped = true;
            else if (ch == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            if (pendingSpace && !result.isEmpty())
                result.append(QLatin1Char(' '));
            pendingSpace = false;
            inString = true;
            result.append(ch);
            continue;
        }
        if (ch.isSpace()) {
            pendingSpace = !result.isEmpty();
            continue;
        }
        if (pendingSpace)
            result.append(QLatin1Char(' '));
        pendingSpace = false;
        result.append(ch);
    }
    return result.trimmed();
}

QString printedSyntax(const SyntaxNode& syntax)
{
    SyntaxPrinter printer;
    printer.setIncludeComments(false);
    printer.setSquashNewlines(true);
    printer.printExcludingLeadingComments(syntax);
    return flattenSyntaxText(
        QString::fromStdString(std::string(printer.str())));
}

QString printedToken(slang::parsing::Token token)
{
    if (!token)
        return QString();
    SyntaxPrinter printer;
    printer.setIncludeComments(false);
    printer.setSquashNewlines(true);
    printer.print(token);
    return flattenSyntaxText(
        QString::fromStdString(std::string(printer.str())));
}

QString joinedParts(const QStringList& parts)
{
    QStringList result;
    for (const QString& part : parts) {
        const QString clean = flattenSyntaxText(part);
        if (!clean.isEmpty())
            result.append(clean);
    }
    return result.join(QLatin1Char(' '));
}

QString attributesText(const SyntaxList<AttributeInstanceSyntax>& attributes)
{
    QStringList parts;
    for (const AttributeInstanceSyntax* attribute : attributes) {
        if (attribute)
            parts.append(printedSyntax(*attribute));
    }
    return joinedParts(parts);
}

QString dimensionsText(
    const SyntaxList<VariableDimensionSyntax>& dimensions)
{
    QStringList parts;
    for (const VariableDimensionSyntax* dimension : dimensions) {
        if (dimension)
            parts.append(printedSyntax(*dimension));
    }
    return joinedParts(parts);
}

QString sourcePackedDimensionsText(const PortHeaderSyntax& header)
{
    const DataTypeSyntax* dataType = nullptr;
    if (header.kind == SyntaxKind::VariablePortHeader) {
        constexpr auto typeMember = &VariablePortHeaderSyntax::dataType;
        dataType = header.as<VariablePortHeaderSyntax>().*typeMember;
    } else if (header.kind == SyntaxKind::NetPortHeader) {
        constexpr auto typeMember = &NetPortHeaderSyntax::dataType;
        dataType = header.as<NetPortHeaderSyntax>().*typeMember;
    }
    if (!dataType)
        return QString();

    QStringList parts;
    auto visitor = makeSyntaxVisitor(
        [&](auto& nestedVisitor,
            const VariableDimensionSyntax& dimension) {
            parts.append(printedSyntax(dimension));
            nestedVisitor.visitDefault(dimension);
        });
    dataType->visit(visitor);
    return joinedParts(parts);
}

QString portSourcePresentationKey(
    const slang::SourceManager* sourceManager,
    slang::SourceLocation location,
    const QString& portName)
{
    if (!sourceManager || !location.valid() || portName.isEmpty())
        return QString();
    const auto position =
        slang_symbols::detail::qTextDocumentSourcePosition(
            sourceManager, location);
    if (!position.isValid())
        return QString();
    return QStringLiteral("%1|%2|%3|o")
        .arg(normalizedFileName(position.fileName))
        .arg(position.position)
        .arg(portName);
}

void appendAnsiPortPresentations(
    const ModuleDeclarationSyntax& module,
    const slang::SourceManager* sourceManager,
    QHash<QString, SemanticSymbolPresentation>* result)
{
    if (!result || !module.header->ports
        || module.header->ports->kind != SyntaxKind::AnsiPortList) {
        return;
    }

    const auto& ports = module.header->ports->as<AnsiPortListSyntax>();
    QString inheritedHeader;
    QString inheritedPackedDimensions;
    for (const MemberSyntax* member : ports.ports) {
        if (!member)
            continue;

        if (member->kind == SyntaxKind::ExplicitAnsiPort) {
            const auto& port = member->as<ExplicitAnsiPortSyntax>();
            const QString name = QString::fromStdString(
                std::string(port.name.valueText()));
            const QString key = portSourcePresentationKey(
                sourceManager, port.name.location(), name);
            if (!key.isEmpty()) {
                SemanticSymbolPresentation presentation;
                presentation.declarationText = printedSyntax(port);
                result->insert(key, std::move(presentation));
            }
            continue;
        }
        if (member->kind != SyntaxKind::ImplicitAnsiPort)
            continue;

        const auto& port = member->as<ImplicitAnsiPortSyntax>();
        QString header = printedSyntax(*port.header);
        QString packed = sourcePackedDimensionsText(*port.header);
        if (header.isEmpty()) {
            header = inheritedHeader;
            packed = inheritedPackedDimensions;
        } else {
            inheritedHeader = header;
            inheritedPackedDimensions = packed;
        }

        const QString name = QString::fromStdString(
            std::string(port.declarator->name.valueText()));
        const QString key = portSourcePresentationKey(
            sourceManager, port.declarator->name.location(), name);
        if (key.isEmpty())
            continue;

        SemanticSymbolPresentation presentation;
        presentation.declarationText = joinedParts(
            {attributesText(port.attributes),
             header,
             printedSyntax(*port.declarator)});
        presentation.packedDimensionsText = packed;
        presentation.unpackedDimensionsText =
            dimensionsText(port.declarator->dimensions);
        result->insert(key, std::move(presentation));
    }
}

void appendNonAnsiPortPresentations(
    const ModuleDeclarationSyntax& module,
    const slang::SourceManager* sourceManager,
    QHash<QString, SemanticSymbolPresentation>* result)
{
    if (!result)
        return;

    for (const MemberSyntax* member : module.members) {
        if (!member || member->kind != SyntaxKind::PortDeclaration)
            continue;

        const auto& declaration = member->as<PortDeclarationSyntax>();
        const QString header = printedSyntax(*declaration.header);
        const QString attributes = attributesText(declaration.attributes);
        const QString packed =
            sourcePackedDimensionsText(*declaration.header);
        for (const DeclaratorSyntax* declarator : declaration.declarators) {
            if (!declarator)
                continue;
            const QString name = QString::fromStdString(
                std::string(declarator->name.valueText()));
            const QString key = portSourcePresentationKey(
                sourceManager, declarator->name.location(), name);
            if (key.isEmpty())
                continue;

            SemanticSymbolPresentation presentation;
            presentation.declarationText = joinedParts(
                {attributes, header, printedSyntax(*declarator)});
            presentation.packedDimensionsText = packed;
            presentation.unpackedDimensionsText =
                dimensionsText(declarator->dimensions);
            result->insert(key, std::move(presentation));
        }
    }
}

QHash<QString, SemanticSymbolPresentation> collectPortSourcePresentations(
    Compilation& compilation,
    const slang::SourceManager* sourceManager,
    const std::function<bool()>& isCancelled)
{
    QHash<QString, SemanticSymbolPresentation> result;
    if (!sourceManager)
        return result;

    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    for (const std::shared_ptr<SyntaxTree>& tree :
         compilation.getSyntaxTrees()) {
        if (!tree || cancelled())
            continue;
        auto visitor = makeSyntaxVisitor(
            [&](auto& nestedVisitor,
                const ModuleDeclarationSyntax& module) {
                if (cancelled())
                    return;
                appendAnsiPortPresentations(module,
                                            sourceManager,
                                            &result);
                appendNonAnsiPortPresentations(module,
                                               sourceManager,
                                               &result);
                nestedVisitor.visitDefault(module);
            });
        tree->root().visit(visitor);
    }
    return result;
}

void mergePortSourcePresentation(
    const SemanticSymbolPresentation& source,
    SemanticSymbolPresentation* destination)
{
    if (!destination || source.declarationText.isEmpty())
        return;
    destination->declarationText = source.declarationText;
    destination->packedDimensionsText = source.packedDimensionsText;
    destination->unpackedDimensionsText = source.unpackedDimensionsText;
}

QChar identityKind(const SemanticSymbolRecord& record)
{
    using CollectorKind = SymbolTaxonomy::CollectorKind;
    switch (record.collectorKind) {
    case CollectorKind::Parameter:
    case CollectorKind::ModuleParameter:
    case CollectorKind::InterfaceParameter:
    case CollectorKind::DefParameter:
    case CollectorKind::Localparam:
        return QLatin1Char('p');
    case CollectorKind::EnumValue:
        return QLatin1Char('e');
    case CollectorKind::PortInput:
    case CollectorKind::PortOutput:
    case CollectorKind::PortInout:
    case CollectorKind::PortRef:
    case CollectorKind::PortInterface:
    case CollectorKind::PortInterfaceModport:
        return QLatin1Char('o');
    default:
        return record.declarationKind
                       == SymbolTaxonomy::DeclarationKind::Signal
                   ? QLatin1Char('v')
                   : QLatin1Char('?');
    }
}

QChar identityKind(const Symbol& symbol)
{
    switch (symbol.kind) {
    case SymbolKind::Parameter:
        return QLatin1Char('p');
    case SymbolKind::EnumValue:
        return QLatin1Char('e');
    case SymbolKind::Port:
    case SymbolKind::InterfacePort:
        return QLatin1Char('o');
    case SymbolKind::Variable:
    case SymbolKind::Net:
        return QLatin1Char('v');
    default:
        return QLatin1Char('?');
    }
}

QString identityKey(const slang::SourceManager* sourceManager,
                    const Symbol& symbol)
{
    if (!sourceManager || !symbol.location.valid())
        return QString();
    const slang_symbols::detail::QTextDocumentSourcePosition position =
        slang_symbols::detail::qTextDocumentSourcePosition(
            sourceManager, symbol.location);
    const QString fileName = normalizedFileName(position.fileName);
    if (fileName.isEmpty() || !position.isValid())
        return QString();
    return QStringLiteral("%1|%2|%3|%4")
        .arg(fileName)
        .arg(position.position)
        .arg(QString::fromStdString(std::string(symbol.name)))
        .arg(identityKind(symbol));
}

QString identityKey(const SemanticSymbolRecord& record)
{
    const QString fileName = normalizedFileName(record.location.fileName);
    if (fileName.isEmpty() || record.location.position < 0)
        return QString();
    return QStringLiteral("%1|%2|%3|%4")
        .arg(fileName)
        .arg(record.location.position)
        .arg(record.name)
        .arg(identityKind(record));
}

QString exactConstantText(const slang::ConstantValue& value)
{
    if (!value)
        return QString();
    return QString::fromStdString(value.toString(
        std::numeric_limits<slang::bitwidth_t>::max(),
        true));
}

QString semanticRangeText(const slang::ConstantRange& range)
{
    return QString::fromStdString(range.toString());
}

void fillSemanticDimensions(const Type& type,
                            SemanticElaboratedSymbolInfo* result)
{
    if (!result)
        return;

    QStringList unpacked;
    uint64_t unpackedElements = 1;
    bool hasUnpackedElements = false;
    const Type* current = &type.getCanonicalType();
    while (current->kind == SymbolKind::FixedSizeUnpackedArrayType) {
        const auto& array = current->as<FixedSizeUnpackedArrayType>();
        unpacked.append(semanticRangeText(array.range));
        hasUnpackedElements = true;
        const uint64_t extent = array.range.fullWidth();
        if (extent == 0
            || unpackedElements
                   > std::numeric_limits<uint64_t>::max() / extent) {
            unpackedElements = 0;
        } else if (unpackedElements > 0) {
            unpackedElements *= extent;
        }
        current = &array.elementType.getCanonicalType();
    }

    QStringList packed;
    while (current->kind == SymbolKind::PackedArrayType) {
        const auto& array = current->as<PackedArrayType>();
        packed.append(semanticRangeText(array.range));
        current = &array.elementType.getCanonicalType();
    }

    result->packedDimensionsText = packed.join(QLatin1Char(' '));
    result->unpackedDimensionsText = unpacked.join(QLatin1Char(' '));
    if (hasUnpackedElements && unpackedElements > 0) {
        result->unpackedElementCountText =
            QString::number(unpackedElements);
    }
}

SemanticElaboratedSymbolInfo typeInfo(const Type& type)
{
    SemanticElaboratedSymbolInfo result;
    result.available = !type.isError();
    if (type.isError()) {
        result.resolvedTypeText = QString::fromStdString(type.toString());
        result.failureReason = QStringLiteral(
            "slang resolved the symbol to an error type.");
        return result;
    }
    const Type& canonicalType = type.getCanonicalType();
    if (canonicalType.kind == SymbolKind::EnumType) {
        // Slang's full enum TypePrinter evaluates every member. Presentation
        // collection needs the resolved type, not a second enum evaluation
        // pass, so use the Slang-resolved base type directly.
        const auto& enumType = canonicalType.as<EnumType>();
        result.resolvedTypeText = QStringLiteral("enum %1").arg(
            QString::fromStdString(enumType.baseType.toString()));
    } else {
        result.resolvedTypeText = QString::fromStdString(type.toString());
    }
    fillSemanticDimensions(type, &result);
    const uint64_t bitWidth = type.isFixedSize()
        ? type.getBitstreamWidth()
        : uint64_t(type.getBitWidth());
    result.bitWidthText = bitWidth > 0
        ? QString::number(bitWidth)
        : QStringLiteral("not statically known");

    const Type* signedType = &type.getCanonicalType();
    while (signedType->kind == SymbolKind::FixedSizeUnpackedArrayType) {
        signedType = &signedType
                          ->as<FixedSizeUnpackedArrayType>()
                          .elementType
                          .getCanonicalType();
    }
    result.signednessText = signedType->isNumeric()
        ? (signedType->isSigned() ? QStringLiteral("signed")
                                  : QStringLiteral("unsigned"))
        : QStringLiteral("not applicable");
    return result;
}

void fillInterfaceDimensions(const InterfacePortSymbol& port,
                             SemanticElaboratedSymbolInfo* result)
{
    if (!result)
        return;
    const auto ranges = port.getDeclaredRange();
    if (!ranges)
        return;

    QStringList dimensions;
    uint64_t elementCount = 1;
    for (const slang::ConstantRange& range : *ranges) {
        dimensions.append(semanticRangeText(range));
        const uint64_t extent = range.fullWidth();
        if (extent == 0
            || elementCount
                   > std::numeric_limits<uint64_t>::max() / extent) {
            elementCount = 0;
        } else if (elementCount > 0) {
            elementCount *= extent;
        }
    }
    result->unpackedDimensionsText =
        dimensions.join(QLatin1Char(' '));
    if (!dimensions.isEmpty() && elementCount > 0) {
        result->unpackedElementCountText =
            QString::number(elementCount);
    }
}

SemanticElaboratedSymbolInfo constantInfo(
    const slang::ConstantValue& value,
    const Type& type)
{
    SemanticElaboratedSymbolInfo result = typeInfo(type);
    if (!value) {
        result.available = false;
        result.failureReason = QStringLiteral(
            "slang reported an invalid constant value.");
        return result;
    }
    result.available = true;
    result.valueText = exactConstantText(value);
    return result;
}

SemanticElaboratedSymbolInfo enumConstantInfo(
    const slang::ConstantValue& value,
    const Type& type)
{
    SemanticElaboratedSymbolInfo result = constantInfo(value, type);
    if (!result.available || !value.isInteger())
        return result;

    // Both renderings come from Slang's elaborated SVInt; neither interprets
    // the source expression. Keep the lossless binary value as the semantic
    // result, while offering plain decimal for enum UI when every bit is
    // known. Decimal cannot preserve partial X / Z positions, so unknown
    // values deliberately fall back to the exact binary representation.
    const slang::SVInt& integer = value.integer();
    result.valueText = QString::fromStdString(integer.toString(
        slang::LiteralBase::Binary,
        true,
        std::numeric_limits<slang::bitwidth_t>::max()));
    result.displayValueText = integer.hasUnknown()
        ? result.valueText
        : QString::fromStdString(integer.toString(
              slang::LiteralBase::Decimal,
              false,
              std::numeric_limits<slang::bitwidth_t>::max()));
    return result;
}

bool isDirectValueLiteralKind(ExpressionKind kind)
{
    switch (kind) {
    case ExpressionKind::IntegerLiteral:
    case ExpressionKind::RealLiteral:
    case ExpressionKind::TimeLiteral:
    case ExpressionKind::UnbasedUnsizedIntegerLiteral:
    case ExpressionKind::StringLiteral:
        return true;
    default:
        return false;
    }
}

const Expression* directSourceValueExpression(
    const Expression& initializer)
{
    const Expression& expression =
        initializer.unwrapImplicitConversions();
    if (expression.isParenthesized())
        return nullptr;
    if (isDirectValueLiteralKind(expression.kind))
        return &expression;
    if (expression.kind != ExpressionKind::UnaryOp)
        return nullptr;

    const auto& unary = expression.as<UnaryExpression>();
    if (unary.op != UnaryOperator::Plus
        && unary.op != UnaryOperator::Minus) {
        return nullptr;
    }
    const Expression& operand =
        unary.operand().unwrapImplicitConversions();
    if (operand.isParenthesized()
        || !isDirectValueLiteralKind(operand.kind)) {
        return nullptr;
    }
    // Use the unary expression's Slang value so the sign operation remains
    // part of the source value being compared.
    return &expression;
}

bool sameDisplayedConstantValue(const slang::ConstantValue& source,
                                const slang::ConstantValue& effective)
{
    if (!source || !effective)
        return false;
    if (!source.isInteger() || !effective.isInteger())
        return source == effective;

    const slang::SVInt& sourceInteger = source.integer();
    const slang::SVInt& effectiveInteger = effective.integer();
    if (sourceInteger.hasUnknown() || effectiveInteger.hasUnknown()) {
        // Decimal cannot represent individual X / Z positions. Be
        // conservative: suppress only if width, signedness, and all four-
        // state bits are identical according to Slang.
        return sourceInteger.getBitWidth() == effectiveInteger.getBitWidth()
            && sourceInteger.isSigned() == effectiveInteger.isSigned()
            && exactlyEqual(sourceInteger, effectiveInteger);
    }

    // Ask Slang for the mathematical decimal interpretation on both sides.
    // This detects truncation and signed coercion while allowing harmless
    // width extension of a value the source already displays.
    const auto decimalText = [](const slang::SVInt& integer) {
        return integer.toString(
            slang::LiteralBase::Decimal,
            false,
            std::numeric_limits<slang::bitwidth_t>::max());
    };
    return decimalText(sourceInteger) == decimalText(effectiveInteger);
}

slang::ConstantValue sourceDisplayedConstantValue(
    const Expression& expression)
{
    switch (expression.kind) {
    case ExpressionKind::IntegerLiteral:
        // getConstant() can reflect the assignment target's contextual
        // conversion. IntegerLiteral::getValue() is the value encoded by the
        // source token before truncation or signed coercion.
        return expression.as<IntegerLiteral>().getValue();
    case ExpressionKind::UnbasedUnsizedIntegerLiteral:
        // '0 / '1 / 'x / 'z are context-sized by definition; Slang's node
        // value is therefore the source-displayed value in that context.
        return expression.as<UnbasedUnsizedIntegerLiteral>().getValue();
    case ExpressionKind::RealLiteral:
        return slang::ConstantValue(
            slang::real_t(expression.as<RealLiteral>().getValue()));
    case ExpressionKind::StringLiteral:
        return slang::ConstantValue(std::string(
            expression.as<StringLiteral>().getValue()));
    case ExpressionKind::TimeLiteral:
        // Time scaling is itself Slang semantics. The unwrapped expression
        // was evaluated while ParameterSymbol produced its final value, so
        // its cached constant is the correct source-side result.
        return expression.getConstant()
            ? *expression.getConstant()
            : slang::ConstantValue();
    case ExpressionKind::UnaryOp: {
        const auto& unary = expression.as<UnaryExpression>();
        const slang::ConstantValue operand =
            sourceDisplayedConstantValue(
                unary.operand().unwrapImplicitConversions());
        if (!operand || unary.op == UnaryOperator::Plus)
            return operand;
        if (unary.op != UnaryOperator::Minus)
            return slang::ConstantValue();
        if (operand.isInteger())
            return slang::ConstantValue(-operand.integer());
        if (operand.isReal())
            return slang::ConstantValue(
                slang::real_t(-static_cast<double>(operand.real())));
        if (operand.isShortReal())
            return slang::ConstantValue(
                slang::shortreal_t(
                    -static_cast<float>(operand.shortReal())));
        return slang::ConstantValue();
    }
    default:
        return slang::ConstantValue();
    }
}

bool sourceValueDisplaysEffectiveParameterValue(
    const ParameterSymbol& parameter,
    const slang::ConstantValue& effectiveValue)
{
    const Expression* initializer = parameter.getInitializer();
    if (!initializer)
        return false;
    const Expression* source = directSourceValueExpression(*initializer);
    if (!source)
        return false;
    slang::ConstantValue sourceValue =
        sourceDisplayedConstantValue(*source);
    if (source->kind == ExpressionKind::StringLiteral
        && effectiveValue.isInteger()) {
        // Slang represents an implicitly typed string parameter as the
        // literal's packed integer value. Compare against that Slang-owned
        // representation so `parameter STR = "text"` is recognized as
        // already displaying its effective value.
        sourceValue = source->as<StringLiteral>().getIntValue();
    }
    return sameDisplayedConstantValue(sourceValue, effectiveValue);
}

QString parameterDefaultExpressionText(const ParameterSymbol& parameter)
{
    if (!parameter.defaultValSyntax)
        return QString();
    if (parameter.defaultValSyntax->kind == SyntaxKind::Declarator) {
        const auto& declarator =
            parameter.defaultValSyntax->as<DeclaratorSyntax>();
        if (declarator.initializer)
            return printedSyntax(*declarator.initializer->expr);
    }
    return printedSyntax(*parameter.defaultValSyntax);
}

QString parameterExpressionText(const ParameterSymbol& parameter)
{
    if (const Expression* initializer = parameter.getInitializer()) {
        if (initializer->syntax)
            return printedSyntax(*initializer->syntax);
    }
    return parameterDefaultExpressionText(parameter);
}

QString parameterValueSourceText(const ParameterSymbol& parameter)
{
    if (parameter.isLocalParam())
        return QStringLiteral("slang elaborated localparam constant");
    if (parameter.isFromConfig())
        return QStringLiteral("slang elaborated config parameter override");
    if (parameter.isOverridden())
        return QStringLiteral("slang elaborated parameter override");
    return QStringLiteral("slang elaborated parameter default");
}

QString parentInstancePath(const QString& instancePath)
{
    const int separator = instancePath.lastIndexOf(QLatin1Char('.'));
    return separator < 0 ? QString() : instancePath.left(separator);
}

void fillParameterSourcePresentation(
    const ParameterSymbol& parameter,
    SemanticSymbolPresentation* presentation)
{
    if (!presentation)
        return;
    if (presentation->expressionText.isEmpty())
        presentation->expressionText =
            parameterDefaultExpressionText(parameter);

    if (!presentation->declarationText.isEmpty())
        return;
    const SyntaxNode* syntax = parameter.getSyntax();
    if (!syntax)
        return;

    if (syntax->kind == SyntaxKind::Declarator
        && syntax->parent
        && syntax->parent->kind == SyntaxKind::ParameterDeclaration) {
        const auto& declaration =
            syntax->parent->as<ParameterDeclarationSyntax>();
        presentation->declarationText = joinedParts(
            {printedToken(declaration.keyword),
             printedSyntax(*declaration.type),
             printedSyntax(*syntax)});
        return;
    }
    presentation->declarationText = printedSyntax(*syntax);
}

void fillEnumSourcePresentation(
    const EnumValueSymbol& enumValue,
    SemanticSymbolPresentation* presentation)
{
    if (!presentation)
        return;
    if (const SyntaxNode* syntax = enumValue.getSyntax()) {
        if (presentation->declarationText.isEmpty())
            presentation->declarationText = printedSyntax(*syntax);
        if (presentation->expressionText.isEmpty()
            && syntax->kind == SyntaxKind::Declarator) {
            const auto& declarator = syntax->as<DeclaratorSyntax>();
            if (declarator.initializer)
                presentation->expressionText =
                    printedSyntax(*declarator.initializer->expr);
        }
    }

    const Type& canonical = enumValue.getType().getCanonicalType();
    if (canonical.kind == SymbolKind::EnumType) {
        const auto& enumType = canonical.as<EnumType>();
        presentation->enumUnderlyingBitWidthText =
            QString::number(enumType.baseType.getBitWidth());
    }
}

bool definitionHasDefaults(const DefinitionSymbol& definition)
{
    for (const DefinitionSymbol::ParameterDecl& parameter :
         definition.parameters) {
        if (!parameter.isLocalParam && !parameter.hasDefault())
            return false;
    }
    return true;
}

QString qualifiedLexicalScopePath(const Symbol& symbol,
                                  const Scope& collectionRoot,
                                  const QString& rootPath)
{
    QStringList nestedScopes;
    const Scope* scope = symbol.getParentScope();
    for (int depth = 0; scope && scope != &collectionRoot && depth < 128;
         ++depth) {
        const Symbol& scopeSymbol = scope->asSymbol();
        const QString name = QString::fromStdString(
            std::string(scopeSymbol.name));
        if (!name.isEmpty() && !name.startsWith(QLatin1Char('$')))
            nestedScopes.prepend(name);

        const Scope* parent = scopeSymbol.getParentScope();
        if (parent == scope)
            break;
        scope = parent;
    }

    QString result = rootPath;
    for (const QString& nested : std::as_const(nestedScopes)) {
        if (!result.isEmpty())
            result += QStringLiteral("::");
        result += nested;
    }
    return result;
}

struct EffectiveScopeDescriptor {
    SemanticEffectiveScopeKind kind =
        SemanticEffectiveScopeKind::Unknown;
    const Scope* root = nullptr;
    QString qualifiedPath;
};

EffectiveScopeDescriptor effectiveScopeForSymbol(
    const Symbol& symbol,
    const Scope& collectionRoot,
    SemanticEffectiveScopeKind collectionKind,
    const QString& collectionPath)
{
    EffectiveScopeDescriptor result{
        collectionKind, &collectionRoot, collectionPath};
    if (symbol.getDeclaringDefinition())
        return result;

    const Scope* scope = symbol.getParentScope();
    for (int depth = 0; scope && depth < 128; ++depth) {
        if (scope == &collectionRoot)
            return result;
        const Symbol& scopeSymbol = scope->asSymbol();
        if (const auto* package = scopeSymbol.as_if<PackageSymbol>()) {
            result.kind = SemanticEffectiveScopeKind::Package;
            result.root = package;
            result.qualifiedPath = QString::fromStdString(
                std::string(package->name));
            return result;
        }
        if (const auto* unit =
                scopeSymbol.as_if<CompilationUnitSymbol>()) {
            result.kind = SemanticEffectiveScopeKind::CompilationUnit;
            result.root = unit;
            result.qualifiedPath = QStringLiteral("$unit");
            return result;
        }
        const Scope* parent = scopeSymbol.getParentScope();
        if (parent == scope)
            break;
        scope = parent;
    }
    return result;
}

EffectiveValueFact factForRange(
    EffectiveValueFactKind kind,
    slang::SourceRange range,
    const slang::SourceManager* sourceManager,
    const QString& instancePath,
    bool defaultEvaluation,
    SemanticEffectiveScopeKind effectiveScopeKind,
    const QString& qualifiedScopePath,
    const QString& anchorInstancePath)
{
    EffectiveValueFact fact;
    fact.kind = kind;
    fact.instancePath = instancePath;
    fact.anchorInstancePath = anchorInstancePath;
    fact.defaultEvaluation = defaultEvaluation;
    fact.effectiveScopeKind = effectiveScopeKind;
    fact.qualifiedScopePath = qualifiedScopePath;
    fact.scopePath = qualifiedScopePath;
    fact.provenance = QStringLiteral("Slang AST/type elaboration");
    if (!sourceManager || !range.start().valid() || !range.end().valid())
        return fact;

    const slang_symbols::detail::QTextDocumentSourcePosition start =
        slang_symbols::detail::qTextDocumentSourcePosition(
            sourceManager, range.start());
    const slang_symbols::detail::QTextDocumentSourcePosition end =
        slang_symbols::detail::qTextDocumentSourcePosition(
            sourceManager, range.end());
    if (!start.isValid() || !end.isValid()
        || normalizedFileName(start.fileName)
               != normalizedFileName(end.fileName)) {
        return fact;
    }
    fact.fileName = normalizedFileName(start.fileName);
    fact.startPosition = start.position;
    fact.endPosition = end.position;
    fact.line = start.line;
    fact.status = EffectiveValueStatus::Current;
    fact.stableSourceIdentity = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(fact.fileName)
        .arg(fact.startPosition)
        .arg(fact.endPosition)
        .arg(static_cast<int>(kind))
        .arg(static_cast<int>(effectiveScopeKind))
        .arg(qualifiedScopePath)
        .arg(instancePath)
        .arg(anchorInstancePath);
    return fact;
}

void fillFactType(const Type& type, EffectiveValueFact* fact)
{
    if (!fact)
        return;
    const SemanticElaboratedSymbolInfo info = typeInfo(type);
    fact->resolvedTypeText = info.resolvedTypeText;
    fact->packedDimensionsText = info.packedDimensionsText;
    fact->unpackedDimensionsText = info.unpackedDimensionsText;
    fact->unpackedElementCountText = info.unpackedElementCountText;
    fact->bitWidthText = info.bitWidthText;
    fact->signednessText = info.signednessText;
    if (!info.available) {
        fact->status = EffectiveValueStatus::Error;
        fact->failureReason = info.failureReason;
    }
}

void appendExpressionFact(
    EffectiveValueFactKind kind,
    const Expression& expression,
    const QString& instancePath,
    bool defaultEvaluation,
    SemanticEffectiveScopeKind effectiveScopeKind,
    const QString& qualifiedScopePath,
    const slang::SourceManager* sourceManager,
    QList<EffectiveValueFact>* facts,
    const std::function<bool()>& isCancelled)
{
    if (!facts || !expression.syntax || (isCancelled && isCancelled()))
        return;
    EffectiveValueFact fact = factForRange(kind,
                                           expression.sourceRange,
                                           sourceManager,
                                           instancePath,
                                           defaultEvaluation,
                                           effectiveScopeKind,
                                           qualifiedScopePath,
                                           instancePath);
    if (!fact.isValid())
        return;
    fact.expressionText = printedSyntax(*expression.syntax);
    fillFactType(*expression.type, &fact);
    facts->append(std::move(fact));
}

void gatherScopePresentations(
    const Scope& scope,
    const QString& instancePath,
    bool storeAsDefault,
    SemanticEffectiveScopeKind effectiveScopeKind,
    const QString& qualifiedScopePath,
    const slang::SourceManager* sourceManager,
    QHash<QString, SemanticSymbolPresentation>* presentations,
    QList<EffectiveValueFact>* effectiveValueFacts,
    const std::function<bool()>& isCancelled)
{
    if (!sourceManager || !presentations
        || (!storeAsDefault && instancePath.isEmpty())
        || (isCancelled && isCancelled())) {
        return;
    }

    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };

    auto storeInfo = [&](const Symbol& symbol,
                         const SemanticElaboratedSymbolInfo& info,
                         auto&& fillSource) {
        if (cancelled())
            return;
        const QString key = identityKey(sourceManager, symbol);
        if (key.isEmpty())
            return;
        const EffectiveScopeDescriptor effectiveScope =
            effectiveScopeForSymbol(symbol,
                                    scope,
                                    effectiveScopeKind,
                                    qualifiedScopePath);
        SemanticSymbolPresentation& presentation = (*presentations)[key];
        const auto isStaticScope = [](SemanticEffectiveScopeKind kind) {
            return kind == SemanticEffectiveScopeKind::Package
                || kind == SemanticEffectiveScopeKind::CompilationUnit;
        };
        // A package / compilation-unit declaration can be encountered again
        // through an instance-local type reference. Its declaration identity
        // and value remain static; an instance traversal must never turn that
        // record into an instance-bound value.
        if (isStaticScope(presentation.effectiveScopeKind)
            && !isStaticScope(effectiveScope.kind)) {
            fillSource(&presentation);
            return;
        }
        presentation.effectiveScopeKind = effectiveScope.kind;
        presentation.qualifiedScopePath = qualifiedLexicalScopePath(
            symbol,
            effectiveScope.root ? *effectiveScope.root : scope,
            effectiveScope.qualifiedPath);
        fillSource(&presentation);
        const bool staticValue = effectiveScope.kind
                == SemanticEffectiveScopeKind::Package
            || effectiveScope.kind
                == SemanticEffectiveScopeKind::CompilationUnit;
        if (staticValue)
            presentation.instanceInfoByPath.clear();
        // Detached definition elaboration supplies declaration defaults only.
        // It must never manufacture an instance-map entry such as "child" or
        // overwrite an actual top.u* entry produced by the real hierarchy.
        if (!staticValue && !storeAsDefault && !instancePath.isEmpty())
            presentation.instanceInfoByPath.insert(instancePath, info);
        if (staticValue || storeAsDefault)
            presentation.defaultInfo = info;
    };

    auto storeEnumType = [&](const EnumType& enumType) {
        for (const EnumValueSymbol& enumValue : enumType.values()) {
            if (cancelled())
                return;
            SemanticElaboratedSymbolInfo info = enumConstantInfo(
                enumValue.getValue(), enumValue.getType());
            info.valueSourceText = QStringLiteral(
                "slang elaborated enum constant");
            storeInfo(enumValue,
                      info,
                      [&](SemanticSymbolPresentation* presentation) {
                          fillEnumSourcePresentation(enumValue,
                                                     presentation);
                      });
        }
    };

    auto visitor = makeVisitor(
        [&](auto&, const InstanceSymbol&) {
            // Nested instances are gathered separately with their own path.
        },
        [&](auto&, const PackageSymbol&) {
            // Packages are gathered independently so their values never
            // inherit an enclosing compilation-unit or instance context.
        },
        [&](auto&, const DefinitionSymbol&) {
            // Definitions nested in a compilation unit are elaborated via
            // their instance trees. Static-scope collection must not publish
            // their members as compilation-unit defaults.
        },
        [&](auto& nestedVisitor, const ParameterSymbol& parameter) {
            if (cancelled())
                return;
            if (parameter.isFromGenvar()) {
                nestedVisitor.visitDefault(parameter);
                return;
            }
            const slang::ConstantValue& parameterValue =
                parameter.getValue();
            const bool sourceDisplaysValue =
                sourceValueDisplaysEffectiveParameterValue(
                    parameter, parameterValue);
            SemanticElaboratedSymbolInfo info = constantInfo(
                parameterValue, parameter.getType());
            // Symbol value Ghosts are anchored at the declaration. An
            // instance override is written elsewhere and therefore cannot
            // make the declaration text redundant.
            info.sourceTextDisplaysEffectiveValue =
                !parameter.isOverridden() && sourceDisplaysValue;
            info.expressionText = parameterExpressionText(parameter);
            info.valueSourceText = parameterValueSourceText(parameter);
            storeInfo(parameter, info,
                      [&](SemanticSymbolPresentation* presentation) {
                          fillParameterSourcePresentation(parameter,
                                                          presentation);
                      });
            if (effectiveValueFacts && parameter.isOverridden()) {
                const Expression* initializer = parameter.getInitializer();
                if (initializer && initializer->syntax) {
                    EffectiveValueFact fact = factForRange(
                        EffectiveValueFactKind::ParameterOverride,
                        initializer->sourceRange,
                        sourceManager,
                        instancePath,
                        storeAsDefault,
                        effectiveScopeKind,
                        qualifiedLexicalScopePath(
                            parameter, scope, qualifiedScopePath),
                        parentInstancePath(instancePath));
                    if (fact.isValid()) {
                        fact.expressionText = parameterExpressionText(parameter);
                        fact.valueText = info.valueText;
                        fact.scopePath = fact.qualifiedScopePath;
                        fact.provenance = info.valueSourceText;
                        fact.resolvedTypeText = info.resolvedTypeText;
                        fact.packedDimensionsText = info.packedDimensionsText;
                        fact.unpackedDimensionsText = info.unpackedDimensionsText;
                        fact.unpackedElementCountText =
                            info.unpackedElementCountText;
                        fact.bitWidthText = info.bitWidthText;
                        fact.signednessText = info.signednessText;
                        fact.status = info.available
                            ? EffectiveValueStatus::Current
                            : EffectiveValueStatus::Error;
                        fact.failureReason = info.failureReason;
                        fact.sourceTextDisplaysEffectiveValue =
                            sourceDisplaysValue;
                        effectiveValueFacts->append(std::move(fact));
                    }
                }
            }
            nestedVisitor.visitDefault(parameter);
        },
        [&](auto& nestedVisitor, const EnumValueSymbol& enumValue) {
            if (cancelled())
                return;
            SemanticElaboratedSymbolInfo info = enumConstantInfo(
                enumValue.getValue(), enumValue.getType());
            info.valueSourceText = QStringLiteral(
                "slang elaborated enum constant");
            storeInfo(enumValue, info,
                      [&](SemanticSymbolPresentation* presentation) {
                          fillEnumSourcePresentation(enumValue,
                                                     presentation);
                      });
            nestedVisitor.visitDefault(enumValue);
        },
        [&](auto& nestedVisitor, const TypeAliasType& typeAlias) {
            if (cancelled())
                return;
            const Type& canonical = typeAlias.getCanonicalType();
            if (canonical.kind == SymbolKind::EnumType)
                storeEnumType(canonical.as<EnumType>());
            nestedVisitor.visitDefault(typeAlias);
        },
        [&](auto& nestedVisitor, const VariableSymbol& variable) {
            if (cancelled())
                return;
            const Type& canonical = variable.getType().getCanonicalType();
            if (canonical.kind == SymbolKind::EnumType)
                storeEnumType(canonical.as<EnumType>());
            storeInfo(variable,
                      typeInfo(variable.getType()),
                      [](SemanticSymbolPresentation*) {});
            nestedVisitor.visitDefault(variable);
        },
        [&](auto& nestedVisitor, const NetSymbol& net) {
            if (cancelled())
                return;
            storeInfo(net,
                      typeInfo(net.getType()),
                      [](SemanticSymbolPresentation*) {});
            nestedVisitor.visitDefault(net);
        },
        [&](auto& nestedVisitor, const PortSymbol& port) {
            if (cancelled())
                return;
            const SemanticElaboratedSymbolInfo info = typeInfo(port.getType());
            storeInfo(port, info, [](SemanticSymbolPresentation*) {});
            nestedVisitor.visitDefault(port);
        },
        [&](auto& nestedVisitor, const InterfacePortSymbol& port) {
            if (cancelled())
                return;
            SemanticElaboratedSymbolInfo info;
            info.available = !port.isInvalid();
            info.interfaceName = port.interfaceDef
                ? QString::fromStdString(
                      std::string(port.interfaceDef->name))
                : QStringLiteral("interface");
            info.modportName = QString::fromStdString(
                std::string(port.modport));
            info.resolvedTypeText = info.interfaceName;
            if (!info.modportName.isEmpty()) {
                info.resolvedTypeText += QLatin1Char('.');
                info.resolvedTypeText += info.modportName;
            }
            info.signednessText = QStringLiteral("not applicable");
            info.bitWidthText = QStringLiteral("not applicable");
            fillInterfaceDimensions(port, &info);
            if (!info.available) {
                info.failureReason = QStringLiteral(
                    "slang could not resolve the interface port type.");
            }
            storeInfo(port, info, [](SemanticSymbolPresentation*) {});
            nestedVisitor.visitDefault(port);
        },
        [&](auto& nestedVisitor, const RangeSelectExpression& expression) {
            if (cancelled())
                return;
            appendExpressionFact(
                EffectiveValueFactKind::PartSelectWidth,
                expression,
                instancePath,
                storeAsDefault,
                effectiveScopeKind,
                qualifiedScopePath,
                sourceManager,
                effectiveValueFacts,
                isCancelled);
            nestedVisitor.visitDefault(expression);
        },
        [&](auto& nestedVisitor,
            const ConcatenationExpression& expression) {
            if (cancelled())
                return;
            appendExpressionFact(
                EffectiveValueFactKind::ConcatenationWidth,
                expression,
                instancePath,
                storeAsDefault,
                effectiveScopeKind,
                qualifiedScopePath,
                sourceManager,
                effectiveValueFacts,
                isCancelled);
            nestedVisitor.visitDefault(expression);
        },
        [&](auto&, const ReplicationExpression& expression) {
            if (cancelled())
                return;
            appendExpressionFact(
                EffectiveValueFactKind::ConcatenationWidth,
                expression,
                instancePath,
                storeAsDefault,
                effectiveScopeKind,
                qualifiedScopePath,
                sourceManager,
                effectiveValueFacts,
                isCancelled);
        },
        [&](auto& nestedVisitor,
            const GenerateBlockArraySymbol& generateArray) {
            if (cancelled())
                return;
            if (effectiveValueFacts) {
                const SyntaxNode* syntax = generateArray.getSyntax();
                while (syntax && syntax->kind != SyntaxKind::LoopGenerate)
                    syntax = syntax->parent;
                if (syntax) {
                    EffectiveValueFact fact = factForRange(
                        EffectiveValueFactKind::GenerateCount,
                        syntax->sourceRange(),
                        sourceManager,
                        instancePath,
                        storeAsDefault,
                        effectiveScopeKind,
                        qualifiedScopePath,
                        instancePath);
                    if (fact.isValid()) {
                        fact.expressionText = printedSyntax(*syntax);
                        fact.valueText = QString::number(
                            generateArray.entries.size());
                        fact.provenance = QStringLiteral(
                            "Slang elaborated generate block array");
                        effectiveValueFacts->append(std::move(fact));
                    }
                }
            }
            nestedVisitor.visitDefault(generateArray);
        });
    for (const Symbol& member : scope.members()) {
        if (cancelled())
            return;
        member.visit(visitor);
    }
}

void gatherInstanceTree(
    const InstanceSymbol& root,
    const slang::SourceManager* sourceManager,
    QHash<QString, SemanticSymbolPresentation>* presentations,
    QList<EffectiveValueFact>* effectiveValueFacts,
    const std::function<bool()>& isCancelled)
{
    auto visitor = makeVisitor(
        [&](auto& nestedVisitor, const InstanceSymbol& instance) {
            if (isCancelled && isCancelled())
                return;
            const QString path = QString::fromStdString(
                instance.getHierarchicalPath());
            const QString lexicalPath = QString::fromStdString(
                std::string(instance.getDefinition().name));
            gatherScopePresentations(instance.body,
                                     path,
                                     false,
                                     SemanticEffectiveScopeKind::Instance,
                                     lexicalPath,
                                     sourceManager,
                                     presentations,
                                     effectiveValueFacts,
                                     isCancelled);
            if (isCancelled && isCancelled())
                return;
            nestedVisitor.visitDefault(instance);
        });
    root.visit(visitor);
}

void gatherStaticScope(
    const Scope& scope,
    SemanticEffectiveScopeKind effectiveScopeKind,
    const QString& qualifiedScopePath,
    const slang::SourceManager* sourceManager,
    QHash<QString, SemanticSymbolPresentation>* presentations,
    QList<EffectiveValueFact>* effectiveValueFacts,
    const std::function<bool()>& isCancelled)
{
    gatherScopePresentations(scope,
                             QString(),
                             true,
                             effectiveScopeKind,
                             qualifiedScopePath,
                             sourceManager,
                             presentations,
                             effectiveValueFacts,
                             isCancelled);
}
}

void slang_symbols::populateSymbolPresentations(
    Compilation& compilation,
    QList<SemanticSymbolRecord>& records,
    QList<EffectiveValueFact>* effectiveValueFacts,
    const std::function<bool()>& isCancelled)
{
    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };
    const slang::SourceManager* sourceManager = compilation.getSourceManager();
    if (!sourceManager || records.isEmpty() || cancelled())
        return;
    slang_symbols::detail::resetQTextDocumentSourcePositionCache(
        sourceManager);
    struct SourcePositionCacheReset {
        ~SourcePositionCacheReset()
        {
            slang_symbols::detail::resetQTextDocumentSourcePositionCache(
                nullptr);
        }
    } sourcePositionCacheReset;

    // Build the entire publication off to the side. A cancelled or throwing
    // Slang traversal must not expose a mixture of old and partial values.
    QHash<QString, SemanticSymbolPresentation> presentations;
    const QHash<QString, SemanticSymbolPresentation>
        portSourcePresentations = collectPortSourcePresentations(
            compilation, sourceManager, isCancelled);
    if (cancelled())
        return;
    QList<EffectiveValueFact> collectedFacts;
    QList<EffectiveValueFact>* facts = effectiveValueFacts
        ? &collectedFacts : nullptr;
    const RootSymbol& root = compilation.getRoot();
    for (const InstanceSymbol* top : root.topInstances) {
        if (cancelled())
            return;
        if (!top)
            continue;
        gatherInstanceTree(*top,
                           sourceManager,
                           &presentations,
                           facts,
                           isCancelled);
    }

    // Package and compilation-unit constants live outside the elaborated
    // instance tree. Their values are global to the compilation, so publish
    // them as defaults only and never attach an instance path.
    for (const PackageSymbol* package : compilation.getPackages()) {
        if (cancelled())
            return;
        if (!package || !package->location.valid())
            continue;
        gatherStaticScope(*package,
                          SemanticEffectiveScopeKind::Package,
                          QString::fromStdString(
                              std::string(package->name)),
                          sourceManager,
                          &presentations,
                          facts,
                          isCancelled);
    }
    for (const CompilationUnitSymbol* unit : root.compilationUnits) {
        if (cancelled())
            return;
        if (!unit)
            continue;
        gatherStaticScope(*unit,
                          SemanticEffectiveScopeKind::CompilationUnit,
                          QStringLiteral("$unit"),
                          sourceManager,
                          &presentations,
                          facts,
                          isCancelled);
    }

    QList<const DefinitionSymbol*> definitions;
    for (const Symbol* symbol : compilation.getDefinitions()) {
        if (cancelled())
            return;
        const auto* definition = symbol
            ? symbol->as_if<DefinitionSymbol>()
            : nullptr;
        if (definition)
            definitions.append(definition);
    }

    for (const DefinitionSymbol* definition : std::as_const(definitions)) {
        if (cancelled())
            return;
        if (!definition || !definitionHasDefaults(*definition))
            continue;
        InstanceSymbol& defaultInstance =
            InstanceSymbol::createDefault(compilation, *definition);
        // createDefault() normally becomes usable only after Compilation adds
        // it to the root scope. This collector owns a detached default used
        // solely for presentation, so provide the same parent compilation
        // context before ports or interface-dependent constants elaborate.
        defaultInstance.setParent(root);
        // Every definition is elaborated independently for declaration
        // defaults. Do not traverse nested instances here: those paths are
        // detached pseudo-hierarchy and each nested definition receives its
        // own default elaboration in this loop.
        gatherScopePresentations(defaultInstance.body,
                                 QString(),
                                 true,
                                 SemanticEffectiveScopeKind::Instance,
                                 QString::fromStdString(
                                     std::string(definition->name)),
                                 sourceManager,
                                 &presentations,
                                 facts,
                                 isCancelled);
    }

    QList<SemanticSymbolRecord> updatedRecords = records;
    for (SemanticSymbolRecord& record : updatedRecords) {
        if (cancelled())
            return;
        const QChar kind = identityKind(record);
        if (kind == QLatin1Char('?'))
            continue;
        const QString key = identityKey(record);
        const auto it = presentations.constFind(key);
        if (it != presentations.constEnd())
            record.presentation = it.value();

        if (kind == QLatin1Char('o')) {
            const auto sourcePort = portSourcePresentations.constFind(
                identityKey(record));
            if (sourcePort != portSourcePresentations.constEnd()) {
                mergePortSourcePresentation(sourcePort.value(),
                                            &record.presentation);
            }
        }

        if (record.collectorKind
            == SymbolTaxonomy::CollectorKind::EnumValue) {
            record.presentation.enumTypeName = record.owner.name;
        }
        if (!record.presentation.defaultInfo.available
            && record.presentation.defaultInfo.failureReason.isEmpty()) {
            record.presentation.defaultInfo.failureReason = QStringLiteral(
                "No valid slang default elaboration is available for this declaration.");
        }
    }

    if (cancelled())
        return;
    records = std::move(updatedRecords);
    if (effectiveValueFacts)
        *effectiveValueFacts = std::move(collectedFacts);
}
