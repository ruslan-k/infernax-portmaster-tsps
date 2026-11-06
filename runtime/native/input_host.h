/* SDL controller provider; physical inputs and bounded diagnostic button presses
 * enter the same Npad ring. No game function/scene/state is patched by input.
 * SDL is owned by this normal host thread, outside guest signal handlers.
 */
#include <SDL2/SDL.h>
#include <sys/un.h>
#include <math.h>
#include "input_ring.h"
#include "input_gptk.h"
#include "input_c6.h"
#include "t2_face_position.h"
#include "megaman_virtual_pad.h"
#include "megaman_left_stick.h"
static const char *hid_input_provider(void) {
#if defined(MEGAMAN_HOST_DIAGNOSTIC) && MEGAMAN_HOST_DIAGNOSTIC
    if(megaman_virtual_pad.enabled)return "host-virtual";
#endif
    return "physical";
}
#define HID_EXTRA_PADS 3
static struct {pthread_t worker;SDL_GameController *pad;SDL_GameController *extra[HID_EXTRA_PADS];int command_fd;
    struct megaman_left_stick left_stick;
    uint64_t sequence,last_buttons,diagnostic_buttons,diagnostic_until,rumble_until;
    int32_t diagnostic_lx,diagnostic_ly;
    unsigned connected,rumble_capable,rumble_pending,rumble_done,rumble_error;
    unsigned vibration_disabled;
    unsigned face_swap,face_swap_extra[HID_EXTRA_PADS];
#if ZERO_ZX_GAME
    SDL_JoystickID instance,rumble_zero_instance;
    unsigned rumble_zero_confirmed;
    uint64_t rumble_zero_skipped;
#endif
    uint16_t rumble_low,rumble_high;
    float vibration[2][4],rumble_values[2][4],vibration_actual[2][4];} hid_host;
