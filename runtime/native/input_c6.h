/* Port-side C6 admission for the firmware SDL. No private SDL or face remap.
 * Same boundary as the pinned V6 pure SDL consumer: before controller Open. */
#include "nxc6_glue.h"
#include "t2_sdl_optional.h"
#include <dlfcn.h>
static void hid_c6_initialize(void) {
    static char staged[65536];size_t length=0;
    if(nxc6_declare_port_bundle_for_layout(".",NXC6_FACE_LAYOUT_AUTO)<0 ||
       nxc6_stage_before_init(staged,sizeof staged,&length)<0)
        stop("HID_C6_PREINIT_FAILED",0,0,21);
}
static int hid_c6_available(int index) {
    SDL_JoystickID instance=t2_sdl_device_instance(index);
    if(instance<0 || SDL_GameControllerFromInstanceID(instance))return 0;
    char guid[33];SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index),guid,sizeof guid);
    /* Optional SDL2.24 API: keep the old firmware SDL link floor. */
    const char *(*path_for_index)(int)=NULL;
    void *symbol=dlsym(RTLD_DEFAULT,"SDL_JoystickPathForIndex");
    memcpy(&path_for_index,&symbol,sizeof path_for_index);
    const char *path=path_for_index?path_for_index(index):NULL;
    if(!nxc6_admit_before_announce_named(instance,guid,path,SDL_JoystickNameForIndex(index)))return 0;
    return SDL_IsGameController(index);
}
static void hid_c6_close(SDL_GameController *pad) {
    nxc6_forget(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)));
    SDL_GameControllerClose(pad);
}
