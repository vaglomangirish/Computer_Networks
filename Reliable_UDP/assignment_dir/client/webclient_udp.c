// UDP Client sending and receiving data over UDP
// Author: Mangirish Wagle

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#define BUFFERSIZE 1024*1024*20   //Setting sufficient buffer size to handle large files
#define MTU 1500
#define HEADERLEN 22  // Fixed Header Length
#define DATALEN MTU - HEADERLEN  // Data length is MTU - Header Length
#define SEQMOD 1000    // MOD for sequence number after which it would start over from 1
#define NODATA "!!!<<<NO_DATA>>>!!!"    //Constant string to indicate No Data in the packet
#define INITSEQ 1  // Initial sequence number

// ----------Structure Declarations---------- //

// Structure that mimics TCP Packet to be used over UDP
typedef struct udp_packet
{

  /* HEADER PARAMS BEGIN */
  int src_port;      // Source Port
  int dest_port;     // Destination Port
  int seq_num;       // Sequence Number
  int ack_num;       // Acknowledgement Number
  int head_length;   // Header Length
  int is_ack;        // ACK Flag
  int is_fin;        // FIN Flag
  /* HEADER PARAMS END */

  char *data;        // Data

} udp_pack;

// ----------End of Structure Declarations---------- //

// ----------Global Variable Declarations---------- //

int server_sfd, client_sfd, port, n, lnt;
socklen_t clen;
struct sockaddr_in client_addr, server_addr;
struct hostent *server;

int sequence_num = 1;  // Sequence number to be used for ACKs initialized to 1.
int window_size = 10;
int ordered_count = 0;  // This is the variable that stores number of ordered packets received at a point of time.
int timeout = 1000;   //Timeout value initialized to 1000usecs.

int delays_enabled = 0;   // Flag for arbitrary delays to simulate high latency.

udp_pack packet_buffer[BUFFERSIZE];

/*
 * Flag indicating if the transfer is finished. 1- True, 0- False
 * This will be set to 1 after receiving the final packet from the server.
 */
int IS_TRANSFER_FIN = 0;

// ----------End of Global Variable Declarations---------- //

void initialize_packet(udp_pack *pack, int port, int seq_num, int is_ack)
{
    pack->src_port = port;
    pack->dest_port = port;
    pack->seq_num = seq_num;
    pack->ack_num = 0;
    pack->head_length = HEADERLEN;
    pack->is_ack = is_ack;
    pack->is_fin = 0;
    pack->data = (char *)malloc((DATALEN)*sizeof(char));
    bzero(pack->data, DATALEN);
    strcat(pack->data, NODATA);
}

/*
 * Function to serialize the udp_packet to send over the socket.
 */
char *serialize_packet(udp_pack packet)
{
    char *serialized_pack = (char *)malloc(MTU * sizeof(char));
    char *temp = (char *)malloc(5 * sizeof(char));
    bzero(serialized_pack, MTU);
    bzero(temp, 5);

    // Add Source Port
    sprintf(temp, "%04d", packet.src_port);
    strcat(serialized_pack, temp);

    // Adding Delimiter \a
    strcat(serialized_pack, "\a");

    // Add Destination Port
    sprintf(temp, "%04d", packet.dest_port);
    strcat(serialized_pack, temp);

    // Adding Delimiter \a
    strcat(serialized_pack, "\a");

    // Add Sequence Number
    sprintf(temp, "%04d", packet.seq_num);
    strcat(serialized_pack, temp);

    // Adding Delimiter \a
    strcat(serialized_pack, "\a");

    // Add ACK Number
    sprintf(temp, "%04d", packet.ack_num);
    strcat(serialized_pack, temp);

    // Adding Delimiter \a
    strcat(serialized_pack, "\a");

    // Add Header Length
    sprintf(temp, "%04d", packet.head_length);
    strcat(serialized_pack, temp);

    // Adding Delimiter \a
    strcat(serialized_pack, "\a");

    // Add ACK Flag
    sprintf(temp, "%d", packet.is_ack);
    strcat(serialized_pack, temp);

    // Adding Delimiter \a
    strcat(serialized_pack, "\a");

    // Add FIN Flag
    sprintf(temp, "%d", packet.is_fin);
    strcat(serialized_pack, temp);

    // Adding Delimiter \a
    strcat(serialized_pack, "\a");

    // Add Data
    strcat(serialized_pack, packet.data);

    return serialized_pack;
}

