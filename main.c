#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define STDOUT_FD 1
#define STDERR_FD 2

typedef struct {
    pthread_mutex_t mutex; 
    pthread_cond_t cond;   
    int count;             
    int total;             
} Barrier;

void barrier_init(Barrier* barrier, int total) {
    pthread_mutex_init(&barrier->mutex, NULL);
    pthread_cond_init(&barrier->cond, NULL);
    barrier->count = 0;
    barrier->total = total;
}

void barrier_wait(Barrier* barrier) {
    pthread_mutex_lock(&barrier->mutex);
    barrier->count++;
    if (barrier->count < barrier->total) {
        pthread_cond_wait(&barrier->cond, &barrier->mutex);
    }
    else {
        barrier->count = 0;
        pthread_cond_broadcast(&barrier->cond);
    }
    pthread_mutex_unlock(&barrier->mutex);
}

void barrier_destroy(Barrier* barrier) {
    pthread_mutex_destroy(&barrier->mutex);
    pthread_cond_destroy(&barrier->cond);
}

typedef struct {
    int* array;
    int n;
    int thread_id;
    int total_threads;
    Barrier* barrier;
} ThreadData;

static void print_str(int fd, const char* str) {
    write(fd, str, strlen(str));
}

static void print_int(int fd, long long val) {
    char buf[32];
    int idx = 0;
    if (val == 0) {
        write(fd, "0", 1);
        return;
    }
    if (val < 0) {
        write(fd, "-", 1);
        val = -val;
    }
    while (val > 0) {
        buf[idx++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int i = 0; i < idx / 2; i++) {
        char tmp = buf[i];
        buf[i] = buf[idx - 1 - i];
        buf[idx - 1 - i] = tmp;
    }
    write(fd, buf, idx);
}

static void print_double(int fd, double val, int precision) {
    if (val < 0) {
        write(fd, "-", 1);
        val = -val;
    }
    long long int_part = (long long)val;
    print_int(fd, int_part);
    write(fd, ".", 1);

    double frac = val - (double)int_part;
    for (int i = 0; i < precision; i++) {
        frac *= 10.0;
        int digit = (int)frac;
        char c = (char)('0' + digit);
        write(fd, &c, 1);
        frac -= digit;
    }
}

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static inline void compare_and_swap(int* arr, int i, int j) {
    if (arr[i] > arr[j]) {
        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}

void odd_even_merge_sort_seq(int* arr, int n) {
    for (int p = n / 2; p > 0; p >>= 1) {
        int q = n / 2;
        int r = 0;
        int d = p;
        while (d > 0) {
            for (int i = 0; i < n - d; i++) {
                if ((i & p) == r) {
                    compare_and_swap(arr, i, i + d);
                }
            }
            d = q - p;
            q >>= 1;
            r = p;
        }
    }
}

static void* thread_worker(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    int tid = data->thread_id;
    int num_threads = data->total_threads;
    int n = data->n;
    int* arr = data->array;

    for (int p = n / 2; p > 0; p >>= 1) {
        int q = n / 2;
        int r = 0;
        int d = p;
        while (d > 0) {
            int total_pairs = n - d;
            int chunk = total_pairs / num_threads;
            int start = tid * chunk;
            int end = (tid == num_threads - 1) ? total_pairs : start + chunk;

            for (int i = start; i < end; i++) {
                if ((i & p) == r) {
                    compare_and_swap(arr, i, i + d);
                }
            }
            barrier_wait(data->barrier);

            d = q - p;
            q >>= 1;
            r = p;
        }
    }
    return NULL;
}

void odd_even_merge_sort_par(int* arr, int n, int num_threads) {
    if (num_threads <= 1) {
        odd_even_merge_sort_seq(arr, n);
        return;
    }

    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    ThreadData* tdata = (ThreadData*)malloc(sizeof(ThreadData) * num_threads);
    Barrier barrier;

    barrier_init(&barrier, num_threads);

    for (int i = 0; i < num_threads; i++) {
        tdata[i].array = arr;
        tdata[i].n = n;
        tdata[i].thread_id = i;
        tdata[i].total_threads = num_threads;
        tdata[i].barrier = &barrier;
        pthread_create(&threads[i], NULL, thread_worker, &tdata[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    barrier_destroy(&barrier);
    free(threads);
    free(tdata);
}

static int is_sorted(const int* arr, int n) {
    for (int i = 1; i < n; i++) {
        if (arr[i - 1] > arr[i]) return 0;
    }
    return 1;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_str(STDERR_FD, "Usage: ./batcher_sort <array_size> <num_threads> <mode: -s|-p|-b>\n");
        return 1;
    }

    int n = atoi(argv[1]);
    int num_threads = atoi(argv[2]);
    char* mode = argv[3];

    if (n <= 0 || (n & (n - 1)) != 0) {
        print_str(STDERR_FD, "Error: array_size must be a power of 2!\n");
        return 1;
    }

    srand(42);
    int* arr_seq = (int*)malloc(sizeof(int) * n);
    int* arr_par = (int*)malloc(sizeof(int) * n);

    for (int i = 0; i < n; i++) {
        int val = rand() % 10000000;
        arr_seq[i] = val;
        arr_par[i] = val;
    }

    if (strcmp(mode, "-s") == 0) {
        double t0 = get_time_ms();
        odd_even_merge_sort_seq(arr_seq, n);
        double t1 = get_time_ms();

        print_str(STDOUT_FD, "Sequential time: ");
        print_double(STDOUT_FD, t1 - t0, 3);
        print_str(STDOUT_FD, " ms. Sorted: ");
        print_str(STDOUT_FD, is_sorted(arr_seq, n) ? "OK\n" : "FAIL\n");
    }
    else if (strcmp(mode, "-p") == 0) {
        double t0 = get_time_ms();
        odd_even_merge_sort_par(arr_par, n, num_threads);
        double t1 = get_time_ms();

        print_str(STDOUT_FD, "Parallel (threads = ");
        print_int(STDOUT_FD, num_threads);
        print_str(STDOUT_FD, ") time: ");
        print_double(STDOUT_FD, t1 - t0, 3);
        print_str(STDOUT_FD, " ms. Sorted: ");
        print_str(STDOUT_FD, is_sorted(arr_par, n) ? "OK\n" : "FAIL\n");
    }
    else if (strcmp(mode, "-b") == 0) {
        double ts0 = get_time_ms();
        odd_even_merge_sort_seq(arr_seq, n);
        double ts1 = get_time_ms();
        double seq_time = ts1 - ts0;

        double tp0 = get_time_ms();
        odd_even_merge_sort_par(arr_par, n, num_threads);
        double tp1 = get_time_ms();
        double par_time = tp1 - tp0;

        double speedup = seq_time / par_time;
        double efficiency = speedup / (double)num_threads;

        print_str(STDOUT_FD, "Threads: ");
        print_int(STDOUT_FD, num_threads);
        print_str(STDOUT_FD, " | T_par: ");
        print_double(STDOUT_FD, par_time, 2);
        print_str(STDOUT_FD, " ms | Speedup: ");
        print_double(STDOUT_FD, speedup, 3);
        print_str(STDOUT_FD, " | Efficiency: ");
        print_double(STDOUT_FD, efficiency, 3);
        print_str(STDOUT_FD, "\n");
    }

    free(arr_seq);
    free(arr_par);
    return 0;
}