#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>     // socket, bind, listen, accept, sockaddr, socklen_t
#include <netinet/ip.h>     // sockaddr_in, htons
#include <arpa/inet.h>      // INADDR_ANY
#include <sys/epoll.h>      // epoll_wait
#include <netdb.h>
#include <sys/types.h>
#include <fcntl.h>

#define FALSE 0
#define TRUE 1

//main function return values
#define SUCCESS 0
#define INCORRECT_CMD_ARGS 1
#define CMD_LINE_FILE_DNE 2
#define NO_ORIGINAL_FILE 3
#define BAD_FRAGMENT 4
#define SOCKET_ISSUE 5
#define EPOLL_ISSUE 6
#define ERROR_READING_FILE 7
#define ERROR_GETTING_ADDRESS_INFO 8
#define ERROR_EPOLL_SETUP 9

#define EXPECTED_ARGS 2

//argv index of arguments
#define FILE_ARG 1
#define PORT_ARG 2

#define DECIMAL_NUM 10

//constants for socket functions
#define LISTENING_BACKLOG 1024
#define HOST_MAX_LEN 1024

#define BUFFER_RW_SIZE 1024

#define DELIMITER '\n'

//holds info about client
//used for knowing 
struct buff_info
{
    int cfd;
    int line_index;
    int curr_len_line;
    int file_closed;
    int done_reading;
    char * line;
    int client_index;
};

//Balanced AVL Tree created partly by me and partly by chatgpt

// AVL-balanced binary tree node. 'line' is caller-allocated; tree takes ownership.
struct btree {
    struct btree *left;
    struct btree *right;
    int line_num;
    char *line;
    int line_length;
    int height;
};

// Get height of a node (NULL -> 0)
static int height(struct btree *n) {
    return n ? n->height : 0;
}

// Return max of two ints
static int max(int a, int b) {
    return (a > b) ? a : b;
}

// Right rotate subtree rooted at y
static struct btree *right_rotate(struct btree *y) {
    struct btree *x = y->left;
    struct btree *T2 = x->right;

    x->right = y;
    y->left  = T2;

    // Update heights
    y->height = max(height(y->left), height(y->right)) + 1;
    x->height = max(height(x->left), height(x->right)) + 1;

    return x;
}

// Left rotate subtree rooted at x
static struct btree *left_rotate(struct btree *x) {
    struct btree *y = x->right;
    struct btree *T2 = y->left;

    y->left  = x;
    x->right = T2;

    // Update heights
    x->height = max(height(x->left), height(x->right)) + 1;
    y->height = max(height(y->left), height(y->right)) + 1;

    return y;
}

// Compute balance factor of n: left height - right height
static int get_balance(struct btree *n) {
    return n ? height(n->left) - height(n->right) : 0;
}

// Rebalance node if unbalanced, using child's balance
static struct btree *rebalance(struct btree *node) {
    int balance = get_balance(node);
    
    // Left heavy
    if (balance > 1) {
        if (get_balance(node->left) >= 0) {
            // LL case
            return right_rotate(node);
        } else {
            // LR case
            node->left = left_rotate(node->left);
            return right_rotate(node);
        }
    }
    // Right heavy
    if (balance < -1) {
        if (get_balance(node->right) <= 0) {
            // RR case
            return left_rotate(node);
        } else {
            // RL case
            node->right = right_rotate(node->right);
            return left_rotate(node);
        }
    }
    return node;
}

// Create new node, taking ownership of 'line'
struct btree *new_node(int line_num, char *line, int line_length) {
    struct btree *n = malloc(sizeof(*n));
    if (!n) return NULL;
    n->line_num = line_num;
    n->line     = line;
    n->left = n->right = NULL;
    n->line_length = line_length;
    n->height = 1;
    return n;
}

// Insert a node, rebalance along the way
struct btree *add(struct btree *root, int line_num, char *line, int line_length) {
    if (!root)
        return new_node(line_num, line, line_length);

    if (line_num < root->line_num) {
        root->left  = add(root->left,  line_num, line, line_length);
    } else if (line_num > root->line_num) {
        root->right = add(root->right, line_num, line, line_length);
    } else {
        printf("Duplicate line number (%d) given. Skipping this node\n", line_num);
        free(line);
        return root;
    }

    // update height
    root->height = 1 + max(height(root->left), height(root->right));
    // rebalance
    return rebalance(root);
}

// Find node with minimum key
struct btree *find_min(struct btree *root) {
    while (root && root->left)
        root = root->left;
    return root;
}

// Delete node by key, rebalance, free line and node
struct btree *delete_node(struct btree *root, struct btree *node) {
    if (!root || !node) return root;

