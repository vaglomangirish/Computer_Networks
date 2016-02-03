#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/if_link.h>
#include <pthread.h>
#include <semaphore.h>
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
//Argument end
//max char needed for int representation 10 + 1 for ; +  1 for null termination+1 for :+ strlen destination+15 for ip representation
#define SIZE_FOR_UPDATE_RECORD 30
//Buffer size for receiving messages
#define READ_BUFFER_SIZE 3000
//Set Debug mode. Enables/disables debug messages
#define DEBUG_MODE 0

typedef struct {
	unsigned char isNeighbour;
	// Flag to check if any msg has been received from the node.
	int msg_recvd;
	char *ip;
} GraphNode;

typedef struct {
	char *destination;
	char *next_hop;
	int cost;
	int ttl;
	//for split horizon
	char *learnt_from;
} RouteEntry;

typedef struct {
	int socket_fd;
} ParamReceive;

typedef struct {
	int socket_fd;
	int port;
	int split_horizon;
	int total_nodes;
	int default_period;
	int default_infinity;
} ParamUpdate;

typedef struct QueueNode {
	char *msg;
	char *from;
	struct QueueNode *next;
} QueueNode;

int number_of_nodes = 0;
int **graph;
GraphNode **graph_node_name;
RouteEntry **routing_table;
QueueNode *msg_queue;
sem_t msg_queue_sem;
pthread_mutex_t msg_queue_lock, routing_table_lock;

void signal_error(char *err_msg);
void processArgs(int argc, char *argv[], char** file_name, int *port,
		int *default_ttl, int *default_infinity, int *default_period,
		int *split_horizon);
int readConfig(char *file_name, char **lines);
int initialize(char *file_name, int default_infinity, int default_ttl,
		int default_period, int split_horizon, int port);
char* getlocalhostIP();
GraphNode *createGraphNode(char* ip, unsigned char isNeighbour);
void initializeGraph(int config_node_count, char** config_node_entries,
		int default_infinity);
int **create2DArray(int rows, int columns);
RouteEntry* createRouteEntry(char* destination, char* next_hop, int cost,
		int ttl);
void initializeRoutingTable(int config_node_count, char** config_node_entries,
		int default_infinity, int default_ttl);
void updateGraph(char *source_ip, char *ip, int distance);
int recalculateDistanceVectors(int default_infinity);
char* generateUpdateMessage(int total_nodes, int split_horizon, char *for_ip);
void processUpdateMessage(char* msg, char *source_ip, int default_ttl,
		int default_infinity, int socket_fd, int port, int split_horizon);
void sendMessage(int socket_fd, char *ip, int port, char *update_msg);
void Send_Advertisement(int socket_fd, int port, int split_horizon,
		int total_nodes);
int initSocket(int port);
void *receiver_worker(void* params);
void *updater_worker(void* params);
void initializeMsgQueue();
void startThreads(int socket_fd, int default_period, int default_infinity,
		int split_horizon, int port);
int bellmanFord(int default_ttl, int default_infinity);
QueueNode* dequeue(QueueNode **head);
void enqueue(QueueNode **head, char *msg, char *from);
char* getIPAddress(char *hostname, char *port);
int getGraphNodeIndexByIP(char *ip);
int isNeighbour(char *indicator);
void decrementTtl(int magnitude, int default_infinity);
void resetTtl(char *ip, char *source_ip, int default_ttl, int default_infinity);
void initRoutingTableLock();
void processReceivedMsgs(int default_ttl, int default_infinity, int socket_fd,
		int port, int split_horizon);
void printRoutingTable(char *msg);
void printGraph(char *msg);
int recalculate(int default_infinity, int default_ttl);

