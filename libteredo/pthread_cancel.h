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

bool pthread_cancel_register_handler (void);
// const static extern bool pthread_cancel_handler_registered;
int pthread_setcancelstate (int state, int *oldstate);
int pthread_cancel (pthread_t thread);
void pthread_testcancel (void);

# ifdef __cplusplus
}
# endif /* ifdef __cplusplus */
#endif /* ifndef LIBTEREDO_PTHREAD_CANCEL_H */
