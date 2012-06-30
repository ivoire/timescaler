#include <errno.h>
#include <linux/futex.h>    /* futex */
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>         /* strstr */
#include <time.h>           /* alarm */
#include <sys/select.h>
#include <sys/time.h>

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


/**
 * Global configuration variables
 */
LOCAL int   timescaler_initialized = 0;
LOCAL int   timescaler_verbosity = 0;
LOCAL float timescaler_scale = 1.0f;
LOCAL int   timescaler_initial_time;
LOCAL int   timescaler_initial_clock;
LOCAL int   timescaler_hooks;

/**
 * Global function pointers
 */
LOCAL unsigned int (*timescaler_alarm)(unsigned int seconds) = NULL;
LOCAL int          (*timescaler_clock_gettime)(clockid_t, struct timespec *) = NULL;
LOCAL int          (*timescaler_clock_nanosleep)(clockid_t clock_id, int flags,
                                                 const struct timespec *request,
                                                 struct timespec *remain) = NULL;

LOCAL int           (*timescaler_futex)(int *uaddr, int op, int val,
                                        const struct timespec *timeout,
                                        int *uaddr2, int val3) = NULL;

LOCAL int          (*timescaler_gettimeofday)(struct timeval *tv,
                                              struct timezone *tz) = NULL;
LOCAL int          (*timescaler_nanosleep)(const struct timespec *req,
                                           struct timespec *rem) = NULL;
LOCAL int          (*timescaler_pselect)(int nfds, fd_set *readfds,
                                         fd_set *writefds, fd_set *exceptfds,
                                         const struct timespec *timeout,
                                         const sigset_t *sigmask) = NULL;
LOCAL int          (*timescaler_select)(int nfds, fd_set *readfds,
                                        fd_set *writefds, fd_set *exceptfds,
                                        struct timeval *timeout) = NULL;
LOCAL unsigned int (*timescaler_sleep)(unsigned int) = NULL;
LOCAL time_t       (*timescaler_time)(time_t*) = NULL;


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

typedef enum
{
  ALARM             = 1 << 0,
  CLOCK_GETTIME     = 1 << 1,
  CLOCK_NANOSLEEP   = 1 << 2,
  FUTEX             = 1 << 3,
  GETTIMEOFDAY      = 1 << 4,
  NANOSLEEP         = 1 << 5,
  PSELECT           = 1 << 6,
  SELECT            = 1 << 7,
  SLEEP             = 1 << 8,
  TIME              = 1 << 9,

  LAST              = 1 << 10
} hook_id;


static inline int is_hooked(hook_id id)
{
  return (timescaler_hooks & id) != 0;
}

