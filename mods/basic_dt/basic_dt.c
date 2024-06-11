
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <poll.h>

#include "dynload.h"
#include "module_interop.h"
#include "cli_support.h"
#include "basic_dt.h"
#include "debug.h"
#include "mgmt.h"

struct pollplus
{
    unsigned char type;
    void *private_data;
    time_t timestamp;
};

ModuleArray modules = {0};
static size_t max_fds;
static struct pollfd *fds;
static struct pollplus **fdpp;
static int nfds;
static int *fdtypes;
static int ntypes;
static int maxtypes;
static short *fdperm;
static int fdpermsize;
static int maxfds;
static int nprio;

#define PRIOFLAG 0x80
#define TYPEMASK 0x7f
#define ISPRIO(X) ((X) & PRIOFLAG)

#define TYPE2MGR(X) modules.modules[(fdtypes[((X) & TYPEMASK)])]

int main_loop(void)
{
    time_t now;
    int n, i;
    while (1)
    {
        n = poll(fds, nfds, -1);
        // now = qtime();
        if (n < 0)
        {
            if (errno != EINTR)
                printf("poll %s", strerror(errno));
        }
        else
        {
            for (i = 0; /*i < nfds &&*/ n > 0; i++)
            {
                if (fds[i].revents != 0)
                {
                    int prenfds = nfds;
                    n--;
                    // fdpp[i]->timestamp = now;

                    // find which module to call

                    TYPE2MGR(fdpp[i]->type).handle_io(fdpp[i]->type, fds[i].fd, fds[i].revents, fdpp[i]->private_data);

                    // TYPE2MGR(fdpp[i]->type)->handle_io(fdpp[i]->type, fds[i].fd, fds[i].revents, fdpp[i]->private_data);
                    if (nfds != prenfds) /* the current fd has been deleted */
                        break;           /* PERFORMANCE it is faster returning to poll */
                }
            }
        }
    }
}

void handle_io(unsigned char type, int fd, int revents, void* private_data) {
    
}

void help()
{
}

void init(void *data)
{
    max_fds = 4;
    fds = malloc(max_fds * sizeof(struct pollfd*));
    fdpp = malloc(max_fds * sizeof(struct pollplus *));
    nfds = 0;
}

static struct extended_option opts[] = {
    {"threads", required_argument, 0, OPT('t'), JSON_INT}};

EXPORT int parse_args(int argc, void *argv)
{
    int c = 0;
    switch (argc)
    {
    case 't':
        //printf("Threads: %d\n", atoi((char *)argv));
        break;
    default:
        c = argc;
    }
    return c;
}

HIDDEN Module_Data data = {0};
EXPORT Module_Data *on_load(int tag)
{
    data.num_options = sizeof(opts) / sizeof(struct extended_option);
    data.options = opts;
    return &data;
}

int cleanup()
{
    if(fds) free(fds);

    if(fdpp) {
        for(int i = 0; i < nfds; i++) {
            if(fdpp[i]) free(fdpp[i]);
        }
        free(fdpp);
    }

    if(fdperm) free(fdperm);
    if(fdtypes) free(fdtypes);

    if(modules.modules) free(modules.modules);


    return 0;
}

// dtplane methods

#define MAXFDS_INITIAL 8
#define MAXFDS_STEP 16
#define FDPERMSIZE_LOGSTEP 4