int main(int argc, char *argv[]) {
	char* file_name;
	int port;
	int default_ttl;
	int default_infinity;
	int default_period;
	int split_horizon;
	processArgs(argc, argv, &file_name, &port, &default_ttl, &default_infinity,
			&default_period, &split_horizon);
	if (DEBUG_MODE) {
		printf(
				"\nfilename:%s,port:%d,default_ttl:%d,default_infinity:%d,default_period:%d, split_horizon:%d",
				file_name, port, default_ttl, default_infinity, default_period,
				split_horizon);
	}

	int socket_fd = initialize(file_name, default_infinity, default_ttl,
			default_period, split_horizon, port);
	processReceivedMsgs(default_ttl, default_infinity, socket_fd, port,
			split_horizon);
	//(void) pthread_join(receiver, NULL);
	//(void) pthread_join(updater, NULL);
	return 0;
}

void processReceivedMsgs(int default_ttl, int default_infinity, int socket_fd,
		int port, int split_horizon) {
	while (1) {
		sem_wait(&msg_queue_sem);
		printf("\n*************processing received msg*************");
		QueueNode *msg = dequeue(&msg_queue);
		//TODO if msg==NULL
		if (msg->msg == NULL || strlen(msg->msg) == 0) {
			printf("\nEmpty Message Received from:%s\n", msg->from);
			//reset the TTL for sender
			resetTtl(msg->from, msg->from, default_ttl, default_infinity);
			free(msg->from);
			free(msg->msg);
			free(msg);
			continue;
		}
		processUpdateMessage(msg->msg, msg->from, default_ttl, default_infinity,
				socket_fd, port, split_horizon);
		free(msg);
	}
}
/**
 * Returns socket_fd
 */
int initialize(char *file_name, int default_infinity, int default_ttl,
		int default_period, int split_horizon, int port) {
	char* config_lines[100];
	int lines_read = readConfig(file_name, config_lines);
	number_of_nodes = lines_read + 1;
	initializeGraph(lines_read, config_lines, default_infinity);
	initializeRoutingTable(lines_read, config_lines, default_infinity,
			default_ttl);
	initializeMsgQueue();
	initRoutingTableLock();
	//for testing
	int socket_fd = initSocket(port);
	Send_Advertisement(socket_fd, port, split_horizon, lines_read + 1);

	//char *msg=generateUpdateMessage(lines_read+1,split_horizon,NULL);
	//printf("\nCalling processUpdate");
	//processUpdateMessage(msg, "10.1.0.2");

	startThreads(socket_fd, default_period, default_infinity, split_horizon,
			port);
	/*
	 char *msg = generateUpdateMessage(lines_read + 1, split_horizon, NULL);
	 processUpdateMessage(msg);
	 */
	return socket_fd;
}
/**
 * Initialize the message queue semaphore
 */
void initMsgQueueSem() {
	sem_init(&msg_queue_sem, 0, 0);
}

/**
 * Initializes Routing Table Lock
 */
void initRoutingTableLock() {
	if (pthread_mutex_init(&routing_table_lock, NULL) != 0) {
		signal_error(
				"Initialization of routing table synchronization lock failed");
	}
}
void startThreads(int socket_fd, int default_period, int default_infinity,
		int split_horizon, int port) {
	pthread_t receiver, updater;
	int receiver_thread_id, updater_thread_id;

	ParamReceive *params = (ParamReceive*) calloc(1, sizeof(ParamReceive));
	params->socket_fd = socket_fd;

	ParamUpdate *update_params = (ParamUpdate*) calloc(1, sizeof(ParamUpdate));
	update_params->socket_fd = socket_fd;
	update_params->default_period = default_period;
	update_params->split_horizon = split_horizon;
	update_params->port = port;
	update_params->total_nodes = number_of_nodes;
	update_params->default_infinity = default_infinity;

	//scheduling config start
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
	//pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	int config_success = pthread_attr_setschedpolicy(&attr, SCHED_RR);
	if (config_success != 0) {
		signal_error("\nFailed to configure thread scheduler policy");
	}

	receiver_thread_id = pthread_create(&receiver, &attr, receiver_worker,
			(void*) params);
	//scheduling config end

	if (receiver_thread_id < 0) {
		signal_error("\nFailed to create receiver thread");
	}

	updater_thread_id = pthread_create(&updater, &attr, updater_worker,
			(void*) update_params);
	//scheduling config end

	if (updater_thread_id < 0) {
		signal_error("\nFailed to create updater thread");
	}

}
//Receiver thread
void *receiver_worker(void* params) {
	ParamReceive *actualParams = (ParamReceive*) params;

	while (1) {
		struct sockaddr_in *other_router;
		socklen_t client_addr_size;
		client_addr_size = sizeof(other_router);
		char msg[READ_BUFFER_SIZE] = "";
		if(DEBUG_MODE){
			printf("\ncalling rcv");
		}
		recvfrom(actualParams->socket_fd, msg, READ_BUFFER_SIZE, 0,
				(struct sockaddr *) other_router, &client_addr_size);
		//printf("\test%d", other_router == NULL);
		char *from_ip = strdup(inet_ntoa(other_router->sin_addr));
		enqueue(&msg_queue, strdup(msg), from_ip);
		sem_post(&msg_queue_sem);
		//if (DEBUG_MODE) {
		printf("\nUpdate received from : %s", from_ip);
		printf("\nUpdate message: %s\n", msg);
		//}
	}

}

