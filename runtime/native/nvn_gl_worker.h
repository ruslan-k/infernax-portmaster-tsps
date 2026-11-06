/* GL worker thread (opt-in COTM_GL_THREAD=1). Every dispatcher that can
 * touch EGL/GLES runs on this thread. SYNC jobs block the posting guest
 * thread (identical semantics); FRAME/PRESENT jobs (COTM_ASYNC_FRAME) run
 * later from a private snapshot. The worker owns the single GLES context. */
enum {NGJ_SYNC,NGJ_FRAME,NGJ_PRESENT};
struct nvn_gl_job {unsigned type,id,handled,done;struct nvn_frame *f;uint64_t result,caller;struct nvn_snapshot *snapshot;uint64_t args[3];};
#define NVN_GL_RING 32U
static struct {struct nvn_gl_job *ring[NVN_GL_RING];unsigned head,count;uint64_t frames,presents,dropped;} nvn_gl_queue;
static void nvn_async_frame_execute(struct nvn_snapshot *s);
static void *nvn_gl_worker_main(void *arg) {
    (void)arg;
    {   /* Never share the guest main core (CPU0) or the audio mixer core (CPU2). */
        cpu_set_t set;CPU_ZERO(&set);CPU_SET(1,&set);CPU_SET(3,&set);
        int rc=pthread_setaffinity_np(pthread_self(),sizeof(set),&set);
        printf("NVN_GL_WORKER_AFFINITY cpus=1,3 rc=%d\n",rc);
    }
    pthread_mutex_lock(&nvn_gl_worker.mutex);
    nvn_gl_worker.thread=pthread_self();nvn_gl_worker.started=1;pthread_cond_broadcast(&nvn_gl_worker.finished);
    for(;;) {
        while(!nvn_gl_queue.count)pthread_cond_wait(&nvn_gl_worker.post,&nvn_gl_worker.mutex);
        struct nvn_gl_job *job=nvn_gl_queue.ring[nvn_gl_queue.head];
        nvn_gl_queue.head=(nvn_gl_queue.head+1)%NVN_GL_RING;nvn_gl_queue.count--;
        if(job->type==NGJ_FRAME)job->snapshot->started=1;
        pthread_mutex_unlock(&nvn_gl_worker.mutex);
        nvn_caller=job->caller;nvn_bridge_depth=1;
        if(job->type==NGJ_SYNC) {
            if(job->id==NI_nvnQueueFinalize||job->id==NI_nvnWindowFinalize||job->id==NI_nvnDeviceFinalize)
                nvn_gpu_frame_gate_wait(NVN_GFG_TEARDOWN);
            job->handled=nvn_gl_dispatch_body(job->id,job->f,&job->result);
        }
        else if(job->type==NGJ_FRAME){nvn_gpu_frame_gate_wait(NVN_GFG_FRAME);nvn_async_frame_execute(job->snapshot);}
        else {
            struct nvn_frame f={.args={job->args[0],job->args[1],job->args[2]},.lr=job->caller};uint64_t r=0;
            nvn_present_from_job=1;nvn_present_dispatch(NI_nvnQueuePresentTexture,&f,&r);nvn_present_from_job=0;
        }
        nvn_bridge_depth=0;
        pthread_mutex_lock(&nvn_gl_worker.mutex);
        if(job->type==NGJ_SYNC){job->done=1;}
        else {if(job->type==NGJ_FRAME)job->snapshot->live=0;free(job);}
        nvn_gl_worker.jobs++;pthread_cond_broadcast(&nvn_gl_worker.finished);
    }
    return NULL;
}
static void nvn_gl_worker_start_locked(void) {
    if(nvn_gl_worker.started)return;
    pthread_attr_t attributes;pthread_attr_init(&attributes);pthread_attr_setstacksize(&attributes,4U<<20);
    pthread_t thread;
    if(pthread_create(&thread,&attributes,nvn_gl_worker_main,NULL))stop("NVN_GL_WORKER_CREATE",nvn_caller,errno,21);
    pthread_attr_destroy(&attributes);
    while(!nvn_gl_worker.started)pthread_cond_wait(&nvn_gl_worker.finished,&nvn_gl_worker.mutex);
    printf("NVN_GL_WORKER_EXPERIMENT enabled=1 owner=gl-worker jobs=sync-frame-present ring=%u\n",NVN_GL_RING);
}
static void nvn_gl_queue_push_locked(struct nvn_gl_job *job) {
    while(nvn_gl_queue.count==NVN_GL_RING)pthread_cond_wait(&nvn_gl_worker.finished,&nvn_gl_worker.mutex);
    nvn_gl_queue.ring[(nvn_gl_queue.head+nvn_gl_queue.count)%NVN_GL_RING]=job;nvn_gl_queue.count++;
    pthread_cond_broadcast(&nvn_gl_worker.post);
}
static uint64_t nvn_gl_sync_calls[NI_COUNT];
static unsigned nvn_gl_worker_call(unsigned id,struct nvn_frame *f,uint64_t *result) {
    struct nvn_gl_job job={.type=NGJ_SYNC,.f=f,.id=id,.caller=nvn_caller};
    nvn_gl_sync_calls[id]++;
    pthread_mutex_lock(&nvn_gl_worker.mutex);
    nvn_gl_worker_start_locked();
    nvn_gl_queue_push_locked(&job);
    while(!job.done)pthread_cond_wait(&nvn_gl_worker.finished,&nvn_gl_worker.mutex);
    pthread_mutex_unlock(&nvn_gl_worker.mutex);
    *result=job.result;return job.handled;
}
/* Post a frame; a not-yet-started pending frame (and its present) is dropped
 * so the worker never falls more than one frame behind the game. */