void add_fd(int fd, unsigned char type, void *private_data)
{
   struct pollfd *p;
    int index;
    /* enlarge fds and fdpp array if needed */
    if (nfds == maxfds)
    {
        maxfds = maxfds ? maxfds + MAXFDS_STEP : MAXFDS_INITIAL;
        if ((fds = realloc(fds, maxfds * sizeof(struct pollfd))) == NULL)
        {
            ERROR("realloc fds %s", strerror(errno));
            exit(1);
        }
        if ((fdpp = realloc(fdpp, maxfds * sizeof(struct pollplus *))) == NULL)
        {
            ERROR("realloc pollplus %s", strerror(errno));
            exit(1);
        }
    }
    if (fd >= fdpermsize)
    {
        fdpermsize = ((fd >> FDPERMSIZE_LOGSTEP) + 1) << FDPERMSIZE_LOGSTEP;
        if ((fdperm = realloc(fdperm, fdpermsize * sizeof(short))) == NULL)
        {
            ERROR("realloc fdperm %s", strerror(errno));
            exit(1);
        }
    }
    if (ISPRIO(type))
    {
        fds[nfds] = fds[nprio];
        fdpp[nfds] = fdpp[nprio];
        index = nprio;
        nprio++;
    }
    else
        index = nfds;
    if ((fdpp[index] = malloc(sizeof(struct pollplus))) == NULL)
    {
       WARN("realloc pollplus elem %s", strerror(errno));
        exit(1);
    }
    fdperm[fd] = index;
    p = &(fds)[index];
    p->fd = fd;
    p->events = POLLIN | POLLHUP;
    fdpp[index]->type = type;
    fdpp[index]->private_data = private_data;
    fdpp[index]->timestamp = 0;
    nfds++;
}

void remove_fd(int fd)
{
    int i = 0;

    for(i = 0; i < nfds; i++) {
        if (fds[i].fd == fd)
            break;
    }
    if(i == nfds) {
        WARN("remove_fd: fd %d not found", fd);
    } else {
        struct pollplus *pp = fdpp[i];
        // cleanup the file
        //TYPE2MGR(pp->type).cleanup(fdpp[i]->type, fds[i].fd, fdpp[i]->private_data);
        if(ISPRIO(fdpp[i]->type))
            nprio--;
        memmove(&fds[i], &fds[i + 1], (nfds - i - 1) * sizeof(struct pollfd));
		memmove(&fdpp[i], &fdpp[i + 1], (nfds - i - 1) * sizeof(struct pollplus *));
		for (; i < nfds; i++)
			fdperm[fds[i].fd] = i;
		free(pp);
		nfds--;
    }
}

int add_type(int mod_tag, int priority)
{
    int i;
    if (ntypes == maxtypes)
    {
        maxtypes = maxtypes ? 2 * maxtypes : 8;
        if (maxtypes > 128)
        {
            ERROR("too many file types");
            exit(1);
        }
        if ((fdtypes = realloc(fdtypes, maxtypes * sizeof(struct swmodule *))) == NULL)
        {
            ERROR("realloc fdtypes %s", strerror(errno));
            exit(1);
        }
        memset(fdtypes + ntypes, 0, sizeof(struct swmodule *) * maxtypes - ntypes);
        i = ntypes;
    }
    else
        for (i = 0; fdtypes[i] != 0; i++)
            ;
    fdtypes[i] = modules.modules[mod_tag].mod_tag;
    ntypes++;
    return i | ((priority != 0) ? 128 : 0);
}

void remove_type(int type)
{
    type &= TYPEMASK;
    if (type < maxtypes)
    {
        fdtypes[type] = 0;
        ntypes--;
    }
}


MODULE_INTEROP_MAIN(Module* module)
{
    // append module pointer to the modules array
    modules.modules = realloc(modules.modules, sizeof(Module) * (modules.size + 1));
    modules.modules[modules.size] = *module;
    modules.size++;

    DTPLANE_init_func_t *init = dlsym(module->handle, INIT_SYMBOL_STR);

    if (init)
    {
        DT_Methods *methods = init();
        methods->add_fd = add_fd;
        methods->remove_fd = remove_fd;
        methods->add_type = add_type;
        methods->remove_type = remove_type;
    }
}

// CLI Module integration

int show_fds(int fd)
{
    char out[256];
    for (int i = 0; i < nfds; i++)
    {
        snprintf(out, 256, "fd: %d, type: %d, handler: %s\n", fds[i].fd, fdpp[i]->type, TYPE2MGR(fdpp[i]->type).mod_name);
        write(fd, out, strlen(out));
    }
    return 0;
}

ENABLE_CLI_SUPPORT(
    {"dt/show_fds", "dt/show_fds", "Show all file descriptors", show_fds, WITHFD}
)