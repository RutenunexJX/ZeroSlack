#ifndef VERSION_H
#define VERSION_H

// Keep this in sync with user-visible release commits.
#define APP_VERSION "0.0.20/slang25"

// Build time is refreshed whenever a translation unit including this header is rebuilt.
#define APP_BUILD_TIME (__DATE__ " " __TIME__)

#endif // VERSION_H
