/**
 * \file logging.c - Contains debug log functions
 *
 * Copyright (c) 2015 Michael Casadevall
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 **/

#define __FTL_INTERNAL
#include "ftl.h"

static ftl_logging_function_t ftl_log_cb;

void ftl_logging_init() {
  ftl_log_cb = 0;
}

void ftl_register_log_handler(ftl_logging_function_t log_func) {
  ftl_log_cb = log_func;
}

// Convert compiler macro to actual printf call to stderr
void ftl_log_message(ftl_log_severity_t log_level, const char * file, int lineno, const char * fmt, ...) {
    va_list args;
    char message[2048];
    va_start(args, fmt);
    vsnprintf(message, 2048, fmt, args);
    va_end(args);

    // and now spit it out
    if (ftl_log_cb != 0) {
      (*ftl_log_cb)(log_level, message);
    } else {
      fprintf(stderr, "[%s]:%d %s\n", file, lineno, message);
    }
}
