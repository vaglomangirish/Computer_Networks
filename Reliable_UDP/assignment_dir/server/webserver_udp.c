// UDP Server serving data requests from client
// Author: Mangirish Wagle

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <math.h>

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

typedef struct packet_meta
{
    struct timeval send_time;
    struct timeval ack_time;
    int ack_count;
    int to_be_dropped;
    udp_pack pack;
} pack_meta;

// ----------End of Structure Declarations---------- //

// ----------Global Variable Declarations---------- //
pack_meta packet_buffer[BUFFERSIZE];
int packet_buffer_length = 0;

float window_size = 1;  // Window size initialized to 1 MSS
int current_pos = 0;
int syn_timeout = 60; // SYN Timeout value initialized to 60 Seconds.
int timeout = 1500;   // Timeout value initialized to 1500usecs.
float estimated_rtt = 500;  // Estimated RTT set to 500usecs.
float rtt_deviation = 0;  // RTT deviation initialized to 0

//Congestion Control variables
int is_congestion = 0;     // Flag for congestion. 1 -> Congestion control phase; 0 -> Slow Start phase.
int slow_start_count = 0;  // Count for packets transmitted in slow start.
int congestion_count = 0;  // Count for packets transmitted in Congestion phase.

int enable_packet_drop = 0;  // Flag for packet drop simulating packet loss.
int drop_percent = 0;   // Drop percentage used if packet drop is enabled.

int len;
socklen_t clen;
struct sockaddr_in serv_address, cli_address;

int server_sfd, client_sfd, port;

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

    //free(temp);

    return serialized_pack;
}

/*
 * Function to deserialize the data received over the socket to udp packet.
 */
