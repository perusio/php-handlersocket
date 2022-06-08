
#ifndef PHP_HANDLERSOCKET_H
#define PHP_HANDLERSOCKET_H

#define HANDLERSOCKET_EXTENSION_VERSION "0.3.1"

extern zend_module_entry handlersocket_module_entry;
#define phpext_handlersocket_ptr &handlersocket_module_entry

#ifdef PHP_WIN32
#   define PHP_HANDLERSOCKET_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#   define PHP_HANDLERSOCKET_API __attribute__ ((visibility("default")))
#else
#   define PHP_HANDLERSOCKET_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#ifdef ZTS
#define HANDLERSOCKET_G(v) TSRMG(handlersocket_globals_id, zend_handlersocket_globals *, v)
#else
#define HANDLERSOCKET_G(v) (handlersocket_globals.v)
#endif

#endif  /* PHP_HANDLERSOCKET_H */
