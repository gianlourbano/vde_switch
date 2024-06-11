#ifndef MGMT_H
#define MGMT_H

#define no_argument 0
#define required_argument 1
#define optional_argument 2

#define GET_TYPE(x) ((x) & 0x7)

#define NOOPT 0
#define OPT(x) (x)

#define JSON_INT 1
#define JSON_STRING 2
#define JSON_BOOL 3
#define JSON_ARRAY(x) (4 | (x))
#define JSON_OBJECT 5

#define JSON_OPTIONAL
#define JSON_REQUIRED (1 << 3)
#define JSON_IS_REQUIRED(x) ((x) & JSON_REQUIRED)

#define END_OF_OPTS      \
    {                    \
        0, 0, 0, 0, 0 \
    }

typedef struct extended_option
{
    const char *name;
    int has_arg;
    int *data;
    int val;

    // json specific
    int flags;
} extended_option;

#endif