/**
 * Update thread handler. Sends an advertisement every 'default_period' seconds.
 */
void *updater_worker(void *params) {

	ParamUpdate *actualParams = (ParamUpdate*) params;
	int default_period = actualParams->default_period;
	int default_infinity = actualParams->default_infinity;
	// Decrement TTL for routing table entries.
	// TODO: Confirm on what should be the value of magnitude...
	int magnitude = default_period;

	while (1) {

		// Sleep for default period
		sleep(default_period);

		// Decrement TTL
		decrementTtl(magnitude, default_infinity);

		pthread_mutex_lock(&routing_table_lock);
		// Send Advertisement
		printf("\nINFO: Sending Advertisement...");
		Send_Advertisement(actualParams->socket_fd, actualParams->port,
				actualParams->split_horizon, actualParams->total_nodes);
		printRoutingTable("\nUpdated routing table after send advertisement:\n");
		pthread_mutex_unlock(&routing_table_lock);
	}
}

/**
 * Function resets the TTL value for a routing table entry to default TTL, IF the cost is not infinity.
 */
void resetTtl(char *ip, char *source_ip, int default_ttl, int default_infinity) {

	if(DEBUG_MODE){
		printf("\nTTL Reset for IP %s at index %d", ip, getGraphNodeIndexByIP(ip));
	}
	pthread_mutex_lock(&routing_table_lock);
	if (routing_table[getGraphNodeIndexByIP(ip)]->cost != default_infinity
			&& (strcmp(routing_table[getGraphNodeIndexByIP(ip)]->next_hop,
					source_ip) == 0)) {
		routing_table[getGraphNodeIndexByIP(ip)]->ttl = default_ttl;
	}
	pthread_mutex_unlock(&routing_table_lock);
}

/**
 * Function decrements ttl values of all non-self entries in routing table by a specified value passed as magnitude.
 * It also sets the distance to infinity if the TTL is expired, i.e. if equals zero.
 */
void decrementTtl(int magnitude, int default_infinity) {

	// Confirm what should be the value for magnitude...
	int x;
	// Skipping the self node by x=1.
	for (x = 1; x < number_of_nodes; x++) {
		// Decrement if ttl is valid and cost is not infinity.

		if(DEBUG_MODE){
			printf("\nCHECKING DECR [%d]: ttl: %d, cost: %d", x,
					routing_table[x]->ttl, routing_table[x]->cost);
		}
		if (routing_table[x]->ttl > 0
				&& routing_table[x]->cost != default_infinity) {
			if(DEBUG_MODE){
				printf("\nDecrementing ttl of %s from %d by %d",
						routing_table[x]->destination, routing_table[x]->ttl,
						magnitude);
			}
			routing_table[x]->ttl -= magnitude;
		}
		//If TTL Expired.
		if (routing_table[x]->ttl <= 0) {
			routing_table[x]->cost = default_infinity;
			routing_table[x]->next_hop = NULL;
			// Updating graph
			graph[0][x] = default_infinity;
		}
	}
}

/**
 * Initializes Routing Table
 */
