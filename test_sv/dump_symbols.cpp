// Headless harness: dump symbols produced by SlangManager::extractSymbols.
// Build/link against the already-compiled slangmanager.cpp.obj + slang libs + Qt6Core.
#include "slangmanager.h"
#include <QFile>
#include <QTextStream>
#include <QString>
#include <cstdio>

static const char* typeName(sym_list::sym_type_e t) {
    using S = sym_list;
    switch (t) {
    case S::sym_reg: return "reg";
    case S::sym_wire: return "wire";
    case S::sym_logic: return "logic";
    case S::sym_interface: return "interface";
    case S::sym_enum: return "enum";
    case S::sym_enum_var: return "enum_var";
    case S::sym_enum_value: return "enum_value";
    case S::sym_packed_struct: return "packed_struct";
    case S::sym_unpacked_struct: return "unpacked_struct";
    case S::sym_packed_struct_var: return "packed_struct_var";
    case S::sym_unpacked_struct_var: return "unpacked_struct_var";
    case S::sym_struct_member: return "struct_member";
    case S::sym_typedef: return "typedef";
    case S::sym_task: return "task";
    case S::sym_function: return "function";
    case S::sym_localparam: return "localparam";
    case S::sym_parameter: return "parameter";
    case S::sym_module: return "module";
    case S::sym_inst: return "inst";
    case S::sym_inst_pin: return "inst_pin";
    case S::sym_port_input: return "port_input";
    case S::sym_port_output: return "port_output";
    case S::sym_port_inout: return "port_inout";
    case S::sym_port_ref: return "port_ref";
    case S::sym_package: return "package";
    case S::sym_user: return "user";
    default: return "?";
    }
}

int main(int argc, char** argv) {
    QString path = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                              : QStringLiteral("test_sv/test_symbols.sv");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QFile::Text)) {
        fprintf(stderr, "cannot open %s\n", path.toLocal8Bit().constData());
        return 1;
    }
    QString content = QTextStream(&f).readAll();
    f.close();

    SlangManager mgr;
    QList<sym_list::SymbolInfo> syms = mgr.extractSymbols(path, content);

    printf("extracted %d symbols from %s\n\n", (int)syms.size(),
           path.toLocal8Bit().constData());
    printf("%-20s %-20s %5s  %-14s %-12s\n",
           "name", "type", "line", "moduleScope", "dataType");
    printf("--------------------------------------------------------------------------\n");
    for (const auto& s : syms) {
        printf("%-20s %-20s %5d  %-14s %-12s\n",
               s.symbolName.toLocal8Bit().constData(),
               typeName(s.symbolType),
               s.startLine,
               s.moduleScope.toLocal8Bit().constData(),
               s.dataType.toLocal8Bit().constData());
    }
    return 0;
}
