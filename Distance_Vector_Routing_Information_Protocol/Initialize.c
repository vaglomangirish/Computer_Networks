#include <stdlib.h>
#include <stdio.h>
#include <string.h>

//Arguments start
//minimum arguments required
#define MIN_ARGS 7
//arg for file name
#define ARG_FILE_NAME 1
//arg for port no
#define ARG_PORT 2
//arg for default TTL
#define ARG_DEFAULT_TTL 3
//arg for default infinity value
#define ARG_DEFAULT_INFINITY 4
//arg  for default period
#define ARG_DEFAULT_PERIOD 5
//arg for boolean split horizon
#define ARG_USE_SPLIT_HORIZON 6
//Arguments end

//Set Debug mode. Enables/disables debug messages
#define DEBUG_MODE 1

void signal_error(char *err_msg);
void processArgs(int argc, char *argv[], char** file_name, int *port,
		int *default_ttl, int *default_infinty, int *default_period,
		int *split_horizon);
int readConfig(char *file_name, char **lines);
void initialize(char *file_name);

int main(int argc, char *argv[]) {
	char* file_name;
	int port;
	int default_ttl;
	int default_infinty;
	int default_period;
	int split_horizon;
	processArgs(argc, argv, &file_name, &port, &default_ttl, &default_infinty,
			&default_period, &split_horizon);
	if (DEBUG_MODE) {
		printf(
				"filename:%s,port:%d,default_ttl:%d,default_infinty:%d,default_period:%d, split_horizon:%d",
				file_name, port, default_ttl, default_infinty, default_period,
				split_horizon);
	}
	initialize(file_name);
	return 0;
}

void initialize(char *file_name) {
	char* config_lines[100];
	int lines_read = readConfig(file_name, config_lines);
	int i;
	for (i = 0; i < lines_read; i++) {
		char* entry = strtok(config_lines[i], " ");
		char *ip = (char *) calloc(strlen(entry), sizeof(char));
		//TODO At the end of program remember to free the ip
		strcpy(ip, entry);
		int neighbour=atoi(strtok(NULL, " "));
		printf("\nip:%s neighbour:%d", ip,neighbour);
		free(config_lines[i]);
	}
}


/**
 * Read configuration file
 * Return the no. of lines read
 */
int readConfig(char *file_name, char **lines) {
	int lines_read = 0;
	FILE *config_file;
	char * line = NULL;
	ssize_t read;
	size_t read_len = 0;
	config_file = fopen(file_name, "r");
	if (config_file == NULL) {
		signal_error("Failed to read the config file");
	}
	while ((read = getline(&line, &read_len, config_file)) != -1) {
		if (read > 5) {
			//read lines which are not blank
			char *line_cp = (char *) calloc(strlen(line), sizeof(char));
			strcpy(line_cp, line);
			lines[lines_read] = line_cp;
			lines_read++;
		}
	}
	fclose(config_file);
	return lines_read;
}

/**
 * Process command line args
 */
void processArgs(int argc, char *argv[], char** file_name, int *port,
		int *default_ttl, int *default_infinty, int *default_period,
		int *split_horizon) {
	if (argc < MIN_ARGS) {
		signal_error("Insufficient arguments");
	} else {
		*file_name = argv[ARG_FILE_NAME];
		*port = atoi(argv[ARG_PORT]);
		*default_ttl = atoi(argv[ARG_DEFAULT_TTL]);
		*default_infinty = atoi(argv[ARG_DEFAULT_TTL]);
		*default_period = atoi(argv[ARG_DEFAULT_PERIOD]);
		*split_horizon = atoi(argv[ARG_USE_SPLIT_HORIZON]);
	}
}

void signal_error(char *err_msg) {
	fprintf(stderr, err_msg);
	fprintf(stderr, "shutting down");
	exit(1);
}
