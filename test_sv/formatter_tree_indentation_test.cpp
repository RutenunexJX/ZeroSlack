#include "formatterservice.h"
#include "structuredwhitespaceformatter.h"

#include <QCoreApplication>
#include <QStringList>

#include <iostream>

namespace {

int failures = 0;

void expect(const char* name, bool condition)
{
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return;
    }
    std::cerr << "[FAIL] " << name << '\n';
    ++failures;
}

bool hasLine(const QString& text, const QString& expected)
{
    return text.split(QLatin1Char('\n')).contains(expected);
}

QString lineContaining(const QString& text, const QString& token)
{
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (line.contains(token))
            return line;
    }
    return {};
}

bool usesOnlyCrlfLineEndings(const QString& text)
{
    bool sawNewline = false;
    for (int position = 0; position < text.size(); ++position) {
        const QChar ch = text.at(position);
        if (ch == QLatin1Char('\r')) {
            if (position + 1 >= text.size()
                || text.at(position + 1) != QLatin1Char('\n')) {
                return false;
            }
            continue;
        }
        if (ch != QLatin1Char('\n'))
            continue;
        if (position == 0
            || text.at(position - 1) != QLatin1Char('\r')) {
            return false;
        }
        sawNewline = true;
    }
    return sawNewline;
}

