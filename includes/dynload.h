#ifndef DYNLOAD_H
#define DYNLOAD_H
/**
 * Define the Module internal data struct
 */
typedef struct
{
    int module_tag;
    int num_options;
    struct extended_option *options;
} Module_Data;

/**
 * Define helper macros for internal data
 */
#define MGMT_MODULE 0
#define DTPLANE_MODULE 1

// multithreading flags ?
// #define MT_GLOBAL_POOL 0
// #define MT_PRIVATE_POOL 0b1000

/**
 * Define the list of functions that the module must implement
 */
#define LIST_OF_FUNCTIONS              \
    FUNC(help, void, void)             \
    FUNC(on_load, Module_Data *, int)  \
    FUNC(parse_args, int, int, void *) \
    FUNC(init, void, void *)           \
    FUNC(handle_io, void, unsigned char, int, int, void*) \
    FUNC(cleanup, int, void)

/**
 * Define the function signature for each function in the list
 */
#define FUNC(name, ret, ...) typedef ret(name##_t)(__VA_ARGS__);
LIST_OF_FUNCTIONS
#undef FUNC

typedef int main_loop_t(void);

#define HIDDEN static
#define EXPORT

typedef struct Module
{
    // module methods
#define FUNC(name, ret, ...) name##_t *name;
    LIST_OF_FUNCTIONS
#undef FUNC
    // module internal data
    Module_Data *data;

    // modloader data
    void *handle;
    char *mod_name;
    int mod_tag;

    // module dependencies
    char **deps;
    int (*handle_deps)(struct Module*);
} Module;

typedef struct
{
    Module *modules;
    size_t size;
    size_t capacity;
} ModuleArray;

/**
 * Dependancies
 */
#define DEPENDS_ON(...) EXPORT char **deps = {__VA_ARGS__, NULL};

#define HANDLE_DEPS() int handle_deps(Module* module)



#endif