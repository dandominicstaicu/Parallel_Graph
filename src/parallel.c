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

static int sum;
static os_graph_t *graph;
static os_threadpool_t *tp;
/* TODO: Define graph synchronization mechanisms. */
pthread_mutex_t sum_lock;
pthread_mutex_t *visit_locks;

/* Variable used to count the number of visited nodes */
unsigned int nr_visited = 0;

/* TODO: Define graph task argument. */
typedef struct {
    // os_threadpool_t *tp;
    // pthread_mutex_t *sum_lock;
    // pthread_mutex_t *visited_locks;
    int index;
} process_node_t;

int processing_is_done(os_threadpool_t *tp) 
{
    return nr_visited == graph->num_nodes ? 1 : 0;
}

void os_destroy_arg(void *arg)
{
	process_node_t *tmp = (process_node_t *) arg;
	free(tmp);
}

void process_node_function(void *arg) 
{
	process_node_t *tmp = (process_node_t *) arg;
	int index = tmp->index;
	// free(tmp);

	// the actual graph node
	os_node_t *node = graph->nodes[index];

	pthread_mutex_lock(&sum_lock);
	sum += node->info;
	nr_visited++;
	pthread_mutex_unlock(&sum_lock);

	// printf("Visited node %d, sum = %d\n", index, sum);

	for (unsigned i = 0; i < node->num_neighbours; ++i) {
		pthread_mutex_lock(&visit_locks[node->neighbours[i]]);

		if (graph->visited[node->neighbours[i]] == NOT_VISITED) {
			graph->visited[node->neighbours[i]] = PROCESSING;

			process_node_t *tmp = (process_node_t *) malloc(sizeof(process_node_t));
			tmp->index = node->neighbours[i];

			os_task_t *task = create_task(process_node_function, (void*) tmp, os_destroy_arg);
			enqueue_task(tp, task);

			// printf("Enqueued node %d\n", i);
		}

		pthread_mutex_unlock(&visit_locks[node->neighbours[i]]);
	}
}

static void process_node(unsigned int idx)
{
	/* TODO: Implement thread-pool based processing of graph. */

	// TODO: idx is always 0, remeber this when you move the code back in main
	for (unsigned int i = idx; i < graph->num_nodes; ++i) {
		pthread_mutex_lock(&visit_locks[i]);

		if (graph->visited[i] == NOT_VISITED) {
			graph->visited[i] = PROCESSING;

			process_node_t *arg = (process_node_t *) malloc(sizeof(process_node_t));
			// arg->tp = tp;
			// arg->sum_lock = &sum_lock;
			arg->index = i;
			// arg->visited_locks = visit_locks;

			// new_task = create_task(process_neighbours, (void *) new_arg, os_destroy_arg);
			os_task_t *task = create_task(process_node_function, (void*) arg, os_destroy_arg);
			enqueue_task(tp, task);

			// printf("Enqueued node %d\n", i);
		}

		pthread_mutex_unlock(&visit_locks[i]);
	}
}

void free_graph(os_graph_t *graph)
{
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

	/* --------------------------- Thread Pool --------------------------- */
	/* TODO: Initialize graph synchronization mechanisms. */
	tp = create_threadpool(NUM_THREADS);
	
	pthread_mutex_init(&sum_lock, NULL);

	visit_locks = (pthread_mutex_t *) malloc(graph->num_nodes * sizeof(pthread_mutex_t));
	for (unsigned int i = 0; i < graph->num_nodes; ++i) {
		pthread_mutex_init(&visit_locks[i], NULL);
	}

	process_node(0);

	wait_for_completion(tp, processing_is_done);
	destroy_threadpool(tp);

	/* --------------------------- Thread Pool --------------------------- */

	printf("%d", sum);

	free(visit_locks);
	free_graph(graph);
	fclose(input_file);

	return 0;
}
