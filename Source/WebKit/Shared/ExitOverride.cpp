
// Implement custom exit() function that calls _exit() immediately
// to avoid running exit handlers and static destructors.
// This overrides default exit() function from stdlib.h.

#include "ExitOverride.h"

void exit(int status) {
    {
        QuickExitAfterScope _exit(status);
    }
}