void initializeRoutingTable(int config_node_count, char** config_node_entries,
		int default_infinity, int default_ttl) {
	int total_nodes = config_node_count + 1;
	routing_table = (RouteEntry**) calloc(total_nodes, sizeof(RouteEntry*));
	char* local_host_ip = getlocalhostIP();
	int row = 0;
	routing_table[row] = createRouteEntry(local_host_ip, local_host_ip, 0,
			default_ttl);
	for (row++; row <= config_node_count; row++) {
		char* entry = strtok(strdup(config_node_entries[row - 1]), " ");
		char *ip = getIPAddress(entry, NULL);
		int neighbour = isNeighbour(strtok(NULL, " "));
		routing_table[row] = createRouteEntry(ip, neighbour == 1 ? ip : NULL,
				neighbour == 1 ? 1 : default_infinity, default_ttl);
	}
	//if (DEBUG_MODE) {
	printRoutingTable("\nInitialized Routing Table:\n");
	//}
}
/*
 * Initializes graph
 */
void initializeGraph(int config_node_count, char** config_node_entries,
		int default_infinity) {
	//+1 for localhost
	int total_nodes = config_node_count + 1;
	graph = create2DArray(total_nodes, total_nodes);
	graph_node_name = (GraphNode**) calloc(total_nodes, sizeof(GraphNode*));
	graph[0][0] = 0;
	graph_node_name[0] = createGraphNode(getlocalhostIP(), 0);
	int i;
	for (i = 0; i < config_node_count; i++) {
		char* entry = strtok(strdup(config_node_entries[i]), " ");
		char *ip = getIPAddress(entry, NULL);
		//TODO At the end of program remember to free the ip
		//char *status=strtok(NULL, " ");
		int neighbour = isNeighbour(strtok(NULL, " "));
		graph_node_name[i + 1] = createGraphNode(ip, neighbour);
		//if current node is neighbhour, weight =1, else infinity
		graph[0][i + 1] = neighbour == 1 ? 1 : default_infinity;
		//initialize the current node's distance with every node infinity
		int j = 0;
		for (j = 0; j < total_nodes; j++) {
			graph[i + 1][j] = default_infinity;
		}
		//free(config_node_entries[i]);
	}
//	if (DEBUG_MODE) {

		printGraph("\nInitialized graph:\n");
/*
		printf("\nInitialized graph node mapping");

		int k;
		for (k = 0; k < total_nodes; k++) {
			printf("\n");
			printf("Index:%d ip: %s, isNeighbour: %u", k, graph_node_name[k]->ip,
					graph_node_name[k]->isNeighbour);
		}
	}*/
}

/**
 * Initialize Message Queue
 */
void initializeMsgQueue() {
	msg_queue = NULL;
	if (pthread_mutex_init(&msg_queue_lock, NULL) != 0) {
		signal_error("Initialization of msgQueue synchronization lock failed");
	}
}
/**
 * Dynamically creates a route entry and initializes it with the given args
 */
RouteEntry* createRouteEntry(char* destination, char* next_hop, int cost,
		int ttl) {
	RouteEntry *entry = (RouteEntry*) calloc(1, sizeof(RouteEntry));
	entry->cost = cost;
	entry->destination = destination;
	entry->next_hop = next_hop;
	entry->ttl = ttl;
	entry->learnt_from = NULL;
	return entry;
}
/**
 * Sends router table to the neighbours
 */
void Send_Advertisement(int socket_fd, int port, int split_horizon,
		int total_nodes) {
	int i;
	//starting from i==1,as 0th index is localhost
	for (i = 1; i < total_nodes; i++) {
		if (graph_node_name[i]->isNeighbour == 1) {
			//if node is a neighbour, send the update message
			char*msg = generateUpdateMessage(total_nodes, split_horizon,
					graph_node_name[i]->ip);
			//send update message even if its blank, handle blank messages at receiver's side
			sendMessage(socket_fd, graph_node_name[i]->ip, port, msg);
			free(msg);
		}
	}
}
/**
 * Sends message to a given ip address
 */
