#ifndef BASIC_DT_H
#define BASIC_DT_H

#define DT_FUNCS           \
    F(add_fd, void, int, unsigned char, void*)    \
    F(remove_fd, void, int) \
    F(add_type, int, int, int)  \
    F(remove_type, void, int)

#define F(name, ret, ...) typedef ret(name##_t)(__VA_ARGS__);
DT_FUNCS
#undef F

typedef struct
{
#define F(name, ret, ...) name##_t *name;
    DT_FUNCS
#undef F
} DT_Methods;

typedef DT_Methods*(DTPLANE_init_func_t)(void);

#define INIT_SYMBOL dt_plane_init
#define _STR(x) #x
#define _STR2(X) _STR(X)
#define INIT_SYMBOL_STR _STR2(INIT_SYMBOL)

#define DTPLANE_INIT(dt_methods) int request_for_basic_dt() {return 1;};DT_Methods* INIT_SYMBOL() { return &dt_methods; }

#define IS_DT_INITIALIZED(dt_methods) (dt_methods.add_fd && dt_methods.remove_fd && dt_methods.add_type && dt_methods.remove_type)

#endif