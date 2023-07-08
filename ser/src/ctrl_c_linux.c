#include "ctrl_c.h"

#include <stdlib.h>
#include <signal.h>


static ctrl_c_callback_t procedure = NULL;

static void close_handler(int s)
{
    (void)s;

    if (procedure())
        exit(EXIT_SUCCESS);
}


void ctrl_c_register(ctrl_c_callback_t callback)
{
    procedure = callback;

    struct sigaction sigIntHandler = {0};

    sigIntHandler.sa_handler = close_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
}
