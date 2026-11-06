/* Bind the canonical publisher to T2's real readback + successful swap.
 * No receipt for an unmeasured/black frame, menu process liveness or exit 0. */
#include "t2_runtime_health.h"
#include "nxgl_runtime_health.h"
#include "nxgl_frame_proof_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char run_id[161],generation[65],port_id[97];
static int captured,attempted;
static int capture(char *out,size_t size,const char *name) {
    const char *value=getenv(name);
    if(!value || !*value || strlen(value)>=size)return 0;
    memcpy(out,value,strlen(value)+1);return 1;
}
void t2_runtime_health_capture(void) {
    captured=capture(run_id,sizeof run_id,"NXBOOTSTRAP_HEALTH_RUN_ID") &&
        capture(generation,sizeof generation,"NXBOOTSTRAP_HEALTH_GENERATION") &&
        capture(port_id,sizeof port_id,"NXBOOTSTRAP_HEALTH_PORT_ID");
}
void t2_runtime_health_after_present(int image_proved) {
    if(!captured || attempted || !image_proved || nxgl_frame_proof_is_fatal())return;
    attempted=1;
    nxloader_result rc=nxgl_runtime_health_publish_v1(run_id,generation,port_id);
    fprintf(stderr,"NX_RUNTIME_HEALTH/1 producer=t2_runtime_health_after_present boundary=post-first-present result=%d\n",(int)rc);
}