void sendMessage(int socket_fd, char *ip, int port, char *update_msg) {
	struct sockaddr_in si_other;
	memset((char *) &si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(port);
	if (inet_aton(ip, &si_other.sin_addr) == 0) {
		signal_error("\nFailed to convert decimal IP into struct type");
	}
	if (sendto(socket_fd, update_msg, strlen(update_msg), 0,
			(const struct sockaddr *) &si_other, sizeof(si_other)) < 0) {
		if (DEBUG_MODE) {
			signal_error("\nFailed to send update message");
		}
	}
}

/**
 * Initializes a socket for sending/receiving data and
 */
int initSocket(int port) {
	int socket_file_descr;
	//unsigned int sockaddr_in_length;
	//struct hostent *server_entry;
	struct sockaddr_in localhost;
	socket_file_descr = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_file_descr <= -1) {
		signal_error("Failed creating a socket for the client");
	}
	//populate the server address details
	localhost.sin_family = AF_INET;
	//assigning the network byte order equivalent of port no
	localhost.sin_port = htons(port);
	localhost.sin_addr.s_addr = INADDR_ANY;
	//bind socket
	int bind_status = bind(socket_file_descr, (struct sockaddr *) &localhost,
			sizeof(localhost));
	if (bind_status == -1) {
		signal_error("Socket binding failed");
	}
	return socket_file_descr;
}

int **create2DArray(int rows, int columns) {
	int **arr = (int **) calloc(rows, sizeof(int*));
	int row;
	for (row = 0; row < rows; row++) {
		arr[row] = (int *) calloc(columns, sizeof(int));
	}
	return arr;
}

/**
 * Dynamically Allocates memory for a GraphNode and initializes with the given parameters
 */
GraphNode *createGraphNode(char* ip, unsigned char isNeighbour) {
	GraphNode* node = (GraphNode*) calloc(1, sizeof(GraphNode));
	node->ip = ip;
	node->msg_recvd = 0;
	node->isNeighbour = isNeighbour;
	return node;
}

/**
 * Read configuration file into char array
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
		signal_error("\nFailed to read the config file");
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
 * Returns the local host IP
 */
char* getlocalhostIP() {
	static char *ip = NULL;
	if (ip == NULL) {
		char* hostname = (char*) calloc(100, sizeof(char));
		gethostname(hostname, 100);
		ip = getIPAddress(hostname, NULL);
		free(hostname);
	}
	return strdup(ip);

	/*not required, only works on iu n/w
	 char* ip = NULL;
	 struct ifaddrs *ifaddr, *iterator;
	 getifaddrs(&ifaddr);
	 for (iterator = ifaddr; iterator != NULL && iterator->ifa_addr; iterator =
	 iterator->ifa_next) {
	 if (iterator->ifa_addr->sa_family == AF_INET
	 && strcmp(iterator->ifa_name, "em1") == 0) {
	 ip = inet_ntoa(
	 ((struct sockaddr_in *) iterator->ifa_addr)->sin_addr);
	 if (DEBUG_MODE) {
	 printf("\nLocal Machine IP:%s,%s", ip, iterator->ifa_name);
	 }
	 }
	 }
	 freeifaddrs(ifaddr);
	 return ip;
	 */
}

/**
 * Process command line args
 */
void processArgs(int argc, char *argv[], char** file_name, int *port,
		int *default_ttl, int *default_infinity, int *default_period,
		int *split_horizon) {
	if (argc < MIN_ARGS) {
		printf("\nThe program should be invocated w the arguments in following sequence:\n <config-file> <port-no> <default-ttl> <default-infinity> <update-period> <split-horizon (0 or 1)>");
		printf("\nExample for without split horizon: ./init config 65528 90 16 30 0");
		signal_error("\nInsufficient arguments\n");
	} else {
		*file_name = argv[ARG_FILE_NAME];
		*port = atoi(argv[ARG_PORT]);
		*default_ttl = atoi(argv[ARG_DEFAULT_TTL]);
		*default_infinity = atoi(argv[ARG_DEFAULT_INFINITY]);
		*default_period = atoi(argv[ARG_DEFAULT_PERIOD]);
		*split_horizon = atoi(argv[ARG_USE_SPLIT_HORIZON]);
	}
}
/**
 * Creates a update message by concatenating the routing table contents. Remember to free the msg once done
 */
