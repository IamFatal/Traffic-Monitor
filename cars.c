#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "traffic.h"

extern struct intersection isection;
extern struct car *in_cars[];
extern struct car *out_cars[];

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with 
 * its in_direction
 * 
 * Note: this also updates 'inc' on each of the lanes
 */
void parse_schedule(char *file_name) {
	int id;
	struct car *cur_car;
	enum direction in_dir, out_dir;
	FILE *f = fopen(file_name, "r");

	/* parse file */
	while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {
		printf("Car %d going from %d to %d\n", id, in_dir, out_dir);
		/* construct car */
		cur_car = malloc(sizeof(struct car));
		cur_car->id = id;
		cur_car->in_dir = in_dir;
		cur_car->out_dir = out_dir;

		/* append new car to head of corresponding list */
		cur_car->next = in_cars[in_dir];
		in_cars[in_dir] = cur_car;
		isection.lanes[in_dir].inc++;
	}

	fclose(f);
}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 * 
 */
void init_intersection() {
	int i;
	for (i = 0; i < 4; i++) {
		// initialize locks and condition variables
		pthread_mutex_init(&isection.quad[i], NULL);
		pthread_mutex_init(&isection.lanes[i].lock, NULL);
		pthread_cond_init(&isection.lanes[i].producer_cv, NULL);
		pthread_cond_init(&isection.lanes[i].consumer_cv, NULL);
		
		// allocate memory, initialize variables for circular buffers
		isection.lanes[i].buffer = malloc(sizeof(struct car) * LANE_LENGTH);
		isection.lanes[i].head = 0;
		isection.lanes[i].tail = 0;
		isection.lanes[i].capacity = LANE_LENGTH;
		isection.lanes[i].in_buf = 0;
	}
}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 * 
 */
void *car_arrive(void *arg) {
	int dir = *(int*) arg;
	struct lane *l = &(isection.lanes[dir]);

	while (in_cars[dir] != NULL) {
		pthread_mutex_lock(&l->lock);
		if (l->in_buf == l->capacity) { // if lane is full
			pthread_cond_wait(&l->producer_cv, &l->lock);
		}
		
		// add car to next available position in buffer
		l->buffer[l->tail] = in_cars[dir];
        in_cars[dir] = in_cars[dir]->next;
		printf("capacity: %d\n", l->capacity);
        l->tail = (l->tail + 1) % l->capacity;
		l->in_buf++;
		
		// data ready, signal consumer
		pthread_cond_signal(&l->consumer_cv);
		pthread_mutex_unlock(&l->lock);
	}
	
	return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list that corresponds to the car's out_dir
 * 
 * Note: For testing purposes, each car which gets to cross the 
 * intersection should print the following three numbers on a 
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 * 
 * You may add other print statements, but in the end, please 
 * make sure to clear any prints other than the one specified above, 
 * before submitting your final code. 
 */
void *car_cross(void *arg) {
	int i;
	int dir_in = *(int*) arg;
	struct lane *l = &(isection.lanes[dir_in]);
	struct car *cur_car;
	l->passed = 0;
	
	while (l->passed != l->inc) {
		pthread_mutex_lock(&l->lock);
		if (l->in_buf == 0) { // if lane is empty
			pthread_cond_wait(&l->consumer_cv, &l->lock);
		}
		
		int dir_out = l->buffer[l->head]->out_dir;
		int *path = compute_path(dir_in, dir_out);
		
		// lock intersection quadrant locks
		for (i = 0; i < 4; i++) {
			if (path[i] != -1)
				pthread_mutex_lock(&isection.quad[path[i] - 1]);
		}
		
		// car crossed, add car to out_cars and update buffer
		cur_car = l->buffer[l->head];
		cur_car->next = out_cars[cur_car->out_dir];
		out_cars[cur_car->out_dir] = cur_car;
        printf("%d %d %d\n", cur_car->in_dir, cur_car->out_dir, cur_car->id);
		l->head = (l->head + 1) % l->capacity;
		l->passed++;
		l->in_buf--;
		
		// unlock intersection quadrant locks
		for (i = 0; i < 4; i++) {
			if (path[i] != -1)
				pthread_mutex_unlock(&isection.quad[path[i] - 1]);
		}
		
		// data received, signal producer
		pthread_cond_signal(&l->producer_cv);
		pthread_mutex_unlock(&l->lock);
	}
	
	return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted 
 * list of the quadrants the car will pass through.
 * 
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {
	int *path = malloc(4 * sizeof(int));
	if(in_dir == NORTH){
		switch(out_dir){
			case SOUTH: {
				int temp_path[4] = {2 , 3 , -1 , -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case WEST:{
				int temp_path[4] = {2, -1, -1, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case EAST:{
				int temp_path[4] = {2, 3, 4, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case NORTH:{
				int temp_path[4] = {1, 2, -1, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			default :
				printf("Will not reach this");
		}
	} else if(in_dir == SOUTH){
		switch(out_dir){
			case SOUTH: {
				int temp_path[4] = {3 , 4 , -1 , -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case WEST: {
				int temp_path[4] = {1, 2, 4, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case EAST: {
				int temp_path[4] = {4, -1, -1, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case NORTH: {
				int temp_path[4] = {1, 4, -1, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			default :
				printf ("Will not reach this");
		}
	} else if(in_dir == WEST){
		switch(out_dir){
			case SOUTH: {
				int temp_path[4] = {3 , -1 , -1 , -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case WEST: {
				int temp_path[4] = {2, 3, -1, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case EAST: {
				int temp_path[4] = {3, 4, -1, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case NORTH: {
				int temp_path[4] = {1, 3, 4, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			default :
				printf ("Will not reach this");
		}
	} else if(in_dir == EAST){
		switch(out_dir){
			case SOUTH:{
				int temp_path[4] = {1 , 2 , 3 , -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case WEST: {
				int temp_path[4] = {1, 2, -1, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
		}
			case EAST: {
				int temp_path[4] = {1, 4, -1, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			case NORTH: {
				int temp_path[4] = {1, -1, -1, -1};
				memcpy(path, temp_path, sizeof(int) * 4);
				break;
			}
			default :
				printf("Will not reach this");
		}
	}
	
	return path;
}