udp_pack deserialize_data(char *data)
{
    char *local_data_copy = (char *)malloc((DATALEN)*sizeof(char));
    bzero(local_data_copy, DATALEN);
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
 * Function that calculates difference between two timestamps given in struct timeval.
 * Returns the difference in microseconds.
 */
int diff_timeval(struct timeval x, struct timeval y)
{
  struct timeval result;
  long retval;

  if (x.tv_usec < y.tv_usec) {
    int nsec = (y.tv_usec - x.tv_usec) / 1000000 + 1;
    y.tv_usec -= 1000000 * nsec;
    y.tv_sec += nsec;
  }
  if (x.tv_usec - y.tv_usec > 1000000) {
    int nsec = (x.tv_usec - y.tv_usec) / 1000000;
    y.tv_usec += 1000000 * nsec;
    y.tv_sec -= nsec;
  }

  result.tv_sec = x.tv_sec - y.tv_sec;
  result.tv_usec = x.tv_usec - y.tv_usec;

  retval = (result.tv_sec * 1000000) + result.tv_usec;

  return retval;
}

/*
 *  Function that marks packets for dropping based on rand() and drop percentage.
 */
void mark_dropped_packets()
{
    int no_of_packs_to_drop = (int)(((float)drop_percent/100.00)*(float)packet_buffer_length);

    printf("\n%d packets will not be transmitted for simulating packet drop...\n", no_of_packs_to_drop);

    int d, randnum;

    for(d = 0; d < no_of_packs_to_drop; d++)
    {
        // Get a random number within the packet buffer length
        randnum = rand() % (packet_buffer_length-1);

        packet_buffer[randnum].to_be_dropped = 1;

        //printf("\nPacket at index %d dropped", randnum);
    }
}

/*
 * Function that handles the handshake initiated by the client
 */
char *handle_handshake()
{
    int len;
    udp_pack syn, synack, ack;

    fd_set fds;                // fd set declare
    struct timeval time_out;   // Struct for Timeout value

    char *buffer = (char *)malloc(MTU*sizeof(char));
    bzero(buffer, MTU);

    printf("\nWaiting for handshake from client for next 60 seconds...\n");

    // Receive SYN

    FD_ZERO(&fds);             // Clear the fd set
    FD_SET(server_sfd, &fds);  // Set the server fd for select

    time_out.tv_sec = syn_timeout;      // Server will expect the connection from client within 1 min.
    time_out.tv_usec = 0;

    // Waiting until timeout for next packet
    if ( select(32, &fds, NULL, NULL, &time_out) == 0 )
    {
        printf("SYN receiving timed out...\n");
    }
    else
    {
        len = recvfrom(server_sfd ,buffer, MTU-1, 0, (struct sockaddr *) &cli_address, &clen);
        if (len < 0)
        {
          report_error("\nHANDSHAKE ERR: Socket could not be read for SYN\n");
        }
        //printf("\nSYN Rcvd: %s", buffer);
        syn = deserialize_data(buffer);

        if(syn.is_ack != 0)
        {
            report_error("\nHANDSHAKE ERR: First packet is not SYN\n");
        }

        printf("\nHANDSHAKE SYN Received\n");

        synack = syn;
        synack.ack_num = synack.seq_num + 1;
        synack.is_ack = 1;

        // Allocating buffers

        // Send SYNACK
        char *synack_pack = (char *)malloc(MTU*sizeof(char));
        bzero(synack_pack, MTU);
        strcpy(synack_pack, serialize_packet(synack));
        //printf("\nSYNACK to send: %s", synack_pack);
        len = sendto(server_sfd, synack_pack, strlen(synack_pack), 0, (struct sockaddr *) &cli_address, clen);
        if (len < 0)
        {
          report_error("\nHANDSHAKE ERR: Socket could not be written for SYNACK\n");
        }
        printf("\nHANDSHAKE SYNACK Sent\n");

        bzero(buffer, MTU);
        // Receive ACK
        FD_ZERO(&fds);             // Clear the fd set
        FD_SET(server_sfd, &fds);  // Set the server fd for select

        time_out.tv_sec = 0;
        time_out.tv_usec = timeout;

        // Waiting until timeout for next packet
        if ( select(32, &fds, NULL, NULL, &time_out) == 0 )
        {
            printf("SYNACK receiving timed out...\n");
        }
        else
        {
            len = recvfrom(server_sfd ,buffer, MTU-1, 0, (struct sockaddr *) &cli_address, &clen);
            if (len < 0)
            {
              report_error("\nHANDSHAKE ERR: Socket could not be read for ACK\n");
            }

            //printf("ack received: %s", buffer);

            ack = deserialize_data(buffer);

            if(ack.is_ack == 0)
            {
                report_error("\nHANDSHAKE ERR: First packet is not SYN\n");
            }
            //printf("ACK Rcvd: %s", buffer);
            printf("\nHANDSHAKE ACK Received\n");

            printf("\n-------- Handshake Successful --------\n");

            //printf("filename recvd: %s", ack.data);

            return ack.data;
        }
        //free(synack_pack);
    }
    //free(buffer);
    return "";
}

/*
 * Function that prints packet data.
 */
void print_buff_packet(pack_meta packet)
{
    printf("Seq. No: %d | Sent time- %ld: %ld\n", packet.pack.seq_num, packet.send_time.tv_sec, packet.send_time.tv_usec);
}

/*
 * Function that breaks file content in packets of appropriate length
 */
void prepare_meta_packets(char *filename, int port)
{
    FILE *filep;

    filep = fopen(filename,"r");

    if(filep == NULL)
    {
        fprintf(stderr, "\nFile %s cannot be opened\n", filename);
        exit(1);
    }

    fseek(filep, 0L, SEEK_SET);

    int buffindex = 0;

    while(!feof(filep))
    {

        initialize_packet(&packet_buffer[buffindex].pack, port, buffindex + 1, 0);

        packet_buffer[buffindex].pack.data = (char *)malloc(DATALEN*sizeof(char));

        memset(packet_buffer[buffindex].pack.data, 0, DATALEN);

        fread(packet_buffer[buffindex].pack.data, DATALEN, 1, filep);

        packet_buffer[buffindex].send_time.tv_sec = 0;
        packet_buffer[buffindex].send_time.tv_usec = 0;

        packet_buffer[buffindex].ack_time.tv_sec = 0;
        packet_buffer[buffindex].ack_time.tv_usec = 0;

        packet_buffer[buffindex].ack_count = 0;
        packet_buffer[buffindex].to_be_dropped = 0;

        buffindex++;
    }

    packet_buffer_length = buffindex;
    packet_buffer[packet_buffer_length - 1].pack.is_fin = 1;
    printf("Packet Buffer Length = %d", packet_buffer_length);
}

/*
 * Function that calculates RTT with Jacob Karels algorithm
 * Accepts last acknowledged packet number as parameter.
 */
void update_timeout(int last_ack_pack_num)
{
    printf("\n---------- Calculating Estimated RTT and Timeout ----------");
    printf("\nPacket %d | Send time: %ld:%ld | Ack Time: %ld:%ld",
              last_ack_pack_num + 1, packet_buffer[last_ack_pack_num].send_time.tv_sec,
              packet_buffer[last_ack_pack_num].send_time.tv_usec, packet_buffer[last_ack_pack_num].ack_time.tv_sec,
              packet_buffer[last_ack_pack_num].ack_time.tv_usec);

    float alpha = 0.0125, psi = 4, mu = 1, delta = 0.5;
    long sampled_rtt;

    sampled_rtt = diff_timeval(packet_buffer[last_ack_pack_num].ack_time, packet_buffer[last_ack_pack_num].send_time);
    printf("\nSampled RTT: %d", sampled_rtt);

    estimated_rtt = ((1 - alpha)*estimated_rtt) + ((alpha)*(float)sampled_rtt);
    printf("\nEstimated RTT: %f", estimated_rtt);

    rtt_deviation = rtt_deviation + delta*(abs((float)sampled_rtt - estimated_rtt) - rtt_deviation);
    printf("\nRTT Deviation: %f", rtt_deviation);

    timeout = (int)((mu*estimated_rtt) + (psi*rtt_deviation));
    printf("\nUpdated Timeout: %d\n", timeout);
}

/*
 * Function to handle congestion control.
 */
void congestion_control()
{
    // Check if 3 duplicate ACKs or a retransmit.
    if((packet_buffer[current_pos].ack_count >= 3) || (packet_buffer[current_pos].send_time.tv_sec != 0)
        && (is_congestion == 0))
    {
        is_congestion = 1;
        window_size = (window_size/2);    //Window size halved in case of congestion.
        //Ensure min 1 for window size.
        if(window_size < 1)
        {
            window_size = 1.0;
        }
        printf("\n!!! Transmission entering Congestion Avoidance Mode !!!\n");
    }

    if(is_congestion)
    {
        // If congestion, increment window size linearly.
        int mss = 1;   // MSS is counted as 1 packet = 1500 bytes.
        window_size += ((float)mss*((float)mss/window_size));
        //printf("Updated window size: %f", window_size);
        congestion_count += current_pos - (congestion_count + slow_start_count);
    }
    else
    {
        // Else, increase window size by 1 for every ACK received.
        window_size++;
        slow_start_count += current_pos - (congestion_count + slow_start_count);
    }
    printf("\nWindow Size = %d", (int)window_size);
}

void receive_acks_handler()
{
    udp_pack ack;

    fd_set fds;                // fd set declare
    struct timeval time_out;   // Struct for Timeout value

    char *buffer = (char *)malloc(MTU*sizeof(char));
    bzero(buffer, MTU);

    bzero(buffer, MTU);

    // Receive ACK
    FD_ZERO(&fds);             // Clear the fd set
    FD_SET(server_sfd, &fds);  // Set the server fd for select

    time_out.tv_sec = 0;
    time_out.tv_usec = timeout;
    //printf("Waiting for ack...");

    // Waiting until timeout for next ack
    if ( select(32, &fds, NULL, NULL, &time_out) == 0 )
    {
        printf("\nACK receiving timed out...\n");
    }
    else
    {
        len = recvfrom(server_sfd ,buffer, MTU-1, 0, (struct sockaddr *) &cli_address, &clen);
        if (len < 0)
        {
            printf("\nError while receiving ACK at current count = %d\n", current_pos);
            report_error("\nERROR: Socket could not be read for ACK\n");
        }
        ack = deserialize_data(buffer);

        if(ack.is_ack == 1)    //Check if the received packet is an ACK.
        {
            int ack_number = ack.ack_num;
            int x;
            if(ack_number > 0)    //First ACK number would be 1 for packet 0
            {
                // Iterate over the last transmitted window and marked the packets acked.
                for(x = current_pos; x <=(ack_number - 2); x++)
                {
                    //Update the first ACK time for the packet
                    if(packet_buffer[x].ack_count == 0)
                    {
                        gettimeofday(&packet_buffer[x].ack_time, NULL);    //Sample ACK time for the packet.
                    }
                    //Increment ack count for the packet. This is used for congestion control for 3 dup acks.
                    packet_buffer[x].ack_count += 1;    //Set ACKed flag for the acked packets in buffer
                }
                printf("\nReceived ACK: %d\n", ack_number);
                current_pos = ack_number - 1;

                update_timeout(current_pos - 1);
            }
        }
    }
}

void send_packets_handler()
{
    int len, x;

    //printf("Inside send handler: current_pos= %d, packet_buff=%d", current_pos, packet_buffer_length);
    printf("\n-------- Data Packets transfer begins --------\n");

    char *packet_to_send = (char *)malloc(MTU*sizeof(char));
    bzero(packet_to_send, MTU);

    while(current_pos < packet_buffer_length)
    {
        int limit = (packet_buffer_length < (current_pos + (int)window_size)
                                          ? (packet_buffer_length - current_pos) : (int)window_size);

        for(x=0; x < limit; x++)
        {
            if(packet_buffer[current_pos + x].to_be_dropped != 1)
            {
                //printf("Entered for");
                strcpy(packet_to_send, serialize_packet(packet_buffer[current_pos + x].pack));
                //printf("\nSending data: %s", packet_to_send);

                len = sendto(server_sfd, packet_to_send, strlen(packet_to_send), 0, (struct sockaddr *) &cli_address, clen);
                if (len < 0)
                {
                  printf("\nError while sending packet: ");
                  print_buff_packet(packet_buffer[current_pos + x]);
                  report_error("\nERROR: Socket could not be written while data transfer\n");
                }
                gettimeofday(&packet_buffer[current_pos + x].send_time, NULL);
                printf("\nPacket sent: ");
                print_buff_packet(packet_buffer[current_pos + x]);
            }
            else
            {
				//Record Send time to simulate an attempted send but dropped.
				gettimeofday(&packet_buffer[current_pos + x].send_time, NULL);

                printf("\nPacket with Seq. No. %d Dropped!\n", packet_buffer[current_pos + x].pack.seq_num);
                packet_buffer[current_pos + x].to_be_dropped = 0;
            }
        }
        receive_acks_handler();
        congestion_control();
    }
    printf("\n-------- Data Packets transfer ends --------\n");
}

/*
 * Core server run function that accepts udp requests and responds to the client over udp on specified port parameter
 */
void server_run(char *portno)
{
  server_sfd = socket(AF_INET, SOCK_DGRAM, 0); //Initializing DGRAM Socket
  if(server_sfd < 0)
    report_error("\nERR: Socket opening error\n");
    //TODO: If the port is already occupied, should be reused?

  port = atoi(portno);

  bzero((char *) &serv_address, sizeof(serv_address));
  serv_address.sin_family = AF_UNSPEC;
  serv_address.sin_addr.s_addr = INADDR_ANY;
  serv_address.sin_port = htons(port);

  clen = sizeof(cli_address);

  if (bind(server_sfd, (struct sockaddr *) &serv_address,
            sizeof(serv_address)) < 0)
  {
    report_error("\nERR: binding unsuccessful\n");
  }

  printf("\nServer started. Waiting for datagram requests...\n", portno);

  char *filename = (char *)malloc(DATALEN*sizeof(char));
  bzero(filename, DATALEN);
  strcpy(filename, handle_handshake());

  if(strcmp(filename, "") != 0)
  {
    prepare_meta_packets(filename, atoi(portno));
    if(enable_packet_drop == 1)
    {
        mark_dropped_packets();
    }
    send_packets_handler();

    float percent_slow = ((float)slow_start_count/(float)packet_buffer_length)*100.0;
    float percent_congestion = ((float)congestion_count/(float)packet_buffer_length)*100.0;

    printf("\nNumber of packets transmitted in slow start: %d\n", slow_start_count);
    printf("\nNumber of packets transmitted in Congestion Control phase: %d\n", congestion_count);
    printf("\nPercentage of packets transmitted in slow start: %f percent\n", percent_slow);
    printf("\nPercentage of packets transmitted in Congestion Control phase: %f percent\n", percent_congestion);
  }

  free(filename);
  close(client_sfd);
  close(server_sfd);
}

/*
 * Main function
 */
void main(int argc, char *argv[])
{

  if(argc < 3)
  {
    fprintf(stderr, "\nERR: Usage: %s <PORT> <WINDOW_SIZE> [<ENABLE_PACKET_LOSS (1/0) Default: 0>] [DROP_PERCENTAGE]\n", argv[0]);
    exit(1);
  }

  char *port = argv[1];
  window_size = (float)atoi(argv[2]);

  if(argv[3] != NULL)
  {
    if(argv[4] == NULL)
    {
        printf("\nPlease provide the DROP_PERCENTAGE\n");
        fprintf(stderr, "\nERR: Usage: %s <PORT> <WINDOW_SIZE> [<ENABLE_PACKET_LOSS (1/0) Default: 0>] [DROP_PERCENTAGE]\n", argv[0]);
        exit(1);
    }
    enable_packet_drop = atoi(argv[3]);
    drop_percent = atoi(argv[4]);
  }

  server_run(port);
}
