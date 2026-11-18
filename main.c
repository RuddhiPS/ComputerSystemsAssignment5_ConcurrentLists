
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>

// #define TOTAL_OPS 30000
#define MAX_KEY 100
// #define REPEATS 3

#define TOTAL_OPS 1000000
#define REPEATS 5

typedef struct Node {
    int key;
    struct Node *next;
    pthread_mutex_t lock;
} Node;

typedef struct {
    Node *head;
    pthread_mutex_t lock;
} List;

List list;

// Create a node
Node *new_node(int key) {
    Node *n = malloc(sizeof(Node));
    if (!n) {
        perror("malloc");
        exit(1);
    }
    n->key = key;
    n->next = NULL;
    pthread_mutex_init(&n->lock, NULL);
    return n;
}

// Create an empty sorted list with two dummy nodes
void init_list() {
    list.head = new_node(INT_MIN);
    list.head->next = new_node(INT_MAX);
    pthread_mutex_init(&list.lock, NULL);
}

// ---------------- GLOBAL LOCK ----------------

// Search for a key
int global_find(int key) {
    pthread_mutex_lock(&list.lock);

    Node *curr = list.head->next;
    while (curr->key < key)
        curr = curr->next;

    int found = (curr->key == key);
    pthread_mutex_unlock(&list.lock);
    return found;
}

// Insert only if key is absent
int global_insert(int key) {
    pthread_mutex_lock(&list.lock);

    Node *pred = list.head;
    Node *curr = pred->next;

    while (curr->key < key) {
        pred = curr;
        curr = curr->next;
    }

    int added = (curr->key != key);
    if (added) {
        Node *n = new_node(key);
        n->next = curr;
        pred->next = n;
    }

    pthread_mutex_unlock(&list.lock);
    return added;
}

// Delete only if key is present
int global_delete(int key) {
    pthread_mutex_lock(&list.lock);

    Node *pred = list.head;
    Node *curr = pred->next;

    while (curr->key < key) {
        pred = curr;
        curr = curr->next;
    }

    int deleted = (curr->key == key);
    if (deleted) {
        pred->next = curr->next;
        pthread_mutex_destroy(&curr->lock);
        free(curr);
    }

    pthread_mutex_unlock(&list.lock);
    return deleted;
}

// ---------------- HAND-OVER-HAND LOCK ----------------

// Find the correct position, keeping pred and curr locked
void find_position(int key, Node **pred, Node **curr) {
    *pred = list.head;
    pthread_mutex_lock(&(*pred)->lock);

    *curr = (*pred)->next;
    pthread_mutex_lock(&(*curr)->lock);

    while ((*curr)->key < key) {
        pthread_mutex_unlock(&(*pred)->lock);

        *pred = *curr;
        *curr = (*curr)->next;
        pthread_mutex_lock(&(*curr)->lock);
    }
}

// Search with node-by-node locking
int hoh_find(int key) {
    Node *pred, *curr;
    find_position(key, &pred, &curr);

    int found = (curr->key == key);

    pthread_mutex_unlock(&curr->lock);
    pthread_mutex_unlock(&pred->lock);
    return found;
}

// Insert with node-by-node locking
int hoh_insert(int key) {
    Node *pred, *curr;
    find_position(key, &pred, &curr);

    int added = (curr->key != key);
    if (added) {
        Node *n = new_node(key);
        n->next = curr;
        pred->next = n;
    }

    pthread_mutex_unlock(&curr->lock);
    pthread_mutex_unlock(&pred->lock);
    return added;
}

// Delete with node-by-node locking
int hoh_delete(int key) {
    Node *pred, *curr;
    find_position(key, &pred, &curr);

    int deleted = (curr->key == key);
    if (deleted) {
        pred->next = curr->next;
        pthread_mutex_unlock(&curr->lock);
        pthread_mutex_destroy(&curr->lock);
        free(curr);
    } else {
        pthread_mutex_unlock(&curr->lock);
    }

    pthread_mutex_unlock(&pred->lock);
    return deleted;
}

