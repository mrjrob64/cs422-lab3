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

#define FALSE 0
#define TRUE 1

#define DECIMAL_NUM 10

#define LISTENING_BACKLOG 50

#define SUCCESS 0
#define INCORRECT_CMD_ARGS 1
#define FAILED_TO_CLOSE_FD 2
#define FAILED_TO_READ_FD 3
#define SOCKET_ISSUE 5

#define EXPECTED_ARGS 2

#define IP_ARG 1
#define PORT_ARG 2

#define HOST_MAX_LEN 1024

#define BUFFER_RW_SIZE 1024

#define DELIMITER '\n'

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

        //1 additional char for end string '\0'
        *line = realloc(*line, (*curr_len_line + 1) * sizeof(char));

    }
}


int usage(char * message)
{
    printf("Expected ./client <ip> <port>\n%s\n", message);
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
    return 0;

}

int main(int argc, char ** argv)
{
	
	int quit = 0;
	int num_args = argc - 1;
	if(num_args != EXPECTED_ARGS)
	{
		return usage("You can only have 2 argument");
	}

	int sfd = socket(AF_INET, SOCK_STREAM, 0);

	//check if valid socket file descriptor
	if(sfd == -1)
	{
		printf("Error Creating Socket: %s\n", strerror(errno));
		return SOCKET_ISSUE;
	}

	char * server_ip = argv[IP_ARG];
	
    int port;
	if(!string_to_int(&port, argv[PORT_ARG]))
	{
		return usage("you did not give a number for the port");
	}
	
    struct sockaddr_in addr;
    //clear struct
    memset(&addr, 0, sizeof(struct sockaddr_in));
    //AF_INET domain address
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(inet_aton(server_ip, &addr.sin_addr) == -1)
	{
		printf("Error Setting IP: %s\n", strerror(errno));
		return SOCKET_ISSUE;
	}

	if(connect(sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in)) == -1)
	{
		printf("Error Connecting: %s\n", strerror(errno));
		return SOCKET_ISSUE;
	}
	
    print_host_network_info();

    char buf [BUFFER_RW_SIZE];
    memset(buf, 0, BUFFER_RW_SIZE);

    char * line = NULL;
    int line_index = 0;
    int curr_len_line = 0;
    struct btree *root = NULL;

    ssize_t bytes_read;
    int cont = 1;
    //Add functionality from buffer to string
    while(cont)
    {
        while((bytes_read = read(sfd, buf, BUFFER_RW_SIZE)) < 0);
        //failed to read bytes so trying again
        if(bytes_read == -1)
        {
            //if the error is an interruption - try again
            if(errno == EINTR)
            {
                continue;
            }

            //otherwise we want to safely end program
            printf("Client can't continue reading: %s\n", strerror(errno));
            if(line)
            {
                free(line);
            }
            free_tree(root);
            close(sfd);
            return SOCKET_ISSUE;
        }
        int index;
        int last_index = 0;

        //each time we find the next delim, we start where we left off by adding last_index
        while ((index = position_delim(buf + last_index, BUFFER_RW_SIZE - last_index, DELIMITER)) != -1) {


            //index does not start from beggining every time
            //so the indexes need to be accumulated
            index += last_index;

            get_mem_for_line(&line, &line_index, &curr_len_line, index - last_index + 1);

            line[curr_len_line] = '\0';

            // Copy the message fragment that ends at the delimiter.
            memcpy(line + line_index, buf + last_index, index - last_index + 1);

            if(strcmp(line, "EOF\n") == 0)
            {
                free(line);
                line = NULL;
                cont = 0;
                break;
            }

            //printf("%s\n", line);

            int line_num;
            if(sscanf(line, "%d", &line_num) == 1)
            {
                //add the line to the tree data structure
                root = add(root, line_num, line, curr_len_line);
                line = NULL;
                
            }
            else
            {
                //badly formatted input
                printf("received badly formatted line (skipping): %s\n", line);
                free(line);
                line = NULL;
            }

            // Reset for a new message.
            line_index = 0;

            //move past the delim
            last_index = index + 1;

            curr_len_line = 0;

        }
        

        if (cont && last_index < bytes_read) {
            int buffer_size;
        
            // search only the valid remainder, not the full BUFFER_RW_SIZE
            index = position_delim(buf + last_index,
                                   bytes_read - last_index,
                                   '\0');
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
                get_mem_for_line(&line,
                                 &line_index,
                                 &curr_len_line,
                                 copy_len);
                // copy just those bytes
                memcpy(line + line_index,
                       buf + last_index,
                       copy_len);
                line_index += copy_len;
            }
        }
        
        //set the buf back to '\0' chars
        memset(buf, 0, BUFFER_RW_SIZE);
    }


    printf("read all lines!\n");

    //write sorted lines back to server
    while(root != NULL)
    {
        //get lowest line number node
        struct btree * min_node = find_min(root);

        //write the string in sizes of BUFFER_RW_SIZE until reached end
        line = min_node->line;
        curr_len_line = min_node->line_length;
        line_index = 0;
        while(line_index < curr_len_line)
        {
            if(line_index - curr_len_line < BUFFER_RW_SIZE)
            {
                write(sfd, line + line_index, curr_len_line);
            }
            else
            {
                write(sfd, line + line_index, BUFFER_RW_SIZE);
            }
            
            line_index += BUFFER_RW_SIZE;
        }

        //delete and free lowest line number node
        root = delete_node(root, min_node);
    }

    char * end_message = "EOF\n";
    if(write(sfd, end_message, strlen(end_message)) <= 0)
    {
        printf("WEIRD WRITE STUFF\n");
    }

	if(close(sfd) == -1)
	{
		printf("Failed to close: %s\n", strerror(errno));
		return FAILED_TO_CLOSE_FD;
	}

	return 0;
}

