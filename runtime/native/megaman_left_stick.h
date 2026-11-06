/* LC2 digital movement from the left stick. Native axes remain available.
 * State is owned by the SDL thread and reset when its controller changes. */
#ifndef MEGAMAN_LEFT_STICK_H
#define MEGAMAN_LEFT_STICK_H
#include <stdint.h>

#define MEGAMAN_STICK_ENTER 10000
#define MEGAMAN_STICK_LEAVE 8000
struct megaman_left_stick {int x,y;};

static int megaman_stick_direction(int32_t value,int previous) {
    if(value>=MEGAMAN_STICK_ENTER)return 1;
    if(value<=-MEGAMAN_STICK_ENTER)return -1;
    if(previous>0&&value>MEGAMAN_STICK_LEAVE)return 1;
    if(previous<0&&value<-MEGAMAN_STICK_LEAVE)return -1;
    return 0;
}

static uint64_t megaman_left_stick_buttons(struct megaman_left_stick *state,
        int32_t x,int32_t y,uint64_t buttons) {
    state->x=megaman_stick_direction(x,state->x);
    state->y=megaman_stick_direction(y,state->y);
    /* Npad Y is positive upward. A pressed D-pad wins on its own axis;
     * the other stick axis can still form a diagonal. Preserve every bit
     * already supplied, including simultaneous physical opposite directions. */
    if(!(buttons&UINT64_C(0x5000))) {
        if(state->x<0)buttons|=UINT64_C(0x1000);
        if(state->x>0)buttons|=UINT64_C(0x4000);
    }
    if(!(buttons&UINT64_C(0xa000))) {
        if(state->y>0)buttons|=UINT64_C(0x2000);
        if(state->y<0)buttons|=UINT64_C(0x8000);
    }
    return buttons;
}
#endif
