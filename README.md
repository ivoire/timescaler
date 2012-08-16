Timescaler
==========
This library allows to hook time-related function from the libc in order to
scale the time. The hooked program will see the time running slower than the
real time.


Usage
-----
The library must be used through the **LD_PRELOAD** mechanism:

    LD_PRELOAD=$INSTALL_PATH/timescaler.so my_program

In this case, timescaler will just hook the time related function but without
altering the behavior of the functions.

To scale the time two times, run:

    TIMESCALER_SCALE=2 LD_PRELOAD=$INSTALL_PATH/timescaler.so my_program

timescaler will scale the time by two. The program will see the time as if it
runs two times slower than the real time.


Available options
-----------------
timescaler comes with some options to control the hooks:

* TIMESCALER_VERBOSITY: set the verbosity from 0 to 3
* TIMESCALER_SCALE: set the scaling applied to the time as a floating point
* TIMESCALER_HOOKS: coma seperated list of functions to hook. timescaler will
  only hook the selected functions.


Implemented function:
---------------------
timescaler handles the folowing list of time-dependant functions:

* clock_gettime
* clock_nanosleep
* futex
* getitimer
* gettimeofday
* nanosleep
* pselect
* poll
* select
* setitimer
* sleep
* time
* times
* ualarm
* usleep


Contributing
------------
If you have any question, bug, feature or patches, feel free to send them by
mail or through the bug tracker.
