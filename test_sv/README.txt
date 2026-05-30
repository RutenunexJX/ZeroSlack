test_sv — Slang 符号提取 / 补全 的测试与无头调试
====================================================

文件
----
- test_symbols.sv     测试用例：覆盖 module/port/reg/wire/logic/param、typedef enum/struct、
                      内联匿名 enum/struct、enum/struct 变量与成员、task/function、实例化等。
- dump_symbols.cpp    无头工具：调用 SlangManager::extractSymbols 打印每个符号的
                      name/type/line/moduleScope/dataType。用于核对符号字段。
- completion_test.cpp 无头测试：用 Slang 填充 sym_list 后，驱动 CompletionManager 的公开方法
                      （getStructTypeForVariable / getStructMemberCompletions /
                      getModuleInternalSymbolsByType）并断言结果。不弹出 GUI 窗口。

为什么能无头测试
----------------
补全/跳转的“正确性根源”在数据层（Slang 产出哪些符号、moduleScope/dataType 是否正确）与
CompletionManager 的查询逻辑——二者都可脱离 GUI 直接调用验证。GUI 仅在验证弹窗/鼠标交互
（MyCodeEditor 内的渲染、Ctrl+Click）时才需要手动运行。

构建与运行（MinGW + 已构建的 build 目录）
-----------------------------------------
在 build/Desktop_Qt_6_10_2_MinGW_64_bit-Debug 目录下，复用已编译的对象与 slang 静态库即可。

变量：
  GXX   = E:/QT6/Tools/mingw1310_64/bin/g++.exe
  QTINC = E:/QT6/6.10.2/mingw_64/include
  QTLIB = E:/QT6/6.10.2/mingw_64/lib

1) dump_symbols（只需 slangmanager 对象 + slang + Qt6Core）：
   $GXX -std=c++20 -D__USE_MINGW_ANSI_STDIO=1 -I<repo> -I$QTINC -I$QTINC/QtCore \
        -c ../../test_sv/dump_symbols.cpp -o dump_symbols.o
   $GXX -o dump_symbols.exe dump_symbols.o CMakeFiles/demo.dir/slangmanager.cpp.obj \
        -Wl,--start-group thirdparty/slang/lib/libsvlang.a thirdparty/slang/lib/libfmtd.a -Wl,--end-group \
        -L$QTLIB -lQt6Core
   运行（PATH 加 Qt/MinGW bin）：./dump_symbols.exe ../../test_sv/test_symbols.sv

2) completion_test（链接除 main.cpp.obj 外的全部 demo 对象 + Qt Widgets/Gui/Core/Concurrent + slang，
   自带 main()）：见提交说明或脚本；运行时设 QT_QPA_PLATFORM=offscreen、QT_PLUGIN_PATH=<qt>/plugins。

注意
----
- 生成的 .exe 为 Debug 静态链接，体积很大（数百 MB），用完即删，勿入库。
- CompletionManager 的匹配是“缩写/模糊匹配”，前缀 'r' 会同时匹配 red 和 green（均含 r）。
