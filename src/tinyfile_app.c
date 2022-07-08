#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ipc_shared.h"
#include <time.h>

static void* sync_call(char* filename, size_t seg_size) {
	char compressed[MAX_FILE_SIZE];
	size_t compressed_size = 0;

	clock_t time_calc;
	time_calc = clock();

	call_service(filename, seg_size, compressed, &compressed_size);

	time_calc = clock() - time_calc;
	double time_elapsed = ((double)time_calc)/CLOCKS_PER_SEC;

	char* fname = filename;

	char* pch = strstr(fname, ".");
	if (pch) {
		strncpy(pch, ".snappy", 4);
	}
	else {
		strcat(fname, ".snappy");
	}
	printf("Compressed file name: %s\n", fname);
	printf("Compression took %f seconds to execute\n", time_elapsed);

	FILE* fs = fopen(fname, "w");
	fwrite(compressed, compressed_size, 1, fs);
	fclose(fs);

	return fs;
}

/**
./bin/tinyfile_app --sms_size SIZE_BYTES --state ASYNC/SYNC --file{--files} FILE(S)
**/

int main(int argc, char** argv) {

	size_t seg_size = atol(argv[2]);
	int call_type = 0;
	if(strcmp(argv[4],"SYNC") == 0){
		call_type = 0;
	}else if(strcmp(argv[4],"ASYNC") == 0){
		call_type = 1;
	}

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
	printf("call type: %d\n", call_type);

	int mid_pos = file_num/2;

	/*
		QoS - Quality of service implemented through process forks
	*/

	if (call_type == 0) {
		pid_t pid = fork();
		if (pid == 0) {
			for (int i = 0; i < mid_pos; ++i)	sync_call(filenames[i], seg_size);
		}
		else if (pid > 0) {
			for (int i = mid_pos; i < file_num; ++i) sync_call(filenames[i], seg_size);
		}
		else printf("client process creation failed\n");
	}

	else if (call_type == 1) {

		clock_t time_calc;
		time_calc = clock();

			for (int i = 0; i < file_num; ++i) {
				p_arg* handle = initiate_service(filenames[i], seg_size);
				while (!handle->has_result);
				/* produce the name of compressed file */
				char* fname = filenames[i];
				char* pch = strstr(fname, ".");
				if (pch) {
					strncpy(pch, ".snappy", 4);
				}
				else {
					strcat(fname, ".snappy");
				}
				printf("name of compressed file: %s\n", fname);

				FILE* fs = fopen(fname, "w");
				fwrite(handle->result_buf, handle->compressed_size, 1, fs);
				fclose(fs);
			}

			time_calc = clock() - time_calc;
			double time_elapsed = ((double)time_calc)/CLOCKS_PER_SEC;

			printf("compression took %f seconds to execute\n", time_elapsed);
	}

	else printf("invalid call type\n");

	return 0;
}
