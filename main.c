#include <stdlib.h>

#include "dynload.h"
#include "debug.h"

extern main_loop_t *main_loop;

int main(int argc, char **argv)
{
    // loads confing and/or modules
    scan_necessary_opts(argc, argv);

    // enables module inter communication
    module_interop();

    // parses other options and/or config.
    parse_opts(argc, argv);

    // we're ready to start
    init();

    if (main_loop == NULL)
    {
        ERROR("No main loop defined\n");
        exit(1);
    }
    main_loop();

    cleanup();

    return 0;
}