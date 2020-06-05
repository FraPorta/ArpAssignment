#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>
#include <string.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

double calculateToken(double receivedToken, float DT, double RF)
{
    if(receivedToken >= 1)
        receivedToken = -1;
    double newToken;
    newToken = receivedToken + DT * (1 - (receivedToken * receivedToken) / 2 )* (2 * M_PI * RF);
    printf("New Token:%f\n", newToken);
    return newToken;
}


// Returns the current time in microseconds.
long getMicrotime(){
	struct timeval currentTime;
	gettimeofday(&currentTime, NULL);
	return currentTime.tv_sec * (int)1e6 + currentTime.tv_usec;
}

/* 
    argv[0] = program name
    argv[1] = logfile path
    argv[2] = IP Address PREV
    argv[3] = IP Address MY
    argv[4] = IP Address NEXT
    argv[5] = RF (Sine Wave Frequency)
    argv[6] = process_G_path
    argv[7] = portno
    argv[8] = Default Token
    argv[9] = counter
*/

int main(int argc, char *argv[])
{
    // Load data from configuration file
    char *logfile;
    char *ip_address_prev;
    char *ip_address_my;
    char *ip_address_next;
    const char *process_G_path;
    const char *pid_P_path;
    const char *pid_G_path;
    const char *DEFAULT_TOKEN;
    double RF;
    int portno;
    int counter;
    const char *waiting_time;

    logfile = argv[1];
    ip_address_prev = argv[2];
    ip_address_my = argv[3];
    ip_address_next = argv[4];
    RF = atof(argv[5]);
    process_G_path = argv[6];
    portno = atoi(argv[7]);
    DEFAULT_TOKEN = argv[8];
    counter = atoi(argv[9]);
    waiting_time = argv[10];

    if (argc < 11)
    {
        fprintf(stderr, "ERROR, not enough arguments\n");
        exit(-1);
    }

    pid_P_path = "/home/francesco/Desktop/ARP/log/pid_P.txt";
    pid_G_path = "/home/francesco/Desktop/ARP/log/pid_G.txt";

    // Variables declaration
    pid_t pid[3];
    pid_t pid_P;
    pid_t pid_L;
    long startTime, endTime;
    float DT;
    double nextToken;
    const int BSIZE = 50;
    const int ACTBUF = 10;
    const int STRSIZE = 50;
    const int ONELOGSAMPLE = 40;
    FILE *logname;
    char bufferL[BSIZE], bufferS[ACTBUF];
    ssize_t prevToken, action, logoutput, command;
    fd_set rfds_P, rfds_L, rfds_S, wfds_P, wfds_S;
    struct timeval tvP, tvL, tvS;
    int retval;
    int nfds;

    // Server variables
    int sockfd, newsockfd, clilen;
    char server_buf[256];
    struct sockaddr_in serv_addr, cli_addr;

    const int start = 1; // "start"
    const int stop = 2;  // "stop"
    const int log = 3;   // "dump log"

    // Pipes initialization
    int pipe_SP[2];
    int pipe_GP[2];
    int pipe_LP[2];

    if (pipe(pipe_SP) == -1)
    {
        perror("pipe_SP");
        exit(-1);
    }
    if (pipe(pipe_GP) == -1)
    {
        perror("pipe_GP");
        exit(-1);
    }
    if (pipe(pipe_LP) == -1)
    {
        perror("pipe_LP");
        exit(-1);
    }

    printf("Type:\n'1' to start the token flow\n'2' to stop the token flow\n'3' to dump the log file\n\n");

    // First fork
    pid[0] = fork();
    if (pid[0] < 0)
    {
        perror("First fork");
        return -1;
    }
    // Process P
    if (pid[0] > 0)
    {
        int n;
        const int option = 1;
        // Process P code
        printf("Process P starts wit pid: %d\n", getpid());
        pid_P = getpid();

        // Save pid_P on an external file
        FILE *pid_P_file = fopen(pid_P_path, "w");
        if (pid_P_file == NULL)
        {
            perror("File opening");
            exit(-1);
        }
        if (fprintf(pid_P_file, "%i", pid_P) == -1)
        {
            perror("File writing");
            exit(-1);
        }
        fclose(pid_P_file);

        // Begin the infinite loop
        while (1)
        {
            char bufferP[BSIZE];
            char logstring[STRSIZE * 2];

            // Clear the file descriptors set
            FD_ZERO(&rfds_P);
            // Add the read end of the pipes to the file descriptor set
            FD_SET(pipe_GP[0], &rfds_P);
            FD_SET(pipe_SP[0], &rfds_P);

            // close write end of the pipes
            close(pipe_SP[1]);
            close(pipe_GP[1]);

            // Check which file descriptor belonging to the set is bigger
            if (pipe_GP[0] > pipe_SP[0])
                nfds = pipe_GP[0] + 1;
            else
                nfds = pipe_SP[0] + 1;

            // Set the amount of time the select blocks
            tvP.tv_sec = 0;
            tvP.tv_usec = 1000;

            if (counter == 0)
            {
                time_t clk = time(NULL);
                char temp[STRSIZE];

                // Create string to be sent to L (logfile)
                int n = sprintf(logstring, "%s| from G | oldToken: %s\n", ctime(&clk), DEFAULT_TOKEN);
                if (n == -1)
                {
                    perror("String creation");
                    exit(-1);
                }

                // Send updated token via socket
                sockfd = socket(AF_INET, SOCK_STREAM, 0);
                if (sockfd < 0)
                {
                    perror("ERROR opening socket");
                    exit(-1);
                }
                setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
                fflush(stdout);
                bzero((char *)&serv_addr, sizeof(serv_addr));
                serv_addr.sin_family = AF_INET;
                serv_addr.sin_addr.s_addr = INADDR_ANY;
                serv_addr.sin_port = htons(portno);

                if (bind(sockfd, (struct sockaddr *)&serv_addr,
                         sizeof(serv_addr)) < 0)
                {
                    perror("ERROR on binding");
                    exit(-1);
                }
                listen(sockfd, 5);
                clilen = sizeof(cli_addr);

                newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
                if (newsockfd < 0)
                {
                    perror("ERROR on accept");
                    exit(-1);
                }
                bzero(server_buf, 256);

                // Write to the socket the updated token
                n = write(newsockfd, DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN));
                if (n < 0)
                {
                    perror("ERROR writing to socket");
                    exit(-1);
                }
                clk = time(NULL);
                // Complete the string (logstring) to be sent to L with the nextToken line
                int m = sprintf(temp, "%sNewToken: %s\n\n", ctime(&clk), DEFAULT_TOKEN);
                if (m == -1)
                {
                    perror("String creation");
                    exit(-1);
                }
                int o = sprintf(logstring, "%s%s", logstring, temp);
                if (o == -1)
                {
                    perror("String creation");
                    exit(-1);
                }
                counter++;
            }
            else
            {
                retval = select(nfds, &rfds_P, NULL, NULL, &tvP);
                if (retval == -1)
                {
                    perror("select()");
                    exit(-1);
                }
                else if (retval)
                {
                    if (FD_ISSET(pipe_GP[0], &rfds_P))
                    {
                        // Receive token via pipe_GP from process G
                        prevToken = read(pipe_GP[0], bufferP, BSIZE);

                        if (prevToken == -1)
                        {
                            perror("No token from G\n");
                            exit(-1);
                        }
                        startTime = getMicrotime();
                        bufferP[prevToken] = '\0';
                        char temp[STRSIZE];

                        //printf("From G: %s\t", bufferP);

                        // Calculate timestamp
                        time_t clk = time(NULL);

                        // Create string to be sent to L (logfile)
                        int n = sprintf(logstring, "\n%s| from G | oldToken: %s\n", ctime(&clk), bufferP);
                        if (n == -1)
                        {
                            perror("String creation");
                            exit(-1);
                        }
                        // Send updated token via socket
                        sockfd = socket(AF_INET, SOCK_STREAM, 0);
                        if (sockfd < 0)
                        {
                            perror("ERROR opening socket");
                            exit(-1);
                        }
                        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
                        fflush(stdout);
                        bzero((char *)&serv_addr, sizeof(serv_addr));
                        serv_addr.sin_family = AF_INET;
                        serv_addr.sin_addr.s_addr = INADDR_ANY;
                        serv_addr.sin_port = htons(portno);
                        if (bind(sockfd, (struct sockaddr *)&serv_addr,
                                 sizeof(serv_addr)) < 0)
                        {
                            perror("ERROR on binding");
                            exit(-1);
                        }
                        listen(sockfd, 5);
                        clilen = sizeof(cli_addr);

                        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
                        if (newsockfd < 0)
                        {
                            perror("ERROR on accept");
                            exit(-1);
                        }
                        bzero(server_buf, 256);

                        // Calculate time spent before delivering the new token
                        endTime = getMicrotime();

                        //printf("start:%li\nend:%li\n",startTime,endTime);
                        DT = (double)(endTime-startTime)/1000000;

                        // Calculate next Token
                        char tokenBuf[BSIZE];
                        nextToken = calculateToken(atof(bufferP), DT, RF);
                        sprintf(tokenBuf, "%f", nextToken);

                        // Write to the socket the updated token
                        n = write(newsockfd, tokenBuf, BSIZE);
                        if (n < 0)
                        {
                            perror("ERROR writing to socket");
                            exit(-1);
                        }
                        clk = time(NULL);
                        // Complete the string (logstring) to be sent to L with the nextToken line
                        int m = sprintf(temp, "%sNewToken: %f", ctime(&clk), nextToken);
                        if (m == -1)
                        {
                            perror("String creation");
                            exit(-1);
                        }

                        int p = sprintf(logstring, "%s%s\n\n", logstring, temp);
                        if (p == -1)
                        {
                            perror("String creation");
                            exit(-1);
                        }
                    }
                    else if (FD_ISSET(pipe_SP[0], &rfds_P))
                    {
                        // Receive the action from process S
                        action = read(pipe_SP[0], bufferP, BSIZE);
                        bufferP[action] = '\0';
                        time_t clk = time(NULL);

                        // Create string to be sent to L (logfile)
                        if (sprintf(logstring, "%s| from S | %s\n", ctime(&clk), bufferP) == -1)
                        {
                            perror("String creation");
                            exit(-1);
                        }
                        //printf("%s",logstring);
                    }
                }
            }

            // Initialiaze select for the pipe_LP
            FD_ZERO(&wfds_P);
            FD_SET(pipe_LP[1], &wfds_P);

            // Set the amount of time the select blocks
            tvP.tv_sec = 0;
            tvP.tv_usec = 1000;

            close(pipe_LP[0]);

            retval = select(pipe_LP[1] + 1, NULL, &wfds_P, NULL, &tvP);
            if (retval == -1)
            {
                perror("select()");
                exit(-1);
            }
            else if (retval)
            {
                if (FD_ISSET(pipe_LP[1], &wfds_P))
                {
                    //printf("2--->%s",logstring);
                    // Send to L the string to be written on the logfile
                    write(pipe_LP[1], logstring, STRSIZE * 2);
                }
            }
        }
    }
    else if (pid[0] == 0)
    {
        // Second fork
        pid[1] = fork();
        if (pid[1] < 0)
        {
            perror("Second fork");
            exit(-1);
        }

        // Process L
        if (pid[1] > 0)
        {
            // Process L code
            printf("Process L starts wit pid: %d\n", getpid());
            pid_L = getpid();

            // Start the infinite loop
            while (1)
            {
                // Read from pipe_LP and check which is the action performed
                // Initialize the file descriptor set
                FD_ZERO(&rfds_L);
                FD_SET(pipe_LP[0], &rfds_L);

                close(pipe_LP[1]);

                // Set the amount of time the select blocks
                tvL.tv_sec = 0;
                tvL.tv_usec = 1000;

                retval = select(pipe_LP[0] + 1, &rfds_L, NULL, NULL, &tvL);
                if (retval == -1)
                {
                    perror("select()");
                    exit(-1);
                }
                else if (retval)
                {
                    if (FD_ISSET(pipe_LP[0], &rfds_L))
                    {
                        // Open logfile
                        logname = fopen(logfile, "a");
                        if (logname == NULL)
                        {
                            perror("Cannot open log file");
                        }
                        // Read from the pipe the action
                        logoutput = read(pipe_LP[0], bufferL, BSIZE*2);
                        
                        if (logoutput == -1)
                        {
                            perror("Read from pipe_LP");
                            exit(-1);
                        }
                        bufferL[logoutput] = '\0';
                        //printf("%s",bufferL);

                        // Write on log file
                        if (fputs(bufferL, logname) == EOF)
                        {
                            perror("Logfile writing");
                            exit(-1);
                        }
                        fclose(logname);
                    }
                }
            }
        }
        else if (pid[1] == 0)
        {
            // Third fork
            pid[2] = fork();
            if (pid[2] < 0)
            {
                perror("Third fork");
                exit(-1);
            }
            // Process G
            if (pid[2] > 0)
            {
                printf("Process G starts wit pid: %d\n", getpid());
                // Save pid_G on an external file
                FILE *pid_G_file = fopen(pid_G_path, "w");
                if (pid_G_file == NULL)
                {
                    perror("File opening");
                    exit(-1);
                }
                if (fprintf(pid_G_file, "%i", getpid()) == -1)
                {
                    perror("File writing");
                    exit(-1);
                }
                fclose(pid_G_file);

                char *portnum;
                char pipeGP_0[5];
                char pipeGP_1[5];

                // Prepare the variables to be given to the G process
                if (sprintf(pipeGP_0, "%i", pipe_GP[0]) < 0)
                {
                    perror("String creation");
                    exit(-1);
                }
                if (sprintf(pipeGP_1, "%i", pipe_GP[1]) < 0)
                {
                    perror("String creation");
                    exit(-1);
                }

                portnum = argv[7];

                // exec G process passing the necessary arguments
                if (execl(process_G_path, ip_address_my, portnum, pipeGP_0, pipeGP_1,waiting_time, NULL) == -1)
                {
                    perror("Exec failed");
                    exit(-1);
                }
            }
            // Process S
            else
            {
                printf("Process S starts wit pid: %d\n", getpid());

                // Get process P pid
                char strP[5];
                int pid_P_;
                FILE *pid_P_f = fopen(pid_P_path, "r");
                if (pid_P_f == NULL)
                {
                    perror("file opening");
                    exit(-1);
                }
                while (fgets(strP, 10, pid_P_f) == NULL)
                {
                }
                pid_P_ = atoi(strP);
                // Get process G pid
                char strG[5];
                int pid_G_;
                FILE *pid_G_f = fopen(pid_G_path, "r");
                if (pid_G_f == NULL)
                {
                    perror("file opening");
                    exit(-1);
                }
                while (fgets(strG, 10, pid_G_f) == NULL)
                {
                }
                pid_G_ = atoi(strG);

                // Stop the token flow initially
                if (kill(pid_P_, SIGSTOP) == -1)
                {
                    perror("Stop signal failed");
                    exit(-1);
                }
                if (kill(pid_G_, SIGSTOP) == -1)
                {
                    perror("Stop signal failed");
                    exit(-1);
                }
                printf("Token flow stopped.\n");

                // Start the infinite loop
                while (1)
                {
                    // Initialiaze select for the stdin
                    FD_ZERO(&rfds_S);
                    FD_ZERO(&wfds_S);
                    FD_SET(STDIN_FILENO, &rfds_S);
                    FD_SET(pipe_SP[1], &wfds_S);

                    close(pipe_SP[0]);

                    // Set the amount of time the select blocks
                    tvS.tv_sec = 0;
                    tvS.tv_usec = 1000;
                    int ret = select(pipe_SP[1] + 1, &rfds_S, &wfds_S, NULL, NULL);
                    if (ret == -1)
                    {
                        perror("select()");
                        exit(-1);
                    }
                    else if (ret)
                    {
                        // Wait for a Console command: it could be 'start','stop' or 'log'
                        if (FD_ISSET(STDIN_FILENO, &rfds_S))
                        {
                            command = read(STDIN_FILENO, bufferS, ACTBUF);
                            if (command == -1)
                            {
                                perror("Read()");
                                exit(-1);
                            }
                            bufferS[command] = '\0';

                            if (atoi(bufferS) == start)
                            {
                                // start process P -> SIGCONT
                                if (kill(pid_P_, SIGCONT) == -1)
                                {
                                    perror("Start signal failed");
                                    exit(-1);
                                }
                                // start process G -> SIGCONT
                                if (kill(pid_G_, SIGCONT) == -1)
                                {
                                    perror("Start signal failed");
                                    exit(-1);
                                }
                                printf("Token flow started\n");

                                // Send the action to P
                                if (FD_ISSET(pipe_SP[1], &wfds_S))
                                    write(pipe_SP[1], bufferS, ACTBUF);
                            }
                            else if (atoi(bufferS) == stop)
                            {
                                // Send to P the action performed
                                if (FD_ISSET(pipe_SP[1], &wfds_S))
                                    write(pipe_SP[1], bufferS, ACTBUF);

                                // stop process P -> SIGSTOP
                                if (kill(pid_P_, SIGSTOP) == -1)
                                {
                                    perror("Stop signal failed");
                                    exit(-1);
                                }
                                // stop process G -> SIGSTOP
                                if (kill(pid_G_, SIGSTOP) == -1)
                                {
                                    perror("Stop signal failed");
                                    exit(-1);
                                }
                                printf("Token flow stopped\n");
                            }
                            else if (atoi(bufferS) == log)
                            {
                                printf("================================================================\n");
                                printf("Dump log file:\n\n");
                                int dump_size = ONELOGSAMPLE * 20;
                                // Send to P the action performed
                                if (FD_ISSET(pipe_SP[1], &wfds_S))
                                    write(pipe_SP[1], bufferS, ACTBUF);

                                logname = fopen(logfile, "r");
                                if (logname == NULL)
                                {
                                    printf("The logfile has not been created yet, first start the token flow\n");
                                }
                                else
                                {
                                    fseek(logname, 0, SEEK_END);
                                    int size = ftell(logname);
                                    fseek(logname, size - dump_size, SEEK_SET);
                                    if (size >= dump_size)
                                    {
                                        char *buffer_log = malloc(dump_size + 1);
                                        // read  the file and put it in bufferlog
                                        if (fread(buffer_log, 1, dump_size, logname) != dump_size)
                                        {
                                            perror("Reading the file");
                                            exit(-1);
                                        }
                                        fclose(logname);

                                        if (write(STDOUT_FILENO, buffer_log, dump_size) == -1)
                                        {
                                            perror("write() log on stdout");
                                            exit(-1);
                                        }
                                        printf("\n================================================================\n\n");
                                        fflush(stdout);
                                    }
                                    else
                                    {
                                        printf("The log file is almost empty, please first perform some actions.\n");
                                    }
                                }
                            }
                            else
                            {
                                printf("Please write 1,2 or 3\n");
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}


