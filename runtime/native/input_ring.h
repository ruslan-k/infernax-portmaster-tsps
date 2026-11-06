/* SDK 4.5 Npad ABI: base 0x9a00, stride 0x5000; seven 0x350 rings.
 * The guest SDK reads count/tail with acquire, then checks each slot sequence.
 * Keep one spare slot (16 readable, 17 storage) and publish tail last.
 * Layout corroborated against libnx dbcc1bea and this game's original SDK.
 */
#include <stdint.h>
struct hid_sample {uint64_t number,buttons;int32_t lx,ly,rx,ry;uint32_t attributes,reserved;};
struct hid_slot {uint64_t sequence;struct hid_sample sample;};
struct hid_ring {uint64_t unused,capacity,tail,count;struct hid_slot slots[17];};
_Static_assert(sizeof(struct hid_sample)==0x28,"Npad sample ABI");
_Static_assert(sizeof(struct hid_ring)==0x350,"Npad ring ABI");
static inline void hid_ring_publish(struct hid_ring *ring,struct hid_sample sample) {
    uint64_t next=(ring->tail+1)%17;
    struct hid_slot *slot=&ring->slots[next];
    __atomic_store_n(&slot->sequence,sample.number,__ATOMIC_RELEASE);
    slot->sample=sample;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    ring->capacity=17;
    __atomic_store_n(&ring->tail,next,__ATOMIC_RELEASE);
    if(ring->count<16)__atomic_store_n(&ring->count,ring->count+1,__ATOMIC_RELEASE);
}

/* TouchScreen shared+0x400: header32 + 17*(sequence8 + state0x290).
 * Publish actual absence of contacts while using the controller provider.
 * ABI: libnx HidTouchScreenState and HidTouchScreenLifo. */
struct zero_touch_sample {uint64_t number;uint32_t count,reserved;uint8_t contacts[16][0x28];};
struct zero_touch_slot {uint64_t sequence;struct zero_touch_sample sample;};
struct zero_touch_ring {uint64_t unused,capacity,tail,count;struct zero_touch_slot slots[17];};
_Static_assert(sizeof(struct zero_touch_ring)==0x2c38,"TouchScreen ring ABI");
static inline void zero_touch_publish(void *address,uint64_t number) {
    struct zero_touch_ring *ring=address;uint64_t next=(ring->tail+1)%17;
    struct zero_touch_slot *slot=&ring->slots[next];
    __atomic_store_n(&slot->sequence,number,__ATOMIC_RELEASE);
    slot->sample=(struct zero_touch_sample){.number=number};
    __atomic_thread_fence(__ATOMIC_RELEASE);ring->capacity=17;
    __atomic_store_n(&ring->tail,next,__ATOMIC_RELEASE);
    if(ring->count<16)__atomic_store_n(&ring->count,ring->count+1,__ATOMIC_RELEASE);
}
