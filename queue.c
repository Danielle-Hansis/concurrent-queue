#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

mtx_t queue_mutex_lock;

// this structure is used to manage threads that are waiting for an item to be dequeued
typedef struct Dequeue_waiter
{
    cnd_t my_cond;
    struct Dequeue_waiter *next;
    void *item;
    int has_item;
} Dequeue_waiter;

// this structure is used to manage the queue of threads waiting for an item to be dequeued
typedef struct Dequeu_waiter_queue
{
    Dequeue_waiter *head;
    Dequeue_waiter *tail;
} Dequeu_waiter_queue;

static Dequeu_waiter_queue *dequeue_waiters;

// this structure is used to represent items in the queue
typedef struct Node 
{
    void *user_item; 
    struct Node *next; 
} Node;

// this structure is used to represent the queue itself
typedef struct Queue 
{
    Node *head; 
    Node *tail; 
    size_t queue_size;
    atomic_size_t visit_count;
} Queue;

static Queue *queue;

//====================================================

void initQueue(void);
void destroyQueue(void);
void enqueue(void*);
void* dequeue(void);
size_t visited(void);

//====================================================

void initQueue(void)
{
    queue = malloc(sizeof(Queue));
    dequeue_waiters = malloc(sizeof(Dequeu_waiter_queue));

    // Initialize the queue:
    queue->head = NULL;
    queue->tail = NULL; 
    queue->queue_size = 0;
    atomic_init(&queue->visit_count, 0);

    // Initialize the dequeue waiters queue:
    dequeue_waiters->head = NULL;
    dequeue_waiters->tail = NULL;

    // initialize the mutex:
    mtx_init(&queue_mutex_lock, mtx_plain);
}    

void destroyQueue(void)
{
    Node *current_node;
    Node *next_node;
    Dequeue_waiter *current_waiter;
    Dequeue_waiter *next_waiter;


    mtx_destroy(&queue_mutex_lock);

    // free the entire queue:
    current_node= queue->head;
    while (current_node != NULL)
     {
        next_node = current_node->next;
        free(current_node);
        current_node = next_node;
    }
    free(queue);

    // free the dequeue waiters queue:
    current_waiter = dequeue_waiters->head;
    while (current_waiter != NULL)
    {
        next_waiter = current_waiter->next;
        cnd_destroy(&current_waiter->my_cond);
        current_waiter = next_waiter;
    }
    free(dequeue_waiters);
}

void enqueue(void* item)
{
    mtx_lock(&queue_mutex_lock);
    // If there are threads waiting for an item to be dequeued, signal the first one:
    if (dequeue_waiters->head != NULL)
    {
        Dequeue_waiter *head = dequeue_waiters->head;
        head->item = item;
        head->has_item = 1;
        cnd_signal(&head->my_cond);

        if (head->next == NULL)
        {
            dequeue_waiters->head = NULL;
            dequeue_waiters->tail = NULL;
        }
        else
        {
            dequeue_waiters->head = head->next;
        }
    }
    // If there are no threads waiting, proceed with enqueue operation:
    else
    {
        // create new node:
        Node *new_node;
        new_node = malloc(sizeof(Node));
        new_node->user_item = item;
        new_node->next = NULL;
        // link new node to queue:
        queue->queue_size++;
        // If the queue was empty, set head and tail to new node
        if(queue->tail == NULL) 
        {
            queue->head = new_node;
            queue->tail = new_node;
        } 
        // Link the new node to the end of the queue:
        else
        {
            queue->tail->next = new_node;
            queue->tail = new_node;
        }  
    }
    mtx_unlock(&queue_mutex_lock);
}

void* dequeue(void)
{
    mtx_lock(&queue_mutex_lock);
    Dequeue_waiter self;
    void *first_node_item;
    // If the queue is empty, wait for an item to be enqueued:
    if (queue->queue_size == 0) 
    {
        self.has_item = 0;
        self.item = NULL;
        self.next = NULL;
        cnd_init(&self.my_cond);
        
        // Add the current thread to the dequeue waiters queue:
        if (dequeue_waiters->head == NULL)
        {
            dequeue_waiters->head = &self;
            dequeue_waiters->tail = &self;
        } 
        else 
        {
            dequeue_waiters->tail->next = &self;
            dequeue_waiters->tail = &self;
        }
        // Wait until an item is available (loop is for spurious wakeups):
        while (!self.has_item) 
        {
            cnd_wait(&self.my_cond, &queue_mutex_lock);
        }
        
        first_node_item = self.item;
        cnd_destroy(&self.my_cond);
    }
    // If the queue is not empty, proceed with dequeue operation:
    else 
    {
        first_node_item = queue->head->user_item;
        // If there is only one item in the queue, reset head and tail:
        if (queue->head == queue->tail) 
        { 
            free(queue->head);
            queue->head = NULL;
            queue->tail = NULL;
        }
        // If there are multiple items, remove the head and update the head pointer:
        else 
        {
            Node *first_node = queue->head;
            queue->head = queue->head->next;
            free(first_node);
        }
        queue->queue_size--;
    }
    
    atomic_fetch_add(&queue->visit_count, 1);
    mtx_unlock(&queue_mutex_lock);
    return first_node_item;
}

size_t visited(void)
{
    size_t visited = atomic_load(&queue->visit_count);
    return visited;
}

//====================================================

// add chatgpt link : https://chatgpt.com/share/68456d35-dfb4-8001-ac92-4a30220f64c1