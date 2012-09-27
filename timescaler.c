/*****************************************************************************
 * Copyright (C) 2012 Rémi Duraffort
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include <errno.h>
#include <linux/futex.h>    /* futex */
#include <math.h>
#include <poll.h>           /* poll */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>         /* memset, strstr */
#include <time.h>           /* alarm */
#include <sys/select.h>
#include <sys/time.h>
#include <sys/times.h>      /* getitimer, setitimer, times, */
#include <unistd.h>         /* ualarm, usleep, */

#define __USE_GNU
#include <dlfcn.h>

/**
 * Hide most symboles by default and export only the hooks
 */
#if __GNUC__ >= 4
# define LOCAL  __attribute__ ((visibility ("hidden")))
# define GLOBAL __attribute__ ((visibility ("default")))
#else
# define GLOBAL
# define LOCAL
#endif

/**
 * The unlikely hint for the compiler as initialized check are unlikely to fail
 */
#ifdef __GNUC__
# define unlikely(p) __builtin_expect(!!(p), 0)
#else
# define unlikely(p) (!!(p))
#endif


/** Current version */
#define TIMESCALER_VERSION_MAJOR 0
#define TIMESCALER_VERSION_MINOR 1


/**
 * Structure to select the hooks to put in place
 */
typedef struct hooks_s {
  int alarm:1;
  int clock_gettime:1;
  int clock_nanosleep:1;
  int futex:1;
  int getitimer:1;
  int gettimeofday:1;
  int nanosleep:1;
  int pselect:1;
  int poll:1;
  int select:1;
  int setitimer:1;
  int sleep:1;
  int time:1;
  int times:1;
  int ualarm:1;
  int usleep:1;
} hooks_s;


/**
 * Global configuration variables
 */
LOCAL int       timescaler_initialized = 0;
LOCAL int       timescaler_verbosity = 0;
LOCAL float     timescaler_scale = 1.0f;
LOCAL int       timescaler_initial_time;
LOCAL int       timescaler_initial_clock_monotonic;
LOCAL int       timescaler_initial_clock_realtime;
LOCAL hooks_s   timescaler_hooks;

/**
 * Global function pointers
 */
LOCAL unsigned int (*timescaler_alarm)(unsigned int) = NULL;
LOCAL int          (*timescaler_clock_gettime)(clockid_t, struct timespec *) = NULL;
LOCAL int          (*timescaler_clock_nanosleep)(clockid_t, int,
                                                 const struct timespec *,
                                                 struct timespec *) = NULL;

LOCAL int          (*timescaler_futex)(int *, int, int,
                                       const struct timespec *,
                                       int *, int) = NULL;
LOCAL int          (*timescaler_getitimer)(int, struct itimerval *) = NULL;
LOCAL int          (*timescaler_gettimeofday)(struct timeval *,
                                              struct timezone *) = NULL;
LOCAL int          (*timescaler_nanosleep)(const struct timespec *,
                                           struct timespec *) = NULL;
LOCAL int          (*timescaler_poll)(struct pollfd *, nfds_t, int) = NULL;
LOCAL int          (*timescaler_pselect)(int nfds, fd_set *, fd_set *,
                                         fd_set *, const struct timespec *,
                                         const sigset_t *) = NULL;
LOCAL int          (*timescaler_select)(int nfds, fd_set *, fd_set *, fd_set *,
                                        struct timeval *) = NULL;
LOCAL int          (*timescaler_setitimer)(int, const struct itimerval *,
                                           struct itimerval *) = NULL;
LOCAL unsigned int (*timescaler_sleep)(unsigned int) = NULL;
LOCAL time_t       (*timescaler_time)(time_t*) = NULL;
LOCAL clock_t      (*timescaler_times)(struct tms *) = NULL;
LOCAL useconds_t   (*timescaler_ualarm)(useconds_t, useconds_t) = NULL;
LOCAL int          (*timescaler_usleep)(useconds_t) = NULL;