#define PROLOGUE()                                  \
  if(unlikely(!timescaler_initialized))             \
    timescaler_init();                              \
  timescaler_log(DEBUG, "Calling '%s", __func__);

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

  char *psz_hooks = getenv("TIMESCALER_HOOKS");
  if(psz_hooks && !*psz_hooks)
  {
    timescaler_log(DEBUG, "Removing all hooks");
    timescaler_hooks = 0;
  }
  else if(psz_hooks && *psz_hooks)
  {
    char *save_ptr, *token;
    psz_hooks = strdup(psz_hooks);
    timescaler_hooks = 0;

    /* Loop on every arguments seperated by ',' and match them with the hooks */
    token = strtok_r(psz_hooks, ",", &save_ptr);
    timescaler_log(DEBUG, "List of hooks:");
    while(token)
    {
#define HOOK(psz, value) if(!strcmp(token, psz)) { timescaler_hooks |= value; timescaler_log(DEBUG, " * %s", psz); }
      HOOK("alarm", ALARM)
      else HOOK("clock_gettime", CLOCK_GETTIME)
      else HOOK("clock_nanosleep", CLOCK_NANOSLEEP)
      else HOOK("futex", FUTEX)
      else HOOK("gettimeofday", GETTIMEOFDAY)
      else HOOK("nanosleep", NANOSLEEP)
      else HOOK("pselect", PSELECT)
      else HOOK("select", SELECT)
      else HOOK("sleep", SLEEP)
      else HOOK("time", TIME)
      else timescaler_log(ERROR, "Unknwon hook: '%s'", token);
#undef HOOK

      token = strtok_r(NULL, ",", &save_ptr);
    }
    free(psz_hooks);
  }
  else
  {
    timescaler_log(DEBUG, "Hooking every implemented symbols");
    timescaler_hooks = LAST - 1;
  }

  /* Resolv the symboles that we will need afterward */
  timescaler_alarm           = dlsym(RTLD_NEXT, "alarm");
  timescaler_clock_gettime   = dlsym(RTLD_NEXT, "clock_gettime");
  timescaler_clock_nanosleep = dlsym(RTLD_NEXT, "clock_nanosleep");
  timescaler_futex           = dlsym(RTLD_NEXT, "futex");
  timescaler_gettimeofday    = dlsym(RTLD_NEXT, "gettimeofday");
  timescaler_nanosleep       = dlsym(RTLD_NEXT, "nanosleep");
  timescaler_pselect         = dlsym(RTLD_NEXT, "pselect");
  timescaler_select          = dlsym(RTLD_NEXT, "select");
  timescaler_sleep           = dlsym(RTLD_NEXT, "sleep");
  timescaler_time            = dlsym(RTLD_NEXT, "time");

  /* Get some time references */
  timescaler_initial_time = timescaler_time(NULL);

  if(timescaler_clock_gettime)
  {
    struct timespec tp;
    timescaler_clock_gettime(CLOCK_REALTIME, &tp);
    timescaler_initial_clock = tp.tv_sec;
  }

  timescaler_log(DEBUG, "Timescaler initialization finished with:");
  timescaler_log(DEBUG, " * verbosity=%d", timescaler_verbosity);
  timescaler_log(DEBUG, " * scale=%f", timescaler_scale);
}


/**
 * The time function
 */
GLOBAL time_t time(time_t* tp)
{
  PROLOGUE();

  if(unlikely(!is_hooked(TIME)))
    return timescaler_time(tp);

  time_t now = timescaler_time(tp);
  return timescaler_initial_time + (now - timescaler_initial_time) / timescaler_scale;
}

/**
 * The clock_gettime function
 * TODO: special care of the tp structure should be taken
 * TODO: more clk_id should be used
 */
GLOBAL int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
  PROLOGUE();

  if(unlikely(!is_hooked(CLOCK_GETTIME)))
    return timescaler_clock_gettime(clk_id, tp);

  if(clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC)
  {
    timescaler_log(ERROR, "Wrong clock given to clock_gettime");
    return EINVAL;
  }

  int return_value = timescaler_clock_gettime(clk_id, tp);

  double now = (tp->tv_sec + (double)tp->tv_nsec / 1000000000L);
  double time = timescaler_initial_clock + (now - timescaler_initial_clock) / timescaler_scale;
  tp->tv_sec = floor(time);
  tp->tv_nsec = (time - tp->tv_sec) * 1000000000L;

  timescaler_log(DEBUG, "tv_sec=%d, tv_nsec=%d", tp->tv_sec, tp->tv_nsec);

  return return_value;
}


int clock_nanosleep(clockid_t clk_id, int flags,
                           const struct timespec *req,
                           struct timespec *remain)
{
  PROLOGUE();

  if(unlikely(!is_hooked(CLOCK_NANOSLEEP)))
    return timescaler_clock_nanosleep(clk_id, flags, req, remain);

  if(clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC)
  {
    timescaler_log(ERROR, "Wrong clock given to clock_nanosleep");
    return EINVAL;
  }

  /* Transform the time to a double */
  double time = (req->tv_sec + (double)req->tv_nsec / 1000000000L);

  /* Transform an absolute wait into a relative one */
  if(flags == TIMER_ABSTIME)
  {
    struct timespec req_now;
    timescaler_clock_gettime(clk_id, &req_now);

    time -= req_now.tv_sec + (double)req_now.tv_nsec / 1000000000L;
  }

  /* TODO: check the return value for remaining time to sleep */
  struct timespec req_scale = { };
  req_scale.tv_sec = floor(time);
  req_scale.tv_nsec = (time - req_scale.tv_sec) * 1000000000L;

  int return_value = timescaler_clock_nanosleep(clk_id, 0, &req_scale, remain);
  return return_value;
}


/**
 * The futex function
 */
