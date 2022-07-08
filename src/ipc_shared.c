#include <sys/types.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include "ipc_shared.h"

//
extern void call_service(char* filename, size_t seg_size, char* result_buf, size_t* compressed_size) {
	printf("In call_service()\n");
	printf("%s.\n", filename);
	key_t file_key = ftok(filename, 65);

	FILE* fs = fopen(filename, "r");
	if (!fs) perror("fopen failed\n");
	fseek(fs, 0L, SEEK_END);
	long int file_end = ftell(fs);
	fseek(fs, 0L, SEEK_SET);
	long int file_cur = ftell(fs), file_size = file_end - file_cur;
	size_t seg_needed = file_size/seg_size + (file_size%seg_size == 0? 0: 1);
	printf("file size: %ld, segs needed: %ld\n", file_size, seg_needed);

	key_t requestq_key = ftok("requestq", 65);
	int requestq_id = msgget(requestq_key, 0666);
	printf("requestq id: %d\n", requestq_id);
	mymsg request;
	mymsg* request_msg = &request;
	request_msg->type = file_key + 1;
	memcpy(request_msg->data, &seg_needed, SIZE_T_BYTES);
	msgsnd(requestq_id, request_msg, SIZE_T_BYTES*4, 0);
	key_t responseq_key = ftok("responseq", 65);
	int responseq_id = msgget(responseq_key, 0666);
	printf("responseq id: %d\n", responseq_id);
	mymsg response;
	mymsg* response_msg = &response;
	while (msgrcv(responseq_id, response_msg, SIZE_T_BYTES*4, file_key + 1, IPC_NOWAIT) == -1);

	size_t seg_num = 0;
	memcpy(&seg_num, response_msg->data, SIZE_T_BYTES);
	int shmids[seg_num];
	char* shmaddrs[seg_num];
	for (int i = 0; i < seg_num; ++i) {
		key_t seg_key = ftok(filename, 65 + i);
		shmids[i] = shmget(seg_key, seg_size, 0666 | IPC_CREAT);
		shmaddrs[i] = shmat(shmids[i], NULL, 0);
		char* buf;
		buf = shmat(shmids[i], NULL, 0);
	}

	size_t segs = 0;
	while (segs < seg_needed) {
		printf("cursor position: %ld\n", ftell(fs));
		size_t bytes_written = file_end - ftell(fs) > seg_size? seg_size: file_end - ftell(fs);
		fread(shmaddrs[segs%seg_num], bytes_written, 1, fs);
		printf("%ld bytes of data written into shmaddrs[%ld]\n", bytes_written, segs%seg_num);
		/* send orig message */
		mymsg orig;
		mymsg* orig_msg = &orig;
		orig_msg->type = file_key + 1;
		memcpy(orig_msg->data, &segs, SIZE_T_BYTES);
		memcpy(orig_msg->data + SIZE_T_BYTES, &seg_needed, SIZE_T_BYTES);
		memcpy(orig_msg->data + SIZE_T_BYTES*2, &bytes_written, SIZE_T_BYTES);
		key_t origq_key = ftok("origq", 65);
		int origq_id = msgget(origq_key, 0666);
		printf("orig queue id: %d\n", origq_id);
		msgsnd(origq_id, orig_msg, SIZE_T_BYTES*4, 0);
		segs++;
		mymsg server_ack;
		mymsg* server_ack_msg = &server_ack;
		int server_ackq_id = msgget(ftok("serverackq", 65), 0666);
		printf("server_ackq_id: %d\n", server_ackq_id);
		printf("waiting for message type of %d in server_ack (server_ackq_id: %d)\n\n", file_key + 1, server_ackq_id);
		while (msgrcv(server_ackq_id, server_ack_msg, SIZE_T_BYTES*4, file_key + 1, IPC_NOWAIT) == -1);
	}

	fclose(fs);

	size_t seg_received = 0;
	while(seg_received < seg_needed) {
		int resultq_id = msgget(ftok("resultq", 65), 0666);
		mymsg result;
		mymsg* result_msg = &result;
		while (msgrcv(resultq_id, result_msg, SIZE_T_BYTES*4, file_key + 1, IPC_NOWAIT) == -1);
		printf("received result message\n");
		size_t cur_seg = 0, bytes_to_read = 0;
		memcpy(&bytes_to_read, result_msg->data, SIZE_T_BYTES);
		memcpy(result_buf + *compressed_size, shmaddrs[seg_received%seg_num], bytes_to_read);

		seg_received++;
		*compressed_size += bytes_to_read;
		printf("Size of compressed file: %ld\n", *compressed_size);

		mymsg client_ack;
		mymsg* client_ack_msg = &client_ack;
		client_ack_msg->type = file_key + 1;
		memcpy(client_ack_msg->data, &seg_received, SIZE_T_BYTES);
		memcpy(client_ack_msg->data + SIZE_T_BYTES, &seg_needed, SIZE_T_BYTES);
		int client_ackq_id = msgget(ftok("clientackq", 65), 0666);
		msgsnd(client_ackq_id, client_ack_msg, SIZE_T_BYTES*4, 0);
	}
}



