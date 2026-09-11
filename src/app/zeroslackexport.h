#ifndef ZEROSLACKEXPORT_H
#define ZEROSLACKEXPORT_H

#include <QtCore/qglobal.h>

#if defined(ZEROSLACK_CORE_SHARED)
#  if defined(zeroslack_core_EXPORTS)
#    define ZEROSLACK_API Q_DECL_EXPORT
#  else
#    define ZEROSLACK_API Q_DECL_IMPORT
#  endif
#else
#  define ZEROSLACK_API
#endif

#endif // ZEROSLACKEXPORT_H
