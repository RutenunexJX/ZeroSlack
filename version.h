#ifndef VERSION_H
#define VERSION_H

// Keep this in sync with user-visible release commits.
#define APP_VERSION "0.0.14/slang19"

// Build time is refreshed whenever a translation unit including this header is rebuilt.
#define APP_BUILD_TIME (__DATE__ " " __TIME__)

#endif // VERSION_H
