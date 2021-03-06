/*
 * This file is part of selfrando.
 * Copyright (c) 2015-2017 Immunant Inc.
 * For license information, see the LICENSE file
 * included with selfrando.
 *
 */

#include "Debug.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

void Debug::PrintfImpl(const char *fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(tmp, 255, fmt, args);
    va_end(args);
    // FIXME: find better printing output
    ssize_t retcode = write(STDERR_FILENO, tmp, len);
    assert(retcode != -1 && "Write to stderr failed");
    (void) retcode;
}

void Error::printf(const char *fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(tmp, 255, fmt, args);
    va_end(args);
    // FIXME: find better printing output
    ssize_t retcode = write(STDERR_FILENO, tmp, len);
    assert(retcode != -1 && "Write to stderr failed");
    assert(false);
    (void) retcode;
}
