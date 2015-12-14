/**
 * create_stream.c - Stream Creation Functions
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

// Allocates the stream configuration structure
ftl_status_t ftl_create_stream_configuration(ftl_stream_configuration_t** stream_config) {
  /* First step, make sure we got a zero pointer coming in */
  if (*stream_config != 0) {
    return FTL_NON_ZERO_POINTER;
  }

  *stream_config = malloc(sizeof(ftl_stream_configuration_t));
  /* eeek, malloc() should almost never fail in real world conditions */
  if (*stream_config == 0) {
    return FTL_MALLOC_FAILURE;
  }

  /* Allocate the private struct now */
  (*stream_config)->private = malloc(sizeof(ftl_stream_configuration_private_t));
  if ((*stream_config)->private == 0) {
    free (*stream_config);
    return FTL_MALLOC_FAILURE;
  }

  /* and we're done here */
  return FTL_SUCCESS;
}
