/* Private PCM ownership until real host consumption; no guest pointers retained. */
#define AOUT_SLOTS 64U
#define AOUT_BYTES_LIMIT (8U<<20)
struct aout_buffer {uint64_t tag,end;uint8_t *pcm;uint32_t bytes,sent;};
struct aout_queue {struct aout_buffer buffers[AOUT_SLOTS];unsigned head,count;uint32_t bytes;uint64_t submitted,consumed;};
static int aout_push(struct aout_queue *q,uint64_t tag,const void *pcm,uint32_t bytes) {
    if(!tag || !bytes || bytes%4 || q->count==AOUT_SLOTS || bytes>AOUT_BYTES_LIMIT-q->bytes ||
       q->submitted>UINT64_MAX-bytes/4)return 0;
    for(unsigned i=0;i<q->count;i++)if(q->buffers[(q->head+i)%AOUT_SLOTS].tag==tag)return 0;
    uint8_t *copy=malloc(bytes);if(!copy)return 0;memcpy(copy,pcm,bytes);
    q->submitted+=bytes/4;q->bytes+=bytes;
    q->buffers[(q->head+q->count++)%AOUT_SLOTS]=(struct aout_buffer){tag,q->submitted,copy,bytes,0};
    return 1;
}
static unsigned aout_released(const struct aout_queue *q) {
    unsigned count=0;
    while(count<q->count && q->buffers[(q->head+count)%AOUT_SLOTS].end<=q->consumed)count++;
    return count;
}
static uint64_t aout_pop(struct aout_queue *q) {
    if(!aout_released(q))return 0;
    struct aout_buffer *b=&q->buffers[q->head];uint64_t tag=b->tag;
    q->bytes-=b->bytes;free(b->pcm);memset(b,0,sizeof(*b));q->head=(q->head+1)%AOUT_SLOTS;q->count--;
    return tag;
}
static uint32_t aout_release(struct aout_queue *q,uint64_t *tags,size_t capacity) {
    /* SDK GetReleasedAudioOutBuffer reads the output tag even when count=0. */
    memset(tags,0,capacity*sizeof(*tags));uint32_t count=0;
    while(count<capacity && aout_released(q))tags[count++]=aout_pop(q);
    return count;
}
static void aout_clear(struct aout_queue *q) {
    for(unsigned i=0;i<q->count;i++)free(q->buffers[(q->head+i)%AOUT_SLOTS].pcm);
    memset(q,0,sizeof(*q));
}