char *generateUpdateMessage(int total_nodes, int split_horizon, char *for_ip) {
	/*if(DEBUG_MODE){
	 printf("in update msg");
	 }
	 */
	int size_required = total_nodes * SIZE_FOR_UPDATE_RECORD;
	char *msg = (char*) calloc(size_required * SIZE_FOR_UPDATE_RECORD,
			sizeof(char));
	int row;
	char *temp;
	temp = msg;
	for (row = 0; row < total_nodes; row++) {
		if (split_horizon
				&& (routing_table[row]->next_hop == NULL
						|| strcmp(routing_table[row]->next_hop, for_ip) != 0)) {
			//TODO test the split horizon part
			//add the row only if split horizon and either the learnt from entry is null or learnt from entry does not match the ip for which these messages are being generated
			temp += snprintf(temp, msg + size_required - temp, "%s:%d;",
					routing_table[row]->destination, routing_table[row]->cost);
		}
		if (!split_horizon) {
			if (DEBUG_MODE) {
				printf("%s:%d;", routing_table[row]->destination,
						routing_table[row]->cost);
			}
			temp += snprintf(temp, msg + size_required - temp, "%s:%d;",
					routing_table[row]->destination, routing_table[row]->cost);
		}
	}
	//last row fix
	*(temp - 1) = *temp;
	*temp = 0;
	//if (DEBUG_MODE) {
	printf("\nUpdate message generated:\n%s\nFor:%s\n", msg, for_ip);
	printf("\n");
	//}
	return msg;
}
/**
 * Tokenizes the update message into each entry and then splits into ip and distance
 * Calls Bellman Ford to converge and update the routing table and graph.
 */
void processUpdateMessage(char* msg, char *source_ip, int default_ttl,
		int default_infinity, int socket_fd, int port, int split_horizon) {
	char* msg_copy = strdup(msg);
	char* tempptr;
	char* tempptr1;
	char* entry = strtok_r(strdup(msg_copy), ";", &tempptr);
	if(DEBUG_MODE) {
		printf("\n%s", entry);
	}
	char* ip = strtok_r(entry, ":", &tempptr1);
	char* distance = strtok_r(NULL, ":", &tempptr1);
	if(DEBUG_MODE) {
		printf("\nCost to %s: %s", ip, distance);
	}
	updateGraph(source_ip, ip, atoi(distance));
	//tempptr=NULL;
	//tempptr1=NULL;
	while ((entry = strtok_r(NULL, ";", &tempptr)) != NULL) {
		if(DEBUG_MODE) {
			printf("\n%s", entry);
		}
		//tempptr=NULL;
		//tempptr1=NULL;
		char* ip = strtok_r(entry, ":", &tempptr1);
		char* distance = strtok_r(NULL, ":", &tempptr1);
		if(DEBUG_MODE) {
			printf("\nCost to %s: %s", ip, distance);
		}
		updateGraph(source_ip, ip, atoi(distance));
		// TODO: Confirm if the TTL resetting should be here!!!
		// Resetting the TTL for every node reported in the update msg.
		resetTtl(ip, source_ip, default_ttl, default_infinity);
	}
	// TODO: Confirm if the TTL resetting should be here!!!
	// Resetting the TTL for the source IP entry in routing table.
	resetTtl(source_ip, source_ip, default_ttl, default_infinity);

	graph_node_name[getGraphNodeIndexByIP(source_ip)]->msg_recvd = 1;

	free(msg_copy);
	free(msg);
	free(source_ip);



	// Send advertisement if routing table changed.
	if (recalculate( default_infinity, default_ttl)) {
		//char *msg=calloc()
		//printRoutingTable("\nUpdated routing table after bellman ford convergence:\n");
		Send_Advertisement(socket_fd, port, split_horizon, number_of_nodes);
	}
	printRoutingTable("\nUpdated routing table after bellman ford convergence:\n");

	if (DEBUG_MODE) {
		/*
		 printf("\nUpdated routing table after bellman ford convergence:\n");
		 int row;
		 for (row = 0; row < number_of_nodes; row++) {
		 printf("\t%s\t%s\t%d\t%d\n", routing_table[row]->destination,
		 routing_table[row]->next_hop, routing_table[row]->cost,
		 routing_table[row]->ttl);
		 }
		 */

		printGraph("\nUpdated graph after bellman ford convergence:\n");
	}
}