//
extern void* call_service_async(void* ptr) {
	p_arg* arg_ptr = (p_arg*) ptr;
	size_t seg_num = arg_ptr->seg_num;
	char* filename = arg_ptr->filename;
	size_t seg_size = arg_ptr->seg_size;
	char* result_buf = arg_ptr->result_buf;
	size_t* compressed_size = &(arg_ptr->compressed_size);

	int shmids[seg_num];
	char* shmaddrs[seg_num];
	for (int i = 0; i < seg_num; ++i) {
		key_t seg_key = ftok(filename, 65 + i);
		shmids[i] = shmget(seg_key, seg_size, 0666 | IPC_CREAT);
		shmaddrs[i] = shmat(shmids[i], NULL, 0);
		char* buf;
		buf = shmat(shmids[i], NULL, 0);
	}

	FILE* fs = fopen(filename, "r");
	size_t segs = 0;
	fseek(fs, 0L, SEEK_END);
	long int file_end = ftell(fs);
	fseek(fs, 0L, SEEK_SET);
	size_t seg_needed = file_end/seg_size + (file_end%seg_size == 0? 0: 1);
	key_t file_key = ftok(filename, 65);
	while (segs < seg_needed) {
		printf("cursor position: %ld\n", ftell(fs));
		size_t bytes_written = file_end - ftell(fs) > seg_size? seg_size: file_end - ftell(fs);
		fread(shmaddrs[segs%seg_num], bytes_written, 1, fs);
		printf("%ld bytes of data written into shmaddrs[%ld]\n", bytes_written, segs%seg_num);
		mymsg orig;
		mymsg* orig_msg = &orig;
		orig_msg->type = file_key + 1;
		memcpy(orig_msg->data, &segs, SIZE_T_BYTES);
		memcpy(orig_msg->data + SIZE_T_BYTES, &seg_needed, SIZE_T_BYTES);
		memcpy(orig_msg->data + SIZE_T_BYTES*2, &bytes_written, SIZE_T_BYTES);
		key_t origq_key = ftok("origq", 65);
		int origq_id = msgget(origq_key, 0666);
		printf("orig queue id: %d\n", origq_id);
		msgsnd(origq_id, orig_msg, SIZE_T_BYTES*4, 0);
		segs++;
		mymsg server_ack;
		mymsg* server_ack_msg = &server_ack;
		int server_ackq_id = msgget(ftok("serverackq", 65), 0666);
		printf("server_ackq_id: %d\n", server_ackq_id);
		printf("waiting for message type of %d in server_ack (server_ackq_id: %d)\n\n", file_key + 1, server_ackq_id);
		while (msgrcv(server_ackq_id, server_ack_msg, SIZE_T_BYTES*4, file_key + 1, IPC_NOWAIT) == -1);
	}

	fclose(fs);

	size_t seg_received = 0;
	while(seg_received < seg_needed) {
		int resultq_id = msgget(ftok("resultq", 65), 0666);
		mymsg result;
		mymsg* result_msg = &result;
		while (msgrcv(resultq_id, result_msg, SIZE_T_BYTES*4, file_key + 1, IPC_NOWAIT) == -1);
		printf("received result message\n");
		size_t cur_seg = 0, bytes_to_read = 0;
		memcpy(&bytes_to_read, result_msg->data, SIZE_T_BYTES);
		memcpy(result_buf + *compressed_size, shmaddrs[seg_received%seg_num], bytes_to_read);

		seg_received++;
		*compressed_size += bytes_to_read;
		printf("Size of compressed file: %ld\n", *compressed_size);

		mymsg client_ack;
		mymsg* client_ack_msg = &client_ack;
		client_ack_msg->type = file_key + 1;
		memcpy(client_ack_msg->data, &seg_received, SIZE_T_BYTES);
		memcpy(client_ack_msg->data + SIZE_T_BYTES, &seg_needed, SIZE_T_BYTES);
		int client_ackq_id = msgget(ftok("clientackq", 65), 0666);
		msgsnd(client_ackq_id, client_ack_msg, SIZE_T_BYTES*4, 0);
	}
	arg_ptr->has_result = 1;
}


