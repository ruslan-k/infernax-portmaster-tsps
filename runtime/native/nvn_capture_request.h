#ifndef MEGAMAN_NVN_CAPTURE_REQUEST_H
#define MEGAMAN_NVN_CAPTURE_REQUEST_H
#include <stdatomic.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"capture signal requires always-lock-free atomic int");
static atomic_int nvn_capture_request_pending;
static unsigned nvn_capture_request_enabled;
/* Async-signal-safe: no logging, allocation, GL or ordinary shared state. */
static void nvn_capture_request_signal(int number) {
    (void)number;
    atomic_store_explicit(&nvn_capture_request_pending,1,memory_order_relaxed);
}
static int nvn_capture_request_install(void) {
    const char *option=getenv("COTM_INPUT_DIAGNOSTIC");
    if(!option||strcmp(option,"1"))return 0;
    struct sigaction action;memset(&action,0,sizeof(action));
    action.sa_handler=nvn_capture_request_signal;action.sa_flags=SA_RESTART|SA_ONSTACK; /* 13/09: without SA_ONSTACK the frame lands on small guest stacks (SIGSEGV SI_KERNEL after svc, runs 1035/1045) */
    if(sigemptyset(&action.sa_mask)||sigaction(SIGUSR1,&action,NULL))return -1;
    nvn_capture_request_enabled=1;
    return 1;
}
/* Call only at a rendered physical-present boundary, never an omitted frame. */
static unsigned nvn_capture_request_consume(void) {
    return nvn_capture_request_enabled &&
        atomic_exchange_explicit(&nvn_capture_request_pending,0,memory_order_relaxed);
}
#endif
