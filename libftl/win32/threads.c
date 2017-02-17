/**
* \file threads.c - Windows Threads Abstractions
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

int os_init(){
    return 0;
}

int os_create_thread(OS_THREAD_HANDLE *handle, OS_THREAD_ATTRIBS *attibs, OS_THREAD_START_ROUTINE func, void *args) {
  HANDLE thread;
  thread = CreateThread(NULL, 0, func, args, 0, NULL);

  if (thread == NULL) {
    return -1;
  }

  *handle = thread;

  return 0;
}

int os_destroy_thread(OS_THREAD_HANDLE handle) {
  CloseHandle(handle);

  return 0;
}

int os_wait_thread(OS_THREAD_HANDLE handle) {
  WaitForSingleObject(handle, INFINITE);

  return 0;
}

int os_init_mutex(OS_MUTEX *mutex) {

  InitializeCriticalSection(mutex);

  return 0;
}

int os_lock_mutex(OS_MUTEX *mutex) {

  EnterCriticalSection(mutex);

  return 0;
}

int os_trylock_mutex(OS_MUTEX *mutex) {

  return TryEnterCriticalSection(mutex);
}

int os_unlock_mutex(OS_MUTEX *mutex) {

  LeaveCriticalSection(mutex);

  return 0;
}

int os_delete_mutex(OS_MUTEX *mutex) {

  DeleteCriticalSection(mutex);

  return 0;
}

char tmp[1024];

int os_semaphore_create(OS_SEMAPHORE *sem, const char *name, int oflag, unsigned int value) {

  if (name == NULL) {
    return -1;
  }

  if ( (*sem = CreateSemaphore(NULL, value, MAX_SEM_COUNT, name)) == NULL){

    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, (LPTSTR)&tmp, 1000, NULL);

    return -3;
  }

  return 0;
}

int os_semaphore_pend(OS_SEMAPHORE *sem, int ms_timeout) {

  if (WaitForSingleObject(*sem, ms_timeout) != WAIT_OBJECT_0) {
    return -1;
  }

  return 0;
}

int os_semaphore_post(OS_SEMAPHORE *sem) {
  if (ReleaseSemaphore(*sem, 1, NULL)) {
    return 0;
  }

  return -1;
}

int os_semaphore_delete(OS_SEMAPHORE *sem) {
  if (CloseHandle(*sem)) {
    return 0;
  }

  return -1;
}

void sleep_ms(int ms)
{
        Sleep(ms);
}


