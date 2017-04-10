#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define BACKLOG (10)
#define MAX_CLIENTS (1024)
struct dirent **namelist;

typedef struct {
    int connected;
    int location;
    int song;
} client;

typedef struct {
    char id;
    int size;
    FILE *data;
    char name[1024];
    char *info;
} song;


song songs[11];
client clients[MAX_CLIENTS];

/*
Helper function for sending data
*/
void sendData(int sock, char *buffer, int size){
    send(sock, buffer, size, MSG_NOSIGNAL); //send request
}

/*
Helper function for receiving data from clients
Returns: int for success
*/
int receiveClient(char *buf, int sock){
    /* Handles receiving data from the client */
    int recv_count = 5000;
    while(1){
        recv_count = recv(sock, buf, 7, 0);

        if(recv_count < 7){
            break;
        }
    }
    if(recv_count == 0){
        return 1;
    }
    return 0;
}

/*
Breaks files into 5000 byte chunks and transmits
*/
void sendFile(FILE *fp, int sock, int size, int val, int location){
    /* sends a music file */

    char buffer[5000];
    char header [12];
    memset(buffer,0,sizeof(buffer));
    fread(buffer, sizeof(char), 5000, fp);
    sprintf(buffer, "%s",buffer);
    if (location+5000 > size){
        memset(header,0,sizeof(header));
        int final = size - location;

        sprintf(header, "play-%d%d EH",val,final);
        sendData(sock, header, strlen(header));
        sendData(sock, buffer, final);
    }
    else{
        memset(header,0,sizeof(header));
        sprintf(header, "play %d    EOH",val);
        sendData(sock, header, strlen(header));
        sendData(sock, buffer, 5000);
    }
    fseek(fp, location+5000, location);
    clients[sock].location+=5000;

}

/*
Keeps track of what song each client is listening to
*/
void playMusic(int sock, char *buf){
    char * goal = buf;
    goal +=5;
    int val = atoi(goal);
    clients[sock].song = val;
}

/*
Sends list of songs
*/
void sendList(int sock, int song_count){
    char toSend[5000];
    int k;
    strcpy(toSend, "list ");
    for (k=0; k < song_count; k++){
        char songBuf[200];
        memset(songBuf,0,sizeof(songBuf));

        sprintf(songBuf,"%d,%s/",songs[k].id,songs[k].name);
        strcat(toSend,songBuf);
    }
    int totalSize = strlen(toSend);
    char header[12];
    sprintf(header, "list %d  EOH",totalSize);
    sendData(sock, header, strlen(header));
    sendData(sock, toSend, strlen(toSend));
}

/*
Sends song info
*/
void sendInfo(int sock, char *buf){
    char * goal = buf;
    goal +=5;
    int val = atoi(goal);
    char toSend[strlen(songs[val].info)+5];
    char header[12];
    sprintf(header, "info %lu EOH",strlen(songs[val].info));
    sendData(sock, header, strlen(header));
    sprintf(toSend,"info %s",songs[val].info);
    sendData(sock, toSend, strlen(toSend));

}

/*
Makes sockets non-blocking
*/
void set_non_blocking(int sock) {
    int socket_flags = fcntl(sock, F_GETFD);
    if (socket_flags < 0) {
        perror("fcntl");
        exit(1);
    }
    socket_flags = socket_flags | O_NONBLOCK;

    int result = fcntl(sock, F_SETFD, socket_flags);
    if (result < 0) {
        perror("fcntl");
        exit(1);
    }
}

int filter(const struct dirent *ent) {
    int len = strlen(ent->d_name);

    return !strncasecmp(ent->d_name + len - 4, ".mp3", 4);
}