static uint64_t hid_now_ms(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static unsigned hid_vibration_mounted(void) {return hid_host.connected&&hid_host.rumble_capable;}
static unsigned hid_vibration_permitted(void) {return hid_vibration_mounted()&&!hid_host.vibration_disabled;}
static void hid_rumble_wait(void) {
    while(hid_host.rumble_pending)pthread_cond_wait(&kernel_changed,&kernel_mutex);
}
static void hid_vibration_actual(unsigned side,float out[4]) {
    if(!hid_vibration_permitted()||hid_now_ms()>=hid_host.rumble_until) {
        memset(hid_host.vibration_actual,0,sizeof(hid_host.vibration_actual));hid_host.rumble_until=0;
    }
    memcpy(out,hid_host.vibration_actual[side],16);
}
static void hid_rumble_request(uint64_t pc) {
    hid_rumble_wait();
    if(!hid_vibration_permitted()) {
        /* A disabled system output accepts validated commands without an
         * actuator effect. Never expose the unapplied request as actual. */
        printf("HID_RUMBLE_SUPPRESSED output=disabled mounted=%u requested=%.6f,%.6f,%.6f,%.6f produced=0\n",
            hid_vibration_mounted(),hid_host.vibration[0][0],hid_host.vibration[0][2],hid_host.vibration[1][0],hid_host.vibration[1][2]);
        memset(hid_host.vibration,0,sizeof(hid_host.vibration));
        memset(hid_host.vibration_actual,0,sizeof(hid_host.vibration_actual));hid_host.rumble_until=0;
#if ZERO_ZX_GAME
        hid_host.rumble_zero_confirmed=0;
#endif
        return;
    }
    /* SDL exposes fixed-frequency left/right motors. Preserve each side's
     * envelope using its larger requested band; HD frequency fidelity is lost. */
    hid_host.rumble_low=(uint16_t)(fmaxf(hid_host.vibration[0][0],hid_host.vibration[0][2])*65535);
    hid_host.rumble_high=(uint16_t)(fmaxf(hid_host.vibration[1][0],hid_host.vibration[1][2])*65535);
#if ZERO_ZX_GAME
    /* Published CotM2 1.0.1, source 5233924074c9cab8b5148dfaa3ebf794d7b9820c:
     * deduplicate neutral motors only after SDL success on this attached
     * instance. Nonzero requests always refresh the real 2000 ms timeout. */
    if(!hid_host.rumble_low&&!hid_host.rumble_high&&hid_host.rumble_zero_confirmed&&
       hid_host.instance>=0&&hid_host.rumble_zero_instance==hid_host.instance) {
        memset(hid_host.vibration_actual,0,sizeof(hid_host.vibration_actual));hid_host.rumble_until=0;
        if(hid_host.rumble_zero_skipped!=UINT64_MAX)hid_host.rumble_zero_skipped++;
        if(hid_host.rumble_zero_skipped<=4 || !(hid_host.rumble_zero_skipped&(hid_host.rumble_zero_skipped-1)))
            printf("ZERO_RUMBLE_NEUTRAL_REUSE instance=%d skipped=%"PRIu64" prerequisite=SDL-success-same-instance nonzero-dedup=forbidden\n",
                hid_host.instance,hid_host.rumble_zero_skipped);
        return;
    }
    hid_host.rumble_zero_confirmed=0;
#endif
    memcpy(hid_host.rumble_values,hid_host.vibration,sizeof(hid_host.rumble_values));
    hid_host.rumble_pending=1;hid_host.rumble_done=0;
    while(!hid_host.rumble_done)pthread_cond_wait(&kernel_changed,&kernel_mutex);
    hid_host.rumble_pending=0;pthread_cond_broadcast(&kernel_changed);
    if(hid_host.rumble_error)stop("HID_PHYSICAL_RUMBLE_FAILED",pc,0,21);
}
/* Called by the SDL owner with kernel_mutex held; also isolates the exact
 * queued transport path for a host callback fixture. */
static void hid_rumble_output(void) {
    if(!hid_host.rumble_pending||hid_host.rumble_done)return;
    uint16_t low=hid_host.rumble_low,high=hid_host.rumble_high;
#if ZERO_ZX_GAME
    SDL_JoystickID rumble_instance=hid_host.instance;
#endif
    pthread_mutex_unlock(&kernel_mutex);
    int rc=hid_host.pad?t2_sdl_rumble(hid_host.pad,low,high,2000):-1;
#if ZERO_ZX_GAME
    unsigned rumble_attached=hid_host.pad&&SDL_GameControllerGetAttached(hid_host.pad);
#endif
    pthread_mutex_lock(&kernel_mutex);
    if(!rc) {
        memcpy(hid_host.vibration_actual,hid_host.rumble_values,sizeof(hid_host.vibration_actual));
        hid_host.rumble_until=(low||high)?hid_now_ms()+2000:0;
    } else {
        memset(hid_host.vibration_actual,0,sizeof(hid_host.vibration_actual));hid_host.rumble_until=0;
    }
#if ZERO_ZX_GAME
    hid_host.rumble_zero_confirmed=!rc&&!low&&!high&&rumble_attached&&hid_host.connected&&
        rumble_instance>=0&&rumble_instance==hid_host.instance;
    hid_host.rumble_zero_instance=rumble_instance;
#endif
    hid_host.rumble_error=rc!=0;hid_host.rumble_done=1;
#if ZERO_ZX_GAME
    static struct frame_log_counter rumble_output_logs;
    struct frame_log_event rumble_log=frame_log_sample(&rumble_output_logs,low,high,(uint64_t)(int64_t)rc,rc!=0);
    if(rumble_log.emit)printf("HID_RUMBLE_OUTPUT left=%u right=%u result=%d frequency=fixed-host safety-cap-ms=2000 occurrence=%"PRIu64" suppressed=%"PRIu64" changed=%u\n",
        low,high,rc,rumble_log.occurrence,rumble_log.suppressed,rumble_log.changed);
#else
    printf("HID_RUMBLE_OUTPUT left=%u right=%u result=%d frequency=fixed-host safety-cap-ms=2000\n",low,high,rc);
#endif
    pthread_cond_broadcast(&kernel_changed);
}
#if !defined(T2_SWITCH_INPUT) || !T2_SWITCH_INPUT
#include "momo_exit_chord.h"
#endif
/* Measured face-position correction for this pad (t2_face_position.h); 0 = SDL mapping kept. */
static unsigned t2_face_swap_of(SDL_GameController *pad) {
    if(pad && pad==hid_host.pad)return hid_host.face_swap;
    for(unsigned i=0;i<HID_EXTRA_PADS;i++)if(pad && pad==hid_host.extra[i])return hid_host.face_swap_extra[i];
    return 0;
}
static uint64_t hid_buttons(SDL_GameController *pad) {
    static const struct {SDL_GameControllerButton host;unsigned bit;} buttons[]={
        /* T2: preserve Switch positions through SDL's normalized controls:
         * SDL A south -> Switch B, SDL B east -> Switch A. The port build selects
         * this before the shader catalogue is included; see docs/DRIVE.md. */
#if defined(T2_SWITCH_INPUT) && T2_SWITCH_INPUT
        {SDL_CONTROLLER_BUTTON_A,1},{SDL_CONTROLLER_BUTTON_B,0},
#elif defined(ANIMAL_WELL_SHADERS)
        /* Animal Well reads the Switch layout by position: B (south) jumps, A (east) confirms, Y (west) uses the
         * item, X (north) opens the inventory. Measured on the device 17/09/2026 through a cloned evdev pad: bit 1
         * jumps and does not confirm the title menu, bit 0 confirms. The host pad keeps those positions. */
        {SDL_CONTROLLER_BUTTON_A,1},{SDL_CONTROLLER_BUTTON_B,0},
#else
        /* Legacy LC2/MM9 labels: host A confirms and host B goes back. */
        {SDL_CONTROLLER_BUTTON_A,0},{SDL_CONTROLLER_BUTTON_B,1},
#endif
        {SDL_CONTROLLER_BUTTON_Y,2},{SDL_CONTROLLER_BUTTON_X,3},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK,4},{SDL_CONTROLLER_BUTTON_RIGHTSTICK,5},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER,6},{SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,7},
        {SDL_CONTROLLER_BUTTON_START,10},{SDL_CONTROLLER_BUTTON_BACK,11},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT,12},{SDL_CONTROLLER_BUTTON_DPAD_UP,13},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT,14},{SDL_CONTROLLER_BUTTON_DPAD_DOWN,15}};
    uint64_t mask=0;
    for(unsigned i=0;i<sizeof(buttons)/sizeof(buttons[0]);i++)
        if(SDL_GameControllerGetButton(pad,buttons[i].host))mask|=1ULL<<buttons[i].bit;
    if(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT)>16384)mask|=1ULL<<8;
    if(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>16384)mask|=1ULL<<9;
