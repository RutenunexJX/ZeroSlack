package zeroslack_events_package;
  timeunit 1ps;
  timeprecision 1fs;

  function automatic string encode(input string text);
    string encoded = "";
    for (int i = 0; i < text.len(); ++i)
      encoded = {encoded, $sformatf("%02x", byte'(text[i]))};
    return encoded;
  endfunction

  function automatic string field(input string name, input string value,
                                   input string radix = "text", input string unit_name = "");
    return {encode(name), ":", encode(value), ":", encode(radix), ":", encode(unit_name)};
  endfunction

  function automatic void emit_event(input realtime time_ps, input string level,
      input string tag, input string instance_name, input string source_file,
      input int source_line, input string message, input string values = "");
    $display("ZS_EVT_V1|%0.3f|%s|%s|%s|%s|%0d|%s|%s", time_ps, level,
      encode(tag), encode(instance_name), encode(source_file), source_line, encode(message), values);
  endfunction
endpackage