FormatterOptions structuralIndentationOptions()
{
    FormatterOptions options;
    options.alignDeclarationBlocks = false;
    options.alignPortLists = false;
    options.alignInstanceMaps = false;
    options.alignCaseItems = false;
    options.alignEnumItems = false;
    options.alignAssignments = false;
    options.alignContinuationOperators = false;
    options.alignCallArgumentContinuations = false;
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    Q_UNUSED(application);

    const QString source =
        QStringLiteral(
            "module tree_demo;\n"
            "function automatic logic pick(input logic a);\n"
            "if (a)\n"
            "pick = 1'b1;\n"
            "else\n"
            "pick = 1'b0;\n"
            "endfunction\n"
            "task automatic pulse;\n"
            "repeat (2)\n"
            "begin\n"
            "tick = ~tick;\n"
            "end\n"
            "endtask\n"
            "generate\n"
            "if (ENABLE) begin : g_enabled\n"
            "always_comb begin\n"
            "case (state)\n"
            "2'b00:\n"
            "if (ready)\n"
            "next = A;\n"
            "else\n"
            "next = B;\n"
            "default: next = IDLE;\n"
            "endcase\n"
            "end\n"
            "end\n"
            "endgenerate\n"
            "initial begin\n"
            "fork\n"
            "a = 1'b1;\n"
            "b = 1'b0;\n"
            "join_any\n"
            "wait fork;\n"
            "end\n"
            "endmodule\n");

    const FormatterReport report =
        FormatterService::getInstance()->formatDocument(
            source, structuralIndentationOptions());
    expect("tree formatter changes unindented structure",
           report.changed);
    expect("function body uses syntax ownership",
           hasLine(report.formattedText,
                   QStringLiteral("    if (a)"))
               && hasLine(report.formattedText,
                          QStringLiteral("        pick = 1'b1;"))
               && hasLine(report.formattedText,
                          QStringLiteral("    else")));
    expect("loop block and body use syntax ownership",
           hasLine(report.formattedText,
                   QStringLiteral("    repeat (2)"))
               && hasLine(report.formattedText,
                          QStringLiteral("        begin"))
               && hasLine(report.formattedText,
                          QStringLiteral("            tick = ~tick;")));
    expect("generate ownership composes with procedural blocks",
           hasLine(report.formattedText,
                   QStringLiteral(
                       "    if (ENABLE) begin : g_enabled"))
               && hasLine(report.formattedText,
                          QStringLiteral("        always_comb begin"))
               && hasLine(report.formattedText,
                          QStringLiteral("            case (state)")));
    expect("case item and nested dangling else are structural",
           hasLine(report.formattedText,
                   QStringLiteral("                2'b00:"))
               && hasLine(report.formattedText,
                          QStringLiteral("                    if (ready)"))
               && hasLine(report.formattedText,
                          QStringLiteral("                    else"))
               && hasLine(report.formattedText,
                          QStringLiteral("            endcase")));
    expect("parallel block owns statements but not wait fork",
           hasLine(report.formattedText,
                   QStringLiteral("    fork"))
               && hasLine(report.formattedText,
                          QStringLiteral("        a = 1'b1;"))
               && hasLine(report.formattedText,
                          QStringLiteral("    join_any"))
               && hasLine(report.formattedText,
                          QStringLiteral("    wait fork;")));
    expect("tree formatter preserves immutable token stream",
           StructuredWhitespaceFormatter::
               hasIdenticalNonWhitespaceStream(
                   source, report.formattedText));

    const FormatterReport stable =
        FormatterService::getInstance()->formatDocument(
            report.formattedText, structuralIndentationOptions());
    expect("tree formatter is idempotent",
           !stable.changed
               && stable.formattedText == report.formattedText);

    const QString caseSource =
        QStringLiteral(
            "module case_demo;\n"
            "always_comb begin\n"
            "case (state)\n"
            "2'b0: value = A; // first\n"
            "default: value = B; // fallback\n"
            "endcase\n"
            "end\n"
            "endmodule\n");
    const FormatterReport alignedCase =
        FormatterService::getInstance()->formatDocument(caseSource);
    expect("case colon and first statement align structurally",
           hasLine(alignedCase.formattedText,
                   QStringLiteral(
                       "        2'b0   : value = A;  // first"))
               && hasLine(alignedCase.formattedText,
                          QStringLiteral(
                              "        default: value = B;  // fallback")));
    expect("case alignment is whitespace-only",
           StructuredWhitespaceFormatter::
               hasIdenticalNonWhitespaceStream(
                   caseSource, alignedCase.formattedText));

    const QString pwmCodecHeader =
        QStringLiteral(
            "module pwm_codec_top #(\n"
            "    parameter P_MODE = \"CODER\",// \"CODER\", \"DECODER\", \"CODEC\"\n"
            "    parameter P_LITE_BASE_ADDR = 32'h0000_0000\n"
            ")(\n"
            "  input logic clk,\n"
            "  input logic rst,\n"
            "  input    logic [31:0]  lite_awaddr  ,\n"
            "  input    logic [2:0]         lite_awprot  ,\n"
            "  input    logic               lite_awvalid ,\n"
            "  output logic pwm_out,\n"
            "  input logic pwm_in\n"
            ");\n"
            "endmodule\n");
    const FormatterReport pwmCodecHeaderReport =
        FormatterService::getInstance()->formatDocument(
            pwmCodecHeader);
    const bool pwmCodecHeaderAligned =
           hasLine(pwmCodecHeaderReport.formattedText,
                   QStringLiteral(
                       "    parameter P_MODE           = \"CODER\"       , // \"CODER\", \"DECODER\", \"CODEC\""))
               && hasLine(pwmCodecHeaderReport.formattedText,
                          QStringLiteral(
                              "    input  logic [31:0] lite_awaddr  ,"))
               && hasLine(pwmCodecHeaderReport.formattedText,
                          QStringLiteral(
                              "    input  logic        lite_awvalid ,"))
               && hasLine(pwmCodecHeaderReport.formattedText,
                          QStringLiteral(
                              "    output logic        pwm_out      ,"))
               && hasLine(pwmCodecHeaderReport.formattedText,
                          QStringLiteral(
                              "    input  logic        pwm_in"));
    expect("pwm codec parameter and ANSI port header is structurally aligned",
           pwmCodecHeaderAligned);
    expect("pwm codec header formatting remains whitespace-only",
           StructuredWhitespaceFormatter::
               hasIdenticalNonWhitespaceStream(
                   pwmCodecHeader,
                   pwmCodecHeaderReport.formattedText));
    const FormatterReport pwmCodecSelectionReport =
        FormatterService::getInstance()->formatSnippet(
            pwmCodecHeader);
    expect("pwm codec header selection uses the same structural alignment",
           pwmCodecSelectionReport.formattedText
               == pwmCodecHeaderReport.formattedText);

    const QString declarationWidths =
        QStringLiteral(
            "module width_alignment;\n"
            "logic [31:0] encoded_period;\n"
            "logic [7:0][3:0] encoded_id;\n"
            "wire ready;\n"
            "endmodule\n");
    const FormatterReport declarationWidthReport =
        FormatterService::getInstance()->formatDocument(
            declarationWidths);
    const QString encodedPeriodLine = lineContaining(
        declarationWidthReport.formattedText,
        QStringLiteral("encoded_period"));
    const QString encodedIdLine = lineContaining(
        declarationWidthReport.formattedText,
        QStringLiteral("encoded_id"));
    const QString readyLine = lineContaining(
        declarationWidthReport.formattedText,
        QStringLiteral("ready"));
    expect("internal declaration type width and name columns align generically",
           !encodedPeriodLine.isEmpty()
               && !encodedIdLine.isEmpty()
               && !readyLine.isEmpty()
               && encodedPeriodLine.indexOf(QStringLiteral("[31:0]"))
                      == encodedIdLine.indexOf(QStringLiteral("[7:0][3:0]"))
               && encodedPeriodLine.indexOf(QStringLiteral("encoded_period"))
                      == encodedIdLine.indexOf(QStringLiteral("encoded_id"))
               && encodedPeriodLine.indexOf(QStringLiteral("encoded_period"))
                      == readyLine.indexOf(QStringLiteral("ready")));
    expect("internal declaration alignment changes whitespace only",
           StructuredWhitespaceFormatter::hasIdenticalNonWhitespaceStream(
               declarationWidths,
               declarationWidthReport.formattedText));
    const FormatterReport declarationWidthStable =
        FormatterService::getInstance()->formatDocument(
            declarationWidthReport.formattedText);
    expect("internal declaration alignment is idempotent",
           !declarationWidthStable.changed
               && declarationWidthStable.formattedText
                      == declarationWidthReport.formattedText);

    const QString danglingElseSource =
        QStringLiteral(
            "module nested_case_if;\n"
            "always_comb begin\n"
            "case (state)\n"
            "IDLE:\n"
            "if (outer)\n"
            "if (inner)\n"
            "next = INNER;\n"
            "else\n"
            "next = OUTER;\n"
            "else if (fallback) begin\n"
            "if (ready)\n"
            "next = READY;\n"
            "else\n"
            "next = WAIT;\n"
            "end\n"
            "else\n"
            "next = IDLE;\n"
            "default: begin\n"
            "next = DEFAULT_VALUE;\n"
            "end\n"
            "endcase\n"
            "end\n"
            "endmodule\n");
    const QString danglingElseExpected =
        QStringLiteral(
            "module nested_case_if;\n"
            "always_comb begin\n"
            "    case (state)\n"
            "        IDLE:\n"
            "            if (outer)\n"
            "                if (inner)\n"
            "                    next = INNER;\n"
            "                else\n"
            "                    next = OUTER;\n"
            "            else if (fallback) begin\n"
            "                if (ready)\n"
            "                    next = READY;\n"
            "                else\n"
            "                    next = WAIT;\n"
            "            end\n"
            "            else\n"
            "                next = IDLE;\n"
            "        default: begin\n"
            "            next = DEFAULT_VALUE;\n"
            "        end\n"
            "    endcase\n"
            "end\n"
            "endmodule\n");
    const FormatterReport danglingElseReport =
        FormatterService::getInstance()->formatDocument(
            danglingElseSource, structuralIndentationOptions());
    expect("nested case and dangling else use parsed ownership",
           danglingElseReport.formattedText == danglingElseExpected);
    expect("nested case and dangling else preserve tokens",
           StructuredWhitespaceFormatter::
               hasIdenticalNonWhitespaceStream(
                   danglingElseSource,
                   danglingElseReport.formattedText));
    const FormatterReport danglingElseStable =
        FormatterService::getInstance()->formatDocument(
            danglingElseReport.formattedText,
            structuralIndentationOptions());
    expect("nested case and dangling else are idempotent",
           !danglingElseStable.changed
               && danglingElseStable.formattedText
                      == danglingElseReport.formattedText);

    const QString triviaSource =
        QStringLiteral(
            "module trivia_demo;\n"
            "string marker = \"case: else\t// unchanged\";\n"
            "always_comb begin\n"
            "`ifdef FEATURE\n"
            "// keep\tcomment : else\n"
            "if (enable)\n"
            "value = marker == \"if : else\";\n"
            "`else\n"
            "/* keep  block:\t */ value = \"fallback :\";\n"
            "`endif\n"
            "end\n"
            "endmodule\n");
    const FormatterReport triviaReport =
        FormatterService::getInstance()->formatDocument(
            triviaSource, structuralIndentationOptions());
    expect("comments strings and preprocessor text remain exact",
           triviaReport.formattedText.contains(
               QStringLiteral(
                   "\"case: else\t// unchanged\""))
               && triviaReport.formattedText.contains(
                   QStringLiteral("// keep\tcomment : else"))
               && triviaReport.formattedText.contains(
                   QStringLiteral("/* keep  block:\t */"))
               && triviaReport.formattedText.contains(
                   QStringLiteral("`ifdef FEATURE"))
               && triviaReport.formattedText.contains(
                   QStringLiteral("`else"))
               && triviaReport.formattedText.contains(
                   QStringLiteral("`endif")));
    expect("comments preprocessor strings preserve token stream",
           StructuredWhitespaceFormatter::
               hasIdenticalNonWhitespaceStream(
                   triviaSource, triviaReport.formattedText));
    const FormatterReport triviaStable =
        FormatterService::getInstance()->formatDocument(
            triviaReport.formattedText,
            structuralIndentationOptions());
    expect("comments preprocessor strings remain idempotent",
           !triviaStable.changed
               && triviaStable.formattedText
                      == triviaReport.formattedText);

    const QString crlfSource =
        QStringLiteral(
            "module crlf_demo;\r\n"
            "always_comb begin\r\n"
            "if (ready)\r\n"
            "value = \"x : else\"; // keep  spaces\r\n"
            "else\r\n"
            "value = '0;\r\n"
            "end\r\n"
            "endmodule\r\n");
    const QString crlfExpected =
        QStringLiteral(
            "module crlf_demo;\r\n"
            "always_comb begin\r\n"
            "    if (ready)\r\n"
            "        value = \"x : else\"; // keep  spaces\r\n"
            "    else\r\n"
            "        value = '0;\r\n"
            "end\r\n"
            "endmodule\r\n");
    const FormatterReport crlfReport =
        FormatterService::getInstance()->formatDocument(
            crlfSource, structuralIndentationOptions());
    expect("CRLF structure formats without line-ending conversion",
           crlfReport.formattedText == crlfExpected
               && usesOnlyCrlfLineEndings(
                   crlfReport.formattedText));
    expect("CRLF formatting preserves immutable tokens",
           StructuredWhitespaceFormatter::
               hasIdenticalNonWhitespaceStream(
                   crlfSource, crlfReport.formattedText));
    const FormatterReport crlfStable =
        FormatterService::getInstance()->formatDocument(
            crlfReport.formattedText,
            structuralIndentationOptions());
    expect("CRLF formatting is idempotent",
           !crlfStable.changed
               && crlfStable.formattedText
                      == crlfReport.formattedText);

    const QString malformed =
        QStringLiteral(
            "module broken;\n"
            "always_comb begin\n"
            "  if (\n"
            "    broken = ;\n"
            "end\n"
            "endmodule\n"
            "module sound;\n"
            "always_comb begin\n"
            "ok = 1'b1;\n"
            "end\n"
            "endmodule\n");
    const FormatterReport malformedReport =
        FormatterService::getInstance()->formatDocument(
            malformed, structuralIndentationOptions());
    expect("parse-error lines remain conservative",
           malformedReport.formattedText.contains(
               QStringLiteral("  if (\n    broken = ;\n")));
    expect("valid structure after an error still formats",
           hasLine(malformedReport.formattedText,
                   QStringLiteral("    ok = 1'b1;")));
    expect("error recovery remains whitespace-only",
           StructuredWhitespaceFormatter::
               hasIdenticalNonWhitespaceStream(
                   malformed, malformedReport.formattedText));

    return failures == 0 ? 0 : 1;
}