/**
 * The logging levels from error to debug
 */
typedef enum
{
  ERROR = 1,
  WARNING = 2,
  DEBUG = 3
} log_level;

static const char *psz_log_level[] =
{
  "ERROR",
  "WARNING",
  "DEBUG"
};

#define IS_HOOKED(func) (timescaler_hooks.func)

#define PROLOGUE()                                  \
  if(unlikely(!timescaler_initialized))             \
    timescaler_init();                              \
  timescaler_log(DEBUG, "Calling '%s'", __func__);

/**
 * Logging function for the timescaler library
 * @param level: the level of the message
 * @param psz_fmt: the message to print
 * @return nothing
 */
static inline void timescaler_log(log_level level, const char *psz_fmt, ...)
{
  if(unlikely(level <= timescaler_verbosity))
  {
    if(level > 3) level = 3;

    va_list args;
    va_start(args, psz_fmt);
    fprintf(stderr, "[%s] ", psz_log_level[level - 1]);
    vfprintf(stderr, psz_fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
  }
}


/**
 * Constructor function that read the environment variables
 * and get the right initial time
 */
LOCAL void __attribute__ ((constructor)) timescaler_init(void)
{
  /*
    The constructor function is not always the first function to be called.
    Indeed one of the hooks can be called by the constructor of a binary
    intiailialized before the timescaler library.
    In this case the hook should call the constructor and then continue to
    execute the right code.
  */
  if(timescaler_initialized)
    return;
  timescaler_initialized = 1;

  /* Fetch the configuration from the environment variables */
  const char *psz_verbosity = getenv("TIMESCALER_VERBOSITY");
  if(psz_verbosity)
    timescaler_verbosity = atoi(psz_verbosity);

  const char *psz_scale = getenv("TIMESCALER_SCALE");
  if(psz_scale)
    timescaler_scale = atof(psz_scale);

  const char *psz_hooks = getenv("TIMESCALER_HOOKS");
  if(psz_hooks && !*psz_hooks)
  {
    timescaler_log(DEBUG, "Removing all hooks");
    memset(&timescaler_hooks, 0, sizeof(timescaler_hooks));
  }
  else if(psz_hooks && *psz_hooks)
  {
    char *save_ptr, *token;
    char *psz_buffer = strdup(psz_hooks);
    memset(&timescaler_hooks, 0, sizeof(timescaler_hooks));

    /* Loop on every arguments seperated by ',' and match them with the hooks */
    token = strtok_r(psz_buffer, ",", &save_ptr);
    timescaler_log(DEBUG, "List of hooks:");
    while(token)
    {
#define HOOK(func) if(!strcmp(token, #func)) { timescaler_hooks.func = 1; timescaler_log(DEBUG, " * %s", #func); }
      HOOK(alarm)
      else HOOK(clock_gettime)
      else HOOK(clock_nanosleep)
      else HOOK(futex)
      else HOOK(getitimer)
      else HOOK(gettimeofday)
      else HOOK(nanosleep)
      else HOOK(pselect)
      else HOOK(poll)
      else HOOK(select)
      else HOOK(setitimer)
      else HOOK(sleep)
      else HOOK(time)
      else HOOK(times)
      else HOOK(ualarm)
      else HOOK(usleep)
      else timescaler_log(ERROR, "Unknwon hook: '%s'", token);
#undef HOOK

      token = strtok_r(NULL, ",", &save_ptr);
    }
    free(psz_buffer);
  }
  else
  {
    timescaler_log(DEBUG, "Hooking every implemented symbols");
    memset(&timescaler_hooks, 1, sizeof(timescaler_hooks));
  }

  /* Resolv the symboles that we will need afterward */
  timescaler_alarm           = dlsym(RTLD_NEXT, "alarm");
  timescaler_clock_gettime   = dlsym(RTLD_NEXT, "clock_gettime");
  timescaler_clock_nanosleep = dlsym(RTLD_NEXT, "clock_nanosleep");
  timescaler_futex           = dlsym(RTLD_NEXT, "futex");
  timescaler_getitimer       = dlsym(RTLD_NEXT, "getitimer");
  timescaler_gettimeofday    = dlsym(RTLD_NEXT, "gettimeofday");
  timescaler_nanosleep       = dlsym(RTLD_NEXT, "nanosleep");
  timescaler_pselect         = dlsym(RTLD_NEXT, "pselect");
  timescaler_poll            = dlsym(RTLD_NEXT, "poll");
  timescaler_select          = dlsym(RTLD_NEXT, "select");
  timescaler_setitimer       = dlsym(RTLD_NEXT, "setitimer");
  timescaler_sleep           = dlsym(RTLD_NEXT, "sleep");
  timescaler_time            = dlsym(RTLD_NEXT, "time");
  timescaler_times           = dlsym(RTLD_NEXT, "times");
  timescaler_ualarm          = dlsym(RTLD_NEXT, "ualarm");
  timescaler_usleep          = dlsym(RTLD_NEXT, "usleep");

  /* Get some time references */
  timescaler_initial_time = timescaler_time(NULL);

  if(timescaler_clock_gettime)
  {
    struct timespec tp;
    timescaler_clock_gettime(CLOCK_REALTIME, &tp);
    timescaler_initial_clock_realtime = tp.tv_sec;
    timescaler_clock_gettime(CLOCK_MONOTONIC, &tp);
    timescaler_initial_clock_monotonic = tp.tv_sec;
  }

  timescaler_log(DEBUG, "Timescaler v%d.%d initialization finished with:", TIMESCALER_VERSION_MAJOR, TIMESCALER_VERSION_MINOR);
  timescaler_log(DEBUG, " * verbosity=%d", timescaler_verbosity);
  timescaler_log(DEBUG, " * scale=%f", timescaler_scale);
}


/**
 * Transform a double into a timespec structure
 * @param time: the time as a double
 * @param t: the timespec structure
 */
static inline void double2timespec(double time, struct timespec *t)
{
  t->tv_sec = floor(time);
  t->tv_nsec = (time - t->tv_sec) * 1000000000L;
}


/**
 * Transform a timespec structure to a double
 * @param t: the timespec structure
 * @return the time as a double
 */
static inline double timespec2double(const struct timespec *t)
{
  return t->tv_sec + (double)t->tv_nsec / 1000000000L;
}


/**
 * Transform a double into a timeval structure
 * @param time: the time as a double
 * @param t: the timeval structure
 */
static inline void double2timeval(double time, struct timeval *t)
{
  t->tv_sec = floor(time);
  t->tv_usec = (time - t->tv_sec) * 1000000L;
}


/**
 * Transform a timeval structure to a double
 * @param t: the timeval structure
 * @return the time as a double
 */
static inline double timeval2double(const struct timeval *t)
{
  return t->tv_sec + (double)t->tv_usec / 1000000L;
}


/**
 * The alarm function
 */
GLOBAL unsigned int alarm(unsigned int seconds)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(alarm)))
    return timescaler_alarm(seconds);

  return timescaler_alarm(seconds * timescaler_scale) / timescaler_scale;
}


