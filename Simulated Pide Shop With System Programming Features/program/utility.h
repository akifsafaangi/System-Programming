typedef struct {
    int pid;
    int numberOfCustomers;
    int p;
    int q;
} client_information;

typedef struct {
    char msg[1024];
} client_server_message;