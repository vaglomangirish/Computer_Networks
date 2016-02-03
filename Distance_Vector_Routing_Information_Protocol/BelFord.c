#include <stdio.h>
#define MAX_ROUTES 10

typedef struct aaa
{
    char *host_addr;     /* can be an IP or hostname */
} NodeAddr;

typedef struct bbb
{
    NodeAddr Destination; /* address of the destination  */
    NodeAddr Nexthop;     /* address of the next hop */
    int Cost;             /* distance metric */
    short TTL;          /* time to live in seconds */
} Route_entry;

Route_entry RoutingTable[MAX_ROUTES];

void run_bellman_ford()
{
    printf("\nInside Belford");
}

void main()
{
    printf("\nWelcome to BelFord");
}