/*
 * Function to deserialize the data received over the socket to udp packet.
 */
udp_pack deserialize_data(char *data)
{
    char *local_data_copy = (char *)malloc((DATALEN)*sizeof(char));
    strcpy(local_data_copy, data);

    udp_pack packet;

    packet.src_port = atoi(strtok(local_data_copy, "\a"));
    packet.dest_port = atoi(strtok(NULL, "\a"));
    packet.seq_num = atoi(strtok(NULL, "\a"));
    packet.ack_num = atoi(strtok(NULL, "\a"));
    packet.head_length = atoi(strtok(NULL, "\a"));
    packet.is_ack = atoi(strtok(NULL, "\a"));
    packet.is_fin = atoi(strtok(NULL, "\a"));
    packet.data = (char *)malloc((DATALEN)*sizeof(char));
    bzero(packet.data, DATALEN);
    strcpy(packet.data, strtok(NULL, "\a"));

    return packet;
}

/*
 * Error reporting function. Closes the socket file descriptors before exiting
 */
void report_error(const char *message)
{
  if(server_sfd)
  {
    close(server_sfd);
  }
  if(client_sfd)
  {
    close(client_sfd);
  }
  perror(message);
  exit(1);
}

/*
 * Function that writes the file contents fetched from server in the response, to a file on the client side.
 */
// TODO: Fix the memory dump for last two packages to be written to file.
void write_response_to_file(char *filename)
{
   //char *complete_data = (char *)malloc(DATALEN*ordered_count*sizeof(char));
   //bzero(complete_data, DATALEN*ordered_count);

   int x;
   FILE *filep;
   filep = fopen(filename, "w");
   if(filep == NULL)
  {
    fprintf(stderr, "\nFile %s cannot be opened\n", filename);
    exit(1);
  }
   //printf("\n2");
   fseek(filep, 0L, SEEK_SET);

   for(x = 0; x < ordered_count; x++)
   {
        //printf("\nwriting packet %d to file", x);
        fwrite(packet_buffer[x].data, strlen(packet_buffer[x].data), 1, filep);
        //sleep(1);
   }
   fclose(filep);

}

/*
 * Function to display file contents of filename passed to the function
 * Accepts filename as param
 */
void display_file_contents(char *filename)
{
  FILE *filep;

  filep = fopen(filename,"r");

  if(filep == NULL)
  {
    fprintf(stderr, "\nFile %s cannot be opened\n", filename);
    exit(1);
  }

  int size;
  char *filecontent = (char *)malloc(BUFFERSIZE*sizeof(char));

  memset(filecontent, 0, BUFFERSIZE);

  fseek(filep, 0L, SEEK_END);
  size = ftell(filep);
  fseek(filep, 0L, SEEK_SET);
  fread(filecontent, size, 1, filep);
  fclose(filep);

  printf("\n%s file content:-\n\n%s\n", filename, filecontent);

  free(filecontent);
}

/*
 * Function to establish handshake.
 */