//
extern p_arg* initiate_service(char* filename, size_t seg_size) {
	printf("In aysnc_service()\n");
	key_t file_key = ftok(filename, 65);
	FILE* fs = fopen(filename, "r");
	if (!fs) perror("fopen failed\n");
	fseek(fs, 0L, SEEK_END);
	long int file_end = ftell(fs);
	fseek(fs, 0L, SEEK_SET);
	long int file_cur = ftell(fs), file_size = file_end - file_cur;
	size_t seg_needed = file_size/seg_size + (file_size%seg_size == 0? 0: 1);
	printf("file size: %ld, segs needed: %ld\n", file_size, seg_needed);
	fclose(fs);

	key_t requestq_key = ftok("requestq", 65);
	int requestq_id = msgget(requestq_key, 0666);
	printf("requestq id: %d\n", requestq_id);
	mymsg request;
	mymsg* request_msg = &request;
	request_msg->type = file_key + 1;
	memcpy(request_msg->data, &seg_needed, SIZE_T_BYTES);
	msgsnd(requestq_id, request_msg, SIZE_T_BYTES*4, 0);
	key_t responseq_key = ftok("responseq", 65);
	int responseq_id = msgget(responseq_key, 0666);
	printf("responseq id: %d\n", responseq_id);
	mymsg response;
	mymsg* response_msg = &response;
	while (msgrcv(responseq_id, response_msg, SIZE_T_BYTES*4, file_key + 1, IPC_NOWAIT) == -1);

	pthread_t helper_thread;
	p_arg* arg_ptr = (p_arg*) malloc(sizeof(p_arg));
	arg_ptr->has_result = 0;
	memcpy(&(arg_ptr->seg_num), response_msg->data, SIZE_T_BYTES);
	arg_ptr->filename = filename;
	arg_ptr->seg_size = seg_size;
	pthread_create(&helper_thread, NULL, call_service_async, arg_ptr);

	return arg_ptr;
}

//
extern void shm_ipc_init(size_t num, long size, ipc_shared_info* shared_info) {
	const char* msgq_files[6] = {"requestq", "responseq", "origq", "serverackq", "resultq", "clientackq"};
	for (int i = 0; i < 6; ++i) {
		key_t msgq_key = ftok(msgq_files[i], 65);
		printf("filename: %s, msgq key: %d\n", msgq_files[i], msgq_key);
		shared_info->msgq_ids[i] = msgget(msgq_key, 0666 | IPC_CREAT);
		printf("id of message queue: %d\n", shared_info->msgq_ids[i]);
		if (shared_info->msgq_ids[i] == -1) perror("At msgget() in shm_ipc_init()");
	}

	shared_info->seg_num = num;
	shared_info->seg_size = size;
	shared_info->remaining = num;
}