static void nvn_gl_post_frame(struct nvn_snapshot *s,uint64_t caller) {
    struct nvn_gl_job *job=calloc(1,sizeof(*job));if(!job)stop("NVN_GL_JOB_MEMORY",caller,0,21);
    job->type=NGJ_FRAME;job->snapshot=s;job->caller=caller;
    pthread_mutex_lock(&nvn_gl_worker.mutex);
    nvn_gl_worker_start_locked();
    unsigned kept=0,dropping=0;
    for(unsigned i=0;i<nvn_gl_queue.count;i++) {
        struct nvn_gl_job *j=nvn_gl_queue.ring[(nvn_gl_queue.head+i)%NVN_GL_RING];
        /* Drop only a frame that has not started, together with ITS present;
         * a present whose frame is already executing must still run. */
        if(j->type==NGJ_FRAME){j->snapshot->live=0;nvn_gl_queue.dropped++;free(j);dropping=1;continue;}
        if(j->type==NGJ_PRESENT&&dropping){free(j);dropping=0;continue;}
        nvn_gl_queue.ring[(nvn_gl_queue.head+kept)%NVN_GL_RING]=j;kept++;
    }
    nvn_gl_queue.count=kept;
    nvn_gl_queue_push_locked(job);nvn_gl_queue.frames++;
    pthread_mutex_unlock(&nvn_gl_worker.mutex);
}
static void nvn_gl_post_present(uint64_t queue,uint64_t window,uint64_t index,uint64_t caller) {
    struct nvn_gl_job *job=calloc(1,sizeof(*job));if(!job)stop("NVN_GL_JOB_MEMORY",caller,0,21);
    job->type=NGJ_PRESENT;job->args[0]=queue;job->args[1]=window;job->args[2]=index;job->caller=caller;
    pthread_mutex_lock(&nvn_gl_worker.mutex);
    nvn_gl_worker_start_locked();
    nvn_gl_queue_push_locked(job);nvn_gl_queue.presents++;
    pthread_mutex_unlock(&nvn_gl_worker.mutex);
}
static void nvn_gl_drain(void) {
    if(!nvn_gl_worker.started||nvn_gl_worker_is_self())return;
    pthread_mutex_lock(&nvn_gl_worker.mutex);
    uint64_t target=nvn_gl_worker.jobs+nvn_gl_queue.count;
    while(nvn_gl_worker.jobs<target)pthread_cond_wait(&nvn_gl_worker.finished,&nvn_gl_worker.mutex);
    pthread_mutex_unlock(&nvn_gl_worker.mutex);
}
static unsigned nvn_gl_route(unsigned id,struct nvn_frame *f,uint64_t *result) {
    if(!nvn_gl_worker_enabled())return nvn_gl_dispatch_body(id,f,result);
    if(nvn_gl_worker_is_self())return nvn_gl_dispatch_body(id,f,result);
    if(nvn_async_frame_enabled()&&nvn_async_frame_local(id,f,result))return 1;
    return nvn_gl_worker_call(id,f,result);
}
static uint64_t nvn_gl_frames_posted(void){return nvn_gl_queue.frames;}
static uint64_t nvn_gl_frames_dropped(void){return nvn_gl_queue.dropped;}
static void nvn_gl_sync_report(void) {
    for(unsigned rank=0;rank<12;rank++) {
        unsigned best=0;for(unsigned i=1;i<NI_COUNT;i++)if(nvn_gl_sync_calls[i]>nvn_gl_sync_calls[best])best=i;
        if(!nvn_gl_sync_calls[best])break;
        printf("NVN_GL_SYNC_CALLS op=%s calls=%"PRIu64" scope=since-last-report\n",nvn_functions[best].name,nvn_gl_sync_calls[best]);nvn_gl_sync_calls[best]=0;
    }
}
static unsigned nvn_gl_frame_pending(void) {
    unsigned pending=0;
    pthread_mutex_lock(&nvn_gl_worker.mutex);
    for(unsigned i=0;i<nvn_gl_queue.count;i++){struct nvn_gl_job *j=nvn_gl_queue.ring[(nvn_gl_queue.head+i)%NVN_GL_RING];if(j->type==NGJ_FRAME){pending=1;break;}}
    pthread_mutex_unlock(&nvn_gl_worker.mutex);
    return pending;
}
