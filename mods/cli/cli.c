#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include "dynload.h"
#include "cli_support.h"

#include "module_interop.h"
#include "basic_dt.h"
#include "mgmt.h"
#include "debug.h"

static char header[] = "VDE switch V.%s\n(C) Virtual Square Team (coord. R. Davoli) 2005,2006,2007 - GPLv2\n";
static char prompt[] = "\nvde$ ";
static char *EOS = "9999 END OF SESSION";

struct comlist *cmds = NULL;
int ncmds = 0;

static int vde_logout()
{
    return -1;
}

static int vde_shutdown()
{
    printf("Shutdown from mgmt command\n");
    return -2;
}

EXPORT int help()
{
    // print all commands
    printf("%-18s %-15s %s\n", "COMMAND PATH", "SYNTAX", "HELP");
    printf("%-18s %-15s %s\n", "------------", "--------------", "------------");
    for (int i = 0; i < ncmds; i++)
    {
        printf("%-18s %-15s %s\n", cmds[i].path, cmds[i].syntax, cmds[i].help);
    }
    return 0;
}

static struct comlist cl[] = {
    {"help", "[arg]", "Help (limited to arg when specified)", help, STRARG | WITHFILE},
    {"logout", "", "logout from this mgmt terminal", vde_logout, NOARG},
    {"shutdown", "", "shutdown of the switch", vde_shutdown, NOARG},
};

// command line / json options

static struct extended_option opts[] = {

};

EXPORT int parse_args(int argc, void *argv)
{
    int c = 0;
    switch (argc)
    {
    default:
        c = argc;
    }
    return c;
}

// dtplane methods
static DT_Methods dt = {0};
DTPLANE_INIT(dt);

static Module_Data data = {
    .num_options = sizeof(opts) / sizeof(struct extended_option),
    .options = opts};

EXPORT Module_Data *on_load(const int tag)
{
    data.module_tag = tag;
    return &data;
}

static unsigned int input_fd = STDIN_FILENO;
static unsigned int mgmt_ctl = -1;
static unsigned int mgmt_data = -1;
static unsigned int console_type = -1;

EXPORT void init()
{
    if (!IS_DT_INITIALIZED(dt))
    {
        ERROR("[CLI] This module needs a data plane!");
        exit(1);
    }

    console_type = dt.add_type(data.module_tag, 0);
    dt.add_fd(input_fd, console_type, NULL);

    // copy commands
    ncmds += sizeof(cl) / sizeof(struct comlist);
    cmds = realloc(cmds, ncmds * sizeof(struct comlist));
    memcpy(cmds + (ncmds - (sizeof(cl) / sizeof(struct comlist))), cl, sizeof(cl));

    ;

    // write(STDOUT_FILENO, GRN, strlen(GRN));
    // write(STDOUT_FILENO, prompt, strlen(prompt));
    // write(STDOUT_FILENO, RESET, strlen(RESET));
}

void printoutc(FILE *f, const char *format, ...)
{
    va_list arg;

    va_start(arg, format);
    if (f)
    {
        vfprintf(f, format, arg);
        fprintf(f, "\n");
    }
    else
    {
    }
    // printlog(LOG_INFO,format,arg);
    va_end(arg);
}

