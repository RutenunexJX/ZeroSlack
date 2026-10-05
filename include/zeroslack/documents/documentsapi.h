#pragma once
#include <QtCore/qglobal.h>
#if defined(ZEROSLACK_DOCUMENTS_SHARED)
# if defined(zeroslack_documents_EXPORTS)
#  define ZEROSLACK_DOCUMENTS_API Q_DECL_EXPORT
# else
#  define ZEROSLACK_DOCUMENTS_API Q_DECL_IMPORT
# endif
#else
# define ZEROSLACK_DOCUMENTS_API
#endif
