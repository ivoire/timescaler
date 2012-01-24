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
LOCAL int timescaler_initialized = 0;
LOCAL int timescaler_verbosity = 0;
LOCAL int timescaler_scale = 0;
LOCAL int timescaler_initial_time;

/**
 * Global function pointers
 */
LOCAL time_t       (*timescaler_time)(time_t*) = NULL;
LOCAL int          (*timescaler_gettimeofday)(struct timeval *tv,
                                              struct timezone *tz) = NULL;
LOCAL unsigned int (*timescaler_sleep)(unsigned int) = NULL;
LOCAL int          (*timescaler_nanosleep)(const struct timespec *req,
                                           struct timespec *rem) = NULL;
LOCAL unsigned int (*timescaler_alarm)(unsigned int seconds) = NULL;
LOCAL int          (*timescaler_select)(int nfds, fd_set *readfds,
                                        fd_set *writefds, fd_set *exceptfds,
                                        struct timeval *timeout) = NULL;
LOCAL int          (*timescaler_pselect)(int nfds, fd_set *readfds,
                                         fd_set *writefds, fd_set *exceptfds,
                                         const struct timespec *timeout,
                                         const sigset_t *sigmask) = NULL;

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
    timescaler_scale = atoi(psz_scale);


  /* Resolv the symboles that we will need afterward */
  timescaler_gettimeofday = dlsym(RTLD_NEXT, "gettimeofday");
  timescaler_nanosleep    = dlsym(RTLD_NEXT, "nanosleep");
  timescaler_pselect      = dlsym(RTLD_NEXT, "pselect");
  timescaler_select       = dlsym(RTLD_NEXT, "select");
  timescaler_sleep        = dlsym(RTLD_NEXT, "sleep");
  timescaler_time         = dlsym(RTLD_NEXT, "time");

  timescaler_initial_time = timescaler_time(NULL);

  if(timescaler_verbosity > 0)
    fprintf(stdout, "TimeScaler initialized and running with scaling to %d and an intilatime of %d\n", timescaler_scale, timescaler_initial_time);
}


/**
 * The time function
 */
GLOBAL time_t time(time_t* tp)
{
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  time_t now = timescaler_time(tp);
  return timescaler_initial_time + (now - timescaler_initial_time) / timescaler_scale;
}


/**
 * The gettimeofday function
 * TODO: Special care of the tv structure should be taken
 */
GLOBAL int gettimeofday(struct timeval *tv, struct timezone *tz)
{
  if(unlikely(!timescaler_initialized))
    timescaler_init();

  int return_value = timescaler_gettimeofday(tv, tz);
  tv->tv_sec = timescaler_initial_time + (tv->tv_sec - timescaler_initial_time) / timescaler_scale;
  return return_value;
}


/**
 * The sleep function
 */
GLOBAL unsigned int sleep(unsigned int seconds)
{
  if(unlikely(!timescaler_initialized))
    timescaler_init();

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

  struct timespec req_scale;
  req_scale.tv_sec = req->tv_sec * timescaler_scale;
  req_scale.tv_nsec = 0;

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

  return timescaler_alarm(seconds * timescaler_scale) / timescaler_scale;
}

/**
 * The select function
 * TODO: Special care of the tv structure should be taken
 */
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
  struct timeval timeout_scale = { timeout->tv_sec * timescaler_scale, 0 };
  int return_value = timescaler_select(nfds, readfds, writefds, exceptfds, &timeout_scale);

  if(timeout_scale.tv_sec != timeout->tv_sec * timescaler_scale)
  {
    timeout->tv_sec = timeout_scale.tv_sec / timescaler_scale;
    timeout->tv_usec = 0;
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
  struct timespec timeout_scale = { timeout->tv_sec * timescaler_scale, 0 };
  return timescaler_pselect(nfds, readfds, writefds, exceptfds, &timeout_scale,
                            sigmask);
}
