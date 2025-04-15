#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>     // socket, bind, listen, accept, sockaddr, socklen_t
#include <netinet/ip.h>     // sockaddr_in, htons
#include <arpa/inet.h>      // INADDR_ANY
#include <sys/epoll.h>      // epoll_wait


#define FALSE 0
#define TRUE 1

//main function return values
#define SUCCESS 0
#define INCORRECT_CMD_ARGS 1
#define CMD_LINE_FILE_DNE 2
#define NO_ORIGINAL_FILE 3
#define BAD_FRAGMENT 4
#define SOCKET_ISSUE 5

#define EXPECTED_ARGS 2

//argv index of arguments
#define FILE_ARG 1
#define PORT_ARG 2

#define DECIMAL_NUM 10

//constants for socket functions
#define LISTENING_BACKLOG 50
#define HOST_MAX_LEN 1024

int usage(char * message)
{

    printf("Expected ./server <filename> <port>\n%s\n", message);
    return INCORRECT_CMD_ARGS;
}

int string_to_int(int * num, char * str)
{
    char *end;
    *num = strtol(str, &end, DECIMAL_NUM);

    //TODO: maybe make sure num is not too long?

    if(end == str || *end != '\0')
    {
        return FALSE;
    }

    return TRUE;
}

void print_host_network_info()
{
	char name[HOST_MAX_LEN];
	memset(name, 0, HOST_MAX_LEN);

    //make sure last char is '\0'
	gethostname(name, HOST_MAX_LEN - 1);
	printf("server hostname: %s\n", name);

    
}

int main(int argc, char * argv[])
{
    int num_args = argc - 1;

    if(num_args != EXPECTED_ARGS)
    {
        return usage("you used incorrect num args");
    }

    int port;
    if(!string_to_int(&port, argv[PORT_ARG]))
    {
        return usage("the port you specified was not an int");
    }
    printf("PORT: %d", port);

    FILE * file_cmd_input = fopen(argv[FILE_ARG], "r");
    if(file_cmd_input == NULL)
    {
        printf("the file you specified does not exist");
        return CMD_LINE_FILE_DNE;
    }

    //go through lines of file given file
    char *line = NULL;
    size_t size = 0;
    ssize_t nread;

    if((nread = getline(&line, &size, file_cmd_input)) == -1)
    {
        printf("No original file was given\n");
        return NO_ORIGINAL_FILE;
    }

    //turn end of line char '\n' into '\0'
    line[nread - 1] = '\0';

    FILE * file_original = fopen(line, "r");

    FILE ** fragment_files = NULL;
    int index = 0;

    while ((nread = getline(&line, &size, file_cmd_input)) != -1) {
        line[nread - 1] = '\0';
        realloc(fragment_files, sizeof(FILE *) * (index + 1));
        fragment_files[index] = fopen(line, "r");
        
        if(fragment_files[index] == NULL)
        {
            printf("Fragment[%d] did not open\nFile name given: %s\n", index, line);
            return BAD_FRAGMENT;
        }

        index++;
    }

    int num_fragment_files = index;

    //we are done looking through the cmd line file
    free(line);
    fclose(file_cmd_input);

    //SOCKET TIME YO!

    int sfd = socket(AF_INET, SOCK_STREAM, 0);

    //check if valid socket file descriptor
    if(sfd == -1)
    {
        printf("Error Creating Socket: %s\n", strerror(errno));
        return SOCKET_ISSUE;
    }

    struct sockaddr_in addr;
    //clear struct
    memset(&addr, 0, sizeof(struct sockaddr_in));
    //AF_INET domain address
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in)) == -1)
    {
        printf("Error Binding Socket: %s\n", strerror(errno));
        return SOCKET_ISSUE;
    }


    if(listen(sfd, LISTENING_BACKLOG) == -1)
    {
        printf("Error Setting up Socket to Listen: %s\n", strerror(errno));
        return SOCKET_ISSUE;
    }




    //close and free all opened files
    fclose(file_original);
    for(int i = 0; i < num_fragment_files; i++)
    {
        fclose(fragment_files[i]);
        free(fragment_files[i]);
    }

    return SUCCESS;

}