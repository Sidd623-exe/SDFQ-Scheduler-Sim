#include <stdio.h>
#include <stdlib.h>
#include "task.h"

#define MAX_TASKS 1000
Task* ready_heap[MAX_TASKS];
int heap_size = 0;

void swap_tasks(int i, int j){
    Task* temp = ready_heap[i];
    ready_heap[i] = ready_heap[j];
    ready_heap[j] = temp;
}

void heapif_up(int index){
    if(index == 0) return;
    int parent = (index - 1) / 2;
    if(ready_heap[index]->vruntime < ready_heap[parent]->vruntime){
        swap_tasks(index, parent);
        heapif_up(parent);
    }
}

void heapif_down(int index){
    int left =2*index + 1;
    int right =2*index + 2;
    int smallest = index;

    if(left < heap_size && ready_heap[left]->vruntime < ready_heap[smallest]->vruntime){
        smallest = left;
    }

    if(right < heap_size && ready_heap[right]->vruntime < ready_heap[smallest]->vruntime){
        smallest = right;
    }

    if(smallest != index){
        swap_tasks(index, smallest);
        heapif_down(smallest);
    }
}

void heap_push(Task* t){
    ready_heap[heap_size] = t;
    heapif_up(heap_size);
    heap_size++;
}

Task* heap_pop(){
    if(heap_size == 0) return NULL;
    Task* min_task = ready_heap[0];
    ready_heap[0] = ready_heap[heap_size - 1];
    heap_size--;
    heapif_down(0);
    return min_task;
}
void run_simulation(int total_time){
    Task* current_task = NULL;
    for(int t=0;t<total_time;t++){
        if(current_task == NULL && heap_size > 0){
            current_task = heap_pop();
            current_task->state = TASK_RUNNING;
            printf("[Tick %d] Task %d scheduled on CPU (vruntime: %ld)\n", 
                   t, current_task->id, current_task->vruntime);
        }

        if(current_task != NULL){
            current_task->burst_time--;
            current_task->vruntime++;
            if(current_task->burst_time == 0){
                printf("[Tick %d] Task %d completed\n", t, current_task->id);
                current_task->state = TASK_TERMINATED;
                free(current_task);
                current_task = NULL;
            }
        }
    }
}