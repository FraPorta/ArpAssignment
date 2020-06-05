#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/select.h>
#include <strings.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

void error(char *msg)
{
    perror(msg);
    exit(-1);
}

/*
argv[0] = server ip address
argv[1] = port number
argv[2] = pipe_GP[0]
argv[3] = pipe_GP[1]
*/

int main(int argc, char *argv[])
{
    int sockfd;
    int portno;
    int n;
    int waiting_time;

    const int BSIZE = 50;
    char *ip_address;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    fd_set wfds_G, rfds_G;
    int pipe_GP[2];
    struct timeval tvG;
    
    //truct timespec ts;
    char buffer[BSIZE];

    ip_address = argv[0];
    portno = atoi(argv[1]);
    pipe_GP[0] = atoi(argv[2]);
    pipe_GP[1] = atoi(argv[3]);
    waiting_time = atoi(argv[4]);

    if (argc != 5)
    {
        fprintf(stderr, "Not enough input arguments for the process G\n");
        exit(-1);
    }
    //buffer[0] = '0';
    
    while (1)
    {
        // Wait for the server to be initialized
        //nanosleep((const struct timespec[]){{0, 100000000}},NULL);
        nanosleep((const struct timespec[]){{0, waiting_time}},NULL);
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
            error("ERROR opening socket");

        // Get server info using the hostname(ip address)
        server = gethostbyname(ip_address);
        if (server == NULL)
        {
            fprintf(stderr, "ERROR, no such host\n");
            exit(0);
        }

        bzero((char *)&serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        bcopy((char *)server->h_addr_list[0],
              (char *)&serv_addr.sin_addr.s_addr,
              server->h_length);
        serv_addr.sin_port = htons(portno);
        
        while (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        {
        }

        // Initialiaze select for the pipe_GP
        FD_ZERO(&rfds_G);
        FD_SET(sockfd, &rfds_G);

        // Set the amount of time the select blocks
        tvG.tv_sec = 0;
        tvG.tv_usec = 1000;

        int retval = select(sockfd + 1, &rfds_G, NULL, NULL, &tvG);
        if (retval == -1)
        {
            perror("select()");
            exit(-1);
        }
        else if (retval)
        {
            if (FD_ISSET(sockfd, &rfds_G))
            {

                bzero(buffer, BSIZE);
                n = read(sockfd, buffer, BSIZE - 1);
                if (n < 0)
                    error("ERROR reading from socket");
            }

            // Initialiaze select for the pipe_GP
            FD_ZERO(&wfds_G);
            FD_SET(pipe_GP[1], &wfds_G);

            // Set the amount of time the select blocks
            tvG.tv_sec = 0;
            tvG.tv_usec = 1000;

            close(pipe_GP[0]);

            int retval = select(pipe_GP[1] + 1, NULL, &wfds_G, NULL, &tvG);
            if (retval == -1)
            {
                perror("select()");
                exit(-1);
            }
            else if (retval)
            {
                if (FD_ISSET(pipe_GP[1], &wfds_G))
                {
                    //printf("Sending Token\n");
                    // Send to P the token
                    write(pipe_GP[1], buffer, BSIZE);
                }
            }
        }
    }
    return 0;
}
