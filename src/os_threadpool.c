// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

/* Create a task that would be executed by a thread. */
os_task_t *create_task(void (*action)(void *), void *arg, void (*destroy_arg)(void *))
{
	os_task_t *t;

	t = malloc(sizeof(*t));
	DIE(t == NULL, "malloc");

	t->action = action;		// the function
	t->argument = arg;		// arguments for the function
	t->destroy_arg = destroy_arg;	// destroy argument function

	return t;
}

/* Destroy task. */
void destroy_task(os_task_t *t)
{
	if (t->destroy_arg != NULL)
		t->destroy_arg(t->argument);
	free(t);
}

/* Put a new task to threadpool task queue. */
void enqueue_task(os_threadpool_t *tp, os_task_t *t)
{
	assert(tp != NULL);
	assert(t != NULL);

	/* Enqueue task to the shared task queue. Use synchronization. */
	pthread_mutex_lock(&tp->task_lock); // Lock the task queue mutex
	list_add_tail(&tp->head, &t->list); // Add the task to the end of the queue
	pthread_cond_signal(&tp->task_cond); // Signal a waiting thread that a task is available
	pthread_mutex_unlock(&tp->task_lock); // Unlock the task queue mutex
}

/*
 * Check if queue is empty.
 * This function should be called in a synchronized manner.
 */
static int queue_is_empty(os_threadpool_t *tp)
{
	return list_empty(&tp->head);
}

/*
 * Get a task from threadpool task queue.
 * Block if no task is available.
 * Return NULL if work is complete, i.e. no task will become available,
 * i.e. all threads are going to block.
 */
os_task_t *dequeue_task(os_threadpool_t *tp)
{
	os_task_t *t = NULL;

	/* Dequeue task from the shared task queue. Use synchronization. */
	pthread_mutex_lock(&tp->task_lock);

	// Wait for a task to become available or for the threadpool to shut down
	while (queue_is_empty(tp) && !tp->shutdown) {/* && not shutting down condition */
		pthread_cond_wait(&tp->task_cond, &tp->task_lock);
	}

	// If the thread pool is shutting down, release mutex and return NULL
	if (tp->shutdown) {
		pthread_mutex_unlock(&tp->task_lock);
		return NULL;
	}

	// If there is a task in the queue, remove it and prepare to execute
	if (!queue_is_empty(tp)) {
		os_list_node_t *node = tp->head.next;

		list_del(node);
		t = list_entry(node, os_task_t, list);
	}

	pthread_mutex_unlock(&tp->task_lock);

	return t;
}

/* Loop function for threads */
static void *thread_loop_function(void *arg)
{
	os_threadpool_t *tp = (os_threadpool_t *) arg;

	while (1) {
		os_task_t *t;

		t = dequeue_task(tp);
		if (t == NULL)
			break;
		t->action(t->argument);
		destroy_task(t);
	}

	return NULL;
}

/* Wait completion of all threads. This is to be called by the main thread. */
void wait_for_completion(os_threadpool_t *tp)
{
	// Wait for the signal indicating all tasks are finished
	pthread_mutex_lock(&tp->finished_tasks_mutex);
	while (tp->queued_tasks > 0)
		pthread_cond_wait(&tp->finished_tasks_cond, &tp->finished_tasks_mutex);
	pthread_mutex_unlock(&tp->finished_tasks_mutex);

	// Signal all threads to shut down
	pthread_mutex_lock(&tp->task_lock);
	tp->shutdown = 1;
	pthread_cond_broadcast(&tp->task_cond);
	pthread_mutex_unlock(&tp->task_lock);

	/* Join all worker threads. */
	for (unsigned int i = 0; i < tp->num_threads; i++)
		pthread_join(tp->threads[i], NULL);
}

/* Create a new threadpool. */
os_threadpool_t *create_threadpool(unsigned int num_threads)
{
	os_threadpool_t *tp = NULL;
	int rc;

	tp = malloc(sizeof(*tp));
	DIE(tp == NULL, "malloc");

	list_init(&tp->head);

	/* Initialize synchronization data. */
	tp->shutdown = 0;
	tp->queued_tasks = 0;
	pthread_mutex_init(&tp->task_lock, NULL);
	pthread_cond_init(&tp->task_cond, NULL);
	pthread_cond_init(&tp->finished_tasks_cond, NULL);
	pthread_mutex_init(&tp->finished_tasks_mutex, NULL);

	tp->num_threads = num_threads;
	tp->threads = malloc(num_threads * sizeof(*tp->threads));
	DIE(tp->threads == NULL, "malloc");
	for (unsigned int i = 0; i < num_threads; ++i) {
		rc = pthread_create(&tp->threads[i], NULL, &thread_loop_function, (void *) tp);
		DIE(rc < 0, "pthread_create");
	}

	return tp;
}

/* Destroy a threadpool. Assume all threads have been joined. */
void destroy_threadpool(os_threadpool_t *tp)
{
	if (tp == NULL)
		return;

	pthread_mutex_destroy(&tp->task_lock);
	pthread_cond_destroy(&tp->task_cond);

	pthread_mutex_destroy(&tp->finished_tasks_mutex);
	pthread_cond_destroy(&tp->finished_tasks_cond);

	os_list_node_t *n, *p;

	/* Cleanup synchronization data. */

	list_for_each_safe(n, p, &tp->head) {
		list_del(n);
		destroy_task(list_entry(n, os_task_t, list));
	}

	free(tp->threads);
	free(tp);
}
