#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

#include "dynload.h"
#include "module_interop.h"
#include "cli_support.h"
#include "mgmt.h"
#include "html.h"

HIDDEN int test_var = 0;

int test(int fd, int a)
{
    char out[256];
    snprintf(out, 256, "test: %d\n", a * 2);
    write(fd, out, strlen(out) + 1);

    return 0;
}

void help()
{
    printf("test_var=%d\n", test_var);
}

void init()
{
}

static struct extended_option opts[] = {
    {"test", required_argument, 0, OPT('t') | 0x100, JSON_INT}};

void handle_io(unsigned char type, int fd, int revents, void *private_data) {}

EXPORT int parse_args(int argc, void *optarg)
{
    int c = 0;
    switch (argc)
    {
    case (OPT('t') | 0x100):
        test_var = atoi((char *)optarg);
        printf("test_var: %d\n", test_var);
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

ENABLE_HTML_SUPPORT()
DECLARE_HTML_ACTIONS(
    {"print_test", help})
