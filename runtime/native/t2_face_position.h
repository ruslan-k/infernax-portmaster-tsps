/* Terminator 2D: physical face-button positions measured from the pad's evdev codes (Frente D, 27/09/2026).
 *
 * The host forwards SDL face buttons by POSITION (SDL A = south -> Switch B, docs/DRIVE.md 2.1). That is
 * only true when the SDL mapping string is positional. MEASURED on dArkOS .138: the firmware mapping for
 * "GO-Super Gamepad" (/opt/inttools/gamecontrollerdb.txt, also what get_controls exports) is
 *     a:b1,b:b0,x:b2,y:b3
 * and the kernel key bitmap (KEY=... 3db000000000000 ...) enumerates, in SDL2's order (BTN_JOYSTICK..KEY_MAX,
 * then 0..BTN_JOYSTICK-1): b0=BTN_SOUTH, b1=BTN_EAST, b2=BTN_NORTH, b3=BTN_WEST. So SDL "a" is the EAST button
 * (the one printed "A" on this Nintendo-layout handheld): the database follows labels, not positions, and
 * the owner saw A/B (and X/Y) inverted.
 *
 * Decision by measured capability, never by CFW name: open the SDL device node, read EV_KEY, rebuild the
 * SDL2 button order and look at the codes bound to a/b/x/y. Only when a->BTN_EAST and b->BTN_SOUTH (and,
 * for the second pair, x->BTN_NORTH and y->BTN_WEST) the pair is swapped back to positions. Anything else
 * (generic HID codes such as the Twin USB adapter's BTN_THUMB*, a hat d-pad without the cross-check, an
 * SDL without SDL_JoystickPathForIndex, an unreadable node) keeps the SDL mapping unchanged.
 * Cross-check: when the mapping binds the d-pad to buttons and the device has BTN_DPAD_*, those bindings
 * must land on BTN_DPAD_* in the rebuilt order, otherwise the order assumption is rejected (no swap).
 * T2_FACE_POSITION=sdl disables the correction (owner override); default auto.
 * This belongs in nxinput/C6 for every port (V6 pending item); the port keeps it as adapter glue. */
#ifndef T2_FACE_POSITION_H
#define T2_FACE_POSITION_H
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define T2_FACE_SWAP_AB 1U
#define T2_FACE_SWAP_XY 2U
#define T2_FACE_ORDER_MAX 768

/* Pure: SDL2 Linux button order from an EV_KEY bitmap. Returns the number of buttons. */
static unsigned t2_face_sdl2_order(const unsigned char *bits,unsigned nbits,unsigned short *order,unsigned max) {
    unsigned n=0;
#define T2_FACE_BIT(c) ((c)<nbits && (bits[(c)>>3]>>((c)&7)&1))
    for(unsigned c=BTN_JOYSTICK;c<KEY_MAX;c++)if(T2_FACE_BIT(c) && n<max)order[n++]=(unsigned short)c;
    for(unsigned c=0;c<BTN_JOYSTICK;c++)if(T2_FACE_BIT(c) && n<max)order[n++]=(unsigned short)c;
#undef T2_FACE_BIT
    return n;
}
/* Pure: decide from the codes bound to a/b/x/y (0 = not a button binding) and the d-pad cross-check result
 * (1 = consistent or not applicable, 0 = contradicts the order). */
