/* Read-only timing of one in sixteen complete PCM blocks. Only the mixer
 * worker reads/writes these counters. No floating-point or renderer changes.
 * Wall time includes preemption; these numbers are not CPU utilization. */
static struct {
    uint64_t blocks,samples,phase_ns[5];
    unsigned sequence;
} aud_mix_profile;
static uint64_t aud_mix_profile_now(void) {
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts))stop("AUDREN_PROFILE_CLOCK",0,errno,21);
    return (uint64_t)ts.tv_sec*1000000000ULL+(uint64_t)ts.tv_nsec;
}
static void aud_mix_profile_report(void) {
    printf("AUDREN_MIX_PHASE_PROFILE blocks=%"PRIu64" sampled=%"PRIu64
           " init-ms=%.3f voices-route-ms=%.3f submix-ms=%.3f output-ms=%.3f pool-usage-ms=%.3f"
           " scope=sampled-wall-time sample-period=16 mutex=%s pcm=unchanged\n",
           aud_mix_profile.blocks,aud_mix_profile.samples,
           aud_mix_profile.phase_ns[0]/1e6,aud_mix_profile.phase_ns[1]/1e6,
           aud_mix_profile.phase_ns[2]/1e6,aud_mix_profile.phase_ns[3]/1e6,aud_mix_profile.phase_ns[4]/1e6,
           aud_snapshot.enabled?"none-snapshot":"kernel");
    aud_mix_profile.blocks=aud_mix_profile.samples=0;
    memset(aud_mix_profile.phase_ns,0,sizeof(aud_mix_profile.phase_ns));
}
