/*
 * Keep the bounded monotonic waits used by the host runtime available on
 * glibc before pthread_cond_clockwait (introduced in glibc 2.34).
 * Convert each short monotonic deadline to the realtime absolute deadline
 * required by pthread_cond_timedwait. All current callers use deadlines no
 * more than a few milliseconds away, so clock adjustments cannot accumulate.
 */
#ifndef T2_COND_CLOCKWAIT_COMPAT_H
#define T2_COND_CLOCKWAIT_COMPAT_H

static inline int t2_pthread_cond_clockwait(pthread_cond_t *cond,
        pthread_mutex_t *mutex, clockid_t clock_id,
        const struct timespec *deadline) {
    if (clock_id == CLOCK_REALTIME)
        return pthread_cond_timedwait(cond, mutex, deadline);

    struct timespec source_now, realtime_now, realtime_deadline;
    if (clock_gettime(clock_id, &source_now) != 0)
        return errno;
    if (clock_gettime(CLOCK_REALTIME, &realtime_now) != 0)
        return errno;

    time_t seconds = deadline->tv_sec - source_now.tv_sec;
    long nanoseconds = deadline->tv_nsec - source_now.tv_nsec;
    if (nanoseconds < 0) {
        nanoseconds += 1000000000L;
        seconds--;
    }
    if (seconds < 0 || (seconds == 0 && nanoseconds == 0))
        return ETIMEDOUT;

    realtime_deadline.tv_sec = realtime_now.tv_sec + seconds;
    realtime_deadline.tv_nsec = realtime_now.tv_nsec + nanoseconds;
    if (realtime_deadline.tv_nsec >= 1000000000L) {
        realtime_deadline.tv_sec++;
        realtime_deadline.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(cond, mutex, &realtime_deadline);
}

#define pthread_cond_clockwait t2_pthread_cond_clockwait
#endif
