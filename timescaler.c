#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#define __USE_GNU
#include <dlfcn.h>

#if __GNUC__ >= 4
# define LOCAL  __attribute__ ((visibility ("hidden")))
# define GLOBAL __attribute__ ((visibility ("default")))
#else
# define GLOBAL
# define LOCAL
#endif

LOCAL int timescaler_initialized = 0;
LOCAL int timescaler_verbosity = 0;
LOCAL int timescaler_scale = 0;
LOCAL int timescaler_initial_time;

LOCAL time_t (*timescaler_real_time)(time_t*) = NULL;
LOCAL int (*timescaler_gettimeofday)(struct timeval *tv, struct timezone *tz) = NULL;
LOCAL unsigned int (*timescaler_real_sleep)(unsigned int) = NULL;
LOCAL int (*timescaler_nanosleep)(const struct timespec *req, struct timespec *rem) = NULL;


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
  timescaler_real_sleep   = dlsym(RTLD_NEXT, "sleep");
  timescaler_real_time    = dlsym(RTLD_NEXT, "time");

  timescaler_initial_time = timescaler_real_time(NULL);

  if(timescaler_verbosity > 0)
    fprintf(stdout, "TimeScaler initialized and running with scaling to %d and an intilatime of %d\n", timescaler_scale, timescaler_initial_time);
}

/**
 * Define the time function
 */
GLOBAL time_t time(time_t* tp)
{
  if(!timescaler_initialized)
    timescaler_init();

  time_t now = timescaler_real_time(tp);
  return timescaler_initial_time + (now - timescaler_initial_time) / timescaler_scale;
}


GLOBAL int gettimeofday(struct timeval *tv, struct timezone *tz)
{
  if(!timescaler_initialized)
    timescaler_init();

  int return_value = timescaler_gettimeofday(tv, tz);
  tv->tv_sec = timescaler_initial_time + (tv->tv_sec - timescaler_initial_time) / timescaler_scale;
  return return_value;
}


/**
 * Sleep for the given seconds
 */
GLOBAL unsigned int sleep(unsigned int seconds)
{
  if(!timescaler_initialized)
    timescaler_init();

  unsigned int return_value = timescaler_real_sleep(seconds * timescaler_scale);
  return return_value / timescaler_scale;
}

GLOBAL int nanosleep(const struct timespec *req, struct timespec *rem)
{
  if(!timescaler_initialized)
    timescaler_init();

  struct timespec req_scale;
  //TODO: scale the tv_nsec element
  req_scale.tv_sec = req->tv_sec * timescaler_scale;

  int return_value = timescaler_nanosleep(&req_scale, rem);

  //TODO: Downscale the second parameter
  return return_value;
}

