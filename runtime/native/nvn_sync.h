/* A queue fence requires real completion, either a final glFinish now or the
 * still-valid result of an earlier final glFinish on the same context.
 * A never-fenced object is invalid for a CPU wait (FAILED, not completion).
 */
#include "nvn_async_queue.h"
struct nvn_sync {uint64_t object,generation,completed,ticket;unsigned live,signaled;};
static struct nvn_sync nvn_syncs[256];
static struct nvn_sync *nvn_sync_find(uint64_t object) {
    for(unsigned i=0;i<256;i++)if(nvn_syncs[i].live&&nvn_syncs[i].object==object)return &nvn_syncs[i];
    stop("NVN_SYNC_INVALID",nvn_caller,object,24);return NULL;
}
static void nvn_async_sync_reset_locked(struct nvn_sync *s) {
    if(!s || !nvn_async_enabled())return;
    if(s->generation==UINT64_MAX)stop("NVN_SYNC_GENERATION_WRAP",nvn_caller,s->object,24);
    nvn_async_release_locked(s->ticket);s->ticket=0;
}
static unsigned nvn_async_sync_wait_locked(struct nvn_sync *s,EGLTimeKHR timeout) {
    if(s->signaled)return 1;
    if(!s->ticket)return 0;
    if(nvn_async_early_enabled()) {
        nvn_async_early_settle_locked();
        nvn_async_release_locked(s->ticket);s->ticket=0;s->completed=s->generation;s->signaled=1;
        nvn_async_early.early_signals++;return 1;
    }
    if(!nvn_async_wait_locked(s->ticket,timeout))return 0;
    nvn_async_release_locked(s->ticket);s->ticket=0;s->completed=s->generation;s->signaled=1;return 1;
}
static unsigned nvn_sync_dispatch(unsigned id,struct nvn_frame *f,uint64_t *result) {
    uint64_t *a=f->args;*result=0;
    if(id==NI_nvnSyncInitialize) {
        nvn_pointer(a[0],8,PROT_READ|PROT_WRITE);
        if(!nvn_device.live || a[1]!=nvn_device.address)stop("NVN_SYNC_DEVICE",nvn_caller,a[1],24);
        unsigned slot=256;
        for(unsigned i=0;i<256;i++) {
            if(nvn_syncs[i].live && nvn_syncs[i].object==a[0])stop("NVN_SYNC_ALREADY_LIVE",nvn_caller,a[0],24);
            if(!nvn_syncs[i].live&&slot==256)slot=i;
        }
        if(slot==256)stop("NVN_SYNC_CAPACITY",nvn_caller,slot,21);
        nvn_syncs[slot]=(struct nvn_sync){.object=a[0],.live=1};*result=1;return 1;
    }
    if(id==NI_nvnSyncWait) {
        struct nvn_sync *s=nvn_sync_find(a[0]);
        /* Original Zero SDK: Initialize 405060 writes type=0; Wait 405470
         * initializes result=3 and jump-table a14358[0] returns it at 4056c0.
         * This holds even with infinite timeout; no fence was ever submitted.
         * Zero deliberately does this on its first frame and handles the result. */
        if(!s->generation && !s->signaled && !s->ticket){*result=3;return 1;}
    }
    if(nvn_async_frame_enabled()) {
        if(id==NI_nvnQueueFenceSync){struct nvn_sync *s=nvn_sync_find(a[1]);nvn_queue_find(a[0]);s->generation++;s->completed=s->generation;s->signaled=1;return 1;}
        if(id==NI_nvnQueueWaitSync){nvn_queue_find(a[0]);nvn_sync_find(a[1]);*result=1;return 1;}
        if(id==NI_nvnSyncWait){struct nvn_sync *s=nvn_sync_find(a[0]);*result=s->signaled?0:2;return 1;}
        if(id==NI_nvnSyncFinalize){struct nvn_sync *s=nvn_sync_find(a[0]);s->live=0;return 1;}
        if(id==NI_nvnSyncSetDebugLabel){nvn_sync_find(a[0]);nvn_debug_label(a[1]);return 1;}
    }
    if(id==NI_nvnQueueFenceSync) {
        struct nvn_queue *q=nvn_queue_find(a[0]);struct nvn_sync *s=nvn_sync_find(a[1]);
        if(a[2]>1 || a[3]&~1U)stop("NVN_SYNC_FLAGS_REQUIRED",nvn_caller,a[3],20);
        if(nvn_async_enabled()) {
            pthread_mutex_lock(&nvn_gl_completion.mutex);nvn_async_sync_reset_locked(s);
            s->generation++;s->signaled=0;s->ticket=nvn_async_fence_queue_locked(q);
            if(!s->ticket){s->completed=s->generation;s->signaled=1;}
            pthread_mutex_unlock(&nvn_gl_completion.mutex);return 1;
        }
        s->generation++;s->signaled=0;nvn_queue_complete(q,1);
        s->completed=s->generation;s->signaled=1;return 1;
    }
    if(id==NI_nvnQueueWaitSync) {
        nvn_queue_find(a[0]);struct nvn_sync *s=nvn_sync_find(a[1]);
        if(nvn_async_enabled()) {
            pthread_mutex_lock(&nvn_gl_completion.mutex);
            if(!nvn_async_sync_wait_locked(s,UINT64_C(2000000000)))stop("NVN_QUEUE_UNSIGNALED_WAIT_REQUIRED",nvn_caller,a[1],20);
            pthread_mutex_unlock(&nvn_gl_completion.mutex);*result=1;return 1;
        }
        if(!s->signaled || s->completed!=s->generation)stop("NVN_QUEUE_UNSIGNALED_WAIT_REQUIRED",nvn_caller,a[1],20);
        *result=1;return 1; /* this dependency has actually completed */
    }
    if(id==NI_nvnSyncWait || id==NI_nvnSyncFinalize || id==NI_nvnSyncSetDebugLabel) {
        struct nvn_sync *s=nvn_sync_find(a[0]);
        if(nvn_async_enabled() && (id==NI_nvnSyncWait||id==NI_nvnSyncFinalize)) {
            pthread_mutex_lock(&nvn_gl_completion.mutex);
            if(id==NI_nvnSyncFinalize){nvn_async_release_locked(s->ticket);s->ticket=0;s->live=0;}
            else {
                unsigned already=s->signaled;
                if(!s->ticket&&!already&&a[1])stop("NVN_SYNC_UNSIGNALED_WAIT_REQUIRED",nvn_caller,a[0],20);
                unsigned signaled=nvn_async_sync_wait_locked(s,a[1]);*result=signaled?(already?0:1):2;
            }
            pthread_mutex_unlock(&nvn_gl_completion.mutex);return 1;
        }
        if(id==NI_nvnSyncSetDebugLabel)nvn_debug_label(a[1]);
        if(id==NI_nvnSyncFinalize)s->live=0;
        if(id==NI_nvnSyncWait) {
            if(!s->signaled && a[1])stop("NVN_SYNC_UNSIGNALED_WAIT_REQUIRED",nvn_caller,a[0],20);
            *result=s->signaled?0:2;
        }
        return 1;
    }
    return 0;
}
