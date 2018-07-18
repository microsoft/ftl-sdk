/**
* \file threads.c - Posix Threads Abstractions
*
* Copyright (c) 2015 Stefan Slivinski
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

#include "threads.h"

pthread_mutexattr_t ftl_default_mutexattr;

int os_create_thread(OS_THREAD_HANDLE *handle, OS_THREAD_ATTRIBS *attibs, OS_THREAD_START_ROUTINE func, void *args) {

  return pthread_create(handle, NULL, func, args);
}

int os_destroy_thread(OS_THREAD_HANDLE handle) {
  return 0;
}

int os_wait_thread(OS_THREAD_HANDLE handle) {
  return pthread_join(handle, NULL);
}

int os_init() {
    pthread_mutexattr_init(&ftl_default_mutexattr);
    // Set pthread mutexes to recursive to mirror Windows mutex behavior
    return pthread_mutexattr_settype(&ftl_default_mutexattr, PTHREAD_MUTEX_RECURSIVE);
}

int os_init_mutex(OS_MUTEX *mutex) {
  return pthread_mutex_init(mutex, &ftl_default_mutexattr);
}

int os_lock_mutex(OS_MUTEX *mutex) {
  return pthread_mutex_lock(mutex);
}

int os_trylock_mutex(OS_MUTEX *mutex) {
  int ret = pthread_mutex_trylock(mutex);
  return (ret == 0) ? 1 : 0;
}

int os_unlock_mutex(OS_MUTEX *mutex) {
  return pthread_mutex_unlock(mutex);
}

int os_delete_mutex(OS_MUTEX *mutex) {
  return 0;
}

int os_semaphore_create(OS_SEMAPHORE *sem, const char *name, int oflag, unsigned int value) {

  int retval = 0;

  if (pthread_mutex_init(&sem->mutex, NULL))
    return -2;

  if (pthread_cond_init(&sem->cond, NULL)) {
    pthread_mutex_destroy(&sem->mutex);
    return -3;
  }

  sem->value = value;

  return 0;
}

int os_semaphore_pend(OS_SEMAPHORE *sem, int ms_timeout) {

  int retval = 0;

  if (pthread_mutex_lock(&sem->mutex))
    return -1;

  while (1) {
    if (sem->value > 0) {
      sem->value--;
      break;
    } else {
      if (ms_timeout < 0) {
        if (pthread_cond_wait(&sem->cond, &sem->mutex)) {
          retval = -2;
          break;
        }
      } else {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts)) {
          retval = -3;
          break;
        }
        timespec_add_ms(&ts, ms_timeout);
        if (pthread_cond_timedwait(&sem->cond, &sem->mutex, &ts)) {
          retval = -4;
          break;
        }
      }
      continue;
    }
  }

  pthread_mutex_unlock(&sem->mutex);
  return retval;
}

int os_semaphore_post(OS_SEMAPHORE *sem) {
  int retval = 0;

  if (pthread_mutex_lock(&sem->mutex))
    return -1;

  sem->value++;
  if (pthread_cond_broadcast(&sem->cond))
    retval = -2;

  pthread_mutex_unlock(&sem->mutex);
  return retval;
}

int os_semaphore_delete(OS_SEMAPHORE *sem) {
  pthread_mutex_destroy(&sem->mutex);
  pthread_cond_destroy(&sem->cond);
  return 0;
}

void sleep_ms(int ms)
{
    usleep(ms * 1000);
}


