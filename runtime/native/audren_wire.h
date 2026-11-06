/* REV4 wire declarations, independently named from the public libnx ABI.
 * Integer pointers are guest addresses, never trusted host pointers.
 */
#include <stddef.h>
#include <math.h>
struct aud_header {uint32_t revision,behavior,pools,voices,channels,effects,mixes,sinks,performance,reserved[6],total;};
struct aud_wave {uint64_t address,size;int32_t start,end;uint8_t loop,eos,sent,pad[5];uint64_t context,context_size,reserved;};
struct aud_biquad {uint8_t enabled,pad;int16_t b[3],a[2];};
struct aud_voice_in {uint32_t id,node;uint8_t is_new,used,state,format;
    uint32_t rate,priority,order,channels;float pitch,volume;struct aud_biquad filters[2];
    uint32_t wave_count;uint16_t head,pad0;uint32_t pad1;uint64_t extra,extra_size;
    uint32_t mix,splitter;struct aud_wave wave[4];uint32_t channel_ids[6];uint8_t pad2[24];};
struct aud_channel_in {uint32_t id;float gain[24];uint8_t used,pad[11];};
struct aud_mix_in {float volume;uint32_t rate,buffers;uint8_t used,pad0[3];uint32_t id,pad1,node,pad2[2];
    float matrix[24][24];uint32_t destination,splitter,pad3;};
struct aud_sink_in {uint8_t type,used,pad0[2];uint32_t node;uint64_t pad1[3];uint8_t data[288];};
struct aud_effect_in {uint8_t type,is_new,enabled,pad;uint32_t mix;uint64_t address,size;uint32_t order,pad2;uint8_t data[160];};
_Static_assert(sizeof(struct aud_header)==64,"header ABI");
_Static_assert(sizeof(struct aud_wave)==56,"wave ABI");
_Static_assert(sizeof(struct aud_voice_in)==0x170,"voice ABI");
_Static_assert(sizeof(struct aud_channel_in)==0x70,"channel ABI");
_Static_assert(sizeof(struct aud_mix_in)==0x930,"mix ABI");
_Static_assert(sizeof(struct aud_sink_in)==0x140,"sink ABI");
_Static_assert(sizeof(struct aud_effect_in)==0xc0,"effect ABI");