int handle_command(unsigned char type, int fd, char *inbuf)
{
    struct comlist *p;
    int rv = ENOSYS;
    while (*inbuf == ' ' || *inbuf == '\t')
        inbuf++;
    if (*inbuf != '\0' && *inbuf != '#')
    {
        char *outbuf;
        size_t outbufsize;
        FILE *f = open_memstream(&outbuf, &outbufsize);

        // find command
        int i = 0;
        for (i = 0; i < ncmds; i++)
        {
            if (strncmp(cmds[i].path, inbuf, strlen(cmds[i].path)) == 0)
            {
                break;
            }
        }

        if (i < ncmds)
        {
            p = &cmds[i];
            inbuf += strlen(p->path);
            while (*inbuf == ' ' || *inbuf == '\t')
                inbuf++;
            if (p->type & WITHFD)
            {
                if (fd >= 0)
                {
                    if (p->type & WITHFILE)
                    {
                        printoutc(f, "0000 DATA END WITH '.'");
                        switch (p->type & ~(WITHFILE | WITHFD))
                        {
                        case NOARG:
                            rv = p->handler(f, fd);
                            break;
                        case INTARG:
                            rv = p->handler(f, fd, atoi(inbuf));
                            break;
                        case STRARG:
                            rv = p->handler(f, fd, inbuf);
                            break;
                        }
                        printoutc(f, ".");
                    }
                    else
                    {
                        switch (p->type & ~WITHFD)
                        {
                        case NOARG:
                            rv = p->handler(fd);
                            break;
                        case INTARG:
                            rv = p->handler(fd, atoi(inbuf));
                            break;
                        case STRARG:
                            rv = p->handler(fd, inbuf);
                            break;
                        }
                    }
                }
                else
                    rv = EBADF;
            }
            else if (p->type & WITHFILE)
            {
                printoutc(f, "0000 DATA END WITH '.'");
                switch (p->type & ~WITHFILE)
                {
                case NOARG:
                    rv = p->handler(f);
                    break;
                case INTARG:
                    rv = p->handler(f, atoi(inbuf));
                    break;
                case STRARG:
                    rv = p->handler(f, inbuf);
                    break;
                }
                printoutc(f, ".");
            }
            else
            {
                switch (p->type)
                {
                case NOARG:
                    rv = p->handler();
                    break;
                case INTARG:
                    rv = p->handler(atoi(inbuf));
                    break;
                case STRARG:
                    rv = p->handler(inbuf);
                    break;
                }
            }
        }
        if (rv == 0)
        {
            printoutc(f, "1000 Success");
        }
        else if (rv > 0)
        {
            printoutc(f, "1%03d %s", rv, strerror(rv));
        }
        fclose(f);
        if (fd >= 0)
            write(fd, outbuf, outbufsize);
        free(outbuf);
    }
    return rv;
}

EXPORT void handle_io(unsigned char type, int fd, int revents, void *private_data)
{
    char buf[128];

    if (type != mgmt_ctl)
    {
        int n = 0;

        if (revents & POLLIN)
        {
            n = read(fd, buf, sizeof(buf));
            if (n < 0)
            {
                WARN("Reading from mgmt %s", strerror(errno));
                return;
            }
        }

        if (n == 0)
        { /*EOF || POLLHUP*/
            if (type == console_type)
            {
                WARN("\nEOF on stdin, cleaning up and exiting\n");
                exit(0);
            }
            else
            {
                dt.remove_fd(fd);
            }
        }
        else
        {
            int cmdout;
            buf[n] = 0;
            if (n > 0 && buf[n - 1] == '\n')
                buf[n - 1] = 0;
            cmdout = handle_command(type, (type == console_type) ? STDOUT_FILENO : fd, buf);

            if (cmdout >= 0)
            {
                write(fd, GRN, strlen(GRN));
                write(fd, prompt, strlen(prompt));
                write(fd, RESET, strlen(RESET));
            }
            else
            {
                if (type == mgmt_data)
                {
                    write(fd, EOS, strlen(EOS));
                    dt.remove_fd(fd);
                }
                if (cmdout == -2)
                    exit(0);
            }
        }
    }
}

EXPORT int cleanup(unsigned char type,int fd,void *arg)
{
    if (cmds)
        free(cmds);
    return 0;
}

// module inter operations
MODULE_INTEROP_MAIN(Module *module)
{
    get_commands_t get_commands = dlsym(module->handle, "get_commands");
    if (get_commands)
    {
        comlist *commands = get_commands();
        int n = 0;
        while (commands[n].path)
        {
            n++;
        }
        // copy commands into global array
        ncmds += n;
        cmds = realloc(cmds, sizeof(comlist) * ncmds);
        for (int i = 0; i < n; i++)
        {
            cmds[ncmds - n + i] = commands[i];
        }
    }
}