/**
 * Recalculates the distance vector and routing table. Return 1 if a routing table entry has changed
 */
int recalculate( default_infinity, default_ttl) {
	int changed;
	pthread_mutex_lock(&routing_table_lock);
	changed = recalculateDistanceVectors(default_infinity);
	// Ring the bell here!
	changed = bellmanFord(default_ttl, default_infinity) || changed;
	pthread_mutex_unlock(&routing_table_lock);

	return changed;
}

/**
 * Function to print routing table.
 */
void printRoutingTable(char *msg) {
	printf(msg);

	//Print Headers
	char* self = (char*)calloc(5, sizeof(char));
	strcat(self, "SELF");
	char* destination = (char*)calloc(12, sizeof(char));
	strcat(destination, "DESTINATION");
	char* nexthop = (char*)calloc(9, sizeof(char));
	strcat(nexthop, "NEXT_HOP");
	char* cost = (char*)calloc(5, sizeof(char));
	strcat(cost, "COST");
	char* ttl = (char*)calloc(4, sizeof(char));
	strcat(ttl, "TTL");

	printf("\n%16s %16s %8s %8s", destination, nexthop, cost, ttl);

	int row;
	for (row = 0; row < number_of_nodes; row++) {
		printf("\n%16s %16s %8d %8d", routing_table[row]->destination,
				routing_table[row]->next_hop, routing_table[row]->cost,
				routing_table[row]->ttl);
	}
	printf("\n");
	free(self);
	free(destination);
	free(nexthop);
	free(cost);
	free(ttl);
}

/**
 * Function to print Graph.
 */
void printGraph(char *msg) {
	printf(msg);
	int k, l;
	printf("\n");
	//Printing Header
	for (k = 0; k < number_of_nodes; k++) {
		printf("\t");
		if(k == 0) {
			printf("SELF");
		}
		else {
			printf("Node%d", k);
		}
	}
	//Printing Rows
	for (k = 0; k < number_of_nodes; k++) {
		printf("\n");
		if(k == 0) {
			printf("SELF");
		}
		else {
			printf("Node%d", k);
		}
		for (l = 0; l < number_of_nodes; l++) {
			printf("\t%d", graph[k][l]);
		}
	}
	printf("\n");
}

/**
 * Function that updates the graph with the distance info received.
 */
void updateGraph(char *source_ip, char *ip, int distance) {
	//printf("\nUpdating Distance of %s as %d, from %s", ip, distance, source_ip);

	// Update the graph with the routing info.
	graph[getGraphNodeIndexByIP(source_ip)][getGraphNodeIndexByIP(ip)] =
			distance;
}

int recalculateDistanceVectors(int default_infinity) {

	int x, temp_cost, changed;
	changed = 0;
	for (x = 0; x < number_of_nodes; x++) {

		// Update routing table entry.
		if (routing_table[x]->next_hop != NULL
				&& routing_table[x]->cost != default_infinity
				&& graph_node_name[getGraphNodeIndexByIP(
						routing_table[x]->next_hop)]->msg_recvd == 1) {
			temp_cost =
					graph[0][getGraphNodeIndexByIP(routing_table[x]->next_hop)]
							+ graph[getGraphNodeIndexByIP(
									routing_table[x]->next_hop)][getGraphNodeIndexByIP(
									routing_table[x]->destination)];

			routing_table[x]->cost =
					(temp_cost < default_infinity) ?
							temp_cost : default_infinity;
			changed = (temp_cost==graph[0][x])?0:1;
			// Update corresponding graph entry.
			graph[0][x] =
					(temp_cost < default_infinity) ?
							temp_cost : default_infinity;

			// Set the next hop to null if cost is infinity.
			if (routing_table[x]->cost == default_infinity) {
				routing_table[x]->next_hop = NULL;
			}
		}
	}
	return changed;
}