#if defined(T2_SWITCH_INPUT) && T2_SWITCH_INPUT
    mask=t2_face_apply(mask,t2_face_swap_of(pad));
#endif
    return mask;
}
static float hid_trigger_value(SDL_GameController *pad,SDL_GameControllerAxis axis) {
    int value=SDL_GameControllerGetAxis(pad,axis);
    if(value<=0)return 0.0f;
    if(value>=32767)return 1.0f;
    return (float)value/32767.0f;
}
static void hid_exit_request(unsigned diagnostic);
static struct hid_sample hid_read_sample(SDL_GameController *pad,uint64_t diagnostic_buttons) {
    struct hid_sample sample={0};
    if(!pad) {
        hid_host.left_stick=(struct megaman_left_stick){0};
        return sample;
    }
    uint64_t physical_buttons=hid_buttons(pad);
    float left_trigger=hid_trigger_value(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    float right_trigger=hid_trigger_value(pad,SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
#if defined(T2_SWITCH_INPUT) && T2_SWITCH_INPUT
    /* Evaluate each source before merging player-one input. A split chord across
     * controllers (or physical/diagnostic input) must never close the port.
     * One poll is sufficient; the diagnostic socket is a separate complete chord. */
    if((physical_buttons&0xc00)==0xc00)hid_exit_request(0);
    if((diagnostic_buttons&0xc00)==0xc00)hid_exit_request(1);
#endif
    sample.buttons=physical_buttons|diagnostic_buttons;
    sample.lx=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX);
    sample.ly=-(int32_t)SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY);
    sample.rx=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTX);
    sample.ry=-(int32_t)SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_RIGHTY);
    /* Every attached controller drives player one: buttons are merged and each axis follows the pad that is
     * deflected the most, so the owner's pad works whatever its enumeration index is. */
    for(unsigned i=0;i<HID_EXTRA_PADS;i++) {
        SDL_GameController *other=hid_host.extra[i];
        if(!other)continue;
        uint64_t other_buttons=hid_buttons(other);
#if defined(T2_SWITCH_INPUT) && T2_SWITCH_INPUT
        if((other_buttons&0xc00)==0xc00)hid_exit_request(0);
#endif
        sample.buttons|=other_buttons;
        float value=hid_trigger_value(other,SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        if(value>left_trigger)left_trigger=value;
        value=hid_trigger_value(other,SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if(value>right_trigger)right_trigger=value;
        int32_t v;
        v=SDL_GameControllerGetAxis(other,SDL_CONTROLLER_AXIS_LEFTX);if(abs(v)>abs(sample.lx))sample.lx=v;
        v=-(int32_t)SDL_GameControllerGetAxis(other,SDL_CONTROLLER_AXIS_LEFTY);if(abs(v)>abs(sample.ly))sample.ly=v;
        v=SDL_GameControllerGetAxis(other,SDL_CONTROLLER_AXIS_RIGHTX);if(abs(v)>abs(sample.rx))sample.rx=v;
        v=-(int32_t)SDL_GameControllerGetAxis(other,SDL_CONTROLLER_AXIS_RIGHTY);if(abs(v)>abs(sample.ry))sample.ry=v;
    }
#if !defined(T2_SWITCH_INPUT) || !T2_SWITCH_INPUT
    momo_exit_chord(sample.buttons&~diagnostic_buttons);
#endif
    if(hid_host.diagnostic_lx||hid_host.diagnostic_ly) {
        sample.lx=hid_host.diagnostic_lx;sample.ly=hid_host.diagnostic_ly;
    }
    if(t2_input_gptk_route(&sample,left_trigger,right_trigger))
        stop("GPTK_INPUT_SINK_FATAL",0,0,21);
    sample.buttons=megaman_left_stick_buttons(&hid_host.left_stick,sample.lx,sample.ly,sample.buttons);
    return sample;
}
static void hid_publish(struct hid_sample sample,unsigned connected) {
    if(!hid_state.shared_plus_one)return;
    uint8_t *shared=shared_regions[hid_state.shared_plus_one-1].writer;
    unsigned target=10;
    if(hid_state.active && (hid_state.styles&1) && connected)
        for(unsigned i=0;i<hid_state.num_ids;i++)if(hid_state.ids[i]<8){target=hid_state.ids[i];break;}
    sample.number=++hid_host.sequence;
    if(hid_state.touch_active)zero_touch_publish(shared+0x400,sample.number);
    /* MOMO_NPAD_HANDHELD=1: also publish the Handheld npad (shared-memory entry 8, NpadId 0x20) with the
     * same sample. MEDIDO (run momo-208): the host publishes buttons=0x1 on No1 with style FullKey and the
     * title's "Press Any Button" does not fire. MEDIDO (run momo-210): with entry 8 published as Handheld
     * the same press LEAVES the title and the main menu comes up - so this build reads the Handheld npad,
     * not No1. ON by default now; MOMO_NPAD_HANDHELD=0 goes back. */
    static int hh_cfg;static unsigned handheld;
    if(!hh_cfg){const char *o=getenv("MOMO_NPAD_HANDHELD");handheld=!o||strcmp(o,"0");hh_cfg=1;
        if(handheld)printf("MOMO_NPAD_HANDHELD enabled=1 entry=8 style-bit=1 scope=diagnostic\n");}
    for(unsigned i=0;i<10;i++) {
        uint8_t *entry=shared+0x9a00+0x5000*i;
        unsigned is_handheld=handheld && i==8 && hid_state.active && (hid_state.styles&2) && connected;
        uint32_t style=i==target?1:(is_handheld?2:0),old=__atomic_load_n((uint32_t *)entry,__ATOMIC_ACQUIRE);
        struct hid_sample actual=(i==target||is_handheld)?sample:(struct hid_sample){.number=sample.number};
        actual.attributes=(i==target||is_handheld)?1:0;
        /* Native full-key and system-ext views; other styles stay disconnected.
         * The handheld entry publishes into its own ring (index 1) instead of the full-key one. */
        for(unsigned ring=0;ring<7;ring++) {
            unsigned live=is_handheld?(ring==1||ring==6):(ring==0||ring==6);
            struct hid_sample value=live?actual:(struct hid_sample){.number=sample.number};
            hid_ring_publish((struct hid_ring *)(entry+0x28+ring*0x350),value);
        }
        put32(entry+0x4188,style); /* Full-key device type. */
        uint64_t properties=style?0x6800:0;memcpy(entry+0x4190,&properties,8);
        __atomic_store_n((uint32_t *)entry,style,__ATOMIC_RELEASE);
        if(style!=old) {
            if(hid_state.events[i])session_get(hid_state.events[i],0)->initialized=1;
            pthread_cond_broadcast(&kernel_changed);
            printf("HID_NPAD_STYLE player=%u style=%u provider=%s connected=%u supported=0x%x\n",i,style,hid_input_provider(),connected,hid_state.styles);
        }
    }
    if(sample.buttons!=hid_host.last_buttons) {
        printf("HID_SAMPLE number=%"PRIu64" buttons=0x%"PRIx64" player=%u\n",sample.number,sample.buttons,target);
        hid_host.last_buttons=sample.buttons;
    }
}
static void hid_exit_request(unsigned diagnostic) {
    int failed=t2_input_gptk_release_all();
    hid_host.diagnostic_buttons=0;hid_host.diagnostic_lx=0;hid_host.diagnostic_ly=0;
    pthread_mutex_lock(&kernel_mutex);
    hid_publish((struct hid_sample){0},hid_host.pad!=NULL);
    pthread_mutex_unlock(&kernel_mutex);
    printf("SELECT+START: lifecycle exit requested source=%s release-all=%s\n",
        diagnostic?"diagnostic":"physical",failed?"failed":"ok");
    fflush(NULL);
    stop(failed?"GPTK_INPUT_SINK_FATAL":"USER_CONTROLLER_EXIT",0,0,failed?21:0);
}
static void hid_diagnostic_update(uint64_t now) {
    if(hid_host.command_fd<0)return;
    struct {uint64_t buttons;uint32_t milliseconds,reserved;} command;
    ssize_t n=recv(hid_host.command_fd,&command,sizeof(command),MSG_DONTWAIT|MSG_TRUNC);
    /* `reserved` carries the left stick as two signed 16-bit halves. A menu driven by the stick cannot be
     * navigated by buttons alone (MEDIDO run momo-245: the d-pad bit moves no cursor), and the packet
     * stays the same size, so zero still means "buttons only". */
    if(n==(ssize_t)sizeof(command) && command.milliseconds<=2000 && !(command.buttons&~0xffffULL)) {
        hid_host.diagnostic_buttons=command.buttons;
        hid_host.diagnostic_lx=(int16_t)(command.reserved&0xffff);
        hid_host.diagnostic_ly=(int16_t)(command.reserved>>16);
        hid_host.diagnostic_until=now+command.milliseconds;
        printf("HID_DIAGNOSTIC_PRESS buttons=0x%"PRIx64" milliseconds=%u stick=%d,%d provider=%s connected-pad-required=1\n",
            command.buttons,command.milliseconds,hid_host.diagnostic_lx,hid_host.diagnostic_ly,hid_input_provider());
    } else if(n>=0)printf("HID_DIAGNOSTIC_REJECT bytes=%zd\n",n);
    if(now>=hid_host.diagnostic_until){hid_host.diagnostic_buttons=0;hid_host.diagnostic_lx=hid_host.diagnostic_ly=0;}
#if defined(MEGAMAN_HOST_DIAGNOSTIC) && MEGAMAN_HOST_DIAGNOSTIC
    if(megaman_virtual_pad.enabled && megaman_virtual_pad_set(hid_host.diagnostic_buttons))
        stop("HID_HOST_VIRTUAL_UPDATE_FAILED",0,0,21);
#endif
}
static void *hid_worker(void *unused) {
    (void)unused;sigset_t blocked;sigemptyset(&blocked);sigaddset(&blocked,SIGALRM);pthread_sigmask(SIG_BLOCK,&blocked,NULL);
#if defined(MEGAMAN_HOST_DIAGNOSTIC) && MEGAMAN_HOST_DIAGNOSTIC
    int virtual_result=megaman_virtual_pad_open(getenv("MEGAMAN_HOST_VIRTUAL_PAD"));
    if(virtual_result<0)stop("HID_HOST_VIRTUAL_OPEN_FAILED",0,0,21);
    if(virtual_result) {
        hid_host.pad=megaman_virtual_pad.controller;
        printf("HID_HOST_VIRTUAL_OPEN provider=host-virtual instance=%d logical=full-key physical-proof=forbidden\n",megaman_virtual_pad.instance);
    }
#endif
    for(;;) {
        SDL_GameControllerUpdate();
        if(hid_host.pad && !SDL_GameControllerGetAttached(hid_host.pad)) {
#if defined(MEGAMAN_HOST_DIAGNOSTIC) && MEGAMAN_HOST_DIAGNOSTIC
            if(megaman_virtual_pad.enabled)stop("HID_HOST_VIRTUAL_DISCONNECTED",0,0,21);
#endif
            hid_c6_close(hid_host.pad);hid_host.pad=NULL;hid_host.face_swap=0;
            hid_host.left_stick=(struct megaman_left_stick){0};
            hid_host.diagnostic_buttons=0;printf("HID_PHYSICAL_DISCONNECTED\n");
        }
        if(!hid_host.pad)for(int i=0;i<SDL_NumJoysticks();i++)if(hid_c6_available(i)) {
            hid_host.pad=SDL_GameControllerOpen(i);
            if(hid_host.pad) {
                hid_host.left_stick=(struct megaman_left_stick){0};
                char guid[33];SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(hid_host.pad)),guid,sizeof(guid));
                printf("HID_PHYSICAL_OPEN name=%s guid=%s provider=system-SDL2 logical=full-key\n",SDL_GameControllerName(hid_host.pad),guid);
#if defined(T2_SWITCH_INPUT) && T2_SWITCH_INPUT
                hid_host.face_swap=t2_face_measure(hid_host.pad,i,"player-one");
#endif
                break;
            }
        }
        for(unsigned i=0;i<HID_EXTRA_PADS;i++)if(hid_host.extra[i]&&!SDL_GameControllerGetAttached(hid_host.extra[i])) {
            hid_c6_close(hid_host.extra[i]);hid_host.extra[i]=NULL;hid_host.face_swap_extra[i]=0;printf("HID_EXTRA_DISCONNECTED slot=%u\n",i);
        }
        {static int seen=-1;int n=SDL_NumJoysticks();if(n!=seen){seen=n;printf("HID_JOYSTICK_COUNT %d\n",n);}}
        if(hid_host.pad)for(int i=0;i<SDL_NumJoysticks();i++) {
            if(!hid_c6_available(i))continue;
            SDL_JoystickID id=t2_sdl_device_instance(i);
            if(id<0||SDL_GameControllerFromInstanceID(id))continue;
            unsigned slot=0;while(slot<HID_EXTRA_PADS&&hid_host.extra[slot])slot++;
            if(slot==HID_EXTRA_PADS)break;
            hid_host.extra[slot]=SDL_GameControllerOpen(i);
            if(hid_host.extra[slot]) {
                printf("HID_EXTRA_OPEN slot=%u name=%s merged-into=player-one\n",slot,SDL_GameControllerName(hid_host.extra[slot]));
#if defined(T2_SWITCH_INPUT) && T2_SWITCH_INPUT
                hid_host.face_swap_extra[slot]=t2_face_measure(hid_host.extra[slot],i,"extra");
#endif
            }
        }
        hid_diagnostic_update(hid_now_ms());
        uint64_t diagnostic_buttons=hid_host.diagnostic_buttons;
#if defined(MEGAMAN_HOST_DIAGNOSTIC) && MEGAMAN_HOST_DIAGNOSTIC
        if(megaman_virtual_pad.enabled)diagnostic_buttons=0;
#endif
        struct hid_sample sample=hid_read_sample(hid_host.pad,diagnostic_buttons);
#if !defined(T2_SWITCH_INPUT) || !T2_SWITCH_INPUT
        if(hid_host.pad) {
            if((sample.buttons&0xc00)==0xc00)stop("USER_CONTROLLER_EXIT",0,0,0);
        }
#endif
        pthread_mutex_lock(&kernel_mutex);
        hid_host.connected=hid_host.pad!=NULL;
#if ZERO_ZX_GAME
        /* Publish attachment identity under the same mutex as all guest
         * rumble requests. Reopened SDL pointers cannot inherit confirmation. */
        SDL_JoystickID instance=hid_host.pad?SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(hid_host.pad)):-1;
        if(instance!=hid_host.instance || !hid_host.connected)hid_host.rumble_zero_confirmed=0;
        hid_host.instance=instance;
