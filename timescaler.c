#include <math.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
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

/**
 * Global function pointers
 */
LOCAL unsigned int (*timescaler_alarm)(unsigned int seconds) = NULL;
LOCAL int          (*timescaler_clock_gettime)(clockid_t, struct timespec *) = NULL;
LOCAL int          (*timescaler_clock_nanosleep)(clockid_t clock_id, int flags,
                                                 const struct timespec *request,
                                                 struct timespec *remain) = NULL;
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

  /* Resolv the symboles that we will need afterward */
  timescaler_alarm           = dlsym(RTLD_NEXT, "alarm");
  timescaler_clock_gettime   = dlsym(RTLD_NEXT, "clock_gettime");
  timescaler_clock_nanosleep = dlsym(RTLD_NEXT, "clock_nanosleep");
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
  timescaler_log(DEBUG, " * scale=%d", timescaler_scale);
}


/**
 * The time function
 */
GLOBAL time_t time(time_t* tp)
{
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  timescaler_log(DEBUG, "Calling 'time'");

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
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  timescaler_log(DEBUG, "Calling 'clock_gettime'");

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
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  timescaler_log(DEBUG, "Calling 'clock_nanosleep'");

  if(clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC)
  {
    timescaler_log(ERROR, "Wrong clock given to clock_nanosleep");
    return EINVAL;
  }

  /* Wait for a time relative to the current time */
  if(flags == 0)
  {
    /* TODO: check the return value for remaining time to sleep */
    struct timespec req_scale = { };

    double time = (req->tv_sec + (double)req->tv_nsec / 1000000000L) * timescaler_scale;
    req_scale.tv_sec = floor(time);
    req_scale.tv_nsec = (time - req_scale.tv_sec) * 1000000000L;

    int return_value = timescaler_clock_nanosleep(clk_id, flags, &req_scale, remain);
    return return_value;
  }
  /*  Wait until a certain point in the future */
  else
  {
    /* TODO: to implement */
    timescaler_log(ERROR, "Waiting for an absolute time is not implemented in clock_nanosleep");
    return EFAULT;
  }
}


/**
 * The gettimeofday function
 */
GLOBAL int gettimeofday(struct timeval *tv, struct timezone *tz)
{
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  timescaler_log(DEBUG, "Calling 'gettimeofday'");

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
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  timescaler_log(DEBUG, "Calling 'sleep'");

  unsigned int return_value = timescaler_sleep(seconds * timescaler_scale);
  return return_value / timescaler_scale;
}

/**
 * The nanosleep function
 * TODO: Special care of the req and rem structures should eb taken
 */
GLOBAL int nanosleep(const struct timespec *req, struct timespec *rem)
{
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  timescaler_log(DEBUG, "Calling 'nanosleep'");

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
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  timescaler_log(DEBUG, "Calling 'alarm'");

  return timescaler_alarm(seconds * timescaler_scale) / timescaler_scale;
}

/**
 * The select function
 * TODO: Special care of the tv structure should be taken
 */
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  timescaler_log(DEBUG, "Calling 'select'");

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
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  timescaler_log(DEBUG, "Calling 'pselect'");

  struct timespec timeout_scale = { timeout->tv_sec * timescaler_scale, 0 };
  return timescaler_pselect(nfds, readfds, writefds, exceptfds, &timeout_scale,
                            sigmask);
}