// ---------------- BENCHMARK ----------------

typedef struct {
    int id;
    int ops;
    int mode;  // 0 = global, 1 = hand-over-hand
    pthread_barrier_t *barrier;
} ThreadArgs;

// Simple random generator, separate for each thread
unsigned int next_random(unsigned int *seed) {
    *seed ^= *seed << 13;
    *seed ^= *seed >> 17;
    *seed ^= *seed << 5;
    return *seed;
}

void *worker(void *arg) {
    ThreadArgs *a = arg;
    unsigned int seed = 12345u + (unsigned int)a->id * 98765u;

    // All threads start together
    pthread_barrier_wait(a->barrier);

    for (int i = 0; i < a->ops; i++) {
        int key = next_random(&seed) % MAX_KEY;
        int op = next_random(&seed) % 100;

        if (a->mode == 0) {
            if (op < 80)
                global_find(key);
            else if (op < 90)
                global_insert(key);
            else
                global_delete(key);
        } else {
            if (op < 80)
                hoh_find(key);
            else if (op < 90)
                hoh_insert(key);
            else
                hoh_delete(key);
        }
    }

    return NULL;
}

// Free the list after all threads finish
void destroy_list() {
    Node *curr = list.head;

    while (curr != NULL) {
        Node *next = curr->next;
        pthread_mutex_destroy(&curr->lock);
        free(curr);
        curr = next;
    }

    pthread_mutex_destroy(&list.lock);
}

// Seconds between two timestamps
double elapsed(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) +
           (b.tv_nsec - a.tv_nsec) / 1e9;
}

// Run one benchmark
double benchmark(int mode, int threads) {
    init_list();

    // Same initial list for both methods: 50 even keys
    for (int i = 0; i < MAX_KEY; i += 2)
        global_insert(i);

    pthread_t *tid = malloc(threads * sizeof(pthread_t));
    ThreadArgs *args = malloc(threads * sizeof(ThreadArgs));
    if (!tid || !args) {
        perror("malloc");
        exit(1);
    }

    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, threads + 1);

    for (int i = 0; i < threads; i++) {
        args[i].id = i;
        args[i].ops = TOTAL_OPS / threads +
                      (i < TOTAL_OPS % threads);
        args[i].mode = mode;
        args[i].barrier = &barrier;

        if (pthread_create(&tid[i], NULL, worker, &args[i]) != 0) {
            fprintf(stderr, "Thread creation failed\n");
            exit(1);
        }
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Release all worker threads together
    pthread_barrier_wait(&barrier);

    for (int i = 0; i < threads; i++)
        pthread_join(tid[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &end);

    double throughput = TOTAL_OPS / elapsed(start, end);

    pthread_barrier_destroy(&barrier);
    free(tid);
    free(args);
    destroy_list();

    return throughput;
}

int main() {
    int counts[] = {1, 2, 4, 8, 16, 32, 64};
    int n = sizeof(counts) / sizeof(counts[0]);

    FILE *fp = fopen("results.csv", "w");
    if (!fp) {
        perror("results.csv");
        return 1;
    }

    fprintf(fp, "threads,global,hand_over_hand\n");
    printf("Threads\tGlobal\t\tHand-over-hand\n");

    for (int i = 0; i < n; i++) {
        int t = counts[i];
        double global = 0, hoh = 0;

        // Average over three runs
        for (int r = 0; r < REPEATS; r++) {
            global += benchmark(0, t);
            hoh += benchmark(1, t);
        }

        global /= REPEATS;
        hoh /= REPEATS;

        fprintf(fp, "%d,%.2f,%.2f\n", t, global, hoh);
        printf("%d\t%.2f\t%.2f\n", t, global, hoh);
        fflush(fp);
    }

    fclose(fp);
    printf("\nResults saved to results.csv\n");
    return 0;
}