/*
Given directory, reads all the songs and info data in it
Returns: number of found files
*/
int read_mp3_files(char *dir, int flag) {
    int i,n;

    n = scandir(dir, &namelist, filter, alphasort);
    if (n < 0) {
        perror("scandir");
        exit(1);
    }
    for (i = 0; i < n; ++i) {
        song newSong;
        newSong.id=(char)i;
        int bytes_read = 0;
        int total_read = 0;
        char path[1024];
        struct stat fileinfo;

        FILE *infofile = NULL;

        char *infostring = NULL;

        /* namelist[i]->d_name now contains the name of an mp3 file. */
        strcpy(newSong.name, namelist[i]->d_name);
        /* Build a path to a possible input file. */
        strcpy(path, dir);
        strcat(path, "/");
        strcat(path, namelist[i]->d_name);

        if (stat(path, &fileinfo)) {
            perror("stat");
            exit(1);
        }
        newSong.size = fileinfo.st_size;
        FILE *f = fopen(path, "r");
        if (f==NULL) {
            perror("fopen");
            exit (1);
        }
        newSong.data = f;

        /* Build a path to the info file by appending ".info" to the path. */
        strcat(path, ".info");

        infofile = fopen(path, "r");
        if (infofile == NULL) {
            infostring = "No information available.";
        } else {
            /* We found and opened the info file. */
            int infosize = 1024;
            infostring = malloc(infosize);

            do {
                infosize *= 2;
                infostring = realloc(infostring, infosize);

                bytes_read = fread(infostring + total_read, 1, infosize - total_read, infofile);
                total_read += bytes_read;
            } while (bytes_read > 0);

            fclose(infofile);
            memset(infostring + total_read, 0, infosize - total_read);
        }

        newSong.info = infostring;
        songs[i] = newSong;
        free(namelist[i]);
    }
    free(namelist);

    /* Return the number of files we found. */
    return n;
}

/*
Main control loop, uses select() to control network flow
*/
int main(int argc, char **argv) {
    int serv_sock;
    int i;
    int val = 1;
    int song_count;
    uint16_t port;
    struct sockaddr_in addr;
    fd_set rfds, wfds;

    if (argc < 3) {
        printf("Usage:\n%s <port> <music directory>\n", argv[0]);
        exit(0);
    }

    /* Get the port number  */
    port = (uint16_t) atoi(argv[1]);

    /* Create the socket */
    serv_sock = socket(AF_INET, SOCK_STREAM, 0);

    val = setsockopt(serv_sock,SOL_SOCKET,SO_REUSEADDR, &val,
            sizeof(val));
    if (val < 0) {
        perror("Setting socket option failed");
        exit(1);
    }
    song_count = read_mp3_files(argv[2], 0);
    printf("Found %d songs.\n", song_count);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* Bind our socket and start listening for connections. */
    val = bind(serv_sock, (struct sockaddr*)&addr, sizeof(addr));
    if(val < 0) {
        perror("Error binding to port");
        exit(1);
    }

    val = listen(serv_sock, BACKLOG);
    if(val < 0) {
        perror("Error listening for connections");
        exit(1);
    }

    while (1) {
        int max_fd = serv_sock;

        FD_ZERO(&rfds);
        FD_ZERO(&wfds);

        FD_SET(serv_sock, &rfds);

        for (i = 0; i < MAX_CLIENTS; ++i) {

             if (clients[i].connected){
                FD_SET(i, &rfds);

                if (i > max_fd)
                    max_fd = i;
             }
             if (clients[i].connected) {
                FD_SET(i, &wfds);
                if (i > max_fd)
                    max_fd = i;
             }
        }

        max_fd += 1;

        val = select(max_fd, &rfds, &wfds, NULL, NULL);
        if (val < 1) {
            perror("select");
            continue;
        }
        for (i = 0; i < max_fd; ++i) {
            char buf[100];
            if (FD_ISSET(i, &rfds)) {
                if(i == serv_sock){
                    struct sockaddr_in remote_addr;
                    unsigned int socklen = sizeof(remote_addr);
                    int sock = accept(serv_sock, (struct sockaddr *) &remote_addr, &socklen);
                    if(sock < 0) {
                        perror("Error accepting connection");
                        exit(1);
                    }
                    if (clients[i].connected != 1){
                        client newClient;
                        newClient.connected = 1;
                        newClient.location = -1;

                        clients[sock] = newClient;
                        char *st = "HelloNope";
                        sendData(sock, st, strlen(st));
                    }
                }
                if (clients[i].connected ==1){
                    receiveClient(buf, i);
                }
            }
            if (FD_ISSET(i, &wfds)) {
                if(clients[i].connected ==1){

                    if (strstr(buf, "list") != NULL){
                        sendList(i, song_count);
                    }
                    else if (strstr(buf,"info")!=NULL){
                        sendInfo(i,buf);
                    }
                    else if(strstr(buf,"play")!= NULL){
                        clients[i].location = 0;
                        playMusic(i, buf);
                    }
                    if (clients[i].location > -1){
                        sendFile(songs[clients[i].song].data,
                                 i, songs[clients[i].song].size,
                                 clients[i].song,
                                 clients[i].location);
                        if (clients[i].location +5000 > songs[clients[i].song].size)
                            clients[i].location = -1;
                    }
                }
                memset(buf,0,sizeof(buf));

            }
        }
    }
}
