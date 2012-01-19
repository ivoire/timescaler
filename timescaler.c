#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define __USE_GNU
#include <dlfcn.h>

#if __GNUC__ >= 4
# define LOCAL  __attribute__ ((visibility ("hidden")))
# define GLOBAL __attribute__ ((visibility ("default")))
#else
# define GLOBAL
# define LOCAL
#endif

LOCAL int timescaler_verbosity = 0;
LOCAL int timescaler_scale = 0;
LOCAL int timescaler_initial_time;

LOCAL time_t (*timescaler_real_time)(time_t*) = NULL;
LOCAL unsigned int (*timescaler_real_sleep)(unsigned int) = NULL;


/**
 * Constructor function that read the environment variables
 * and get the right initial time
 */
LOCAL void __attribute__ ((constructor)) timescaler_init(void)
{
  const char *psz_verbosity = getenv("TIMESCALER_VERBOSITY");
  if(psz_verbosity)
    timescaler_verbosity = atoi(psz_verbosity);

  const char *psz_scale = getenv("TIMESCALER_SCALE");
  if(psz_scale)
    timescaler_scale = atoi(psz_scale);

  timescaler_real_time = dlsym(RTLD_NEXT, "time");
  timescaler_initial_time = timescaler_real_time(NULL);

  if(timescaler_verbosity > 0)
    fprintf(stdout, "TimeScaler initialized and running with scaling to %d and an intilatime of %d\n", timescaler_scale, timescaler_initial_time);
}

/**
 * Define the time function
 */
GLOBAL time_t time(time_t* tp)
{
  time_t now = timescaler_real_time(tp);
  return timescaler_initial_time + (now - timescaler_initial_time) / timescaler_scale;
}

/**
 * Sleep for the given seconds
 */
GLOBAL unsigned int sleep(unsigned int seconds)
{
  if(!timescaler_real_sleep)
    timescaler_real_sleep = dlsym(RTLD_NEXT, "sleep");

  unsigned int return_value = timescaler_real_sleep(seconds * timescaler_scale);
  return return_value / timescaler_scale;
}
