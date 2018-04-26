#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h> 
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define TYPE_QUIT (uint8_t)0x01
#define TYPE_PWD (uint8_t)0x02
#define TYPE_CWD (uint8_t)0x03
#define TYPE_LIST (uint8_t)0x04
#define TYPE_RETR (uint8_t)0x05
#define TYPE_STOR (uint8_t)0x06

#define TYPE_OK (uint8_t)0x10
#define TYPE_CMD_ERR (uint8_t)0x11
#define TYPE_FILE_ERR (uint8_t)0x12
#define TYPE_UNKWN_ERR (uint8_t)0x13

#define TYPE_DATA (uint8_t)0x20

// OK code
#define OK_NO_DATA (uint8_t)0x00
#define OK_DATA_S_C (uint8_t)0x01
#define OK_DATA_C_S (uint8_t)0x02
 
// CMD_ERR code
#define CMD_ERR_SYN_ERR (uint8_t)0x01
#define CMD_ERR_UND_CMD (uint8_t)0x02
#define CMD_ERR_PRO_ERR (uint8_t)0x03

// FILE_ERR code
#define FILE_ERR_NO_EXI (uint8_t)0x00
#define FILE_ERR_NO_AUT (uint8_t)0x01
 
// UNKWN_ERR code
#define UNKWN_ERR_UNKWN_ERR (uint8_t)0x05

// DATA code
#define DATA_END (uint8_t)0x00
#define DATA_CON (uint8_t)0x01
// myftp message
#define DATA_SIZE 1024
#define MESSAGE_SIZE 4
#define MESSAGE_DATA_SIZE 1028

struct myftp_message {
    uint8_t type;
    uint8_t code;
    uint16_t length;
};

struct myftp_message_data {
    uint8_t type;
    uint8_t code;
    uint16_t length;
    char data[DATA_SIZE];
};

int s;                                  // socket
int wait_s;                     // wait socket
struct sockaddr_in serverAddr;          // server
struct sockaddr_in clientAddr;  // client
socklen_t sktlen;

struct service_table {
    uint8_t type;
    void(*func)(struct myftp_message_data *);
};

void print_client(struct sockaddr_in *);
void stor_func(struct myftp_message_data *);
void retr_func(struct myftp_message_data *);

struct service_table ser_tbl[] = {
    { TYPE_RETR, retr_func },
    { TYPE_STOR, stor_func },
    { 0, NULL }
};

#define AV_NUM 4    // end of av is NULL

void init();
void child_process();

void print_message(struct myftp_message *);
void print_message_data(struct myftp_message_data *);
int print_error(struct myftp_message *);
void set_message(struct myftp_message *, uint8_t, uint8_t);
int check_message(struct myftp_message *, uint8_t, uint8_t);
void send_message(struct myftp_message *, int);
void recv_message(struct myftp_message *, int);
void set_message_data(struct myftp_message_data *, uint8_t, uint8_t, uint16_t, char *);
int check_message_data(struct myftp_message_data *, uint8_t, uint8_t);
void send_message_data(struct myftp_message_data *, int);
void recv_message_data(struct myftp_message_data *, int);

int message_data_to_message(struct myftp_message_data *, struct myftp_message *);

int main(int argc, char *argv[]) {
    if (argc < 1 || 2 < argc) {
        fprintf(stderr, "./myftpd [ current directory ]\n");
        exit(1);
    }

    if (argc == 2) chdir(argv[1]);

    init();

    int pid = 1;
    for (;;) {
        if (pid < 0) {
            // error
            perror("fork");
            exit(1);
        }
        else if (pid == 0) {
            // child process
            child_process();
        }
        else {
            // parent process
            if ((s = accept(wait_s, (struct sockaddr *)&clientAddr, &sktlen)) < 0) {
                perror("accept");
                exit(1);
            }
            fprintf(stderr, "--- accept ---\n");
            print_client(&clientAddr);
            fprintf(stderr, "\n");
            pid = fork();
        }
    }

}

