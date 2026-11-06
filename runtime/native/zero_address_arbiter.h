/* Horizon 4+ address arbiter. Caller owns kernel_mutex; pthread waits release
 * it atomically. A memory write alone never completes a registered wait.
 * Semantics checked against Atmosphere KAddressArbiter and SDK 6.5 events.
 */
static struct {uint64_t address,order;} zero_address_waiters[MAX_THREADS];
static uint64_t zero_address_order;
static uint32_t zero_address_wait(uint64_t address,unsigned type,int32_t value,int64_t timeout,uint64_t pc) {
    uint32_t *word=mutex_address(address,pc);
    if(type>2)stop("ZERO_ADDRESS_WAIT_TYPE",pc,type,20);
    uint32_t observed=__atomic_load_n(word,__ATOMIC_ACQUIRE);
    for(;;) {
        if(type==2?(int32_t)observed!=value:(int32_t)observed>=value)return 0xfa01;
        if(type!=1 || __atomic_compare_exchange_n(word,&observed,observed-1U,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))break;
    }
    if(!timeout)return 0xea01;
    unsigned slot=guest_thread_index;
    if(slot>=nthreads || zero_address_waiters[slot].address || !++zero_address_order)
        stop("ZERO_ADDRESS_WAIT_STATE",pc,slot,24);
    zero_address_waiters[slot].address=address;zero_address_waiters[slot].order=zero_address_order;
    struct timespec deadline=timeout_deadline(timeout);uint32_t result=0;
    pthread_cond_broadcast(&kernel_changed);
    while(zero_address_waiters[slot].address) {
        int rc=kernel_wait(timeout,&deadline);
        if(rc==ETIMEDOUT && zero_address_waiters[slot].address){result=0xea01;break;}
        if(rc && rc!=ETIMEDOUT)stop("ZERO_ADDRESS_HOST_WAIT",pc,rc,21);
    }
    zero_address_waiters[slot].address=0;return result;
}
static uint32_t zero_address_signal(uint64_t address,unsigned type,int32_t value,int32_t count,uint64_t pc) {
    uint32_t *word=mutex_address(address,pc);
    if(type>2)stop("ZERO_ADDRESS_SIGNAL_TYPE",pc,type,20);
    unsigned waiting=0;
    for(unsigned i=0;i<nthreads;i++)waiting+=zero_address_waiters[i].address==address;
    if(type) {
        uint32_t expected=(uint32_t)value,next=expected+1U;
        if(type==2 && waiting)next=expected-((count<=0 || waiting<=(unsigned)count)?1U:0U);
        if(!__atomic_compare_exchange_n(word,&expected,next,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0xfa01;
    }
    unsigned limit=count<=0?waiting:(unsigned)count;
    for(unsigned n=0;n<limit && n<waiting;n++) {
        unsigned selected=MAX_THREADS;
        for(unsigned i=0;i<nthreads;i++)if(zero_address_waiters[i].address==address &&
           (selected==MAX_THREADS || threads[i].priority<threads[selected].priority ||
            (threads[i].priority==threads[selected].priority && zero_address_waiters[i].order<zero_address_waiters[selected].order)))selected=i;
        if(selected==MAX_THREADS)stop("ZERO_ADDRESS_SIGNAL_STATE",pc,n,24);
        zero_address_waiters[selected].address=0;
    }
    pthread_cond_broadcast(&kernel_changed);return 0;
}
