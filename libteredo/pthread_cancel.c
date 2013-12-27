#include "pthread_cancel.h"
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
static inline pid_t pthread_cancel_gettid (pthread_t thd)
{
	if (pthread_equal (pthread_self (), thd))
		return gettid ();
	return BIONIC_TID (thd);
}

// minimal pthread_cancel implementation using signals
static pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;
typedef struct
{
	pid_t tid;
	int state;
	pthread_cond_t startup_cond;
	bool startup;
} cancel;
static cancel *thread;
static size_t size_thread = 0, nr_thread = 0;

#define PTHREAD_CANCEL_CANCELED ((int)0x80000000)
#define PTHREAD_CANCEL_STARTUP  ((int)0x40000000)
#define PTHREAD_CANCEL_SHOULDCANCEL (PTHREAD_CANCEL_ENABLE | PTHREAD_CANCEL_CANCELED)
#define PTHREAD_CANCEL_INTERNAL (PTHREAD_CANCEL_CANCELED | PTHREAD_CANCEL_STARTUP)
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

	struct sigaction actions;
	memset (&actions, 0, sizeof (actions)); 
	sigemptyset (&actions.sa_mask);
	actions.sa_flags = 0; 
	actions.sa_handler = pthread_cancel_handler;
	sigaction (PTHREAD_CANCEL_SIGCANCEL, &actions, NULL);
	return true;
}

static int pthread_cancel_find (pid_t tid, bool add)
{
	// assume locked
	assert (size_thread);
	for (size_t i = 0; i < nr_thread; i++)
		if (thread[i].tid == tid)
			return i;

	if (!add)
		return PTHREAD_CANCEL_ESRCH;

	if (nr_thread == size_thread)
		thread = realloc(thread, (size_thread <<= 1) * sizeof (*thread));

	thread[nr_thread].tid = tid;
	thread[nr_thread].state = PTHREAD_CANCEL_ENABLE;
	pthread_cond_init (&thread[nr_thread].startup_cond, NULL);

	return nr_thread++;
}

inline extern void pthread_cancel_remove (int id)
{
	thread[id] = thread[--nr_thread];
}

// return if this thread should exit
static bool pthread_cancel_shouldcancel (int id)
{
	if (id == PTHREAD_CANCEL_ESRCH)
		return false;
	if ((thread[id].state & PTHREAD_CANCEL_SHOULDCANCEL) != PTHREAD_CANCEL_SHOULDCANCEL)
		return false;
	pthread_cancel_remove (id);
	return true;
}

int pthread_setcancelstate (int state, int *oldstate)
{
	pthread_mutex_lock (&thread_mutex);

	int id = pthread_cancel_find (gettid (), true); // in case of another signal handler
	bool shouldcancel = pthread_cancel_shouldcancel (id); // not required by POSIX
	if (!shouldcancel)
	{
		if (oldstate)
			*oldstate = thread[id].state & ~PTHREAD_CANCEL_INTERNAL;
		assert (!(state & PTHREAD_CANCEL_INTERNAL));
		thread[id].state = (thread[id].state & PTHREAD_CANCEL_INTERNAL) | state;
		shouldcancel = pthread_cancel_shouldcancel (id); // not required by POSIX

		if (!shouldcancel)
		{
			sigset_t cancelset;
			sigemptyset (&cancelset);
			sigaddset (&cancelset, PTHREAD_CANCEL_SIGCANCEL);
			pthread_sigmask (state == PTHREAD_CANCEL_ENABLE ? SIG_UNBLOCK : SIG_BLOCK, &cancelset, NULL);
		}

		if (thread[id].state == PTHREAD_CANCEL_ENABLE) // default
			pthread_cancel_remove (id);
	}

	pthread_mutex_unlock (&thread_mutex);
	if (shouldcancel)
		pthread_exit (PTHREAD_CANCELED);
	return 0;
}

#define SYS_tgkill __NR_tgkill
// copy from bionic
static int pthread_cancel_tkill (pid_t tid, int sig)
{
	int  ret;
	int  old_errno = errno;
	pid_t pid = getpid ();
	pthread_mutex_lock (&thread_mutex);

	int id = pthread_cancel_find (tid, false);
	if (id != PTHREAD_CANCEL_ESRCH)
		while (((size_t)id) < nr_thread && (thread[id].state & PTHREAD_CANCEL_STARTUP))
			pthread_cond_wait(&thread[id].startup_cond, &thread_mutex);

	pthread_mutex_unlock (&thread_mutex);

	ret = syscall(SYS_tgkill, pid, tid, sig);
	if (ret < 0) {
		ret = errno;
		errno = old_errno;
	}

	return ret;
}
int __wrap_pthread_kill (pthread_t thd, int sig) 
{
	return pthread_cancel_tkill (pthread_cancel_gettid (thd), sig);
}
// avoid race condition
typedef struct trampoline_arg
{
	void *(*start_routine) (void *);
	void *arg;
} trampoline_arg;
static void *thread_trampoline (void *opaque)
{
	trampoline_arg *new_arg = (trampoline_arg *)opaque;
	void *(*start_routine) (void *) = new_arg->start_routine;
	void *arg = new_arg->arg;
	free (new_arg);
	pthread_mutex_lock (&thread_mutex);

	int id = pthread_cancel_find (gettid (), false);
	assert (id != PTHREAD_CANCEL_ESRCH);
	thread[id].state &= ~PTHREAD_CANCEL_STARTUP;
	pthread_cond_broadcast (&thread[id].startup_cond);

	pthread_mutex_unlock (&thread_mutex);
	return start_routine (arg);
}
int __real_pthread_create(pthread_t *thd, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
int __wrap_pthread_create(pthread_t *thd, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg)
{
	trampoline_arg *new_arg = malloc (sizeof (trampoline_arg));
	new_arg->start_routine = start_routine;
	new_arg->arg = arg;
	pthread_mutex_lock (&thread_mutex);

	int ret = __real_pthread_create (thd, attr, thread_trampoline, new_arg);
	if (!ret)
	{
		int tid = pthread_cancel_gettid (*thd);
		int id = pthread_cancel_find (tid, true);
		assert (id != PTHREAD_CANCEL_ESRCH);
		thread[id].state |= PTHREAD_CANCEL_STARTUP;
		// pthread_cond_signal (thread[id].startup_cond); // we only wait for !startup
	}

	pthread_mutex_unlock (&thread_mutex);
	return ret;
}

int pthread_cancel (pthread_t thd)
{
	pid_t tid = pthread_cancel_gettid (thd);
	pthread_mutex_lock (&thread_mutex);

	int id = pthread_cancel_find (tid, true);
	bool new_cancel = (thread[id].state & PTHREAD_CANCEL_CANCELED) == 0;
	thread[id].state |= PTHREAD_CANCEL_CANCELED;

	pthread_mutex_unlock (&thread_mutex);
	if (new_cancel)
		pthread_cancel_tkill (tid, PTHREAD_CANCEL_SIGCANCEL);
	return 0;
}

void pthread_testcancel (void)
{
	pthread_mutex_lock (&thread_mutex);

	bool shouldcancel = pthread_cancel_shouldcancel (pthread_cancel_find (gettid (), false));

	pthread_mutex_unlock (&thread_mutex);
	if (shouldcancel)
		pthread_exit (PTHREAD_CANCELED);
}