void init() {
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(50904);  // 50021
        sktlen = sizeof(clientAddr);

    if ((wait_s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    if (bind(wait_s, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(wait_s, 5) != 0) {
        perror("listen");
        exit(1);
    }
}

void child_process() {
    struct service_table *st;
    struct myftp_message message;
    struct myftp_message_data message_data;

    for (;;) {
        recv_message_data(&message_data, s);

        for (st = ser_tbl; st->type; st++) {
            if (st->type == message_data.type) {
                (st->func)(&message_data);
                break;
            }
        }

        if (st->type == 0) {
            print_client(&clientAddr);
            print_message_data(&message_data);
            set_message(&message, TYPE_CMD_ERR, CMD_ERR_UND_CMD);
    send_message(&message, s);
        }
    }
}

void print_client(struct sockaddr_in *clientAddr) {
    fprintf(stderr, "client %s %d\n", inet_ntoa(clientAddr->sin_addr), ntohs(clientAddr->sin_port    ));
}

void retr_func(struct myftp_message_data *message_data) {
    struct myftp_message message;

    if (check_message_data(message_data, TYPE_RETR, 0)) {
        print_client(&clientAddr);
        print_message_data(message_data);
        set_message(&message, TYPE_CMD_ERR, CMD_ERR_SYN_ERR);
        send_message(&message, s);
        return;
    }

    char buf[DATA_SIZE];
    int n, fd;

    memset(buf, '\0', DATA_SIZE);
    strncpy(buf, message_data->data, message_data->length);

    if ((fd = open(buf, O_RDONLY)) < 0) {
        if (errno == EACCES)      set_message(&message, TYPE_FILE_ERR, FILE_ERR_NO_AUT);
        else if (errno == ENOENT) set_message(&message, TYPE_FILE_ERR, FILE_ERR_NO_EXI);
        else                      set_message(&message, TYPE_UNKWN_ERR, UNKWN_ERR_UNKWN_ERR);

        send_message(&message, s);
        return;
    }

    set_message(&message, TYPE_OK, OK_DATA_S_C);
    send_message(&message, s);

    for (;;) {
        memset(buf, '\0', sizeof(buf));
        if ((n = read(fd, buf, DATA_SIZE)) < 0) break;

        if (n < DATA_SIZE) break;

        set_message_data(message_data, TYPE_DATA, DATA_CON, n, buf);
        send_message_data(message_data, s);
    }

    if (n <= 0) n = 1;
    set_message_data(message_data, TYPE_DATA, DATA_END, n, buf);
    send_message_data(message_data, s);

    close(fd);
}

void stor_func(struct myftp_message_data *message_data) {
    struct myftp_message message;

    if (check_message_data(message_data, TYPE_STOR, 0)) {
        print_client(&clientAddr);
        print_message(&message);
        set_message(&message, TYPE_CMD_ERR, CMD_ERR_SYN_ERR);
        send_message(&message, s);
        return;
    }

    char buf[DATA_SIZE];
    int fd;

    memset(buf, '\0', DATA_SIZE);
    strncpy(buf, message_data->data, message_data->length);

    if ((fd = open(buf, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) {
        if (errno != EEXIST) {
            if (errno == EACCES)      set_message(&message, TYPE_FILE_ERR, FILE_ERR_NO_AUT);
            else                      set_message(&message, TYPE_UNKWN_ERR, UNKWN_ERR_UNKWN_ERR);
            send_message(&message, s);
            return;
        }

        if ((fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
            set_message(&message, TYPE_UNKWN_ERR, UNKWN_ERR_UNKWN_ERR);

            send_message(&message, s);
            return;
        }
    }

    set_message(&message, TYPE_OK, OK_DATA_C_S);
    send_message(&message, s);

    for (;;) {
        if (recv(s, message_data, MESSAGE_DATA_SIZE, MSG_WAITALL) < 0) {
            perror("recv");
            exit(1);
        }

        if (check_message_data(message_data, TYPE_DATA, DATA_CON)) break;

        write(fd, message_data->data, message_data->length);
    }

    if (check_message_data(message_data, TYPE_DATA, DATA_END)) {
        print_client(&clientAddr);
        print_message_data(message_data);
        close(fd);
        return;
    }

    write(fd, message_data->data, message_data->length);

    close(fd);
}
struct format_table {  
    int value; 
    char *name;
};
struct format_table type_tbl[] = {
    { TYPE_QUIT, "quit (0x01)" },
    { TYPE_PWD, "pwd (0x02)" },
    { TYPE_CWD, "cwd (0x03)" },
    { TYPE_LIST, "list (0x04)" },
    { TYPE_RETR, "retr (0x05)" },
    { TYPE_STOR, "stor (0x06)" },
    { TYPE_OK, "ok (0x10)" },
    { TYPE_CMD_ERR, "cmd err (0x11)" },
    { TYPE_FILE_ERR, "file err (0x12)" },
    { TYPE_UNKWN_ERR, "unkwn err (0x13)" },
    { TYPE_DATA, "data (0x20)" },
    { -1, "unknown type" }
};

struct format_table ok_tbl[] = {
    { OK_NO_DATA, "no data (0x00)" },
    { OK_DATA_S_C, "data s-c (0x01)" },
    { OK_DATA_C_S, "data c-s (0x02)" },
    { -1, "unknown code" }
};

struct format_table cmd_err_tbl[] = {
    { CMD_ERR_SYN_ERR, "syntax error (0x01)" },
    { CMD_ERR_UND_CMD, "undefined command error (0x02)" },
    { CMD_ERR_PRO_ERR, "protocol error (0x03)" },
    { -1, "unknown code" }
};

struct format_table file_err_tbl[] = {
    { FILE_ERR_NO_EXI, "file or directory doesn't exist (0x00)" },
    { FILE_ERR_NO_AUT, "no access permission (0x01)" },
    { -1, "unknown code" }
};
struct format_table unkwn_err_tbl[] = {
    { UNKWN_ERR_UNKWN_ERR, "unknown error (0x05)" },
    { -1, "unknown code" }
};

struct format_table data_tbl[] = {
    { DATA_END, "data end (0x00)" },
    { DATA_CON, "data continue (0x01)" },
    { -1, "unknown code" }
};

void print_message(struct myftp_message *message) {
    struct format_table *t;
    fprintf(stderr, "----- message -----\n");

    for (t = type_tbl; t->value != -1; t++) {
        if (t->value == message->type) {
            fprintf(stderr, "TYPE: %s\n", t->name);
            break;
        }
    }
    if (t->value == -1) fprintf(stderr, "TYPE: %s (%d)\n", t->name, message->type);
    switch (message->type) {
    case TYPE_OK:        t = ok_tbl;        break;
    case TYPE_CMD_ERR:   t = cmd_err_tbl;   break;
    case TYPE_FILE_ERR:  t = file_err_tbl;  break;
    case TYPE_UNKWN_ERR: t = unkwn_err_tbl; break;
    case TYPE_DATA:      t = data_tbl;      break;
    default:             t = NULL;          break;
    }

    if (t != NULL) {
        for (; t->value != -1; t++) {
            if (t->value == message->code) {
                fprintf(stderr, "CODE: %s\n", t->name);
                break;
            }
      }
        if (t->value == -1) fprintf(stderr, "CODE: %s (0x%02x)\n", t->name, message->code);
    }
    else {
        fprintf(stderr, "CODE: 0x%02x\n", message->code);
    }

    fprintf(stderr, "DATA LENGTH: %d\n", message->length);

    fprintf(stderr, "----- message -----\n\n");
}

void print_message_data(struct myftp_message_data *message_data) {
    struct format_table *t;

    fprintf(stderr, "----- message data -----\n");

    for (t = type_tbl; t->value != -1; t++) {
        if (t->value == message_data->type) {
            fprintf(stderr, "TYPE: %s\n", t->name);
            break;
        }
    }

    if (t->value == -1) fprintf(stderr, "TYPE: %s (%d)\n", t->name, message_data->type);

    switch (message_data->type) {
    case TYPE_OK:        t = ok_tbl;        break;
    case TYPE_CMD_ERR:   t = cmd_err_tbl;   break;
    case TYPE_FILE_ERR:  t = file_err_tbl;  break;
    case TYPE_UNKWN_ERR: t = unkwn_err_tbl; break;
    case TYPE_DATA:      t = data_tbl;      break;
    default:             t = NULL;          break;
    }

    if (t != NULL) {
        for (; t->value != -1; t++) {
            if (t->value == message_data->code) {
                fprintf(stderr, "CODE: %s\n", t->name);
                break;
            }
        }
        if (t->value == -1) fprintf(stderr, "CODE: %s (0x%02x)\n", t->name, message_data->code);
    }
    else {
        fprintf(stderr, "CODE: 0x%02x\n", message_data->code);
    }

    fprintf(stderr, "DATA LENGTH: %d\n", message_data->length);

    char buf[DATA_SIZE + 1];
    memset(buf, '\0', sizeof(buf));
    strncpy(buf, message_data->data, message_data->length);
    fprintf(stderr, "DATA: %s\n", buf);

    fprintf(stderr, "----- message data -----\n\n");
}

int print_error(struct myftp_message *message) {
    struct format_table *t, *c;

    for (t = type_tbl; t->value != -1; t++) {
        if (t->value == message->type) break;
    }
    if (t->value == -1) return 0;

    switch (message->type) {
    case TYPE_CMD_ERR:   c = cmd_err_tbl;   break;
    case TYPE_FILE_ERR:  c = file_err_tbl;  break;
    case TYPE_UNKWN_ERR: c = unkwn_err_tbl; break;
    }

    for (; c->value != -1; c++) {
        if (c->value == message->code) break;
    }
    if (c->value == -1) return 0;

    fprintf(stderr, "--- failure ---\n");

    for (t = type_tbl; t->value != -1; t++) {
        if (t->value == message->type) {
            fprintf(stderr, "TYPE: %s\n", t->name);
            break;
        }
    }
    for (; c->value != -1; c++) {
        if (c->value == message->code) {
            fprintf(stderr, "CODE: %s\n", c->name);
            break;
        }
    }

    return 1;
}
void set_message(struct myftp_message *message, uint8_t type, uint8_t code) {
    memset(message, 0, MESSAGE_SIZE);
    message->type = type;
    message->code = code;
    message->length = 0;
}

void set_message_data(struct myftp_message_data *message_data, uint8_t type, uint8_t code, uint16_t length, char *data) {
    memset(message_data, 0, MESSAGE_DATA_SIZE);
    message_data->type = type;
    message_data->code = code;
    message_data->length = length;
    strncpy(message_data->data, data, length);
}

int check_message(struct myftp_message *message, uint8_t type, uint8_t code) {
    if (message->type == type) {
        if (message->code == code) {
            if (message->length == 0) {
                return 0;
            }
        }
    }

    return 1;
}

int check_message_data(struct myftp_message_data *message_data, uint8_t type, uint8_t code) {
    if (message_data->type == type) {
        if (message_data->code == code) {
            if (0 < message_data->length && message_data->length <= DATA_SIZE) {
                return 0;
            }
        }
    }

    return 1;
}

void send_message(struct myftp_message *message, int s) {
    if (send(s, message, MESSAGE_SIZE, 0) < 0) {
        perror("send");
        exit(1);
    }
}

void send_message_data(struct myftp_message_data *message_data, int s) {
    if (send(s, message_data, MESSAGE_DATA_SIZE, 0) < 0) {
        perror("send");
        exit(1);
    }
}

void recv_message(struct myftp_message *message, int s) {
    if (recv(s, message, MESSAGE_SIZE, 0) < 0) {
        perror("recv");
        exit(1);
    }
}

void recv_message_data(struct myftp_message_data *message_data, int s) {
    if (recv(s, message_data, MESSAGE_DATA_SIZE, 0) < 0) {
        perror("recv");
        exit(1);
    }
}

int message_data_to_message(struct myftp_message_data *message_data, struct myftp_message *message) {
    if (message_data->length != 0) return 1;
    message->type = message_data->type;
    message->code = message_data->code;
    message->length = message_data->length;
    return 0;
}