/**
 * The clock_gettime function
 * TODO: more clk_id should be used
 */
GLOBAL int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(clock_gettime)))
    return timescaler_clock_gettime(clk_id, tp);

  if(clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC)
  {
    timescaler_log(ERROR, "Wrong clock given to clock_gettime");
    return EINVAL;
  }

  int return_value = timescaler_clock_gettime(clk_id, tp);


  double time;
  double now = timespec2double(tp);

  if(clk_id == CLOCK_REALTIME)
    time = timescaler_initial_clock_realtime + (now - timescaler_initial_clock_realtime) / timescaler_scale;
  else
    time = timescaler_initial_clock_monotonic + (now - timescaler_initial_clock_monotonic) / timescaler_scale;

  double2timespec(time, tp);

  return return_value;
}


/**
 * The clock_nanosleep function
 */
GLOBAL int clock_nanosleep(clockid_t clk_id, int flags,
                           const struct timespec *req,
                           struct timespec *remain)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(clock_nanosleep)))
    return timescaler_clock_nanosleep(clk_id, flags, req, remain);

  if(clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC)
  {
    timescaler_log(ERROR, "Wrong clock given to clock_nanosleep");
    return EINVAL;
  }

  /* Transform the time to a double */
  double time = timespec2double(req);

  /* Transform an absolute wait into a relative one */
  if(flags == TIMER_ABSTIME)
  {
    struct timespec req_now;
    timescaler_clock_gettime(clk_id, &req_now);

    time -= timespec2double(&req_now);
    if(time <= 0.0)
      return 0;
  }

  /* TODO: check the return value for remaining time to sleep */
  struct timespec req_scale;
  double2timespec(time, &req_scale);

  int return_value = timescaler_clock_nanosleep(clk_id, 0, &req_scale, remain);
  return return_value;
}


