#ifndef VERSION_H
#define VERSION_H

// 应用版本号。每次提交版本时同步更新（与 commit tag 对应，如 0.0.11/slangN）。
#define APP_VERSION "0.0.11/slang15"

// 构建时间：在编译引用本宏的翻译单元（mainwindow.cpp）时确定，每次重新编译即刷新。
#define APP_BUILD_TIME (__DATE__ " " __TIME__)

#endif // VERSION_H
