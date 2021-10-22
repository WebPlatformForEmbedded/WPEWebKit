#define _GNU_SOURCE

#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

// memcheck will create report for each process
// in case of WPE we will have at least 4 instances: WPEWebProcess, WPENetworkProcess, OneMWBrowser, OneMWBrowser-debug
// fifth process (WPEDatabaseProcess) is created only when needed

#define MAX_TRACE 300000
#define MAX_CALLSTACK 16
#define TMP_CALLOC_BUFF (32 * 1024)

// adjust the size of searched allocations
// if the range will be to big, then WPE may be not operational
// (e.g. video may not play, due to memcheck overhead)
static unsigned memcheck_size_min = 16 * 1024;
static unsigned memcheck_size_max = 4 * 1024 * 1024;
static unsigned int startup_delay_in_sec = 30;
static unsigned int memcheck_duration = 30;

static char calloc_buffer[TMP_CALLOC_BUFF];
static int calloc_buffer_offset = 0;
static int calloc_buffer_nb = 0;
static unsigned int memory_trace_max_cnt = 0;
static unsigned int memory_trace_enabled = 0;
static unsigned int memory_trace_free_enabled = 0;
static unsigned int memory_trace_skip = 0;
static unsigned int my_malloc_cnt = 0;
static unsigned int my_calloc_cnt = 0;
static unsigned int my_free_cnt = 0;
static unsigned int my_realloc_cnt = 0;

typedef struct {
    void * address;
    size_t size;
    unsigned thread_id;
    int call_stack_nb;
    void* call_stack[MAX_CALLSTACK + 1];
} memory_trace_t;

static memory_trace_t memory_trace[MAX_TRACE];
static pthread_mutex_t memory_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

void memcheck_control_start() {
    fprintf(stderr, "memcheck: memcheck_control_start from pid: %d\n", getpid());
    pthread_mutex_lock(&memory_lock);
    memory_trace_enabled = 1;
    memory_trace_free_enabled = 1;
    pthread_mutex_unlock(&memory_lock);
}

void memcheck_control_stop() {
    fprintf(stderr, "memcheck: memcheck_control_stop from pid: %d\n", getpid());
    pthread_mutex_lock(&memory_lock);
    memory_trace_enabled = 0;
    memory_trace_free_enabled = 1;
    pthread_mutex_unlock(&memory_lock);
}

char* get_file_name_with_pid(int pid) {
    char extension[8];
    sprintf(extension, "%d", pid);
    const char* file = "/tmp/wpe/memcheck-info_";
    char* file_with_pid = malloc(strlen(file) + 8 + 1);

    strcpy(file_with_pid, file);
    strcat(file_with_pid, extension);

    fprintf(stderr, "memcheck: get_file_name_with_pid file_with_pid: %s\n", file_with_pid);
    return file_with_pid;
}

void memcheck_control_show() {
    int pid = getpid();
    fprintf(stderr, "memcheck: memcheck_control_show from pid: %d\n", pid);

    char* path = get_file_name_with_pid(pid);
    FILE* file = fopen(path, "wb");
    if (!file) {
        return;
    }

    unsigned i;
    int j;
    unsigned int cnt = 0;

    pthread_mutex_lock(&memory_lock);
    memory_trace_enabled = 0;
    memory_trace_free_enabled = 0;
    pthread_mutex_unlock(&memory_lock);

    for (i = 0; i < memory_trace_max_cnt; ++i) {
        if (memory_trace[i].address != (void*) 0) {
            fprintf(file, "memcheck address: %p, allocation size: %zu, thread id: %d\n",
                    memory_trace[i].address, memory_trace[i].size, memory_trace[i].thread_id);

            if (getenv("WPE_MEMCHECK_CALLSTACK")) {
                char** s_callstack;
                s_callstack = backtrace_symbols(memory_trace[i].call_stack, MAX_CALLSTACK);
                if (s_callstack) {
                    for (j = 0; j < memory_trace[i].call_stack_nb; ++j) {
                        if (memory_trace[i].call_stack[j]) {
                            fprintf(file, "  %p(%s)\n", memory_trace[i].call_stack[j], s_callstack[j]);
                        } else {
                            break;
                        }
                    }
                    free(s_callstack);
                }
            }
            cnt++;
            memory_trace[i].address = (void*) 0;
        }
    }
    fprintf(file, "memcheck statistics: malloc:%d calloc:%d realloc:%d free:%d max_depth:%d not_released:%d\n",
            my_malloc_cnt, my_calloc_cnt, my_realloc_cnt, my_free_cnt, memory_trace_max_cnt, cnt);
    fprintf(file, "memcheck statistics: tmp calloc %d %d\n", calloc_buffer_offset, calloc_buffer_nb);
    fclose(file);
    free(path);

    pthread_mutex_lock(&memory_lock);
    my_malloc_cnt = 0;
    my_calloc_cnt = 0;
    my_realloc_cnt = 0;
    my_free_cnt = 0;
    memory_trace_max_cnt = 0;
    pthread_mutex_unlock(&memory_lock);
}

