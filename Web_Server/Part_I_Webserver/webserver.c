// TCP Server handling persistent/ non-persistent connections
// Author: Mangirish Wagle

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#define BUFFERSIZE 1024*1024*2    //Setting sufficient buffer size to handle large files

struct timeval req_recv, start_timev, end_timev;   //Structs to use for gettimeoftheday

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
 * Function to generate response for 404 Not Found
 */
void genresp_not_found(char *response)
{
  strcat(response, "HTTP/1.1 404 Not Found\r\n");
  strcat(response, "Server: mangolap\r\n");
  strcat(response, "\r\n");
  strcat(response, "<html><h1>404: File Not Found</h1></html>\r\n");
}

/*
 * Function to generate response for 400 Bad Request
 */
void genresp_bad_req(char *response)
{
  strcat(response, "HTTP/1.1 400 Bad Request\r\n");
  strcat(response, "Server: mangolap\r\n");
  strcat(response, "\r\n");
  strcat(response, "<html><h1>400: Bad Request</h1></html>\r\n");
}

/*
 * Function to generate response for 500 Internal Error
 */
void genresp_internal_server_error(char *response)
{
  strcat(response, "HTTP/1.1 500 Internal Server Error\r\n");
  strcat(response, "Server: mangolap\r\n");
  strcat(response, "\r\n");
  strcat(response, "<html><h1>500 Internal Server Error</h1></html>\r\n");
}

/*
 * Function to generate response for 200 OK
 */
void genresp_success(char *response)
{  
  strcat(response, "HTTP/1.1 200 OK\r\n");
  strcat(response, "Server: mangolap\r\n");
  strcat(response, "\r\n");
  strcat(response, "<html><h1>Hello<h1></html>");
}

/* Function to validate request.
 * Retuns 0 for valid request, 1 for invalid request
 * Checks for HTTP Method = GET, and the Protocol = HTTP/1.1
 * Can be extended futher to include more methods and request headers etc.
 */
int validate_request(char *request, char *response)
{
  char *localrequestcopy = (char *)malloc(BUFFERSIZE*sizeof(char));
  strcpy(localrequestcopy, request);

  printf("Validating Request...");
  char *token = NULL;
  char *subtoken = NULL;

  //First line of request
  token = strtok(localrequestcopy, "\r\n");
  //printf("\nfirst line: %s\n", token);

  //Method
  subtoken = strtok(token, " ");
  //printf("method subtoken = %s", subtoken);

  if ( strcmp(subtoken, "GET") != 0 )
  {
     printf("\nHTTP Method Invalid: %s\n", subtoken);
     genresp_bad_req(response);
     return 1;
  }

  //File
  subtoken = strtok(NULL, " ");
  //printf("\nFile name: %s\n", subtoken);

  //Protocol
  subtoken = strtok(NULL, " ");
  //printf("\nProtocol: %s\n", subtoken);
  if ( strcmp(subtoken, "HTTP/1.1") != 0 )
  {
     printf("\nProtocol Invalid: %s\n", subtoken);
     genresp_bad_req(response);
     return 1;
  }

  free(localrequestcopy);

  return 0;
}

/*
 * Function that generates response 200 OK with the requested file contents in the response body
 */
void get_file_contents(char *filename, char *response)
{
  FILE *filep;

  filep = fopen(filename,"r");
  
  if(filep == NULL)
  {
    printf("\n%s file does not exists\n", filename);
    genresp_not_found(response);
    return;
  }
  
  strcat(response, "HTTP/1.1 200 OK\r\n");
  strcat(response, "Server: mangolap\r\n");
  strcat(response, "\r\n");

  //File content fetching starts here
  int size;
  char *filecontent = (char *)malloc(BUFFERSIZE*sizeof(char));

  memset(filecontent, 0, BUFFERSIZE);  

  fseek(filep, 0L, SEEK_END);
  size = ftell(filep);
  fseek(filep, 0L, SEEK_SET);
  fread(filecontent, size, 1, filep);
  fclose(filep);

  //Appending file contents to response
  strcat(response, filecontent);

  free(filecontent);
}

/*
 * Function that parses and fetches the file name from the request.
 * The file name is being passed in the initial line like-  GET /<filename> HTTP/1.1
 */
char *get_file_from_req(char *request)
{
  char *localrequestcopy = (char *)malloc(BUFFERSIZE*sizeof(char));

  strcpy(localrequestcopy, request);

  char *token = NULL;
  char *subtoken = NULL;

  //First line of request
  token = strtok(localrequestcopy, "\r\n");
  //printf("token = %s", token);
  
  token = strtok(token, "?");
  
  //Method
  subtoken = strtok(token, " ");
  //printf("subtoken = %s", subtoken);
  
  //File
  subtoken = strtok(NULL, " ");
  //printf("subtoken = %s", subtoken+1);
  
  return subtoken+1;  //Avoiding the preceding fwd slash by +1
}

/*
 * Function that would return a flag to indicate a connection close based on Connection header in the request.
 * This has been implemented to handle Persistent connections.
 */