int establish_handshake(char *req_file)
{
    int success = 0, n;

    udp_pack syn, synack, ack;

    fd_set fds;                // fd set declare
    struct timeval time_out;   // Struct for Timeout value

    initialize_packet(&syn, 2222, sequence_num, 0);

    char *synreq = (char *)malloc(MTU*sizeof(char));
    bzero(synreq, MTU);
    strcpy(synreq, serialize_packet(syn));

    // SYN
    //printf("\nSYN to send: %s", synreq);
    n = sendto(client_sfd, synreq, strlen(synreq), 0, (struct sockaddr *) &server_addr, clen);
    if (n < 0)
    {
      report_error("HANDSHAKE ERR: writing to socket for SYN");
    }
    sequence_num++;
    printf("\nHANDSHAKE SYN Sent\n");
    //gettimeofday(&start_timev,NULL);

    printf("\a");
    char *buffer = (char *)malloc(MTU*sizeof(char));
    bzero(buffer, MTU);

    // SYN-ACK
    FD_ZERO(&fds);             // Clear the fd set
    FD_SET(client_sfd, &fds);           // Set the client fd for select

    time_out.tv_sec = 0;
    time_out.tv_usec = timeout;

    // Waiting until timeout for SYNACK
    if ( select(32, &fds, NULL, NULL, &time_out) == 0 )
    {
        printf("SYNACK receiving timed out...\n");
    }
    else
    {
        bzero(buffer, MTU);
        n = recvfrom(client_sfd, buffer, MTU, 0, (struct sockaddr *) &server_addr, &clen);
        if (n < 0)
        {
          report_error("HANDSHAKE ERR: reading from socket for SYNACK");
        }
        //printf("\nSYNACK response: %s", buffer);

        synack = deserialize_data(buffer);

        if(synack.is_ack != 1)
        {
            report_error("HANDSHAKE ERR: SYNACK not received from server during handshake");
        }
        printf("\nHANDSHAKE SYNACK received\n");

        ack = synack;
        ack.seq_num = sequence_num;
        ack.ack_num = ack.seq_num + 1;
        ack.is_ack = 1;
        memset(ack.data, 0, DATALEN);
        strcpy(ack.data, req_file);

        char *ackpack = (char *)malloc(MTU*sizeof(char));
        bzero(ackpack, MTU);
        strcpy(ackpack, serialize_packet(ack));

        //printf("Filename sent : %s", ackpack);

        // ACK
        //printf("\nACK to send: %s", ackpack);
        n = sendto(client_sfd, ackpack, strlen(ackpack), 0, (struct sockaddr *) &server_addr, clen);
        if (n < 0)
        {
          report_error("HANDSHAKE ERR: writing to socket Handshake ACK");
        }
        printf("\nHANDSHAKE ACK Sent\n");
        sequence_num++;

        success = 1;
        printf("\n-------- Handshake Successful --------\n");
    }
    return success;
}

/*
 * Function that checks if all the packets arrived in sequence upto the FIN packet
 * If the FIN packet not arrived, it updates the ordered count
 */
int check_sequence_fin()
{
    int x = 0, retval = 0;
    for(x = 0; packet_buffer[x].seq_num > 0; x++)
    {
        if(packet_buffer[x].is_fin == 1)
        {
            retval = 1;
            x++;
            break;
        }
    }
    ordered_count = x;
    return retval;
}

/*
 * Function that sends cumulative ACK.
 */
void send_ack()
{
    int len;
    udp_pack ack_packet;
    initialize_packet(&ack_packet, port, sequence_num, 1);
    ack_packet.ack_num = ordered_count + 1;
    strcpy(ack_packet.data, NODATA);

    char *ackpack = (char *)malloc(MTU*sizeof(char));
    bzero(ackpack, MTU);
    strcpy(ackpack, serialize_packet(ack_packet));

    len = sendto(client_sfd, ackpack, strlen(ackpack), 0, (struct sockaddr *) &server_addr, clen);
    if (n < 0)
    {
      printf("\nError while sending ACK at current count = %d\n", ordered_count);
      report_error("ERROR: Sending ACK");
    }
    sequence_num++;
    printf("\nACK: %d Sent\n", ack_packet.ack_num);
}

