#include <sys/types.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc_shared.h"
#include "snappy.h"

extern void compress(char* input, size_t input_length, char* compressed, size_t* compressed_length) {
	struct snappy_env env;
	struct snappy_env* env_ptr = &env;
	if (snappy_init_env(env_ptr) < 0)
	if (snappy_compress(env_ptr, input, input_length, compressed, compressed_length) < 0) printf("compression failed\n");

}

static int find_file_id(key_t* keys, int file_num, long file_key) {
	int file_id = file_num + 1;
	for (int i = 0; i < file_num; ++i) {
		if ((long) keys[i] == file_key) {
			file_id = i;
			break;
		}
	}
	return file_id;
}

/***

Sample input:
/tinyfile --n_sms 5 --sms_size 32 -file(s) a
n_sms = 1, 3 or 5. (Number of segments)
sms_size = 32, 64, 128, 256, 512, 1024, 2048, 4096 or 8192 (Number of bytes per segment)
-file = name of file
-files = input of files is taken through a .txt file containing the filenames
***/

int main(int argc, char** argv) {
	size_t seg_tot = atol(argv[2]);
	long seg_size = atol(argv[4]);

	ipc_shared_info ipc_shared;
	ipc_shared_info* shared_info = &ipc_shared;
	shm_ipc_init(seg_tot, seg_size, shared_info);


	int file_num = 0;
	char line[80] = {0};

	if(strcmp(argv[5], "--file") == 0){
		file_num = 1;
	}else if(strcmp(argv[5], "--files") == 0){
		FILE *filelist_file = fopen(argv[6], "r");

		/* Get number of files */
    while (fgets(line, 80, filelist_file))
    {
				file_num = file_num + 1;
    }
    /* Close file */
    if (fclose(filelist_file))
    {
        return EXIT_FAILURE;
        perror(argv[6]);
    }
		//
	}

	char* filenames[file_num];

	if(strcmp(argv[5], "--file") == 0){
		filenames[0] = argv[6];
	}else if(strcmp(argv[5], "--files") == 0){

		FILE *filelist_final = fopen(argv[6], "r");
		/* Get each line until there are none left */
		int counter = 0;
    while (fgets(line, 80, filelist_final))
    {
				line[strcspn(line, "\n")] = 0;
        /* Copy into filenames array */
				filenames[counter] = line;
				counter = counter + 1;
    }

    /* Close file */
    if (fclose(filelist_final))
    {
        return EXIT_FAILURE;
        perror(argv[6]);
    }

	}


	printf("Number of files: %d\n", file_num);
	size_t cursor_pos[file_num];
	char* orig_buffers[file_num];
	char*** shmaddrs = (char***) malloc(file_num*sizeof(char**));
	key_t keys[file_num];
	size_t seg_nums[file_num];

	for (int i = 0; i < file_num; ++i) {
		keys[i] = ftok(filenames[i], 65);
	}

	int completed_files = 0;
	while (completed_files < file_num) {
		mymsg request;
		mymsg* request_msg = &request;
		while (shared_info->remaining > 0 && msgrcv(shared_info->msgq_ids[0], request_msg, SIZE_T_BYTES*4, 0, IPC_NOWAIT) != -1) {
			long type = request_msg->type;
			printf("Received Request in MSQUEUE of type%ld\n", type);
			size_t seg_needed = 0;
			memcpy(&seg_needed, request_msg->data, SIZE_T_BYTES);
			size_t seg_num = seg_needed > shared_info->remaining? shared_info->remaining: seg_needed;
			int file_id = find_file_id(keys, file_num, type - 1);
			seg_nums[file_id] = seg_num;
			shmaddrs[file_id] = (char**) malloc(seg_num*sizeof(char*));
			shared_info->remaining -= seg_num;
			mymsg response;
			mymsg* response_msg = &response;
			response_msg->type = type;
			memcpy(response_msg->data, &seg_num, SIZE_T_BYTES);
			msgsnd(shared_info->msgq_ids[1], response_msg, SIZE_T_BYTES*4, 0);
			printf("\n");
		}

		mymsg orig;
		mymsg* orig_msg = &orig;
		while (msgrcv(shared_info->msgq_ids[2], orig_msg, SIZE_T_BYTES*4, 0, IPC_NOWAIT) != -1) {
			long type = orig_msg->type;
			size_t segs, seg_needed, bytes_to_read;

			memcpy(&segs, orig_msg->data, SIZE_T_BYTES);
			memcpy(&seg_needed, orig_msg->data + SIZE_T_BYTES, SIZE_T_BYTES);
			memcpy(&bytes_to_read, orig_msg->data + SIZE_T_BYTES*2, SIZE_T_BYTES);

			int file_id = find_file_id(keys, file_num, type - 1);
			printf("file_id: %d, filename: %s\n", file_id, filenames[file_id]);
			if (segs == 0) {
				cursor_pos[file_id] = 0;
				orig_buffers[file_id] = (char*) malloc(MAX_FILE_SIZE);
			}

			if (segs < seg_nums[file_id]) {
				key_t shm_key = ftok(filenames[file_id], 65 + segs);
				int shmid = shmget(shm_key, 0, 0666);
				shmaddrs[file_id][segs] = shmat(shmid, NULL, 0);
			}
			memcpy(orig_buffers[file_id] + cursor_pos[file_id], shmaddrs[file_id][segs%seg_nums[file_id]], bytes_to_read);
			cursor_pos[file_id] += bytes_to_read;

			/* Senk ACK */
			mymsg server_ack;
			mymsg* server_ack_msg = &server_ack;
			server_ack_msg->type = type;
			memcpy(server_ack_msg->data, &segs, SIZE_T_BYTES);
			msgsnd(shared_info->msgq_ids[3], server_ack_msg, SIZE_T_BYTES*4, 0);
			printf("Server ACK type %ld delivered: %d)\n", type, shared_info->msgq_ids[3]);

			/* Buffer compression */
			if (segs == seg_needed - 1) {
				char compressed[MAX_FILE_SIZE];
				size_t compressed_length, cur_bytes = 0;
				compress(orig_buffers[file_id], cursor_pos[file_id], compressed, &compressed_length);
				printf("file size afer compression: %ld\n", compressed_length);
				size_t seg_sent = 0;
				while (seg_sent < seg_needed) {
					size_t bytes_written = compressed_length - cur_bytes > shared_info->seg_size? shared_info->seg_size: compressed_length - cur_bytes;
					/* write compressed data into shared memory */
					memcpy(shmaddrs[file_id][seg_sent%seg_nums[file_id]], compressed + cur_bytes, bytes_written);
					printf("copied %ld bytes of compressed data into shm segment %ld\n", bytes_written, seg_sent%seg_nums[file_id]);
					cur_bytes += bytes_written;
					seg_sent++;

					mymsg result;
					mymsg* result_msg = &result;
					result_msg->type = type;
					memcpy(result_msg->data, &bytes_written, SIZE_T_BYTES);
					msgsnd(shared_info->msgq_ids[4], result_msg, SIZE_T_BYTES*4, 0);

					mymsg client_ack;
					mymsg* client_ack_msg = &client_ack;
					printf("waiting for client ack message\n\n");
					while(msgrcv(shared_info->msgq_ids[5], client_ack_msg, SIZE_T_BYTES*4, type, IPC_NOWAIT) == -1);
				}

				for (int i = 0; i < seg_nums[file_id]; ++i) {
					shmdt(shmaddrs[file_id][i]);
					shmctl(shmget(ftok(filenames[file_id], 65 + i), 0, 0666), IPC_RMID, NULL);
				}
				shared_info->remaining += seg_nums[file_id];
				seg_nums[file_id] = 0;
				cursor_pos[file_id] = 0;

				completed_files++;
			}
			printf("\n");
		}
	}
	return 0;
}
