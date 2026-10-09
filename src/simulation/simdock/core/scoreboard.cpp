#include "scoreboard.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <stdexcept>

namespace simdock {
namespace {
QString k(const char *s) { return QString::fromLatin1(s); }
[[noreturn]] void fail(const QString &s) { throw std::runtime_error(s.toStdString()); }
const char common[] = R"sv(
    // SimDock scoreboard: expected behavior comes from the selected check plan.
    if (1) begin : __simdock_scoreboard
        typedef struct {
            logic [@WIDTH@-1:0] data;
            realtime accepted_at;
            bit expired;
        } expected_item;
        expected_item pending[$];
        integer errors = 0, compared = 0, accepted = 0, received = 0, discarded = 0;
        bit done = 0, decoding = 0, initialized = 0;
        wire in_reset = @RESET@;
        localparam realtime TIMEOUT_NS = @TIMEOUT@;
        localparam bit CHECK_DATA = @CHECK_DATA@, CHECK_COUNT = @CHECK_COUNT@;
        localparam bit CHECK_FORMAT = @CHECK_FORMAT@, CHECK_TIMEOUT = @CHECK_TIMEOUT@;

        clocking sample @(posedge @CLOCK@);
            default input #1step;
@SAMPLES@
        endclocking

        task automatic report_error(input string reason);
            errors++;
            if (errors <= 100) $display("SIMDOCK_CHECK_FAIL time=%0t %s", $time, reason);
            else if (errors == 101) $display("SIMDOCK_CHECK_FAIL Further errors are counted in the final summary.");
        endtask
        task automatic expect_value(input logic [@WIDTH@-1:0] value);
            expected_item item;
            accepted++;
            if (pending.size() >= 4096) begin
                report_error("More than 4096 outstanding items; check the interface bindings or run a smaller case.");
                return;
            end
            if (CHECK_DATA && $isunknown(value)) report_error("Accepted input contains X/Z.");
            item.data = value;
            item.accepted_at = $realtime;
            item.expired = 0;
            pending.push_back(item);
        endtask
        task automatic observe_value(input logic [@WIDTH@-1:0] value);
            expected_item item;
            received++;
            if (pending.size() == 0) begin
                if (CHECK_COUNT || CHECK_DATA)
                    report_error($sformatf("Unexpected output #%0d: actual=0x%0h", received, value));
                return;
            end
            item = pending.pop_front();
            compared++;
            if (CHECK_TIMEOUT && !item.expired && $realtime - item.accepted_at > TIMEOUT_NS)
                report_error("Output arrived after its deadline.");
            if (CHECK_DATA && value !== item.data)
                report_error($sformatf("Item #%0d: expected=0x%0h actual=0x%0h", compared, item.data, value));
        endtask
        task automatic finish_check();
            if (done) return;
            if (compared == 0)
                report_error("No transactions were compared. Check the stimulus, bindings and run duration.");
            if (CHECK_COUNT && pending.size() != 0)
                report_error($sformatf("Missing outputs: %0d expected item(s) remain.", pending.size()));
            if (decoding) report_error("Simulation ended in the middle of a UART frame.");
            done = 1;
            if (errors == 0)
                $display("SIMDOCK_CHECK_PASS compared=%0d accepted=%0d received=%0d reset_discarded=%0d", compared, accepted, received, discarded);
            else
                $display("SIMDOCK_CHECK_FAILED errors=%0d compared=%0d accepted=%0d received=%0d reset_discarded=%0d", errors, compared, accepted, received, discarded);
        endtask