/**
 * The futex function
 */
GLOBAL int futex(int *uaddr, int op, int val, const struct timespec *timeout,
                 int *uaddr2, int val3)
{
  PROLOGUE();

  /* We only have to support the FUTEX_WAIT operation */
  /* The other ones ignore the timeout argument */
  if(unlikely(!IS_HOOKED(futex)) || op != FUTEX_WAIT)
    return timescaler_futex(uaddr, op, val, timeout, uaddr2, val3);

  struct timespec timeout_scale;
  double time = timespec2double(timeout) * timescaler_scale;
  double2timespec(time, &timeout_scale);

  return timescaler_futex(uaddr, op, val, &timeout_scale, uaddr2, val3);
}

/**
 * The getitimer function
 */
GLOBAL int getitimer(int which, struct itimerval *curr_value)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(getitimer)))
    return timescaler_getitimer(which, curr_value);

  int return_value = timescaler_getitimer(which, curr_value);
  double value = timeval2double(&curr_value->it_value) / timescaler_scale;
  double interval = timeval2double(&curr_value->it_interval) / timescaler_scale;

  double2timeval(value, &(curr_value->it_value));
  double2timeval(interval, &(curr_value->it_interval));

  return return_value;
}


/**
 * The gettimeofday function
 */
GLOBAL int gettimeofday(struct timeval *tv, struct timezone *tz)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(gettimeofday)))
    return timescaler_gettimeofday(tv, tz);

  int return_value = timescaler_gettimeofday(tv, tz);
  double now = timeval2double(tv);
  double time = timescaler_initial_time + (now - timescaler_initial_time) / timescaler_scale;

  double2timeval(time, tv);

  return return_value;
}


/**
 * The nanosleep function
 */
GLOBAL int nanosleep(const struct timespec *req, struct timespec *rem)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(nanosleep)))
    return timescaler_nanosleep(req, rem);

  struct timespec req_scale;
  double time = timespec2double(req) * timescaler_scale;
  double2timespec(time, &req_scale);

  int return_value = timescaler_nanosleep(&req_scale, rem);

  if(return_value != 0 && rem)
  {
    double rem_time = timespec2double(rem) / timescaler_scale;
    double2timespec(rem_time, rem);
  }

  return return_value;
}


/**
 * The poll function
 */
GLOBAL int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
  PROLOGUE();
  if(unlikely(!IS_HOOKED(poll)))
    return timescaler_poll(fds, nfds, timeout);

  /* If the timeout is negative, no need to scale it */
  return timescaler_poll(fds, nfds, timeout < 0 ? timeout : timeout * timescaler_scale);
}


/**
 * The pselect function
 */
