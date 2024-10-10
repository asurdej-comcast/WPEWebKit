
#ifndef _EXIT_OVERRIDES_H_
#define _EXIT_OVERRIDES_H_

#include <cstdlib>

struct QuickExitAfterScope {
    QuickExitAfterScope(int status = 0) : status(status) {}
    [[noreturn]] ~QuickExitAfterScope() { std::quick_exit(status); }
    int status = 0;
};

#endif // _EXIT_OVERRIDES_H_

