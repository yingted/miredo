#include "pthread_cancel.h"
#undef NDEBUG
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>

// copied from bionic
typedef struct pthread_internal_t
{
	struct pthread_internal_t*  next;
	struct pthread_internal_t** prev;
	pthread_attr_t              attr;
	pid_t                       kernel_id;
	pthread_cond_t              join_cond;
	int                         join_count;
	void*                       return_value;
	int                         internal_flags;
	__pthread_cleanup_t*        cleanup_stack;
	void**                      tls;         /* thread-local storage area */
} pthread_internal_t;
#define BIONIC_TID(thd) (((pthread_internal_t *) (thd))->kernel_id)
static pid_t pthread_cancel_gettid (pthread_t thd)
{
	if (pthread_equal (pthread_self (), thd))
		return gettid ();
	for (int i = 0; i < 1000000; ++i);
	return BIONIC_TID (thd);
}
// replacement for pthread_equal
static int pthread_cancel_pthread_equal (pthread_t one, pthread_t two)
{
	// one can be pthread_self (), and right after fork (), bionic might
	// not have updated the thread id. two cannot be pthread_self ()
	return pthread_cancel_gettid (one) == BIONIC_TID (two);
}
#define pthread_equal pthread_cancel_pthread_equal
#define SYS_tgkill __NR_tgkill
// copy from bionic
static int pthread_cancel_pthread_kill (pthread_t thd, int sig) 
{
	int  ret;
	int  old_errno = errno;


	ret = syscall(SYS_tgkill, getpid (), pthread_cancel_gettid (thd), sig);
	if (ret < 0) {
		ret = errno;
		errno = old_errno;
	}

	return ret;
}
#define pthread_kill pthread_cancel_pthread_kill

static pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t *thread = NULL;
static int *cancel = NULL;
static size_t size_thread = 0, nr_thread = 0;

#define PTHREAD_CANCEL_CANCELED ((int)0x80000000)
#define PTHREAD_CANCEL_ESRCH (-1)

static void pthread_cancel_handler (int sig)
{ 
	pthread_testcancel ();
	assert (false);
}

// must run before pthread_create
bool pthread_cancel_register_handler (void)
{
	assert (!size_thread);
	thread = malloc((size_thread = 4) * sizeof (*thread));
	cancel = malloc( size_thread      * sizeof (*cancel));

	struct sigaction actions;
	memset (&actions, 0, sizeof (actions)); 
	sigemptyset (&actions.sa_mask);
	actions.sa_flags = 0; 
	actions.sa_handler = pthread_cancel_handler;
	sigaction (PTHREAD_CANCEL_SIGCANCEL, &actions, NULL);
	return true;
}

static int pthread_cancel_getid (pthread_t thd, bool add)
{
	// assume locked
	assert (size_thread);
	for (size_t i = 0; i < nr_thread; i++)
		if (pthread_equal (thd, thread[i]))
			return i;

	if (!add)
		return PTHREAD_CANCEL_ESRCH;

	if (nr_thread == size_thread)
	{
		thread = realloc(thread, (size_thread <<= 1) * sizeof (*thread));
		cancel = realloc(cancel,  size_thread        * sizeof (*cancel));
	}

	thread[nr_thread] = thd;
	cancel[nr_thread] = PTHREAD_CANCEL_ENABLE;

	return nr_thread++;
}

static void pthread_cancel_remove (int id)
{
	thread[id] = thread[--nr_thread];
	cancel[id] = cancel[  nr_thread];
}

// return if this thread should exit
static bool pthread_cancel_shouldcancel (int id)
{
	if (id == PTHREAD_CANCEL_ESRCH)
		return false;
	if (cancel[id] != (PTHREAD_CANCEL_ENABLE | PTHREAD_CANCEL_CANCELED))
		return false;
	pthread_cancel_remove (id);
	return true;
}

int pthread_setcancelstate (int state, int *oldstate)
{
	pthread_mutex_lock (&thread_mutex);

	int id = pthread_cancel_getid (pthread_self(), true);
	bool shouldcancel = pthread_cancel_shouldcancel (id); // not required by POSIX
	if (!shouldcancel)
	{
		if (oldstate)
			*oldstate = cancel[id] & ~PTHREAD_CANCEL_CANCELED;
		cancel[id] = (cancel[id] & PTHREAD_CANCEL_CANCELED) | state;
		shouldcancel = pthread_cancel_shouldcancel (id); // not required by POSIX

		if (!shouldcancel)
		{
			sigset_t cancelset;
			sigemptyset (&cancelset);
			sigaddset (&cancelset, PTHREAD_CANCEL_SIGCANCEL);
			pthread_sigmask (state == PTHREAD_CANCEL_ENABLE ? SIG_UNBLOCK : SIG_BLOCK, &cancelset, NULL);
		}

		if (cancel[id] == PTHREAD_CANCEL_ENABLE)
			pthread_cancel_remove (id);
	}

	pthread_mutex_unlock (&thread_mutex);
	if (shouldcancel)
		pthread_exit (PTHREAD_CANCELED);
	return 0;
}

int pthread_cancel (pthread_t thd)
{
	pthread_mutex_lock (&thread_mutex);

	int id = pthread_cancel_getid (thd, true);
	bool new_cancel = (cancel[id] & PTHREAD_CANCEL_CANCELED) == 0;
	cancel[id] |= PTHREAD_CANCEL_CANCELED;

	pthread_mutex_unlock (&thread_mutex);
	if (new_cancel)
		pthread_kill (thd, PTHREAD_CANCEL_SIGCANCEL);
	return 0;
}

void pthread_testcancel (void)
{
	pthread_mutex_lock (&thread_mutex);

	bool shouldcancel = pthread_cancel_shouldcancel (pthread_cancel_getid (pthread_self (), false));

	pthread_mutex_unlock (&thread_mutex);
	if (shouldcancel)
		pthread_exit (PTHREAD_CANCELED);
}
