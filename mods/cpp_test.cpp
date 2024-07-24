#include <iostream>
#include "dynload.h"
#include "cli_support.h"

extern "C"
{
#define FUNC(name, ret, ...) ret name(__VA_ARGS__);
    LIST_OF_FUNCTIONS
#undef FUNC

    ENABLE_CLI_SUPPORT_FW();
}

int test(int a)
{
    return a * 1;
}

void help()
{
   std::cout << "Hello from c++\n";
}

void init(void* data)
{
    return ;
}

int cleanup(unsigned char type,int fd,void *arg)
{
    return 0;
}

ENABLE_CLI_SUPPORT(
    {"cpp/test", "cpp/test <int>", "return the integer", (int (*)(void))test, 0}
);