#endif
        zero_controller_poll();
        hid_host.rumble_capable=hid_host.pad&&t2_sdl_has_rumble(hid_host.pad);
        if(!hid_vibration_mounted()) {
            memset(hid_host.vibration_actual,0,sizeof(hid_host.vibration_actual));hid_host.rumble_until=0;
#if ZERO_ZX_GAME
            hid_host.rumble_zero_confirmed=0;
#endif
        }
        hid_publish(sample,hid_host.connected);
        hid_rumble_output();
        pthread_mutex_unlock(&kernel_mutex);
        struct timespec pause={0,8000000};nanosleep(&pause,NULL);
    }
    return NULL;
}
static void hid_host_initialize(void) {
    /* NEXTOSCONTROLLERS.gptk is selected before SDL starts; an absent or
     * rejected owner/default leaves the original Switch-position map active. */
    (void)t2_input_gptk_initialize(NULL);
    hid_c6_initialize();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER))stop("HID_SDL_INIT_FAILED",0,0,21);
    SDL_GameControllerEventState(SDL_DISABLE);SDL_JoystickEventState(SDL_DISABLE);
    hid_host.command_fd=-1;
    const char *diagnostic=getenv("COTM_INPUT_DIAGNOSTIC");
    if(diagnostic && !strcmp(diagnostic,"1")) {
        char path[]="/tmp/cotm-input-XXXXXX";
        if(!mkdtemp(path))stop("HID_DIAGNOSTIC_DIRECTORY_FAILED",0,errno,21);
        struct sockaddr_un address={.sun_family=AF_UNIX};
        snprintf(address.sun_path,sizeof(address.sun_path),"%s/buttons.sock",path);
        hid_host.command_fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
        if(hid_host.command_fd<0 || bind(hid_host.command_fd,(struct sockaddr *)&address,sizeof(address)))stop("HID_DIAGNOSTIC_SOCKET_FAILED",0,errno,21);
        printf("HID_DIAGNOSTIC_SOCKET path=%s\n",address.sun_path);
    }
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,1U<<20);
    int rc=pthread_create(&hid_host.worker,&attr,hid_worker,NULL);pthread_attr_destroy(&attr);
    {   /* Opt-in COTM_HID_AFFINITY: keep the HID worker off the guest main core. */
        const char *o=getenv("COTM_HID_AFFINITY");
        if(!rc&&o&&!strcmp(o,"1")){cpu_set_t set;CPU_ZERO(&set);CPU_SET(3,&set);int e=pthread_setaffinity_np(hid_host.worker,sizeof(set),&set);printf("HID_AFFINITY cpu=3 rc=%d\n",e);}
    }
    if(rc)stop("HID_HOST_THREAD_FAILED",0,rc,21);
}
