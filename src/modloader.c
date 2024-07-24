#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include "dynload.h"
#include "debug.h"

#define BASE_MODS_PATH "./mods/"
#define FULL_PATH(modname) "./mods/lib" #modname ".so"

ModuleArray modules = {0};

size_t modcount = 1;

// data plane
main_loop_t *main_loop = NULL;

int load_module(const char *mod_name)
{
    if (modules.size == modules.capacity)
    {
        if (modules.capacity == 0)
            modules.capacity = 1;
        modules.capacity *= 2;
        modules.modules = realloc(modules.modules, modules.capacity * sizeof(Module));
    }

    Module *module = &modules.modules[modules.size];

    // craft the full path (mod_name does not include lib and .so)
    char full_path[256];
    snprintf(full_path, 256, FULL_PATH(% s), mod_name);

    module->handle = dlopen(full_path, RTLD_LAZY);
    if (!module->handle)
    {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

#define FUNC(name, ...)                                           \
    module->name = dlsym(module->handle, #name);                  \
    if (module->name == NULL)                                     \
    {                                                             \
        WARN("DYNLOAD: could not find %s symbol in %s:\n\t %s\n", \
             #name, mod_name, dlerror());                         \
        return 1;                                                 \
    }

    LIST_OF_FUNCTIONS
#undef FUNC

    main_loop_t *ml = dlsym(module->handle, "main_loop");
    if (ml != NULL && main_loop != NULL)
    {
        WARN("DYNLOAD: main_loop already defined by some other module\n");
        exit(1);
    }
    else if (ml != NULL && main_loop == NULL)
    {
        LOG("Found main_loop in %s\n", mod_name);
        main_loop = ml;
    }
    else if (ml == NULL && main_loop == NULL)
    {
        ERROR("DYNLOAD: no main_loop defined\n");
        exit(1);
    }

    module->data = module->on_load(modcount);
    module->mod_tag = module->data->module_tag = modcount++;

    module->mod_name = strdup(mod_name);

    modules.size++;
    LOG("Module %s loaded\n", mod_name);

    return 0;
}

void cleanup()
{
    for (size_t i = 0; i < modules.size; i++)
    {
        Module *module = &modules.modules[i];
        if (module && module->handle)
        {
            module->cleanup(0, -1, NULL);
        }
    }

    for (size_t i = 0; i < modules.size; i++)
    {
        Module *module = &modules.modules[modules.size - i - 1];
        if (dlclose(module->handle))
        {
            ERROR("Module %s could not be unloaded: %s\n", module->mod_name, dlerror());
        }
        LOG("Module %s unloaded\n", module->mod_name);
        free(module->mod_name);
    }

    modules.size = 0;
    modules.capacity = 0;
    if (modules.modules != NULL)
        free(modules.modules);
    modules.modules = NULL;
}

typedef int (*request_for_module_interop_t)();

int module_interop()
{

    for (size_t i = 0; i < modules.size; i++)
    {
        Module *module = &modules.modules[i];
        request_for_module_interop_t main_handler = dlsym(module->handle, "module_interop_request");
        if (main_handler)
        {
            for (size_t j = 0; j < modules.size; j++)
            {
                if (i != j)
                {
                    Module *module2 = &modules.modules[j];
                    // search for the module with the request_for_##module_name symbol
                    char symbol[256];
                    snprintf(symbol, 256, "request_for_%s", module->mod_name);
                    request_for_module_interop_t interop = dlsym(module2->handle, symbol);
                    if (interop)
                    {
                        int prio2 = interop();
                        main_handler(module2);
                    }
                }
            }
        }
    }

    return 0;
}

int init()
{
    for (size_t i = 0; i < modules.size; i++)
    {
        Module *module = &modules.modules[i];
        module->init(module->data);
    }
    atexit(cleanup);
    void set_signal_handlers();
    set_signal_handlers();
    return 0;
}

void signal_handler(int sig)
{
    ERROR("Caught signal %d\n", sig);
    cleanup();
    signal(sig, SIG_DFL);
    if (sig == SIGTERM)
        _exit(0);
    else
        kill(getpid(), sig);
}

void set_signal_handlers()
{
    struct
    {
        int sig;
        const char *name;
        int ignore;
    } signals[] = {
        {SIGHUP, "SIGHUP", 0},
        {SIGINT, "SIGINT", 0},
        {SIGPIPE, "SIGPIPE", 1},
        {SIGALRM, "SIGALRM", 1},
        {SIGTERM, "SIGTERM", 0},
        {SIGUSR1, "SIGUSR1", 1},
        {SIGUSR2, "SIGUSR2", 1},
        {SIGPROF, "SIGPROF", 1},
        {SIGVTALRM, "SIGVTALRM", 1},
#ifdef VDE_LINUX
        {SIGPOLL, "SIGPOLL", 1},
#ifdef SIGSTKFLT
        {SIGSTKFLT, "SIGSTKFLT", 1},
#endif
        {SIGIO, "SIGIO", 1},
        {SIGPWR, "SIGPWR", 1},
#ifdef SIGUNUSED
        {SIGUNUSED, "SIGUNUSED", 1},
#endif
#endif
#ifdef VDE_DARWIN
        {SIGXCPU, "SIGXCPU", 1},
        {SIGXFSZ, "SIGXFSZ", 1},
#endif
        {0, NULL, 0}};

    int i;
    for (i = 0; signals[i].sig != 0; i++)
        if (signal(signals[i].sig,
                   signals[i].ignore ? SIG_IGN : signal_handler) < 0)
            ERROR("Setting signal handler for %s: %s\n", signals[i].name, strerror(errno));
}
