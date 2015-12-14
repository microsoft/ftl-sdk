/**
 * \file ftl.h - Public Interface for the FTL SDK
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

#ifndef __FTL_H
#define __FTL_H

/*! \defgroup ftl_public Public Interfaces for libftl */

/*! \brief Return codes for ftl_init
 *  \ingroup ftl_public
 */

typedef enum {
  FTL_INIT_SUCCESS //! libftl successfully initialized
} ftl_init_status_t;

/*!
 * \ingroup ftl_public
 * \brief FTL Initialization
 *
 *
 * Before using FTL, you must call ftl_init before making any additional calls
 * in this library. ftl_init initializes any submodules that FTL depends on such
 * as libsrtp. Under normal cirmstances, this function should never fail.
 *
 * @returns FTL_INIT_SUCCESS on successful initialization. Otherwise, returns
 * ftl_init_status_t enum with the failure state.
 */
ftl_init_status_t ftl_init();

#endif // __FTL_H