int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout,
            const sigset_t *sigmask)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(pselect)))
    return timescaler_pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);

  /* The timeout can be NULL, which mean that pselect will wait forever */
  if(timeout)
  {
    /* Scale the timeout */
    double time = timespec2double(timeout) * timescaler_scale;
    struct timespec timeout_scale;
    double2timespec(time, &timeout_scale);

    return timescaler_pselect(nfds, readfds, writefds, exceptfds, &timeout_scale,
                              sigmask);
  }
  else
    return timescaler_pselect(nfds, readfds, writefds, exceptfds, NULL, sigmask);
}


/**
 * The select function
 */
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(select)))
    return timescaler_select(nfds, readfds, writefds, exceptfds, timeout);

  /* The timeout can be NULL, which mean that pselect will wait forever */
  if(timeout)
  {
    int return_value;
    /* Scale the timeout */
    double time = timeval2double(timeout) * timescaler_scale;
    struct timeval timeout_scale;
    double2timeval(time, &timeout_scale);

    /* Call the real function */
    return_value = timescaler_select(nfds, readfds, writefds, exceptfds, &timeout_scale);

    /* Un-scale the returned timeout (remaining time) */
    time = timeval2double(&timeout_scale) / timescaler_scale;
    double2timeval(time, timeout);

    return return_value;
  }
  else
    return timescaler_select(nfds, readfds, writefds, exceptfds, NULL);
}


/**
 * The setitimer function
 */
GLOBAL int setitimer(int which, const struct itimerval *new_value,
                     struct itimerval *old_value)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(setitimer)))
    return timescaler_setitimer(which, new_value, old_value);

  struct itimerval new_value_scale;
  double value = timeval2double(&new_value->it_value) * timescaler_scale;
  double interval = timeval2double(&new_value->it_interval) * timescaler_scale;
  double2timeval(value, &(new_value_scale.it_value));
  double2timeval(interval, &(new_value_scale.it_interval));

  int return_value = timescaler_setitimer(which, &new_value_scale, old_value);

  // Change the old_value if not NULL
  if(old_value)
  {
    value = timeval2double(&old_value->it_value) / timescaler_scale;
    interval = timeval2double(&old_value->it_interval) / timescaler_scale;

    double2timeval(value, &(old_value->it_value));
    double2timeval(interval, &(old_value->it_interval));
  }

  return return_value;
}

/**
 * The sleep function
 */
GLOBAL unsigned int sleep(unsigned int seconds)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(sleep)))
    return timescaler_sleep(seconds);

  unsigned int return_value = timescaler_sleep(seconds * timescaler_scale);
  return return_value / timescaler_scale;
}


/**
 * The time function
 */
GLOBAL time_t time(time_t* tp)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(time)))
    return timescaler_time(tp);

  time_t now = timescaler_time(NULL);
  time_t return_value = timescaler_initial_time + (double)(now - timescaler_initial_time) / timescaler_scale;

  if(tp)
    *tp = return_value;
  return return_value;
}


/**
 * The times function
 */

clock_t times(struct tms *buf)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(times)))
    return timescaler_times(buf);

  clock_t return_value = timescaler_times(buf);
  buf->tms_utime = buf->tms_utime / timescaler_scale;
  buf->tms_stime = buf->tms_stime / timescaler_scale;
  buf->tms_cutime = buf->tms_cutime / timescaler_scale;
  buf->tms_cstime = buf->tms_cstime / timescaler_scale;

  // TODO: also change the return value
  return return_value;
}


/**
 * The ualarm function
 */
useconds_t ualarm(useconds_t usecs, useconds_t interval)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(ualarm)))
    return timescaler_ualarm(usecs, interval);

  return timescaler_ualarm(usecs * timescaler_scale, interval * timescaler_scale) / timescaler_scale;
}


/**
 * The usleep function
 */
int usleep(useconds_t usec)
{
  PROLOGUE();

  if(unlikely(!IS_HOOKED(usleep)))
    return timescaler_usleep(usec);

  return timescaler_usleep(usec * timescaler_scale);
}
