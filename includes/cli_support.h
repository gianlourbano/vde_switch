#ifndef CLI_SUPPORT_H
#define CLI_SUPPORT_H

#include "module_interop.h"

#define NOARG 0
#define INTARG 1
#define STRARG 2
#define WITHFILE 0x40
#define WITHFD 0x80

typedef struct comlist {
	const char *path;
	const char *syntax;
	const char *help;
	int (*handler)();
	unsigned char type;
} comlist;

typedef comlist* (*get_commands_t)();

#define ENABLE_CLI_SUPPORT(...) \
MODULE_INTEROP(cli); \
comlist* get_commands() { \
    static comlist commands[] = { \
    __VA_ARGS__ ,  {NULL, NULL, NULL, NULL, 0} \
    }; \
    return commands; \
} \

#define ENABLE_CLI_SUPPORT_FW() \
    MODULE_INTEROP_FW(cli); \
    comlist* get_commands(); \



#endif