    if (node->line_num < root->line_num) {
        root->left = delete_node(root->left, node);
    }
    else if (node->line_num > root->line_num) {
        root->right = delete_node(root->right, node);
    }
    else {
        if (!root->left || !root->right) {
            struct btree *temp = root->left ? root->left : root->right;
            free(root->line);
            free(root);
            return temp;
        }
        else {
            struct btree *succ = find_min(root->right);
            if (root->line) {
                free(root->line);
                // No need to set root->line = NULL yet, it's about to be overwritten
            }

            // 2. Copy the inorder successor's data to this node
            root->line_num    = succ->line_num;
            root->line        = succ->line;        // Take ownership of successor's line pointer
            root->line_length = succ->line_length; // Copy length too

            // 3. IMPORTANT: Nullify the original successor's line pointer.
            //    This prevents the recursive delete call below from freeing
            //    the memory that 'root' now points to.
            succ->line        = NULL;
            succ->line_length = 0; // Also reset length for consistency

            root->right = delete_node(root->right, succ);
        }
    }

    root->height = 1 + max(height(root->left), height(root->right));
    return rebalance(root);
}

//if there is an error we want to free the tree
void free_tree(struct btree * root)
{
    if(root == NULL)
    {
        return;
    }
    free_tree(root->left);
    free_tree(root->right);
    if(root->line)
    {
        free(root->line);
    }
    free(root);
}

//get position of delim for messages
int position_delim(char * str, int len, char delim)
{
	for(int i = 0; i < len; i++)
	{
		if(str[i] == delim)
		{
			return i;
		}
	}
	return -1;
}

//for writing to a string with a current length 
//and an index where writing will happen (line_index)
void get_mem_for_line(char ** line, int * line_index, int * curr_len_line, int amount)
{
    //case 1: line has nothing in it rn
    if(*line == NULL)
    {
        *line_index = 0;
        *curr_len_line = amount;

        //1 additional char for end string '\0'
        *line = malloc((*curr_len_line + 1) * sizeof(char));
        
    }
    //case 2: line has something in it already
    else
    {
        *line_index = *curr_len_line;
        *curr_len_line += amount;

        //should already have the additional char for end string '\0'
        *line = realloc(*line, (*curr_len_line + 1) * sizeof(char));

    }
}

//close up to n fragments
void close_fragments(int n, int * fragment_files)
{
    for(int i = 0; i < n; i++)
    {
        close(fragment_files[i]);
    }
    free(fragment_files);
}

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

//Generated by chat
void print_host_network_info()
{
	char hostname[HOST_MAX_LEN];
	memset(hostname, 0, HOST_MAX_LEN);

    //make sure last char is '\0'
	gethostname(hostname, HOST_MAX_LEN - 1);

    struct addrinfo hints, *res, *p;
    int status;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP

    if ((status = getaddrinfo(hostname, NULL, &hints, &res)) != 0) {
        printf("getaddrinfo: %s\n", gai_strerror(status));
        return;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        char ipstr[INET_ADDRSTRLEN];
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
        void *addr = &(ipv4->sin_addr);

        inet_ntop(p->ai_family, addr, ipstr, sizeof(ipstr));
        printf("Hostname: %s\n", hostname);
        printf("IP Address: %s\n", ipstr);
    }

    freeaddrinfo(res);

}

