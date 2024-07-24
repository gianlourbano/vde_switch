#ifndef WEBSERVER_HTML_H
#define WEBSERVER_HTML_H

#include "module_interop.h"
#include "dynload.h"

#define HTML_INT 1
#define HTML_FLOAT 2
#define HTML_STRING 3
#define HTML_BOOL 4
#define HTML_ARR(x) (8 | x)

#define END_ACTIONS {NULL, NULL}
#define END_VARS {NULL, NULL, 0, NULL, NULL}

typedef struct {
    const char* var_name;
    void* var;
    int type;
    void * (*getter)(void);
    void (*setter)(void *);
} Webserver_Modifiable_Variable;

typedef struct {
    const char* action_name;
    void (*action)(void);
} Webserver_Action;

typedef struct {
    Webserver_Action* actions;
    Webserver_Modifiable_Variable* vars;
    struct Module* module;
} Webserver_Support;

#define ENABLE_HTML_SUPPORT() \
    MODULE_INTEROP(webserver); 

#define DECLARE_HTML_ACTIONS(...) \
    Webserver_Action webserver_actions[] = { \
        __VA_ARGS__, \
        END_ACTIONS \
    };

#define DECLARE_HTML_VARS(...) \
    Webserver_Modifiable_Variable webserver_vars[] = { \
        __VA_ARGS__, \
        END_VARS \
    };

#endif