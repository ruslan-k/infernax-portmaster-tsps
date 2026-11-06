/* Explicit host diagnostic input only. Physical builds contain no SDL virtual
 * calls, virtual state or opt-in. The resulting SDL pad feeds normal HID rings. */
#ifndef MEGAMAN_VIRTUAL_PAD_H
#define MEGAMAN_VIRTUAL_PAD_H
#if defined(MEGAMAN_HOST_DIAGNOSTIC) && MEGAMAN_HOST_DIAGNOSTIC
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static struct {
    SDL_GameController *controller;SDL_Joystick *joystick;
    SDL_JoystickID instance;unsigned enabled;
} megaman_virtual_pad;
static int megaman_virtual_pad_index(void) {
    for(int i=0;i<SDL_NumJoysticks();i++)
        if(SDL_JoystickGetDeviceInstanceID(i)==megaman_virtual_pad.instance)return i;
    return -1;
}
static void megaman_virtual_pad_close(void) {
    if(megaman_virtual_pad.controller)SDL_GameControllerClose(megaman_virtual_pad.controller);
    if(megaman_virtual_pad.joystick)SDL_JoystickClose(megaman_virtual_pad.joystick);
    int index=megaman_virtual_pad_index();
    if(index>=0&&SDL_JoystickIsVirtual(index))SDL_JoystickDetachVirtual(index);
    memset(&megaman_virtual_pad,0,sizeof(megaman_virtual_pad));megaman_virtual_pad.instance=-1;
}
static int megaman_virtual_pad_set(uint64_t buttons) {
    if(!megaman_virtual_pad.enabled || buttons&~0xffffULL)return -1;
    static const struct {SDL_GameControllerButton host;unsigned bit;} map[]={
#if defined(T2_SWITCH_INPUT) && T2_SWITCH_INPUT
        /* Inverse of the physical T2 map: diagnostic Npad labels keep their meaning. */
        {SDL_CONTROLLER_BUTTON_A,1},{SDL_CONTROLLER_BUTTON_B,0},
#else
        {SDL_CONTROLLER_BUTTON_A,0},{SDL_CONTROLLER_BUTTON_B,1},
#endif
        {SDL_CONTROLLER_BUTTON_Y,2},{SDL_CONTROLLER_BUTTON_X,3},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK,4},{SDL_CONTROLLER_BUTTON_RIGHTSTICK,5},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER,6},{SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,7},
        {SDL_CONTROLLER_BUTTON_START,10},{SDL_CONTROLLER_BUTTON_BACK,11},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT,12},{SDL_CONTROLLER_BUTTON_DPAD_UP,13},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT,14},{SDL_CONTROLLER_BUTTON_DPAD_DOWN,15}};
    for(unsigned i=0;i<sizeof(map)/sizeof(map[0]);i++)
        if(SDL_JoystickSetVirtualButton(megaman_virtual_pad.joystick,map[i].host,(buttons>>map[i].bit)&1))return -1;
    if(SDL_JoystickSetVirtualAxis(megaman_virtual_pad.joystick,SDL_CONTROLLER_AXIS_TRIGGERLEFT,
            buttons&(1ULL<<8)?32767:-32768) ||
       SDL_JoystickSetVirtualAxis(megaman_virtual_pad.joystick,SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
            buttons&(1ULL<<9)?32767:-32768))return -1;
    SDL_GameControllerUpdate();return 0;
}
static int megaman_virtual_pad_open(const char *requested) {
    if(!requested || !strcmp(requested,"0"))return 0;
    if(strcmp(requested,"1") || megaman_virtual_pad.enabled)return -1;
    int index=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
        SDL_CONTROLLER_AXIS_MAX,SDL_CONTROLLER_BUTTON_MAX,0);
    if(index<0)return -1;
    megaman_virtual_pad.instance=SDL_JoystickGetDeviceInstanceID(index);
    megaman_virtual_pad.joystick=SDL_JoystickOpen(index);
    if(!megaman_virtual_pad.joystick)goto fail;
    char guid[33],mapping[512];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index),guid,sizeof(guid));
    snprintf(mapping,sizeof(mapping),"%s,LC2 host virtual,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,",guid);
    if(SDL_GameControllerAddMapping(mapping)<0)goto fail;
    megaman_virtual_pad.controller=SDL_GameControllerOpen(index);
    if(!megaman_virtual_pad.controller)goto fail;
    megaman_virtual_pad.enabled=1;
    if(megaman_virtual_pad_set(0))goto fail;
    return 1;
fail:
    megaman_virtual_pad_close();return -1;
}
#endif
#endif
