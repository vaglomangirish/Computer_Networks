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

#define BUFFERSIZE 1024*1024*2   //Setting sufficient buffer size to handle large files

struct timeval start_timev, end_timev;  //Structs to use for gettimeoftheday

/*
 * Utility function to trim white spaces from string.
 * Called by the run functions to trim file names fetched from file for persistent connections.
 */
char *trim_white_spaces(char *string)
{
  // Trim leading space
  while(isspace(*string))
  {
    string++;
  }

  if(*string == 0)  // If all spaces
    return string;

  char *ending;

  // Trim trailing space
  ending = string + strlen(string) - 1;
  while(ending > string && isspace(*ending)) ending--;

  // Write new null terminator
  *(ending+1) = 0;

  return string;
}

/*
 * Error reporting function. Closes the socket file descriptors before exiting
 */
void report_error(const char *message, int *server_sfd, int *client_sfd)
{
  if(*server_sfd)
  {
    close(*server_sfd);
  }
  if(*client_sfd)
  {
    close(*client_sfd);
  }
  perror(message);
  exit(1);
}

/*
 * Constructs request
 * GET /<filename> HTTP/1.1
 */
void construct_request(char *request, char *filename)
{
    strcat(request, "GET");
    strcat(request, " ");
    strcat(request, "/");
    strcat(request, filename);
    strcat(request, " ");
    strcat(request, "HTTP/1.1");
}

/*
 * Function that accepts response string and returns the status code. e.g. 200 for 200 OK
 */
char *get_status_code(char *response)
{
  //printf("stc1");
  char *response_local_copy = (char *)malloc(BUFFERSIZE*sizeof(char));
  char *token = NULL;
  char *subtoken = NULL;
  //printf("stc2");
  strcpy(response_local_copy, response);
  //Parsing response to fetch the status code
  token = strtok(response_local_copy, "\r\n");
  subtoken = strtok(token, " ");
  subtoken = strtok(NULL, " ");
  //printf("stc3: token: %s", subtoken);
  //free(response_local_copy);

  return subtoken;
}

/*
 * Function tha writes the file contents fetched from server in the response, to a file on the client side.
 */
void write_response_to_file(char *response, char *filename)
{
   //printf("\n1");
   FILE *filep;

   filep = fopen(filename, "w");
   //printf("\n2");
   fseek(filep, 0L, SEEK_SET);
   fwrite(response+37, strlen(response), 1, filep);
   //printf("\n3");
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
 * Core client function that sends and receives data to server on udp
 * Accepts hostname, portnumber and filename as params
 */
void run_client(char *host, char *portnum, char *filename)
{
    //printf("Inside run client");
    int server_sfd, client_sfd, port, n, lnt;
    socklen_t clen;
    struct sockaddr_in client_addr, server_addr;
    struct hostent *server;

    char *buffer = (char *)malloc(BUFFERSIZE*sizeof(char));

    port = atoi(portnum);

    client_sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_sfd < 0)
    {
        report_error("ERROR opening socket", &server_sfd, &client_sfd);
    }
    //printf("Client sfd defined");
    server = gethostbyname(host);

    if (server == NULL)
    {
        fprintf(stderr,"ERROR, Host not found\n", &server_sfd, &client_sfd);
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

    construct_request(request, filename);
    printf("\nRequest: %s\n", request);
    gettimeofday(&start_timev,NULL);
    n = sendto(client_sfd, request, strlen(request), 0, (struct sockaddr *) &server_addr, clen);
    if (n < 0)
    {
      report_error("ERROR writing to socket", &server_sfd, &client_sfd);
    }

    bzero(buffer, BUFFERSIZE);

    n = recvfrom(client_sfd, buffer, BUFFERSIZE, 0, (struct sockaddr *) &server_addr, &clen);
    if (n < 0)
    {
      report_error("ERROR reading from socket", &server_sfd, &client_sfd);
    }
    gettimeofday(&end_timev,NULL);

    printf("\nNumber of bytes received: %d\n", n);

    printf("Response:-\n%s\n",buffer);
    //printf("return code:-\n%s\n",get_status_code(buffer));

    if( strcmp(get_status_code(buffer), "200" ) == 0 )
    {

        //printf("Inside st c block");
        //Write to and display from file only if status 200
        write_response_to_file(buffer, filename);
        display_file_contents(filename);
    }

    printf("\nRequest sent at: %ld seconds and %ld microseconds\n", start_timev.tv_sec, start_timev.tv_usec);
    printf("\nLast response received at: %ld seconds and %ld microseconds\n", end_timev.tv_sec, end_timev.tv_usec);

    free(request);

    close(client_sfd);
    close(server_sfd);
}

/*
 * Main function
 */
void main(int argc, char *argv[])
{
  if (argc < 4)
  {
     fprintf(stderr,"usage %s <HOST> <PORT> <FILENAME>\n", argv[0]);
     exit(0);
   }

  run_client(argv[1], argv[2], argv[3]);
}