/**
 * Function runs Bellman Ford convergence on the table.
 * Returns 1 if routing table entry changed.
 */
int bellmanFord(int default_ttl, int default_infinity) {

	int x, y, z, temp_cost, change_flag = 0;
	for (x = 0; x < number_of_nodes; x++) {
		for (y = 0; y < number_of_nodes; y++) {
			for (z = 0; z < number_of_nodes; z++) {

				// Checking for shortest path condition for bellman ford.
				if ((graph[x][z] + graph[z][y]) < graph[x][y]) {

					temp_cost = graph[x][z] + graph[z][y];

					// Updating graph
					graph[x][y] =
							(temp_cost < default_infinity) ?
									temp_cost : default_infinity;

					// Updating routing table if update is for the self.
					if (x == 0) {
						routing_table[y]->cost =
								(temp_cost < default_infinity) ?
										temp_cost : default_infinity;

						free(routing_table[y]->next_hop);
						routing_table[y]->next_hop = (char *) malloc(
						SIZE_FOR_UPDATE_RECORD * sizeof(char));
						bzero(routing_table[y]->next_hop,
						SIZE_FOR_UPDATE_RECORD);
						strcpy(routing_table[y]->next_hop,
								graph_node_name[z]->ip);

						routing_table[y]->ttl = default_ttl;

						change_flag = 1;
					}
				}
			}
		}
	}
	return change_flag;
}

/**
 * Returns index of node in GraphNode using IP. If not found, returns -1.
 */
int getGraphNodeIndexByIP(char *ip) {

	int index = -1, x;
	for (x = 0; x < number_of_nodes; x++) {
		if (strcmp(graph_node_name[x]->ip, ip) == 0) {
			index = x;
			break;
		}
	}
	return index;
}

/**
 * Enqueue Message
 */
void enqueue(QueueNode **head, char *msg, char *from) {
	QueueNode *new = (QueueNode*) calloc(1, sizeof(QueueNode));
	new->msg = msg;
	new->from = from;
	pthread_mutex_lock(&msg_queue_lock);
	if (*head == NULL) {
		*head = new;
	}

	else {
		QueueNode *curr, *prev;
		curr = *head;
		while (curr != NULL) {
			prev = curr;
			curr = curr->next;
		}
		prev->next = new;
	}
	pthread_mutex_unlock(&msg_queue_lock);

	return;

}
/***
 * Dequeue message. In case queue is empty, returns NULL
 */
QueueNode* dequeue(QueueNode **head) {
	QueueNode* result = NULL;
	pthread_mutex_lock(&msg_queue_lock);
	if (*head != NULL) {
		result = *head;
		*head = (*head)->next;
		//free(temp);
	}
	pthread_mutex_unlock(&msg_queue_lock);
	return result;
}
/**
 * Return the ip address of the given host
 */
char* getIPAddress(char *hostname, char *port) {
	struct addrinfo pointers, *host;
	int returnval;
	memset(&pointers, 0, sizeof pointers);
	pointers.ai_family = AF_UNSPEC;
	pointers.ai_socktype = SOCK_DGRAM;
	if ((returnval = getaddrinfo(hostname, NULL, &pointers, &host)) != 0) {
		signal_error("\nFailed to resolve ip address for host");
	}
	//char *ip=(char*)calloc(16,sizeof(char));
	struct sockaddr_in *addr = (struct sockaddr_in *) host->ai_addr;
	return strdup(inet_ntoa((struct in_addr) addr->sin_addr));
	//return ip;
}

void signal_error(char *err_msg) {
	fprintf(stderr, err_msg);
	fprintf(stderr, "\nShutting down\n");
	exit(1);
}

int isNeighbour(char *indicator) {
	return strncmp(indicator, "yes", 3) == 0;
}
