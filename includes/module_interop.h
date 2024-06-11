#ifndef MODULE_INTEROP_H
#define MODULE_INTEROP_H

#include <dlfcn.h>

#define MODULE_INTEROP_FW(module) \
    int request_for_##module(); \

#define MODULE_INTEROP(module) \
    int request_for_##module() { return 1;} \

#define MODULE_INTEROP_MAIN(...)  \
    int module_interop_request( __VA_ARGS__ )

#endif