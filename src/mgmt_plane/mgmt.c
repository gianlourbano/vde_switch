#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dynload.h"
#include "cJSON.h"
#include "mgmt.h"
#include "debug.h"

#define IGNORE_OPT 0

// global variables
const char *prog;

// global options
static struct extended_option global_options[] = {
    {"help", no_argument, 0, 'h', 0},
    {"modules", required_argument, 0, 'm', JSON_ARRAY(JSON_STRING) | JSON_REQUIRED},
    {"config", required_argument, 0, 'c', 0}};

static int n_global_options = sizeof(global_options) / sizeof(struct extended_option);

// json conf options
static cJSON *json_config = NULL;
int scan_json_conf(const char *);
void usage(void);

int parse_global_opt(int c, void *optarg)
{
    switch (c)
    {
    case 'h':
        usage();
        exit(0);
        break;
    case 'm':
    {
        char *requested_modules = strdup(optarg);

        // strtok
        char *token = strtok(requested_modules, ",");
        while (token != NULL)
        {
            load_module(token);
            token = strtok(NULL, ",");
        }
        free(requested_modules);
        break;
    }
    case 'c':
        scan_json_conf(optarg);
        break;
    default:
        break;
    }
}

/**
 * Builds the optstring for getopt_long
 */
char *build_optstring(const struct extended_option *long_options, int total_options)
{
    if (total_options <= 0 || !long_options)
    {
        return NULL;
    }

    char *optstring = malloc(2 * total_options);
    if (!optstring)
    {
        perror("malloc optstring");
        return NULL;
    }

    int i = 0;
    char *opt_ptr = optstring;
    for (i = 0; i < total_options - 1; ++i)
    {
        int val = long_options[i].val & 0xffff;
        if (val > ' ' && val <= '~' && !strchr(optstring, val))
        {
            *opt_ptr++ = val;
            if (long_options[i].has_arg)
            {
                *opt_ptr++ = ':';
            }
        }
    }
    *opt_ptr = '\0';
    return optstring;
}

/**
 * Scans for initialization option, which need to be processed
 * before anything else, e.g. --modules, --config
 */
int scan_necessary_opts(int argc, char **argv)
{
    struct option *gopts = malloc(n_global_options * sizeof(struct option));
    if (!gopts)
    {
        perror("malloc gopts");
        return 1;
    }

    for (int i = 0; i < n_global_options; i++)
    {
        gopts[i].name = global_options[i].name;
        gopts[i].has_arg = global_options[i].has_arg;
        gopts[i].flag = global_options[i].data;
        gopts[i].val = global_options[i].val;
    }

    // get program name
    prog = argv[0];

    char *optstring = build_optstring(global_options, n_global_options + 1);

    int c;
    opterr = 1;
    while (1)
    {
        c = getopt_long(argc, argv, optstring, gopts, NULL);
        if (c == -1)
            break;

        parse_global_opt(c, optarg);
    }

    // reset getopt
    opterr = 1;
    optind = 0;

    free(optstring);
    free(gopts);

    return 0;
}