        // Reset is observed asynchronously, including pulses shorter than a clock.
        always @(posedge in_reset) begin
            if (@DISCARD@) begin
                discarded += pending.size();
                pending.delete();
            end
        end
        always @(sample) begin
            if (!done && !in_reset) begin
@MONITOR@
                if (CHECK_TIMEOUT) begin
                    foreach (pending[i]) begin
                        if (!pending[i].expired && $realtime - pending[i].accepted_at > TIMEOUT_NS) begin
                            report_error($sformatf("Output timed out for expected value 0x%0h.", pending[i].data));
                            pending[i].expired = 1;
                        end
                    end
                end
            end
        end
@DECODER@
    end
)sv";

const char uart[] = R"sv(
        localparam realtime BIT_NS = 1000000000.0 / @BAUD@;
        task automatic decode_frame();
            logic [@WIDTH@-1:0] value;
            logic parity_bit;
            value = '0;
            #(BIT_NS / 2.0);
            if (@TX@ !== 1'b0) begin
                if (CHECK_FORMAT) report_error("Invalid UART start bit.");
                return;
            end
            for (int bit_index = 0; bit_index < @BITS@; bit_index++) begin
                #(BIT_NS);
                value[bit_index] = @TX@;
            end
            if (@PARITY@ != 0) begin
                #(BIT_NS);
                parity_bit = ^value;
                if (@PARITY@ == 2) parity_bit = ~parity_bit;
                if (CHECK_FORMAT && @TX@ !== parity_bit)
                    report_error("UART parity mismatch.");
            end
            #(BIT_NS);
            // Check each half-bit center of the configured stop interval.
            for (int half_bit = 0; half_bit < @STOP@ - 1; half_bit++) begin
                if (half_bit != 0) #(BIT_NS / 2.0);
                if (CHECK_FORMAT && @TX@ !== 1'b1)
                    report_error("Invalid or shortened UART stop bit.");
            end
            #(BIT_NS / 2.0 - 0.001);
            observe_value(value);
        endtask
        initial begin
            forever begin
                @(negedge @TX@);
                if (!done && !in_reset && @TX@ === 1'b0) begin
                    decoding = 1;
                    fork : frame_or_reset
                        decode_frame();
                        begin wait (in_reset || done); end
                    join_any
                    disable frame_or_reset;
                    decoding = 0;
                end
            end
        end
)sv";
}

QJsonObject defaultScoreboard()
{
    return {{k("schema"), k("simdock.scoreboard/v1")}, {k("kind"), k("none")},
            {k("data"), QString()}, {k("valid"), QString()}, {k("ready"), QString()},
            {k("output"), QString()}, {k("outputValid"), QString()}, {k("outputReady"), QString()},
            {k("baud"), 115200}, {k("dataBits"), 8}, {k("parity"), k("none")},
            {k("stopHalfBits"), 2}, {k("timeoutNs"), 1000000}, {k("resetPolicy"), k("discard")},
            {k("checkData"), true}, {k("checkCount"), true}, {k("checkFormat"), true},
            {k("checkTimeout"), true}, {k("values"), QJsonArray{}}};
}
bool scoreboardEnabled(const QJsonObject &plan)
{
    return !plan.isEmpty() && plan.value(k("kind")).toString() != k("none");
}
QString scoreboardLabel(const QJsonObject &plan)
{
    const auto kind = plan.value(k("kind")).toString();
    if (kind == k("uart_tx")) return k("UART transmit");
    if (kind == k("stream")) return k("FIFO / pass-through");
    if (kind == k("values")) return k("Expected values");
    return k("None");
}
QString scoreboardCode(const QList<StimulusSignal> &ports, const TbOptions &timing,
                       const QJsonObject &plan, QString *error)
{
    error->clear();
    if (!scoreboardEnabled(plan)) return {};
    try {
        const auto kind = plan.value(k("kind")).toString();
        if (plan.value(k("schema")).toString() != k("simdock.scoreboard/v1") ||
            !QStringList{k("uart_tx"), k("stream"), k("values")}.contains(kind))
            fail(k("Unsupported scoreboard plan."));
        QSet<QString> bound;
        const auto port = [&](const QString &name, const QString &direction, bool scalar, bool optional) -> StimulusSignal {
            if (name.isEmpty() && optional) return {};
            auto i = std::find_if(ports.begin(), ports.end(), [&](const auto &p) { return p.name == name; });
            if (name.isEmpty() || i == ports.end() || i->direction != direction ||
                (scalar && i->width != 1) || i->width < 1 || i->width > 64)
                fail(QStringLiteral("Select a %1 %2 for %3.").arg(scalar ? k("single-bit") : k("packed"), direction, name.isEmpty() ? k("each required binding") : name));
            if (bound.contains(name)) fail(k("Scoreboard roles must use different signals: ") + name);
            bound.insert(name);
            return *i;
        };
        const auto clock = port(timing.clock, k("input"), true, false);
        const auto reset = port(timing.reset, k("input"), true, true);
        for (const auto &p : ports)
            if (p.name == k("__simdock_scoreboard")) fail(k("The DUT uses a reserved scoreboard name."));
        const auto text = [&](const char *key) { return plan.value(k(key)).toString(); };
        const auto number = [&](const char *key, int lo, int hi) {
            const auto value = plan.value(k(key));
            const auto n = value.toInt(lo - 1);
            if (!value.isDouble() || value.toDouble() != n || n < lo || n > hi)
                fail(k("Invalid scoreboard setting: ") + k(key));
            return n;
        };
        const int timeout = number("timeoutNs", 1, 2000000000);
        const auto policy = text("resetPolicy");
        if (policy != k("discard") && policy != k("keep")) fail(k("Choose the expected reset behavior."));
        bool any = false;
        for (const char *name : {"checkData", "checkCount", "checkFormat", "checkTimeout"}) {
            if (!plan.value(k(name)).isBool()) fail(k("Invalid scoreboard check selection."));
            if (kind == k("uart_tx") || k(name) != k("checkFormat")) any |= plan.value(k(name)).toBool();
        }
        if (!any) fail(k("Select at least one check, or choose None to disable the scoreboard."));
        StimulusSignal input, valid, ready;
        if (kind != k("values")) {
            input = port(text("data"), k("input"), false, false);
            valid = port(text("valid"), k("input"), true, false);
            ready = port(text("ready"), k("output"), true, true);
        }
        const auto output = port(text("output"), k("output"), kind == k("uart_tx"), false);
        StimulusSignal outValid, outReady;
        if (kind != k("uart_tx")) {
            outValid = port(text("outputValid"), k("output"), true, false);
            outReady = port(text("outputReady"), k("input"), true, true);
            if (kind == k("stream") && input.width != output.width)
                fail(k("Pass-through input and output widths must match."));
        }
        const int width = kind == k("values") ? output.width : input.width;
        QString samples, monitor, decoder;
        const auto sample = [&](const QString &alias, const StimulusSignal &p, const QString &fallback = QString()) {
            samples += QStringLiteral("            input %1 = %2;\n").arg(alias, p.name.isEmpty() ? fallback : p.name);
        };
        if (kind != k("values")) {
            sample(k("in_data"), input); sample(k("in_valid"), valid); sample(k("in_ready"), ready, k("1'b1"));
            QString data = k("sample.in_data");
            if (kind == k("uart_tx")) {
                const int bits = number("dataBits", 5, 9);
                if (bits > width) fail(k("UART data bits exceed the bound input width."));
                data = QStringLiteral("sample.in_data[%1:0]").arg(bits - 1);
                const int baud = number("baud", 1, 100000000);
                const int stop = number("stopHalfBits", 2, 4);
                const auto parity = text("parity");
                if (!QStringList{k("none"), k("even"), k("odd")}.contains(parity)) fail(k("Invalid UART parity."));
                decoder = k(uart);
                decoder.replace(k("@BAUD@"), QString::number(baud)).replace(k("@BITS@"), QString::number(bits))
                    .replace(k("@STOP@"), QString::number(stop)).replace(k("@TX@"), output.name)
                    .replace(k("@PARITY@"), parity == k("none") ? k("0") : parity == k("even") ? k("1") : k("2"));
            }
            monitor += QStringLiteral("                if (sample.in_valid === 1'b1 && sample.in_ready === 1'b1) expect_value(%1);\n").arg(data);
        } else {
            const auto values = plan.value(k("values")).toArray();
            if (values.isEmpty() || values.size() > 4096) fail(k("Enter 1 to 4096 expected hexadecimal values."));
            monitor += k("                if (!initialized) begin\n                    initialized = 1;\n");
            for (const auto &value : values) {
                bool ok = false;
                const auto hex = value.toString().trimmed();
                const auto n = hex.toULongLong(&ok, 16);
                if (!ok || hex.startsWith(QLatin1Char('-')) || (width < 64 && n >= (quint64(1) << width)))
                    fail(k("Expected hexadecimal value does not fit the output: ") + hex);
                monitor += QStringLiteral("                    expect_value(%1'h%2);\n").arg(width).arg(QString::number(n, 16));
            }
            monitor += k("                end\n");
        }
        if (kind != k("uart_tx")) {
            sample(k("out_data"), output); sample(k("out_valid"), outValid); sample(k("out_ready"), outReady, k("1'b1"));
            monitor += k("                if (sample.out_valid === 1'b1 && sample.out_ready === 1'b1) observe_value(sample.out_data);\n");
        }
        QString result = k(common);
        result.replace(k("@SAMPLES@"), samples).replace(k("@MONITOR@"), monitor).replace(k("@DECODER@"), decoder)
            .replace(k("@WIDTH@"), QString::number(width)).replace(k("@CLOCK@"), clock.name)
            .replace(k("@TIMEOUT@"), QString::number(timeout)).replace(k("@DISCARD@"), policy == k("discard") ? k("1") : k("0"));
        // An unknown reset is treated as asserted, never as permission to collect transactions.
        result.replace(k("@RESET@"), reset.name.isEmpty() ? k("1'b0") : QStringLiteral("(%1 !== 1'b%2)").arg(reset.name).arg(timing.resetActiveLow ? 1 : 0));
        for (const char *name : {"DATA", "COUNT", "FORMAT", "TIMEOUT"}) {
            const auto field = k("check") + k(name).left(1) + k(name).mid(1).toLower();
            result.replace(k("@CHECK_") + k(name) + QLatin1Char('@'), plan.value(field).toBool() ? k("1") : k("0"));
        }
        return result;
    } catch (const std::exception &e) {
        *error = QString::fromUtf8(e.what());
        return {};
    }
}
}