//Generated by chat
void print_socket_details(int sockfd) {
    // We'll use a sockaddr_storage to support both IPv4 and IPv6.
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);

    // Retrieve the local socket address.
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) != 0) {
        printf("Error getting peer sock info: %s\n", strerror(errno));
        return;
    }

    char ipstr[INET6_ADDRSTRLEN];
    char service[NI_MAXSERV];
    char hostname[NI_MAXHOST];

    // Use getnameinfo to convert the socket address into a human-readable format.
    // NI_NUMERICHOST flag ensures we get the numerical IP address,
    // NI_NUMERICSERV flag ensures we get the port number as a string.
    int flags = NI_NUMERICHOST | NI_NUMERICSERV;
    int res = getnameinfo((struct sockaddr *)&addr, addr_len,
                          hostname, sizeof(hostname),
                          service, sizeof(service),
                          flags);
    if (res != 0) {
        printf("getnameinfo: %s\n", gai_strerror(res));
        return;
    }

    // Alternatively, we can also extract the IP string manually:
    if (addr.ss_family == AF_INET) {  // IPv4
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        inet_ntop(AF_INET, &(s->sin_addr), ipstr, sizeof(ipstr));
    } else if (addr.ss_family == AF_INET6) {  // IPv6
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
        inet_ntop(AF_INET6, &(s->sin6_addr), ipstr, sizeof(ipstr));
    } else {
        strncpy(ipstr, "Unknown AF", sizeof(ipstr));
    }

    // Convert the port string from getnameinfo to an integer if needed.
    int port = atoi(service);

    // Print the details.
    printf("IP Address: %s\n", ipstr);
    printf("Port: %d\n", port);

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
    printf("PORT: %d\n", port);

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
        fclose(file_cmd_input);
        return NO_ORIGINAL_FILE;
    }

    //turn end of line char '\n' into '\0'
    line[nread - 1] = '\0';

    FILE * file_original = fopen(line, "r");

    int * fragment_files = NULL;
    int index = 0;

    while ((nread = getline(&line, &size, file_cmd_input)) != -1) {
        line[nread - 1] = '\0';
        fragment_files = (int *) realloc(fragment_files, sizeof(int) * (index + 1));
        fragment_files[index] = open(line, O_RDONLY);
        
        if(fragment_files[index] == -1)
        {
            printf("Fragment[%d] did not open\nFile name given: %s\n", index, line);

            //file was not correctly given: we should close all opened files
            free(line);
            fclose(file_cmd_input);
            close_fragments(index, fragment_files);
            return BAD_FRAGMENT;
        }

        index++;
    }

    int num_fragment_files = index;

    //we are done looking through the cmd line file
    free(line);
    fclose(file_cmd_input);

    line = NULL;

    print_host_network_info();

    //SOCKET TIME YO!

    int sfd = socket(AF_INET, SOCK_STREAM, 0);

    //check if valid socket file descriptor
    if(sfd == -1)
    {
        
        printf("Error Creating Socket: %s\n", strerror(errno));

        //if there is a problem with socket, we still need to close the fragments
        close_fragments(index, fragment_files);
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
        close_fragments(index, fragment_files);
        close(sfd);
        return SOCKET_ISSUE;
    }


    if(listen(sfd, LISTENING_BACKLOG) == -1)
    {
        printf("Error Setting up Socket to Listen: %s\n", strerror(errno));
        close_fragments(index, fragment_files);
        close(sfd);
        return SOCKET_ISSUE;
    }

    //Going to use epoll to wait for clients
    int epfd = epoll_create1(0);

	struct epoll_event * evlist = (struct epoll_event *) malloc(sizeof(struct epoll_event) * (num_fragment_files + 1));

	//add event listener for stdin
	struct epoll_event ev_server;
	ev_server.events = EPOLLIN;

    ev_server.data.ptr = malloc(sizeof(struct buff_info));

    struct epoll_event ev;

    struct buff_info * sb = (struct buff_info *) ev_server.data.ptr;
    sb->cfd = sfd;
    sb->client_index = -1;

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev_server) == -1)
	{
		printf("Error Setting up epoll STDIN: %s\n", strerror(errno));
        close_fragments(index, fragment_files);
        close(sfd);
		return ERROR_EPOLL_SETUP;
	}

    int file_index = 0;
    //int cont = 1;
    int cfd;
    int ret_val;
    int num_clients_done = 0;

    struct btree *root = NULL;

    while(num_clients_done < num_fragment_files)
	{
        int num_events = epoll_wait(epfd, evlist, num_fragment_files + 1, -1);
        printf("num clients %d.\n", num_clients_done);

        for(int i = 0; i < num_events; i++)
        {

            struct buff_info * cb = (struct buff_info *) evlist[i].data.ptr;
            printf("client %d has data\n", cb->client_index);

            int fd = cb->cfd;
			uint32_t events = evlist[i].events;

            //New Connection!
            if ((fd == sfd) && (events & EPOLLIN) && file_index < num_fragment_files) {
				struct sockaddr_in c_addr;
                socklen_t clen = sizeof(struct sockaddr_in);
                cfd = accept(sfd, (struct sockaddr *) &c_addr, &clen );

                if(cfd == -1)
                {
                    //if there is a connection error we will wait for more connections
                    printf("Error Accepting Connection: %s\n", strerror(errno));
                    continue;
                }
                printf("Made new connection\n");

                print_socket_details(cfd);

                ev.events = EPOLLIN | EPOLLRDHUP;
                ev.data.ptr = malloc(sizeof(struct buff_info));

                struct buff_info * cb = (struct buff_info *) ev.data.ptr;
                cb->line_index = 0;
                cb->curr_len_line = 0;
                cb->done_reading = 0;
                cb->file_closed = 0;
                cb->cfd = cfd;
                cb->line = NULL;
                cb->client_index = file_index;

                if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev) == -1) {
                    printf("Error Adding to EPOLL: %s\n", strerror(errno));
                    close_fragments(index, fragment_files);
                    close(sfd);
                    free_tree(root);
                    return EPOLL_ISSUE;
                }
                ev.data.ptr = NULL;

                //send data from current file to client
                int file_to_send_fd = fragment_files[file_index];
                char buffer[BUFFER_RW_SIZE];
                memset(buffer, 0, BUFFER_RW_SIZE);
                ssize_t bytesRead;
                while ((bytesRead = read(file_to_send_fd, buffer, BUFFER_RW_SIZE)) > 0) {

                    printf("Sending file fragment %d to a client\n", file_index);
                    write(cfd, buffer, bytesRead);

                    memset(buffer, 0, bytesRead);
                }

                char * end_message = "EOF\n";
                write(cfd, end_message, strlen(end_message));

                //increment file index to prepare sending next file
                file_index++;
                
			}

            //Receiving Info from client!
            if((fd != sfd) && (events & EPOLLIN))
            {
                int index;
                int last_index = 0;

                char buf[BUFFER_RW_SIZE];
                memset(buf, 0, BUFFER_RW_SIZE);
                ssize_t bytes_read;
                while((bytes_read = read(fd, buf, BUFFER_RW_SIZE)) < 0);

                int skip = 0;

                //each time we find the next delim, we start where we left off by adding last_index
                while ((index = position_delim(buf + last_index, BUFFER_RW_SIZE - last_index, DELIMITER)) != -1) {


                    //index does not start from beggining every time
                    //so the indexes need to be accumulated
                    index += last_index;

                    get_mem_for_line(&cb->line, &cb->line_index, &cb->curr_len_line, index - last_index + 1);

                    cb->line[cb->curr_len_line] = '\0';

                    // Copy the message fragment that ends at the delimiter.
                    memcpy(cb->line + cb->line_index, buf + last_index, index - last_index + 1);

                    

                    if(strcmp(cb->line, "EOF\n") == 0)
                    {
                        cb->done_reading = 1;
                        
                        free(cb->line);
                        cb->line = NULL;
                        last_index = index + 1;

                        skip = 1;
                        break;
                    }

                    int line_num;
                    if(sscanf(cb->line, "%d", &line_num) == 1)
                    {
                        //add the line to the tree data structure
                        root = add(root, line_num, cb->line, cb->curr_len_line);
                        cb->line = NULL;
                        
                    }
                    else
                    {
                        //badly formatted input
                        printf("received badly formatted line (skipping): %s\n", cb->line);
                        free(cb->line);
                        cb->line = NULL;
                    }
                    
                    // Reset for a new message.
                    cb->line_index = 0;
                    cb->curr_len_line = 0;

                    //move past the delim
                    last_index = index + 1;
                }
                

                if (!skip && last_index < bytes_read) {
                    int buffer_size;
                
                    // search only the valid remainder, not the full BUFFER_RW_SIZE
                    index = position_delim(buf + last_index,
                                            bytes_read - last_index,
                                            DELIMITER);
                    if (index == -1) {
                        // no delimiter found in what we read
                        buffer_size = bytes_read;
                    } else {
                        // index is relative to buf+last_index, so add last_index for absolute
                        buffer_size = last_index + index;
                    }
                
                    // only copy if there’s something new
                    if (last_index < buffer_size) {
                        int copy_len = buffer_size - last_index;
                        // grow our line buffer by exactly copy_len bytes
                        get_mem_for_line(&cb->line,
                                            &cb->line_index,
                                            &cb->curr_len_line,
                                            copy_len);
                        // copy just those bytes
                        memcpy(cb->line + cb->line_index,
                                buf + last_index,
                                copy_len);
                        cb->line_index += copy_len;
                    }
                }

                //set the buf back to '\0' chars
                memset(buf, 0, BUFFER_RW_SIZE);
            }

            //Client Disconnecting!
            if((fd != sfd) && (events & EPOLLRDHUP))
            {
                printf("Client disconnected!\n");
				
				cb->file_closed = 1;
                
            }

            if((fd != sfd) && cb->file_closed && cb->done_reading)
            {
                //ev.events = EPOLLIN | EPOLLRDHUP;
                if(epoll_ctl(epfd, EPOLL_CTL_DEL, cb->cfd, &evlist[i]) == -1) {
                    printf("Error Adding to EPOLL: %s\n", strerror(errno));
                    close_fragments(index, fragment_files);
                    close(sfd);
                    free_tree(root);
                    return EPOLL_ISSUE;
                }

                close(cb->cfd);
                free(evlist[i].data.ptr);
                
                num_clients_done++;
            }
        }

        

    }

    free(ev_server.data.ptr);
    close(sfd);

    int curr_len_line;
    int line_index;
    //print out recombined file
    while(root != NULL)
    {
        //get lowest line number node
        struct btree * min_node = find_min(root);

        //print the line to the terminal
        printf("%s", min_node->line);

        //delete and free lowest line number node
        root = delete_node(root, min_node);
    }

    //close and free all opened files
    fclose(file_original);
    close_fragments(index, fragment_files);

    //free epoll structure
    free(evlist);

    return SUCCESS;

}