void memcheck_configuration() {
    const char* max_size = getenv("WPE_MEMCHECK_MAX_SIZE");
    if (max_size) {
        memcheck_size_max = strtol(max_size, NULL, 10);
    }

    const char* min_size = getenv("WPE_MEMCHECK_MIN_SIZE");
    if (min_size) {
        memcheck_size_min = strtol(min_size, NULL, 10);
    }

    const char* delay = getenv("WPE_MEMCHECK_STARTUP_DELAY");
    if (delay) {
        startup_delay_in_sec = strtol(delay, NULL, 10);
    }

    const char* duration = getenv("WPE_MEMCHECK_DURATION");
    if (duration) {
        memcheck_duration = strtol(duration, NULL, 10);
    }
}

void* memcheck_control(void* not_used) {
    memcheck_configuration();

    fprintf(stderr, "memcheck: memcheck for pid: %d will start in: %d sec, will last: %d sec, allocation range: [%d B, %d B], not_used: %p\n",
            getpid(), startup_delay_in_sec, memcheck_duration, memcheck_size_min, memcheck_size_max, not_used);

    sleep(startup_delay_in_sec);
    memcheck_control_start();
    sleep(memcheck_duration);
    memcheck_control_stop();
    memcheck_control_show();

    return NULL;
}

bool create_memcheck_thread() {
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
        fprintf(stderr, "ERROR memcheck: setting detach state failed\n");
        return false;
    }
    if (pthread_create(&thread, &attr, memcheck_control, NULL)) {
        fprintf(stderr, "ERROR memcheck: thread was not created\n");
        return false;
    }

    return true;
}

void* malloc(size_t size) {
    static void* (*libc_malloc)(size_t) = NULL;
    void * result;
    unsigned i;

    pthread_mutex_lock(&memory_lock);
    if (!libc_malloc) {
        libc_malloc = (void* (*)(size_t)) dlsym(RTLD_NEXT, "malloc");

        // start memcheck control thread
        if (!create_memcheck_thread()) {
            pthread_mutex_unlock(&memory_lock);
            return (void*) 0;
        }
    }

    result = libc_malloc(size);

    if (!memory_trace_enabled || memory_trace_skip) {
        pthread_mutex_unlock(&memory_lock);
        return result;
    }

    if (result && (size >= memcheck_size_min && size <= memcheck_size_max)) {
        for (i = 0; i < MAX_TRACE; ++i) {
            if (memory_trace[i].address == ((void*) 0)) {
                memory_trace[i].address = (void*) result;
                memory_trace[i].size = size;
                memory_trace[i].thread_id = syscall(__NR_gettid);
                if (getenv("WPE_MEMCHECK_CALLSTACK")) {
                    /*
                    when backtrace size is equal to 1 then most probably the function that trigger our malloc wrapper is
                    compiled without appropriate flags (-rdynamic, -fasynchronous-unwind-tables, -funwind-tables) that allow
                    backtrace() to find previous stackframes
                    the same behavior applies to calloc() and realloc() wrappers
                    */
                    memory_trace_skip = 1; // bactrace call will use malloc() function
                    memory_trace[i].call_stack_nb = backtrace(memory_trace[i].call_stack, MAX_CALLSTACK);
                    memory_trace_skip = 0;
                }
                if (i >= memory_trace_max_cnt) {
                    memory_trace_max_cnt = i + 1;
                }
                my_malloc_cnt++;
                break;
            }
        }
    }

    pthread_mutex_unlock(&memory_lock);
    return result;
}

static void* tmp_calloc(size_t nmemb, size_t size) {
    void* p = NULL;
    size_t s = nmemb * size;
    size_t s_alloc;

    // align to 4 bytes
    s_alloc = (s + 3) & 0xfffffffc;
    if ((calloc_buffer_offset + sizeof(s) + s_alloc) <= TMP_CALLOC_BUFF) {
        memcpy( & calloc_buffer[calloc_buffer_offset], &s, sizeof(s)); // store calloc size
        p = &calloc_buffer[calloc_buffer_offset + sizeof(s)];
        memset(p, 0, s);
        calloc_buffer_offset += sizeof(s) + s_alloc;
        calloc_buffer_nb++;
    }
    return p;
}

static bool is_tmp_calloc(void* p) {
    if (((char*) p >= calloc_buffer) && ((char*) p < (calloc_buffer + TMP_CALLOC_BUFF))) {
        return true;
    }
    return false;
}

