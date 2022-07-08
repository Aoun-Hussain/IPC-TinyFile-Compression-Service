#pragma once
#include <pthread.h>

#define REQUEST_TYPE 1
#define MAX_FILE_SIZE 1024*4096
#define SIZE_T_BYTES sizeof(size_t)/sizeof(char)

typedef struct mymsg_t {
	long type;
	char data[SIZE_T_BYTES*4];
} mymsg;

typedef struct ipc_shared_info_t {
	size_t seg_num; // Number of segment sizes
	long seg_size; // Size of segment in bytes
	size_t remaining;
	int msgq_ids[6]; // Message Qs for control plane messages
	pthread_mutex_t mutex;
} ipc_shared_info;

typedef struct p_arg_t {
	size_t seg_num;
	char* filename;
	size_t seg_size;
	char result_buf[MAX_FILE_SIZE];
	size_t compressed_size;
	int has_result;
} p_arg;

extern ipc_shared_info ipc_shared;

extern void shm_ipc_init(size_t num, long size, ipc_shared_info* shared_info);
extern void call_service(char* filename, size_t seg_size, char* result, size_t* compressed_size);
extern p_arg* async_service(char* filename, size_t seg_size);
extern void* call_service_async(void* ptr);
