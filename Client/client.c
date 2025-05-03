 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <arpa/inet.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
 #include <sys/types.h>
 #include <sys/stat.h>
 
 #define S1_PORT 5001
 #define SERVER_IP "127.0.0.1"
 #define BUF_SIZE 1024
 
 // Helper function to trim leading/trailing white spaces
 void trim(char *str) {     
     // Trim leading spaces
     while(*str && (*str == ' ' || *str == '\t'))
         str++;
     // Trim trailing spaces
     char *end = str + strlen(str) - 1;
     while(end > str && (*end == ' ' || *end == '\t'))
         *end-- = '\0';
 }
 
 int main() {
     while (1) {
         printf("w25clients$ ");
         fflush(stdout);
 
         char cmdline[256];
         if (!fgets(cmdline, sizeof(cmdline), stdin)) {
            break;
         }
         // remove trailing newline
         char *nl = strchr(cmdline, '\n');
         if (nl) *nl = '\0';
 
         // If command is empty, then skip to next iteration
         if (strlen(cmdline) == 0) {
             continue;
         }

         // connect to S1
         int sockfd = socket(AF_INET, SOCK_STREAM, 0);  // create socket
         if (sockfd < 0) {
             perror("socket");  // error creating socket
             return 1;
         }
         struct sockaddr_in serv_addr;
         memset(&serv_addr, 0, sizeof(serv_addr));
         serv_addr.sin_family = AF_INET;
         serv_addr.sin_port = htons(S1_PORT);
         serv_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
 
         if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
             perror("connect");
             close(sockfd);
             continue;
         }
 
         // parse first token
         char tmp[256];
         strcpy(tmp, cmdline);
         char *token = strtok(tmp, " ");
 
         // send entire line to S1
         write(sockfd, cmdline, strlen(cmdline));
 
         // handle uploadf
         if (token && strcmp(token, "uploadf") == 0) {
             // expect READY_FOR_FILE from S1
             char buffer[BUF_SIZE];
             memset(buffer, 0, BUF_SIZE);
             int r = read(sockfd, buffer, BUF_SIZE - 1);
             if (r > 0 && strncmp(buffer, "READY_FOR_FILE", 14) == 0) {
                 // parse out fileName from cmdline
                 char *fname = strtok(NULL, " ");
                 char *dest  = strtok(NULL, " ");
                 if (!fname || !dest) {
                     printf("Usage: uploadf <filename> <destPath>\n");
                     close(sockfd);
                     continue;
                 }
                 // open local file
                 FILE *fp = fopen(fname, "rb");
                 if (!fp) {
                     printf("Cannot open local file '%s'\n", fname);
                     close(sockfd);
                     continue;
                 }
                 // get size
                 fseek(fp, 0, SEEK_END);
                 long fsize = ftell(fp);
                 fseek(fp, 0, SEEK_SET);
 
                 // send size
                 char sz[64];
                 snprintf(sz, 64, "%ld", fsize);
                 write(sockfd, sz, strlen(sz));
 
                 // read OK
                 memset(buffer, 0, BUF_SIZE);
                 read(sockfd, buffer, BUF_SIZE - 1);
 
                 if (strncmp(buffer, "OK", 2) == 0) {
                     // send file in chunks
                     while (!feof(fp)) {
                         int n = fread(buffer, 1, BUF_SIZE, fp);
                         if (n > 0) {
                             write(sockfd, buffer, n);
                         }
                     }
                 }
                 fclose(fp);
 
                 // read final response from S1
                 memset(buffer, 0, BUF_SIZE);
                 read(sockfd, buffer, BUF_SIZE - 1);
                 printf("Server says: %s\n", buffer);
 
             } else {
                 printf("Server says: %s\n", buffer);
             }
 
         } else if (token && strcmp(token, "downlf") == 0) {
             // Expect READY_FOR_FILE or error
             char buffer[BUF_SIZE];
             memset(buffer, 0, BUF_SIZE);
             int r = read(sockfd, buffer, BUF_SIZE - 1);    // read response
             while (r > 0 && strlen(buffer) == 0) { // // if empty message, try again
                 memset(buffer, 0, BUF_SIZE);
                 r = read(sockfd, buffer, BUF_SIZE - 1);
             }
             if (r <= 0) {
                 printf("Server closed or error\n");
             } else if (strncmp(buffer, "READY_FOR_FILE", 14) == 0) {
                 // read file size
                 memset(buffer, 0, BUF_SIZE);
                 read(sockfd, buffer, BUF_SIZE - 1);
                 long fsize = atol(buffer);
 
                 // send OK
                 printf("Received file size: %ld bytes\n", fsize);
                 printf("Sending OK to server...\n");
                 write(sockfd, "OK", 2);
 
                 // parse the local name from command
                 char localName[128] = "downloaded_file";
                 {
                     char reparse[256];
                     strcpy(reparse, cmdline);
                     strtok(reparse, " "); // skip downlf
                     char *pathTok = strtok(NULL, " ");
                     if (pathTok) {
                         char *slash = strrchr(pathTok, '/');
                         if (slash)
                             slash++;
                         else
                             slash = pathTok;
                         strcpy(localName, slash);
                     }
                 }
 
                 FILE *fp = fopen(localName, "wb");
                 if (!fp) {
                     printf("Cannot create file '%s'\n", localName);
                     close(sockfd);
                     continue;
                 }
                 long remain = fsize;
                 while (remain > 0) {
                     char dataBuf[BUF_SIZE];
                     int chunk = (remain > BUF_SIZE) ? BUF_SIZE : (int)remain;
                     int rd = read(sockfd, dataBuf, chunk);
                     if (rd <= 0)
                         break;
                     fwrite(dataBuf, 1, rd, fp);
                     remain -= rd;
                 }
                 fclose(fp);
                 printf("Downloaded '%s' (%ld bytes)\n", localName, fsize);
             } else {
                 printf("Server says: %s\n", buffer);
             }
 
         } else if (token && strcmp(token, "removef") == 0) {
             // read server response
             char buffer[BUF_SIZE];
             memset(buffer, 0, BUF_SIZE);
             int r = read(sockfd, buffer, BUF_SIZE - 1);
             if (r > 0) {
                 printf("Server says: %s\n", buffer);
             } else {
                 printf("Server closed or error\n");
             }
 
         } else if (token && strcmp(token, "downltar") == 0) {
             // receive READY_FOR_FILE
             char buffer[BUF_SIZE];
             memset(buffer, 0, BUF_SIZE);
             int r = read(sockfd, buffer, BUF_SIZE - 1);
             if (r <= 0) {
                 printf("Server closed or error\n");
             } else if (strncmp(buffer, "READY_FOR_FILE", 14) == 0) {
                 // read size
                 memset(buffer, 0, BUF_SIZE);
                 read(sockfd, buffer, BUF_SIZE - 1);
                 long fsize = atol(buffer);
 
                 // send OK
                 write(sockfd, "OK", 2);
 
                 FILE *fp = fopen("downloaded.tar", "wb");
                 if (!fp) {
                     printf("Cannot create 'downloaded.tar'\n");
                     close(sockfd);
                     continue;
                 }
                 long remain = fsize;
                 while (remain > 0) {
                     char dataBuf[BUF_SIZE];
                     int chunk = (remain > BUF_SIZE) ? BUF_SIZE : (int)remain;
                     int rd = read(sockfd, dataBuf, chunk);
                     if (rd <= 0)
                         break;
                     fwrite(dataBuf, 1, rd, fp);
                     remain -= rd;
                 }
                 fclose(fp);
                 printf("Downloaded tar: 'downloaded.tar' (%ld bytes)\n", fsize);
 
             } else {
                 printf("Server says: %s\n", buffer);
             }
 
         } else if (token && strcmp(token, "dispfnames") == 0) {
             // read server response
             char buffer[BUF_SIZE];
             memset(buffer, 0, BUF_SIZE);
             int r = read(sockfd, buffer, BUF_SIZE - 1);
             if (r > 0) {
                 printf("Server says:\n%s\n", buffer);
             } else {
                 printf("Server closed or error\n");
             }
 
         } else {
             // read the response
             char buffer[BUF_SIZE];
             memset(buffer, 0, BUF_SIZE);
             int rr = read(sockfd, buffer, BUF_SIZE - 1);
             if (rr > 0) {
                 printf("Server says: %s\n", buffer);
             } else {
                 printf("Server closed or no data\n");
             }
         }
 
         close(sockfd);
     }
     return 0;
 }