static unsigned int tmp_calloc_size(void* p) {
    unsigned int s = 0;
    if (is_tmp_calloc(p)) {
        memcpy(&s, (char*) p - sizeof(s), sizeof(s));
    }
    return s;
}

void* calloc(size_t nmemb, size_t size) {
    static void* (*libc_calloc)(size_t, size_t) = NULL;
    void* result;
    // need to prevent gcc compiler optimizations
    static volatile int calloc_init = 0;
    unsigned i;

    pthread_mutex_lock(&memory_lock);
    if (calloc_init) {
        result = tmp_calloc(nmemb, size); // if calloc is called from dlsym
        pthread_mutex_unlock(&memory_lock);
        return result;
    }

    if (!libc_calloc) {
        calloc_init = 1;
        libc_calloc = (void* (*)(size_t, size_t)) dlsym(RTLD_NEXT, "calloc");
        calloc_init = 0;
    }

    result = libc_calloc(nmemb, size);

    if (!memory_trace_enabled || memory_trace_skip) {
        pthread_mutex_unlock(&memory_lock);
        return result;
    }

    if (result && (size >= memcheck_size_min && size <= memcheck_size_max)) {
        for (i = 0; i < MAX_TRACE; ++i) {
            if (memory_trace[i].address == ((void*) 0)) {
                memory_trace[i].address = (void*) result;
                memory_trace[i].size = size;
                memory_trace[i].thread_id = syscall(__NR_gettid);
                if (getenv("WPE_MEMCHECK_CALLSTACK")) {
                    memory_trace_skip = 1;
                    memory_trace[i].call_stack_nb = backtrace(memory_trace[i].call_stack, MAX_CALLSTACK);
                    memory_trace_skip = 0;
                }
                if (i >= memory_trace_max_cnt) {
                    memory_trace_max_cnt = i + 1;
                }
                my_calloc_cnt++;
                break;
            }
        }
    }

    pthread_mutex_unlock(&memory_lock);
    return result;
}

void* realloc(void* ptr, size_t size) {
    static void* (*libc_realloc)(void* , size_t) = NULL;
    void * result;
    unsigned i;

    pthread_mutex_lock(&memory_lock);
    if (!libc_realloc) {
        libc_realloc = (void* (*)(void* , size_t)) dlsym(RTLD_NEXT, "realloc");
    }

    if (is_tmp_calloc(ptr)) {
        result = malloc(size);
        if (result) {
            unsigned s1 = tmp_calloc_size(ptr);
            memcpy(result, ptr, s1 < size ? s1 : size);
        }
    } else {
        result = libc_realloc(ptr, size);
        if (memory_trace_free_enabled) {
            for (i = 0; i < memory_trace_max_cnt; i++) {
                if (memory_trace[i].address == ((void * ) ptr)) {
                    memory_trace[i].address = (void * ) 0;
                    if ((i + 1) == memory_trace_max_cnt) {
                        memory_trace_max_cnt--;
                    }
                    my_free_cnt++;
                    break;
                }
            }
        }
    }

    if (!memory_trace_enabled || memory_trace_skip) {
        pthread_mutex_unlock(&memory_lock);
        return result;
    }

    if (result && (size >= memcheck_size_min && size <= memcheck_size_max)) {
        for (i = 0; i < MAX_TRACE; i++) {
            if (memory_trace[i].address == (void*) 0) {
                memory_trace[i].address = (void*) result;
                memory_trace[i].size = size;
                memory_trace[i].thread_id = syscall(__NR_gettid);
                if (getenv("WPE_MEMCHECK_CALLSTACK")) {
                    memory_trace_skip = 1;
                    memory_trace[i].call_stack_nb = backtrace(memory_trace[i].call_stack, MAX_CALLSTACK);
                    memory_trace_skip = 0;
                }
                if (i >= memory_trace_max_cnt) {
                    memory_trace_max_cnt = i + 1;
                }
                my_realloc_cnt++;
                break;
            }
        }
    }
    pthread_mutex_unlock(&memory_lock);
    return result;
}

void free(void* p) {
    static void (*libc_free)(void*) = NULL;
    unsigned i;

    pthread_mutex_lock(&memory_lock);
    if (!libc_free) {
        libc_free = (void (*)(void*)) dlsym(RTLD_NEXT, "free");
    }

    if (is_tmp_calloc(p)) {
        pthread_mutex_unlock(&memory_lock);
        return;
    }

    if (memory_trace_free_enabled) {
        for (i = 0; i < memory_trace_max_cnt; i++) {
            if (memory_trace[i].address == (void *) p) {
                memory_trace[i].address = (void *) 0;
                if ((i + 1) == memory_trace_max_cnt) {
                    memory_trace_max_cnt--;
                }
                my_free_cnt++;
                break;
            }
        }
    }
    libc_free(p);
    pthread_mutex_unlock(&memory_lock);
}
