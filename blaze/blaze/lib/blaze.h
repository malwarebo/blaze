
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;

void* thread1_function(void* arg) {
    printf("Thread 1 trying to acquire mutex1\n");
    pthread_mutex_lock(&mutex1);
    printf("Thread 1 acquired mutex1\n");
    
    sleep(1);  // Sleep to ensure thread2 has time to acquire mutex2
    
    printf("Thread 1 trying to acquire mutex2\n");
    pthread_mutex_lock(&mutex2);
    printf("Thread 1 acquired mutex2\n");
    
    pthread_mutex_unlock(&mutex2);
    pthread_mutex_unlock(&mutex1);
    
    return NULL;
}

void* thread2_function(void* arg) {
    printf("Thread 2 trying to acquire mutex2\n");
    pthread_mutex_lock(&mutex2);
    printf("Thread 2 acquired mutex2\n");
    
    sleep(1);  // Sleep to ensure thread1 has time to acquire mutex1
    
    printf("Thread 2 trying to acquire mutex1\n");
    pthread_mutex_lock(&mutex1);
    printf("Thread 2 acquired mutex1\n");
    
    pthread_mutex_unlock(&mutex1);
    pthread_mutex_unlock(&mutex2);
    
    return NULL;
}

int main() {
    pthread_t thread1, thread2;
    
    pthread_create(&thread1, NULL, thread1_function, NULL);
    pthread_create(&thread2, NULL, thread2_function, NULL);
    
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    
    return 0;
}
