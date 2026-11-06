/* SPDX-License-Identifier: MIT
 * I3DL2 mono adapter derived from Ryujinx.Audio's Reverb3d DSP:
 * Copyright (c) Ryujinx Team and Contributors. See vendor/ryujinx-audio/NOTICE.md.
 * Port-local C implementation. This is an effect, not a replacement soundtrack.
 * The original client's channel routing, status and parameters remain in charge.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
struct zero_delay3d {float *data;unsigned rate,max,count,read,write;};
struct zero_reverb_parameter {
    uint8_t input[6],output[6];uint16_t max_channels,channels;uint32_t reserved,rate;
    float room_hf,hf_reference,decay_time,hf_ratio,room_gain,reflections_gain,reverb_gain,
          diffusion,reflection_delay,reverb_delay,density,dry_gain;
    uint8_t status;
};
_Static_assert(offsetof(struct zero_reverb_parameter,status)==72,"reverb parameter ABI");
struct zero_reverb {
    struct zero_delay3d fdn[4],decay1[4],decay2[4],pre;
    float direct[4],current[4],output[4],feedback[4],previous_pre,previous_gain,target_gain;
    float early_gain,late_gain,decay,dry;unsigned early[20],reflection;
};
static const float zero_fdn_min[4]={5,6,13,14},zero_fdn_max[4]={45.704f,82.782f,149.94f,271.58f};
static const float zero_decay1_ms[4]={17,13,9,7},zero_decay2_ms[4]={19,11,10,6};
static const float zero_early_time[20]={.017136f,.059154f,.16173f,.39019f,.42526f,.45541f,.68974f,.74591f,.83384f,.8595f,0,.075024f,.16879f,.2999f,.33744f,.3719f,.59901f,.71674f,.81786f,.85166f};
static const float zero_early_gain[20]={.67096f,.61027f,1,.35680f,.68361f,.65978f,.51939f,.24712f,.45945f,.45021f,.64196f,.54879f,.92925f,.38270f,.72867f,.69794f,.5464f,.24563f,.45214f,.44042f};
static unsigned zero_delay_samples(unsigned rate,float ms) {return (unsigned)nearbyintf(rate*ms);}
static void zero_delay_set(struct zero_delay3d *d,float ms) {
    unsigned count=zero_delay_samples(d->rate,ms);
    if(count<=d->max){d->count=count;d->write=(d->read+count)%(d->max+1);}
}
static int zero_delay_init(struct zero_delay3d *d,unsigned rate,float ms) {
    *d=(struct zero_delay3d){.rate=rate,.max=zero_delay_samples(rate,ms)};
    d->data=calloc(d->max+1,sizeof(float));zero_delay_set(d,ms);return d->data!=NULL;
}
static float zero_delay_update(struct zero_delay3d *d,float value) {
    d->data[d->write++]=value;float result=d->data[d->read++];
    /* Keep the upstream DSP ring's max+1 allocation and max wrap semantics. */
    if(d->write>=d->max)d->write=0;
    if(d->read>=d->max)d->read=0;
    return result;
}
static float zero_delay_tap(const struct zero_delay3d *d,unsigned samples) {
    int index=(int)d->write-(int)samples-1;if(index<0)index+=d->max+1;
    return d->data[index];
}
static float zero_decay_update(struct zero_delay3d *d,float value,float decay) {
    float processed=value-decay*d->data[d->read];return zero_delay_update(d,processed)+processed*decay;
}
static float zero_reverb_pow10(float x) {return x>=0?1:x<=-5.3f?0:powf(10,x);}
static float zero_reverb_cos(float x) {return cosf(x*3.14159265358979323846f/180.0f);}
static float zero_reverb_sin(float x) {return sinf(x*3.14159265358979323846f/180.0f);}
static int zero_reverb_valid(const struct zero_reverb_parameter *p) {
    if(p->channels<1 || p->channels>2 || p->max_channels!=p->channels || p->rate!=48000 || p->status>3)return 0;
    const float values[]={p->room_hf,p->hf_reference,p->decay_time,p->hf_ratio,p->room_gain,
        p->reflections_gain,p->reverb_gain,p->diffusion,p->reflection_delay,p->reverb_delay,p->density,p->dry_gain};
    for(unsigned i=0;i<12;i++)if(!isfinite(values[i]))return 0;
    return p->room_hf<=0 && p->hf_reference>0 && p->hf_reference<24000 && p->decay_time>0 &&
        p->hf_ratio>0 && p->diffusion>=0 && p->diffusion<=100 && p->density>=0 && p->density<=100 &&
        p->reflection_delay>=0 && p->reverb_delay>=0 && p->reflection_delay+p->reverb_delay<=.4f;
}
static void zero_reverb_configure(struct zero_reverb *s,const struct zero_reverb_parameter *p) {
    unsigned rate=p->rate/1000;memset(s->feedback,0,sizeof(s->feedback));s->previous_pre=0;s->dry=p->dry_gain;
    s->early_gain=zero_reverb_pow10(fminf(p->room_gain+p->reflections_gain,5000)/2000);
    s->late_gain=zero_reverb_pow10(fminf(p->room_gain+p->reverb_gain,5000)/2000);
    float hf=zero_reverb_pow10(p->room_hf/2000);
    if(hf<1) {
        float a=1-hf,b=2-(2*hf)*zero_reverb_cos(256*p->hf_reference/p->rate);
        float c=sqrtf(b*b-4*(1-hf)*(1-hf));s->previous_gain=(b-c)/(2*a);s->target_gain=1-s->previous_gain;
    } else {s->previous_gain=0;s->target_gain=1;}
    s->reflection=zero_delay_samples(rate,1000*(p->reflection_delay+p->reverb_delay));s->decay=.6f*p->diffusion*.01f;
    for(unsigned i=0;i<4;i++) {
        zero_delay_set(&s->fdn[i],zero_fdn_min[i]+p->density/100*(zero_fdn_max[i]-zero_fdn_min[i]));
        unsigned samples=s->fdn[i].count+s->decay1[i].count+s->decay2[i].count;
        float a=(-60.0f*samples)/(p->decay_time*p->rate),b=a/p->hf_ratio;
        float c=zero_reverb_cos(128*.5f*p->hf_reference/p->rate)/zero_reverb_sin(128*.5f*p->hf_reference/p->rate);
        float d=zero_reverb_pow10((b-a)/40),e=zero_reverb_pow10((b+a)/40)*.7071f;
        s->direct[i]=e*(d*c+1)/(c+d);s->current[i]=e*(1-d*c)/(c+d);s->output[i]=(c-d)/(c+d);
    }
    for(unsigned i=0;i<20;i++) {
        unsigned samples=zero_delay_samples(rate,p->reflection_delay*1000+zero_early_time[i]*1000*(p->reverb_delay*.9998f+.02f));
        s->early[i]=samples<s->pre.max?samples:s->pre.max;
    }
}
static void zero_reverb_free(struct zero_reverb *s) {
    if(!s)return;
    for(unsigned i=0;i<4;i++){free(s->fdn[i].data);free(s->decay1[i].data);free(s->decay2[i].data);}
    free(s->pre.data);free(s);
}
static struct zero_reverb *zero_reverb_new(const struct zero_reverb_parameter *p) {
    if(!zero_reverb_valid(p))return NULL;
    struct zero_reverb *s=calloc(1,sizeof(*s));if(!s)return NULL;
    unsigned rate=p->rate/1000;
    for(unsigned i=0;i<4;i++)if(!zero_delay_init(&s->fdn[i],rate,zero_fdn_max[i]) ||
        !zero_delay_init(&s->decay1[i],rate,zero_decay1_ms[i]) || !zero_delay_init(&s->decay2[i],rate,zero_decay2_ms[i])) {
        zero_reverb_free(s);return NULL;
    }
    if(!zero_delay_init(&s->pre,rate,400)){zero_reverb_free(s);return NULL;}
    zero_reverb_configure(s,p);return s;
}
static inline float zero_reverb_step(struct zero_reverb *s,float in) {
    float tap=zero_delay_tap(&s->pre,s->reflection),early=0,feedback[4],values[4];
    for(unsigned i=0;i<20;i++)early+=zero_delay_tap(&s->pre,s->early[i])*zero_early_gain[i];
    early*=s->early_gain;s->previous_pre=in*s->target_gain+s->previous_pre*s->previous_gain;
    zero_delay_update(&s->pre,s->previous_pre);
    for(unsigned i=0;i<4;i++) {
        float value=s->fdn[i].data[s->fdn[i].read];feedback[i]=value*s->direct[i]+s->feedback[i];
        s->feedback[i]=value*s->current[i]+feedback[i]*s->output[i];
    }
    float matrix[4]={feedback[2]+feedback[1],-feedback[0]-feedback[3],feedback[0]-feedback[3],feedback[1]-feedback[2]};
    for(unsigned i=0;i<4;i++) {
        float value=zero_decay_update(&s->decay1[i],tap*s->late_gain+matrix[i],s->decay);
        values[i]=zero_decay_update(&s->decay2[i],value,s->decay*-.9f);zero_delay_update(&s->fdn[i],values[i]);
    }
    return early+values[0]+values[1];
}
static void zero_reverb_process(struct zero_reverb *s,const float *input,float *output,unsigned count) {
    for(unsigned n=0;n<count;n++){float in=input[n];output[n]=zero_reverb_step(s,in)+in*s->dry;}
}
/* Animal Well: stereo I3DL2 with one shared late-reverb state fed by the mono sum; dry stays per channel. */
static void zero_reverb_process_stereo(struct zero_reverb *s,const float *in0,const float *in1,float *out0,float *out1,unsigned count) {
    for(unsigned n=0;n<count;n++) {
        float a=in0[n],b=in1[n],wet=zero_reverb_step(s,(a+b)*.5f);
        out0[n]=wet+a*s->dry;out1[n]=wet+b*s->dry;
    }
}
