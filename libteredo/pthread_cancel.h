#ifndef LIBTEREDO_PTHREAD_CANCEL_H
# define LIBTEREDO_PTHREAD_CANCEL_H
# ifdef __cplusplus
extern "C" {
# endif

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#ifndef PTHREAD_CANCEL_DISABLE
enum
{
	PTHREAD_CANCEL_ENABLE,
# define PTHREAD_CANCEL_ENABLE   PTHREAD_CANCEL_ENABLE
	PTHREAD_CANCEL_DISABLE
# define PTHREAD_CANCEL_DISABLE  PTHREAD_CANCEL_DISABLE
};
#endif

#ifndef PTHREAD_CANCELED
# define PTHREAD_CANCELED ((void *) -1)
#endif
#define PTHREAD_CANCEL_SIGCANCEL SIGUSR1

/**
 * Registers the signal handler for pthread_cancel.
 */
void pthread_cancel_register_handler (void);

/**
 * See pthread_setcancelstate(3)
 * Although not required by POSIX, this is a cancellation point
 */
int pthread_setcancelstate (int state, int *oldstate);

/**
 * Simulate pthread_cancel(3) by sending a signal to the thread.
 */
int pthread_cancel (pthread_t thread);

/**
 * See pthread_testcancel(3)
 */
void pthread_testcancel (void);

# ifdef __cplusplus
}
# endif /* ifdef __cplusplus */
#endif /* ifndef LIBTEREDO_PTHREAD_CANCEL_H */