int futex(int *uaddr, int op, int val, const struct timespec *timeout,
          int *uaddr2, int val3)
{
  PROLOGUE();

  /* We only have to support the FUTEX_WAIT operation */
  /* The other ones ignore the timeout argument */
  if(unlikely(!is_hooked(FUTEX)) || op != FUTEX_WAIT)
    return timescaler_futex(uaddr, op, val, timeout, uaddr2, val3);

  struct timespec timeout_scale = { };

  double time = (timeout->tv_sec + (double)timeout->tv_nsec / 1000000000L) * timescaler_scale;
  timeout_scale.tv_sec = floor(time);
  timeout_scale.tv_nsec = (time - timeout_scale.tv_sec) * 1000000000L;

  return timescaler_futex(uaddr, op, val, &timeout_scale, uaddr2, val3);
}


/**
 * The gettimeofday function
 */
GLOBAL int gettimeofday(struct timeval *tv, struct timezone *tz)
{
  PROLOGUE();

  if(unlikely(!is_hooked(GETTIMEOFDAY)))
    return timescaler_gettimeofday(tv, tz);

  int return_value = timescaler_gettimeofday(tv, tz);
  double now = (tv->tv_sec + (double)tv->tv_usec / 1000000L);
  double time = timescaler_initial_clock + (now - timescaler_initial_time) / timescaler_scale;

  tv->tv_sec = floor(time);
  tv->tv_usec = (time - tv->tv_sec) * 1000000L;

  return return_value;
}


/**
 * The sleep function
 */
GLOBAL unsigned int sleep(unsigned int seconds)
{
  PROLOGUE();

  if(unlikely(!is_hooked(SLEEP)))
    return timescaler_sleep(seconds);

  unsigned int return_value = timescaler_sleep(seconds * timescaler_scale);
  return return_value / timescaler_scale;
}

/**
 * The nanosleep function
 * TODO: Special care of the req and rem structures should eb taken
 */
GLOBAL int nanosleep(const struct timespec *req, struct timespec *rem)
{
  PROLOGUE();

  if(unlikely(!is_hooked(NANOSLEEP)))
    return timescaler_nanosleep(req, rem);

  struct timespec req_scale = { };

  double time = (req->tv_sec + (double)req->tv_nsec / 1000000000L) * timescaler_scale;
  req_scale.tv_sec = floor(time);
  req_scale.tv_nsec = (time - req_scale.tv_sec) * 1000000000L;

  int return_value = timescaler_nanosleep(&req_scale, rem);

  return return_value;
}


/**
 * The alarm function
 */
GLOBAL unsigned int alarm(unsigned int seconds)
{
  PROLOGUE();

  if(unlikely(!is_hooked(ALARM)))
    return timescaler_alarm(seconds);

  return timescaler_alarm(seconds * timescaler_scale) / timescaler_scale;
}

/**
 * The select function
 * TODO: Special care of the tv structure should be taken
 */
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
  PROLOGUE();

  if(unlikely(!is_hooked(SELECT)))
    return timescaler_select(nfds, readfds, writefds, exceptfds, timeout);

  /* Take into account the timeout can bu NULL */
  int return_value;
  if(timeout)
  {
    struct timeval timeout_scale = { timeout->tv_sec * timescaler_scale, 0 };
    return_value = timescaler_select(nfds, readfds, writefds, exceptfds, &timeout_scale);

    if(timeout_scale.tv_sec != timeout->tv_sec * timescaler_scale)
    {
      timeout->tv_sec = timeout_scale.tv_sec / timescaler_scale;
      timeout->tv_usec = 0;
    }
  }
  else
  {
    return_value = timescaler_select(nfds, readfds, writefds, exceptfds, NULL);
  }

  return return_value;
}

/**
 * The pselect fnction
 * TODO: Special care of the tv structure should be taken
 */
int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout,
            const sigset_t *sigmask)
{
  PROLOGUE();

  if(unlikely(!is_hooked(PSELECT)))
    return timescaler_pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);

  struct timespec timeout_scale = { timeout->tv_sec * timescaler_scale, 0 };
  return timescaler_pselect(nfds, readfds, writefds, exceptfds, &timeout_scale,
                            sigmask);
}