int is_req_connection_close(char *request)
{
    //printf("In is_req_conn");
    //To check if client has sent a connection close.
    char *point = strstr(request, "Connection: close");
    //printf("ATTENTION: IS_REQ_CON_CLOSE:- %s", point);

    //To check if the request is sent
    char *from = strstr(request, "From: mango@xxx.com");

    //Close connection if request is originating from the programmed client
    //FIXME:- This has been done as a workaround to temporarily fix the issue of browser request to server hanging
    //        indefinitely and waiting for connection to be dropped from server, for persistent connection.
    if ( ( from == NULL ) || ( point != NULL) )
    {
      //Connection Close
      return 1;
    }
    else
    {
      //Connection not close
      return 0;
    }
}

/*
 * Function that initializes the socket descriptors, handles binding, listening and starts server on the
 * specified port.
 */
void server_run(char *portno)
{
  int len;
  socklen_t clen;
  struct sockaddr_in serv_address, cli_address;

    int server_sfd, client_sfd, port;
    server_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_sfd < 0)
      report_error("\nERR: Socket opening error\n", &server_sfd, &client_sfd);

    //TODO: If the port is already occupied, should be reused?
    //FIXME: Server start gives binding error if the port was used earlier in the session.

    port = atoi(portno);

    bzero((char *) &serv_address, sizeof(serv_address));
    serv_address.sin_family = AF_UNSPEC;
    serv_address.sin_addr.s_addr = INADDR_ANY;
    serv_address.sin_port = htons(port);

    if (bind(server_sfd, (struct sockaddr *) &serv_address,
              sizeof(serv_address)) < 0)
    {
      report_error("\nERR: binding unsuccessful\n", &server_sfd, &client_sfd);
    }

    char y=1;

    if( listen(server_sfd,5) < 0 )
    {
      report_error("\nERR: Listening to server socket failed\n", &server_sfd, &client_sfd);
    }
    printf("\nServer started. Listening for connections on port %s...\n", portno);

  while(1)      // Looping to continuously accept connections.
  {
    char *buffer = (char *)malloc(BUFFERSIZE*sizeof(char));

    clen = sizeof(cli_address);

    client_sfd = accept(server_sfd, (struct sockaddr *) &cli_address, &clen);  //TCP connections happens here
    gettimeofday(&req_recv,NULL);

    if (client_sfd < 0)
    {
      report_error("\nERR: client connection accept failed", &server_sfd, &client_sfd);
    }

    printf("\nClient Connected...\n");

    if( fork() == 0 )   //Handling the connection on a separate forked proc.
    {
      char *response = (char *)malloc(BUFFERSIZE*sizeof(char));

      int x = 0;
      while( x != 1 )  //Looping to serve multiple request under the same connection for persistent connections.
      {
        bzero(buffer, BUFFERSIZE);
        len = read(client_sfd ,buffer, BUFFERSIZE-1);

        if (len < 0)
        {
          report_error("\nERR: Socket could not be read\n", &server_sfd, &client_sfd);
        }
        char *localrequestcopy = (char *)malloc(BUFFERSIZE*sizeof(char));
        bzero(localrequestcopy, BUFFERSIZE);
        strcpy(localrequestcopy, buffer);

        printf("\nRequest:\n%s\n",buffer);

        //httpreq *reqstruct = parse_request(buffer);

        memset(response, 0, BUFFERSIZE);
        //printf("\n1");
        if (validate_request(buffer, response) != 0)
        {
          genresp_not_found(response);
          //printf("\n2");
        }
        get_file_contents(get_file_from_req(buffer), response);
        //printf("\n3");
        gettimeofday(&start_timev,NULL);
        //printf("\n4");
        len = write(client_sfd, response, strlen(response));
        //printf("\n5");
        gettimeofday(&end_timev,NULL);
        //printf("\n6");

        if (len < 0)
        {
          report_error("\nERR: Socket could not be written\n", &server_sfd, &client_sfd);
        }
        printf("\nClient Request received at: %ld seconds and %ld micro-seconds\n", req_recv.tv_sec, req_recv.tv_usec);
        printf("\nTransmission started at: %ld seconds and %ld micro-seconds\n", start_timev.tv_sec, start_timev.tv_usec);
        printf("\nLast byte was transmitted at: %ld seconds and %ld micro-seconds\n", end_timev.tv_sec, end_timev.tv_usec);

        char *point = strstr(localrequestcopy, "Connection: close");
        //printf("localcopy: %s", localrequestcopy);

        x = is_req_connection_close(localrequestcopy);

        //printf("\nx = %d", x);
        free(localrequestcopy);
      }
      free(response);

      shutdown(client_sfd, 2);
      close(client_sfd);
      printf("\nClient connection closed\n");
      printf("\n########################################################################################\n");
      exit(0);
    }
    free(buffer);
    //close(server_sfd);
  }
}

/*
 * Main function
 */
void main(int argc, char *argv[])
{  
  if(argc < 1)
  {
    fprintf(stderr, "\nERR: Usage: %s <PORT>", argv[0]);
    exit(1);
  }

  char *port = argv[1];

  server_run(port);
}
