#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>

#include "dynload.h"
#include "module_interop.h"
#include "cli_support.h"
#include "mgmt.h"
#include "basic_dt.h"
#include "debug.h"
#include "html.h"

extern int handle_request(int fd);

#define BUFFER_SIZE (1 << 17)

Webserver_Support *support;
int webserver_support_size = 0;
int webserver_support_capacity = 0;

MODULE_INTEROP_MAIN(Module *module)
{
    Webserver_Action *webserver_actions = dlsym(module->handle, "webserver_actions");
    Webserver_Modifiable_Variable *webserver_vars = dlsym(module->handle, "webserver_vars");

    if (webserver_vars || webserver_actions)
    {
        if (webserver_support_size == webserver_support_capacity)
        {
            webserver_support_capacity = webserver_support_capacity ? webserver_support_capacity * 2 : 1;
            support = realloc(support, webserver_support_capacity * sizeof(Webserver_Support));
        }

        support[webserver_support_size].module = module;
        support[webserver_support_size].actions = webserver_actions;
        support[webserver_support_size].vars = webserver_vars;
        webserver_support_size++;
    }
}

DT_Methods dt;
DTPLANE_INIT(dt);

HIDDEN Module_Data moddata = {0};
HIDDEN int data_type = -1;
int one = 1;

HIDDEN int port = 4242;
HIDDEN int threaded = 0;

EXPORT void init()
{
    if (!IS_DT_INITIALIZED(dt))
    {
        ERROR("DT not initialized\n");
        exit(1);
    }

    int connect_fd;
    struct sockaddr_in sock_addr;

    if ((connect_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
        ERROR("[WEB] socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(connect_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
                   sizeof(one)) < 0)
    {
        return;
    }

    sock_addr.sin_family = AF_UNIX;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(connect_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0)
    {
        ERROR("[WEB] bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(connect_fd, 3) < 0)
    {
        ERROR("[WEB] listen failed");
        exit(EXIT_FAILURE);
    }

    LOG("[WEB] Listening on port %d\n", port);

    data_type = dt.add_type(moddata.module_tag, 0);
    dt.add_fd(connect_fd, data_type, NULL);
}

HIDDEN struct extended_option opts[] = {
    {"port", required_argument, 0, 'p', JSON_INT},
    {"threaded", required_argument, 0, 't', JSON_BOOL}};

EXPORT void handle_io(unsigned char type, int fd, int revents, void *private_data)
{
    // dispatch thread, might need to be changed to a thread pool
    if (revents & POLLIN)
    {
        if (threaded == 0)
        {
            handle_request(fd);
        }
        else
        {

            pthread_t thread;
            pthread_create(&thread, NULL, &handle_request, (void *)fd);
            pthread_detach(thread);
        }
    }
}

EXPORT int parse_args(int argc, void *optarg)
{
    int c = 0;
    switch (argc)
    {
    case 'p':
    {
        port = atoi(optarg);
        break;
    }
    case 't':
    {
        threaded = atoi(optarg);
        LOG("[WEB] %s threaded mode\n", threaded ? "Enabled" : "Disabled");
        break;
    }

    default:
        c = argc;
    }

    return c;
}

EXPORT Module_Data *on_load(int tag)
{
    moddata.module_tag = tag;
    LOG("[WEB] Loaded with tag=%d\n", tag);
    moddata.num_options = sizeof(opts) / sizeof(struct option);
    moddata.options = opts;
    return &moddata;
}

EXPORT int cleanup(unsigned char type,int fd,void *arg)
{
    return 0;
}

EXPORT void help()
{
}
