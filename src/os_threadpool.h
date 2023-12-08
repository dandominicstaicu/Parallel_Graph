/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef __OS_THREADPOOL_H__
#define __OS_THREADPOOL_H__ 1

#include <pthread.h>
#include "os_list.h"

/* Task structure for the threadpool. */
typedef struct {
	void *argument; // Pointer to the argument of the task
	void (*action)(void *arg); // Function pointer to the task function
	void (*destroy_arg)(void *arg); // Function pointer to a function to clean up the argument
	os_list_node_t list; // List node to link this task in a queue
} os_task_t;

/* Threadpool structure. */
typedef struct os_threadpool {
	unsigned int num_threads; // Number of threads in the threadpool
	pthread_t *threads; // Array of thread IDs

	/*
	 * Head of the task queue. 
	 * The queue is implemented as a doubly-linked list.
	 * 'head.next' points to the first task, if the queue is not empty.
	 * 'head.prev' points to the last task, if the queue is not empty.
	 */
	os_list_node_t head;

	/* Threadpool and queue synchronization data. */
	int shutdown; // Flag to indicate if the threadpool is shutting down

	pthread_mutex_t task_lock; // Mutex for synchronizing access to the task queue
	pthread_cond_t task_cond; // Condition variable for task availability

	int queued_tasks; // Counter for the number of tasks currently queued
	pthread_mutex_t finished_tasks_mutex; // Mutex for synchronizing the completion of tasks
	pthread_cond_t finished_tasks_cond; // Condition variable for task completion
} os_threadpool_t;

/* Function declarations. */
os_task_t *create_task(void (*f)(void *), void *arg, void (*destroy_arg)(void *));
void destroy_task(os_task_t *t);

os_threadpool_t *create_threadpool(unsigned int num_threads);
void destroy_threadpool(os_threadpool_t *tp);

void enqueue_task(os_threadpool_t *q, os_task_t *t);
os_task_t *dequeue_task(os_threadpool_t *tp);
void wait_for_completion(os_threadpool_t *tp);

#endif /* __OS_THREADPOOL_H__ */
