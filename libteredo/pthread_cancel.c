#include "pthread_cancel.h"
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>

static pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t *thread = NULL;
static int *cancel = NULL;
static size_t size_thread = 0, nr_thread = 0;

#define PTHREAD_CANCEL_CANCELED ((int)0x80000000)

static void pthread_cancel_handler (int sig)
{ 
	pthread_testcancel ();
}

// must run before pthread_create
bool pthread_cancel_register_handler (void)
{
	struct sigaction actions;
	memset (&actions, 0, sizeof (actions)); 
	sigemptyset (&actions.sa_mask);
	actions.sa_flags = 0; 
	actions.sa_handler = pthread_cancel_handler;
	sigaction (SIGUSR1, &actions, NULL);
	return true;
}

static int pthread_cancel_getid (pthread_t thd, bool add)
{
	// assume locked
	for (size_t i = 0; i < nr_thread; i++)
		if (pthread_equal(thd, thread[i]))
			return i;

	if (!add)
		return -1;

	if (size_thread == 0)
	{
		thread = realloc(thread, (size_thread = 4) * sizeof (*thread));
		cancel = realloc(cancel,  size_thread      * sizeof (*cancel));
	}

	if (nr_thread == size_thread)
	{
		thread = realloc(thread, (size_thread <<= 1) * sizeof (*thread));
		cancel = realloc(cancel,  size_thread        * sizeof (*cancel));
	}

	thread[nr_thread] = thd;
	cancel[nr_thread] = PTHREAD_CANCEL_ENABLE;

	return nr_thread++;
}

static bool pthread_cancel_shouldcancel (int id)
{
	if (cancel[id] != (PTHREAD_CANCEL_ENABLE | PTHREAD_CANCEL_CANCELED))
		return false;
	thread[id] = thread[--nr_thread];
	cancel[id] = cancel[  nr_thread];
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
	}

	pthread_mutex_unlock (&thread_mutex);
	if (shouldcancel)
		pthread_exit (PTHREAD_CANCELED);
	return 0;
}

int pthread_cancel (pthread_t thd)
{
	pthread_mutex_lock (&thread_mutex);

	bool shouldcancel = (cancel[pthread_cancel_getid (thd, true)] |= PTHREAD_CANCEL_CANCELED) == (PTHREAD_CANCEL_ENABLE | PTHREAD_CANCEL_CANCELED);

	pthread_mutex_unlock (&thread_mutex);
	if (shouldcancel)
		pthread_kill (thd, SIGUSR1);
	return 0;
}

void pthread_testcancel (void)
{
	pthread_mutex_lock (&thread_mutex);

	int id = pthread_cancel_getid (pthread_self (), false);
	bool shouldcancel = id >= 0 && pthread_cancel_shouldcancel (id);

	pthread_mutex_unlock (&thread_mutex);
	if (shouldcancel)
		pthread_exit (PTHREAD_CANCELED);
}
