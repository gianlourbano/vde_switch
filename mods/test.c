#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

#include "dynload.h"
#include "module_interop.h"
#include "cli_support.h"
#include "mgmt.h"

HIDDEN int port = 3000;
HIDDEN char strn[256] = "tap0";

int test(int fd, int a)
{
    char out[256];
    snprintf(out, 256, "test: %d\n", a * 2);
    write(fd, out, strlen(out) + 1);

    return 0;
}

void help()
{
    printf("port: %d\n", port);
    printf("tap: %s\n", strn);
}

void init(void *data)
{
}

static struct extended_option opts[] = {
    {"port", required_argument, 0, OPT('p'), JSON_INT}
};

void handle_io(unsigned char type, int fd, int revents, void *private_data) {}

EXPORT int parse_args(int argc, void *argv)
{
    int c = 0;
    switch (argc)
    {
    case 't':
        printf("test\n");
        break;
    case ('p'):
        port = atoi((char*)optarg);
        printf("port: %d\n", port);
        break;
    default:
        c = argc;
    }

    return c;
}

HIDDEN Module_Data data = {0};
EXPORT Module_Data *on_load(int tag)
{
    data.num_options = sizeof(opts) / sizeof(struct option);
    data.options = opts;
    return &data;
}

int cleanup()
{
    return 0;
}

// Cli module support
ENABLE_CLI_SUPPORT(
    {"test", "test <int>", "multiply the integer by 2", test, WITHFD | INTARG});
