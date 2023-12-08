// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#include "os_graph.h"
#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

#define NUM_THREADS		4

static int sum; // Global variable to store the sum of all nodes.
static os_graph_t *graph; // Pointer to the graph
static os_threadpool_t *tp; // Pointer to the threadpool

/* Define graph synchronization mechanisms. */
pthread_mutex_t sum_lock; // Mutex for accesing the sum variable.
pthread_mutex_t *visit_locks; // Mutex for accesing the visited array.

/* Structure to hold arguments for graph node processing task. */
typedef struct {
	int index;
} process_node_t;

void os_destroy_arg(void *arg)
{
	process_node_t *tmp = (process_node_t *) arg;

	free(tmp); // Free the memory allocated for the task argument
}

void process_node_function(void *arg)
{
	process_node_t *tmp = (process_node_t *) arg;
	int index = tmp->index;

	// the actual graph node
	os_node_t *node = graph->nodes[index];

	// Lock the sum mutex, add node's value to sum, and unlock the mutex.
	pthread_mutex_lock(&sum_lock);
	sum += node->info;
	pthread_mutex_unlock(&sum_lock);

	// Iterate over the neighbours of the current node
	for (unsigned int i = 0; i < node->num_neighbours; ++i) {
		pthread_mutex_lock(&visit_locks[node->neighbours[i]]);

		// Check if the neighbour node has not been visited
		if (graph->visited[node->neighbours[i]] == NOT_VISITED) {
			graph->visited[node->neighbours[i]] = PROCESSING;

			// Allocate and set up the task for processing the neighbour node
			process_node_t *tmp = (process_node_t *) malloc(sizeof(process_node_t));

			tmp->index = node->neighbours[i];

			os_task_t *task = create_task(process_node_function, (void *) tmp, os_destroy_arg);

			// Update the task count and enqueue the task in the thread pool
			pthread_mutex_lock(&tp->finished_tasks_mutex);
			++tp->queued_tasks;
			pthread_mutex_unlock(&tp->finished_tasks_mutex);

			enqueue_task(tp, task);
		}

		pthread_mutex_unlock(&visit_locks[node->neighbours[i]]);
	}

	// Update the graph and thread pool state
	pthread_mutex_lock(&tp->finished_tasks_mutex);

	pthread_mutex_lock(&visit_locks[index]);
	graph->visited[index] = DONE;
	pthread_mutex_unlock(&visit_locks[index]);

	if (--tp->queued_tasks == 0)
		pthread_cond_signal(&tp->finished_tasks_cond); // Signal if all tasks are done

	pthread_mutex_unlock(&tp->finished_tasks_mutex);
}

static void process_node(unsigned int idx)
{
	pthread_mutex_lock(&visit_locks[idx]);

	// Check if the current node has not been visited
	if (graph->visited[idx] == NOT_VISITED) {
		graph->visited[idx] = PROCESSING;

		// Allocate and set up the task for processing the current node
		process_node_t *arg = (process_node_t *) malloc(sizeof(process_node_t));

		arg->index = idx;

		os_task_t *task = create_task(process_node_function, (void *) arg, os_destroy_arg);

		// Update the task count and enqueue the task in the thread pool
		pthread_mutex_lock(&tp->finished_tasks_mutex);
		++tp->queued_tasks;
		pthread_mutex_unlock(&tp->finished_tasks_mutex);

		enqueue_task(tp, task);
	}

	pthread_mutex_unlock(&visit_locks[idx]);
}

void free_graph(os_graph_t *graph)
{
	// Free the memory allocated for the graph
	for (unsigned int i = 0; i < graph->num_nodes; ++i) {
		free(graph->nodes[i]->neighbours);
		free(graph->nodes[i]);
	}

	free(graph->nodes);
	free(graph->visited);
	free(graph);
}

int main(int argc, char *argv[])
{
	FILE *input_file;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s input_file\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	input_file = fopen(argv[1], "r");
	DIE(input_file == NULL, "fopen");

	graph = create_graph_from_file(input_file);

	/* Initialize graph synchronization mechanisms. */
	tp = create_threadpool(NUM_THREADS);

	pthread_mutex_init(&sum_lock, NULL);

	visit_locks = (pthread_mutex_t *) malloc(graph->num_nodes * sizeof(pthread_mutex_t));
	for (unsigned int i = 0; i < graph->num_nodes; ++i)
		pthread_mutex_init(&visit_locks[i], NULL);

	process_node(0);

	wait_for_completion(tp);
	destroy_threadpool(tp);

	pthread_mutex_destroy(&sum_lock);

	visit_locks = (pthread_mutex_t *) malloc(graph->num_nodes * sizeof(pthread_mutex_t));
	for (unsigned int i = 0; i < graph->num_nodes; ++i)
		pthread_mutex_destroy(&visit_locks[i]);

	printf("%d", sum);

	free(visit_locks);
	free_graph(graph);
	fclose(input_file);

	return 0;
}