void reverse(char str[], int length)
{
    int start = 0;
    int end = length - 1;
    while (start < end)
    {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        end--;
        start++;
    }
}
// Implementation of citoa()
char *citoa(int num, char *str, int base)
{
    int i = 0;
    int isNegative = 0;

    /* Handle 0 explicitly, otherwise empty string is
     * printed for 0 */
    if (num == 0)
    {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }

    // In standard itoa(), negative numbers are handled
    // only with base 10. Otherwise numbers are
    // considered unsigned.
    if (num < 0 && base == 10)
    {
        isNegative = 1;
        num = -num;
    }

    // Process individual digits
    while (num != 0)
    {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    // If number is negative, append '-'
    if (isNegative)
        str[i++] = '-';

    str[i] = '\0'; // Append string terminator

    // Reverse the string
    reverse(str, i);

    return str;
}

void JSON_ARRSTR_TO_CSTR(cJSON *arr, char *out)
{
    cJSON *iter;

    int i = 0;
    cJSON_ArrayForEach(iter, arr)
    {
        strcat(out, iter->valuestring);
        if (i++ < cJSON_GetArraySize(arr) - 1)
            strcat(out, ",");
    }
}

/**
 * Translates JSON defined types (mgmt.h) into strings usable by the same
 * function used for options.
 *
 * JSON_INT -> cstr
 * JSON_ARRAY(JSON_STRING) -> comma separated cstr
 */
int translate_types(cJSON *obj, struct extended_option *opt, int (*parse_opts)(int, void *))
{
    int c = opt->val & 0xffff;

    switch (GET_TYPE(opt->flags))
    {
    case JSON_INT:
    {
        char out[256];
        citoa(obj->valueint, out, 10);
        parse_opts(c, (void *)out);
        break;
    }
    case JSON_ARRAY(JSON_STRING):
    {
        cJSON *iter;

        char out[256] = "\0"; // TODO: make this safe ffs
        JSON_ARRSTR_TO_CSTR(obj, out);

        parse_opts(c, (void *)out);
    }
    }
}

int scan_json_conf(const char *pathname)
{

    FILE *f = fopen(pathname, "r");
    if (!f)
    {
        ERROR("Could not find config file %s\n", pathname);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(fsize + 1);
    fread(buf, 1, fsize, f);
    fclose(f);
    buf[fsize] = 0;
    json_config = cJSON_Parse(buf);
    if (!json_config)
    {
        fprintf(stderr, "Error parsing json config\n\t%s", cJSON_GetErrorPtr());
        exit(1);
    }

    // look for json-enabled global options
    for (int i = 0; i < n_global_options; i++)
    {
        if (global_options[i].flags & JSON_REQUIRED)
        {
            cJSON *opt = cJSON_GetObjectItem(json_config, global_options[i].name);
            if (global_options[i].flags & JSON_REQUIRED && !opt)
            {
                fprintf(stderr, "Missing required option %s\n", global_options[i].name);
                return 1;
            }

            if (opt)
            {
                translate_types(opt, &global_options[i], parse_global_opt);
            }
        }
    }
    free(buf);

    return 0;
}

struct extended_option *optcpy(struct extended_option *tgt, const struct extended_option *src, int n, int tag)
{
    for (int i = 0; i < n; ++i)
    {
        tgt[i] = src[i];
        tgt[i].val |= (tag << 16);
    }
    return tgt + n;
}

extern ModuleArray modules;
static struct extended_option *long_options;
static struct option *c_opts;
static int total_options = 0;
static char *optstring = NULL;

int build_extended_options()
{
    total_options = n_global_options + 1;

    for (size_t i = 0; i < modules.size; i++)
    {
        if (modules.modules[i].data)
            total_options += modules.modules[i].data->num_options;
    }

    long_options = malloc(total_options * sizeof(struct extended_option));
    if (!long_options)
    {
        ERROR("malloc long_options");
        exit(EXIT_FAILURE);
    }

    int i;

    struct extended_option *opt_iter = optcpy(long_options, global_options, n_global_options, IGNORE_OPT);
    for (size_t i = 0; i < modules.size; i++)
    {
        if (modules.modules[i].data)
            opt_iter = optcpy(opt_iter, modules.modules[i].data->options, modules.modules[i].data->num_options, modules.modules[i].mod_tag);
    }

    *opt_iter = (struct extended_option){0, 0, 0, 0, 0}; // Null terminate

    optstring = build_optstring(long_options, total_options);

    c_opts = malloc(total_options * sizeof(struct option));
    if (!c_opts)
    {
        ERROR("malloc c_opts");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < total_options - 1; i++)
    {
        c_opts[i].name = long_options[i].name;
        c_opts[i].has_arg = long_options[i].has_arg;
        c_opts[i].flag = long_options[i].data;
        c_opts[i].val = long_options[i].val;
    }

    // c_opts[total_options - 1] = (struct option){0, 0, 0, 0}; // Null terminate

    return 0;
}

int parse_json_conf()
{
    cJSON *iter;
    cJSON_ArrayForEach(iter, json_config)
    {
        // find if it is an option
        int c = -1;
        int found = 0;
        int i;
        struct extended_option *opt = NULL;
        for (i = 0; i < total_options; i++)
        {
            if (!strcmp(iter->string, long_options[i].name))
            {
                found = 1;
                c = long_options[i].val >> 16;
                opt = &long_options[i];
                break;
            }
        }
        if (!found)
            WARN("[JSON] Unrecognized option %s\n", iter->string);
        else
        {
            for (size_t i = 0; i < modules.size; i++)
            {
                if (modules.modules[i].data && modules.modules[i].parse_args)
                {
                    if (c == modules.modules[i].mod_tag)
                    {
                        translate_types(iter, opt, modules.modules[i].parse_args);
                        break;
                    }
                }
            }
        }
    }
}

int parse_opts(int argc, char **argv)
{
    build_extended_options();

    int option_index = 0;
    while (1)
    {
        int c = getopt_long(argc, argv, optstring, c_opts, &option_index);
        if (c == -1)
            break;

        for (size_t i = 0; i < modules.size && c != 0; i++)
        {
            if (modules.modules[i].data && modules.modules[i].parse_args)
            {
                if ((c >> 7) == 0)
                    c = modules.modules[i].parse_args(c, optarg);
                else if ((c >> 16) == modules.modules[i].mod_tag)
                {
                    modules.modules[i].parse_args(c & 0xffff, optarg);
                    c = 0;
                }
            }
        }
    }

    if (json_config)
    {
        parse_json_conf();
        cJSON_Delete(json_config);
    }

    free(long_options);
    free(c_opts);
    free(optstring);
}

void usage(void)
{
    // print all options
    printf("Usage: %s [OPTIONS]\n", prog);

    printf("Options:\n");

    for (int i = 0; i < total_options - 1; i++)
    {
        printf("\t--%s", long_options[i].name);
        if (long_options[i].has_arg)
        {
            printf(" <arg>");
        }
        printf("\n");
    }

    printf("\n");

    exit(0);
}