static unsigned t2_face_decide(unsigned a,unsigned b,unsigned x,unsigned y,unsigned dpad_ok) {
    if(!dpad_ok)return 0;
    unsigned swap=0;
    if(a==BTN_EAST && b==BTN_SOUTH)swap|=T2_FACE_SWAP_AB;
    if(x==BTN_NORTH && y==BTN_WEST && (swap&T2_FACE_SWAP_AB))swap|=T2_FACE_SWAP_XY;
    return swap;
}
static uint64_t t2_face_apply(uint64_t mask,unsigned swap) {
    if(swap&T2_FACE_SWAP_AB)mask=(mask&~3ULL)|((mask&1ULL)<<1)|((mask>>1)&1ULL);
    if(swap&T2_FACE_SWAP_XY)mask=(mask&~0xcULL)|((mask&4ULL)<<1)|((mask>>1)&4ULL);
    return mask;
}
/* Resolved at run time: the release link keeps the audited SDL import set unchanged. */
typedef SDL_GameControllerButtonBind (*t2_face_bind_fn)(SDL_GameController *,SDL_GameControllerButton);
static unsigned t2_face_bound_code(t2_face_bind_fn get_bind,SDL_GameController *pad,SDL_GameControllerButton button,const unsigned short *order,unsigned n) {
    SDL_GameControllerButtonBind bind=get_bind(pad,button);
    if(bind.bindType!=SDL_CONTROLLER_BINDTYPE_BUTTON || bind.value.button<0 || (unsigned)bind.value.button>=n)return 0;
    return order[bind.value.button];
}
static unsigned t2_face_measure(SDL_GameController *pad,int device_index,const char *role) {
    const char *option=getenv("T2_FACE_POSITION");
    if(option && strcmp(option,"auto") && strcmp(option,"sdl"))stop("T2_FACE_POSITION_OPTION",0,0,24);
    const char *name=SDL_GameControllerName(pad);if(!name)name="?";
    if(option && !strcmp(option,"sdl")) {
        printf("HID_FACE_POSITION role=%s name=%s source=owner-override swap-ab=0 swap-xy=0\n",role,name);
        return 0;
    }
    const char *(*path_for_index)(int)=NULL;t2_face_bind_fn get_bind=NULL;
    void *symbol=dlsym(RTLD_DEFAULT,"SDL_JoystickPathForIndex");
    memcpy(&path_for_index,&symbol,sizeof path_for_index);
    symbol=dlsym(RTLD_DEFAULT,"SDL_GameControllerGetBindForButton");
    memcpy(&get_bind,&symbol,sizeof get_bind);
    const char *path=path_for_index?path_for_index(device_index):NULL;
    const char *why=NULL;unsigned char bits[(KEY_MAX+8)/8];memset(bits,0,sizeof bits);
    if(!get_bind)why="no-bind-query";
    else if(!path)why="no-device-path";
    else {
        int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if(fd<0)why="node-unreadable";
        else {
            if(ioctl(fd,EVIOCGBIT(EV_KEY,sizeof bits),bits)<0)why="no-ev-key";
            close(fd);
        }
    }
    if(why) {
        printf("HID_FACE_POSITION role=%s name=%s source=sdl-mapping reason=%s swap-ab=0 swap-xy=0\n",role,name,why);
        return 0;
    }
    unsigned short order[T2_FACE_ORDER_MAX];
    unsigned n=t2_face_sdl2_order(bits,KEY_MAX,order,T2_FACE_ORDER_MAX);
    unsigned a=t2_face_bound_code(get_bind,pad,SDL_CONTROLLER_BUTTON_A,order,n),b=t2_face_bound_code(get_bind,pad,SDL_CONTROLLER_BUTTON_B,order,n);
    unsigned x=t2_face_bound_code(get_bind,pad,SDL_CONTROLLER_BUTTON_X,order,n),y=t2_face_bound_code(get_bind,pad,SDL_CONTROLLER_BUTTON_Y,order,n);
    static const struct {SDL_GameControllerButton button;unsigned code;} dpad[]={
        {SDL_CONTROLLER_BUTTON_DPAD_UP,BTN_DPAD_UP},{SDL_CONTROLLER_BUTTON_DPAD_DOWN,BTN_DPAD_DOWN},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT,BTN_DPAD_LEFT},{SDL_CONTROLLER_BUTTON_DPAD_RIGHT,BTN_DPAD_RIGHT}};
    unsigned dpad_ok=1,dpad_checked=0;
    for(unsigned i=0;i<4;i++) {
        unsigned code=t2_face_bound_code(get_bind,pad,dpad[i].button,order,n);
        if(!code || !(bits[dpad[i].code>>3]>>(dpad[i].code&7)&1))continue;
        dpad_checked++;if(code!=dpad[i].code)dpad_ok=0;
    }
    unsigned swap=t2_face_decide(a,b,x,y,dpad_ok);
    printf("HID_FACE_POSITION role=%s name=%s source=evdev-measured path=%s a=0x%x b=0x%x x=0x%x y=0x%x dpad-checked=%u dpad-ok=%u swap-ab=%u swap-xy=%u\n",
           role,name,path,a,b,x,y,dpad_checked,dpad_ok,!!(swap&T2_FACE_SWAP_AB),!!(swap&T2_FACE_SWAP_XY));
    return swap;
}
#endif
