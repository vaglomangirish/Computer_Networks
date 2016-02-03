// Persistent/ Non-persistent TCP Client program
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

#define BUFFERSIZE 1024*1024*2  //Setting sufficient buffer size to handle large files

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
 * Constructs a Connection: keep-alived request for persistent connections
 *
 * GET /<filename> HTTP/1.1
 * From: mango@xxx.com
 * Connection: keep-alive
 *
 * Adds the client identifier header From: mango@xxx.com
 */
void construct_keep_alived_request(char *request, char *filename)
{
    strcat(request, "GET");
    strcat(request, " ");
    strcat(request, "/");
    strcat(request, filename);
    strcat(request, " ");
    strcat(request, "HTTP/1.1\r\n");
    strcat(request, "From: mango@xxx.com\r\n");
    //NOTE:- Connection: keep-alive is not necesary as HTTP/1.1,
    strcat(request, "Connection: keep-alive\r\n");

}

/*
 * Constructs a Connection: close request.
 *
 * GET /<filename> HTTP/1.1
 * From: mango@xxx.com
 * Connection: close
 *
 * Adds the client identifier header From: mango@xxx.com
 */
void construct_close_request(char *request, char *filename)
{
    strcat(request, "GET");
    strcat(request, " ");
    strcat(request, "/");
    strcat(request, filename);
    strcat(request, " ");
    strcat(request, "HTTP/1.1\r\n");
    strcat(request, "From: mango@xxx.com\r\n");
    //This will tell server to close persistent connection
    strcat(request, "Connection: close\r\n");
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
 * Function that sends a terminating 'Connection: close' request for persistent connections for server to end conn.
 */
void send_close_request(int *server_sfd)
{
    int n;
    char *request = (char *)malloc(BUFFERSIZE*sizeof(char));
    char *buffer = (char *)malloc(BUFFERSIZE*sizeof(char));

    construct_close_request(request, "");

    n = write(*server_sfd, request, strlen(request));
    if (n < 0)
    {
      fprintf(stderr, "\nERR: Write to socket failed\n");
      exit(1);
    }

    bzero(buffer, BUFFERSIZE);

    n = read(*server_sfd, buffer, BUFFERSIZE);
    if (n < 0)
    {
      fprintf(stderr, "\nERR: Read from socket failed\n");
      exit(1);
    }

    //printf("\nConnection Close Response:-\n%s\n",buffer);

    free(request);
    free(buffer);
}

/*
 * Function that handles Persistent Connection
 * Accepts hostname, portnumber and filename as params
 */
void run_p(char *host, char *portnum, char *filename)
{
    FILE *pfile;

    pfile = fopen(filename, "r");

    if(pfile == NULL)
    {
      fprintf(stderr, "\nFile %s cannot be opened\n", filename);
      exit(1);
    }

    int server_sfd, client_sfd, port, n;
    struct sockaddr_in server_addr;
    struct hostent *server;

    char buffer[BUFFERSIZE];

    port = atoi(portnum);

    server_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sfd < 0)
    {
        report_error("ERROR opening socket", &server_sfd, &client_sfd);
    }

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

    //TCP handshake happens here...
    if (connect(server_sfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
    {
        report_error("ERROR connecting to server", &server_sfd, &client_sfd);
    }

    char *request = (char *)malloc(BUFFERSIZE*sizeof(char));

    int x;
    char *fline = NULL;
    size_t len = 0;
    ssize_t readline;

    //Request multiple files within one TCP handshake.
    //Reads file line by line and request for each file from the list.
    gettimeofday(&start_timev,NULL);
    while ((readline = getline(&fline, &len, pfile)) != -1)
    {
        bzero(request, BUFFERSIZE);

        construct_keep_alived_request(request, trim_white_spaces(fline));
        printf("Request: %s", request);
        n = write(server_sfd, request, strlen(request));
        //printf("After printf");
        if (n < 0)
        {
          report_error("ERROR writing to socket", &server_sfd, &client_sfd);
        }

        bzero(buffer, BUFFERSIZE);

        n = read(server_sfd, buffer, BUFFERSIZE);
        if (n < 0)
        {
          report_error("ERROR reading from socket", &server_sfd, &client_sfd);
        }

        printf("\nResponse:-\n%s\n",buffer);

        if( strcmp(get_status_code(buffer), "200" ) == 0 )
        {
            //printf("Inside st c block");
            //Write to and display from file only if status 200
            write_response_to_file(buffer, trim_white_spaces(fline));
            display_file_contents(trim_white_spaces(fline));
        }
    }
    send_close_request(&server_sfd); //Send terminating 'Connection: close' request.

    gettimeofday(&end_timev,NULL);

    printf("\nRequests started at: %ld seconds and %ld microseconds\n", start_timev.tv_sec, start_timev.tv_usec);
    printf("\nLast response received at: %ld seconds and %ld microseconds\n", end_timev.tv_sec, end_timev.tv_usec);

    free(request);

    close(server_sfd);
}

/*
 * Function that handles Non- Persistent Connection
 * Accepts hostname, portnumber and filename as params
 */
void run_np(char *host, char *portnum, char *filename)
{
    int server_sfd, client_sfd, port, n;
    struct sockaddr_in server_addr;
    struct hostent *server;

    char *buffer = (char *)malloc(BUFFERSIZE*sizeof(char));

    port = atoi(portnum);

    server_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sfd < 0)
    {
        report_error("ERROR opening socket", &server_sfd, &client_sfd);
    }

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

    //Establish TCP connection
    if (connect(server_sfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
    {
        report_error("ERROR connecting to server", &server_sfd, &client_sfd);
    }

    char *request = (char *)malloc(BUFFERSIZE*sizeof(char));
    bzero(request, BUFFERSIZE);

    construct_close_request(request, filename);
    printf("\nRequest: %s\n", request);

    //Only one request served per TCP connection
    gettimeofday(&start_timev,NULL);
    n = write(server_sfd, request, strlen(request));
    if (n < 0)
    {
      report_error("ERROR writing to socket", &server_sfd, &client_sfd);
    }

    bzero(buffer, BUFFERSIZE);

    n = read(server_sfd, buffer, BUFFERSIZE);
    if (n < 0)
    {
      report_error("ERROR reading from socket", &server_sfd, &client_sfd);
    }
    gettimeofday(&end_timev,NULL);

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

    //Non- persistent connection closed
    close(server_sfd);
}

/*
 * Main function
 */
void main(int argc, char *argv[])
{
  if (argc < 4)
  {
     fprintf(stderr,"usage %s <HOST> <PORT> <ConnectionType: P/NP> <FILENAME>\n", argv[0]);
     exit(0);
   }

  char *hostname = argv[1];
  char *port = argv[2];
  char *connType = argv[3];
  char *filename = argv[4];

  if( strcmp( connType, "p" ) == 0 )
  {
    run_p(hostname, port, filename);
  }
  else if( strcmp( connType, "np" ) == 0 )
  {
    run_np(hostname, port, filename);
  }
  else
  {
    fprintf(stderr,"\nConnection type invalid. It should be n/ np\n");
    exit(1);
  }
}
