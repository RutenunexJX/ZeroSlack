#pragma once
#include <QtCore/qglobal.h>
#if defined(ZEROSLACK_SEMANTIC_SHARED)
# if defined(zeroslack_semantic_EXPORTS)
#  define ZEROSLACK_SEMANTIC_API Q_DECL_EXPORT
# else
#  define ZEROSLACK_SEMANTIC_API Q_DECL_IMPORT
# endif
#else
# define ZEROSLACK_SEMANTIC_API
#endif