// TODO: Fix segmentation fault after receiving 42 packets...
void receive_packets_handler()
{
    udp_pack packet;
    char *buffer = (char *)malloc(MTU*sizeof(char));
    int len, x, window_packets;

    fd_set fds;                // fd set declare
    struct timeval time_out;   // Struct for Timeout value

    printf("\n-------- Data Packets transfer begins --------\n");
    while(IS_TRANSFER_FIN == 0)
    {
        // This variable is used to count number of packets received in a window transmit from server.
        window_packets = 0;

        // Receive complete window.
        for(x = 0; ((x < window_size) && (IS_TRANSFER_FIN == 0)); x++)
        {
            bzero(buffer, MTU);

            // Receive Packet
            FD_ZERO(&fds);             // Clear the fd set
            FD_SET(client_sfd, &fds);           // Set the client fd for select

            time_out.tv_sec = 0;
            time_out.tv_usec = timeout;

            // Waiting until timeout for next packet
            if ( select(32, &fds, NULL, NULL, &time_out) == 0 )
            {
                printf("Packet receiving timed out. Expected packet with Seq. Number: %d\n", ordered_count + 1);
                break;
            }
            else
            {
                len = recvfrom(client_sfd, buffer, MTU, 0, (struct sockaddr *) &server_addr, &clen);
                if (len < 0)
                {
                    printf("\nError while receiving Packet at current count = %d\n", ordered_count);
                    report_error("\nERROR: Socket could not be read for ACK\n");
                }
                packet = deserialize_data(buffer);

                //printf("Serialized pack received: %s", buffer);

                //printf("Packet received: Seq. Num = %d | Is_Fin = %d | Data = %s", packet.seq_num, packet.is_fin, packet.data);

                printf("\nReceived Packet: Seq. Num: %d\n", packet.seq_num);

                // Buffer all packets, including out of sequence.
                int index = packet.seq_num - 1;

                packet_buffer[index] = packet;

                window_packets++;

                if(check_sequence_fin() == 1)    //Check if all packets in sequence until FIN
                {
                    printf("\nAll packets received and placed in order, Transfer finished\n");
                    IS_TRANSFER_FIN = 1;
                }
            }
        }

        //Delay to simulate high latency
        if(delays_enabled == 1)
        {
            // for every 3rd ack add a delay.
            if(sequence_num % 3 == 0)
            {
                printf("\n!!! Adding delay of 1500 micro seconds!!!");
                usleep(1500);  // Delay of 1000 micro seconds.
            }
        }

        //Update next expected window size, i.e. one more than received packets.
        window_size = window_packets + 1;

        send_ack();    //Send Cumulative ACK for entire window transferred by sender.
    }
    printf("\n-------- Data Packets transfer ends --------\n");
    printf("\nReceived packet count: %d\n", ordered_count);
}

/*
 * Core client function that sends and receives data to server on udp
 * Accepts hostname, portnumber and filename as params
 */
void run_client(char *host, char *portnum, char *filename)
{
    //printf("Inside run client");

    char *buffer = (char *)malloc(BUFFERSIZE*sizeof(char));
    bzero(buffer, BUFFERSIZE);

    port = atoi(portnum);

    client_sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_sfd < 0)
    {
        report_error("ERROR opening socket");
    }
    //printf("Client sfd defined");
    server = gethostbyname(host);

    if (server == NULL)
    {
        fprintf(stderr,"ERROR, Host not found\n");
        exit(0);
    }

    bzero((char *) &server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;

    bcopy((char *)server->h_addr,
         (char *)&server_addr.sin_addr.s_addr,
         server->h_length);

    server_addr.sin_port = htons(port);

    char *request = (char *)malloc(BUFFERSIZE*sizeof(char));
    bzero(request, BUFFERSIZE);
    clen = sizeof(server_addr);

    if(establish_handshake(filename) != 1)
    {
        fprintf(stderr,"ERROR, Handshake did not succeed!\n");
        exit(1);
    }

    receive_packets_handler();

    // Transferred file name on client would be original file name requested suffixed with -received.
    strcat(filename, "-received");
    write_response_to_file(filename);

    close(client_sfd);
    close(server_sfd);
}

/*
 * Main function
 */
void main(int argc, char *argv[])
{
  if (argc < 5)
  {
     fprintf(stderr,"usage %s <HOST> <PORT> <FILENAME> <WINDOW_SIZE> [<DELAYS_ENABLED (1/0) Default: 0>]\n", argv[0]);
     exit(0);
   }

  window_size = atoi(argv[4]);

  if(argv[5] != NULL)
  {
    delays_enabled = atoi(argv[5]);
  }

  run_client(argv[1], argv[2], argv[3]);
}
