 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <arpa/inet.h>
 #include <sys/types.h>
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <sys/stat.h>
 #include <sys/wait.h>
 #include <errno.h>
 
 #define SERVER_PORT 5002
 #define BUF_SIZE 1024
 
 // expand ~S2 => ./S2
 void expand_path_s2(const char *input, char *output, size_t outSize) {
     if (strncmp(input, "~S2", 3) == 0) {
         snprintf(output, outSize, "./S2%s", input + 3);
     } else {
         snprintf(output, outSize, "%s", input);
     }
 }
 // create path if it doesn't exist
 int mkpath(const char *path) {
     char tmp[512];
     strncpy(tmp, path, sizeof(tmp));
     tmp[511] = '\0';
 
     for (char *p = tmp + 1; *p; p++) {
         if (*p == '/') {
             *p = '\0';
             mkdir(tmp, 0777);
             *p = '/';
         }
     }
     return mkdir(tmp, 0777);
 }
 
 // handle client connection
 void handle_client(int client_sock) {
     char buffer[BUF_SIZE];
 
     while (1) {
         memset(buffer, 0, BUF_SIZE);
         int bytes_read = read(client_sock, buffer, BUF_SIZE - 1);
         if (bytes_read <= 0) break;
 
         printf("S2 received: %s\n", buffer);
         // commands: UPLOAD <fname> <dest> <size>, DOWNLOAD <path>, REMOVE <path>, CREATETAR
         char *cmd = strtok(buffer, " ");
         if (!cmd) {
             write(client_sock, "ERROR no command", 16);
             continue;
         }
         // trim leading/trailing spaces
         if (strcmp(cmd, "UPLOAD") == 0) {
             char *fileName = strtok(NULL, " ");
             char *destPath = strtok(NULL, " ");
             char *sizeStr  = strtok(NULL, " ");
             if (!fileName || !destPath || !sizeStr) {
                 write(client_sock, "ERROR Invalid UPLOAD", 20);
                 continue;
             }
             long fileSize = atol(sizeStr);
 
             char expanded[512];
             expand_path_s2(destPath, expanded, sizeof(expanded));
             mkpath(expanded);
 
             char fullFile[512];
             snprintf(fullFile, sizeof(fullFile), "%s/%s", expanded, fileName);
 
             FILE *fp = fopen(fullFile, "wb");
             if (!fp) {
                 write(client_sock, "ERROR Cannot open file", 22);
                 continue;
             }
             // Wait for "READY" from S1.
             write(client_sock, "READY", 5);

             // Read file data from client.
             long remain = fileSize;
             while (remain > 0) {
                 char dataBuf[BUF_SIZE];
                 int chunk = (remain > BUF_SIZE) ? BUF_SIZE : (int)remain;
                 int r = read(client_sock, dataBuf, chunk);
                 if (r <= 0) break;
                 fwrite(dataBuf, 1, r, fp);
                 remain -= r;
             }
             fclose(fp);
             write(client_sock, "UPLOAD_OK", 9);
             
         } else if (strcmp(cmd, "DOWNLOAD") == 0) {
             char *path = strtok(NULL, " ");
             if (!path) {
                 write(client_sock, "ERROR Invalid DOWNLOAD", 22);
                 continue;
             }
                // Expand "~S2" to "./S2" for S2
             char expanded[512];
             expand_path_s2(path, expanded, sizeof(expanded));
             fprintf(stderr, "[S2] Expanded download path: %s\n", expanded);
             
             // Check if file exists
             FILE *fp = fopen(expanded, "rb");
             if (!fp) {
                 fprintf(stderr, "[S2] Cannot open '%s'\n", expanded);
                 write(client_sock, "ERROR Cannot open file", 22);
                 continue;
             }
             // Send READY signal to S1 that the file is ready
             fseek(fp, 0, SEEK_END);
             long fsize = ftell(fp);
             fseek(fp, 0, SEEK_SET);
 
                // Send file size to S1
             char msg[64];
             snprintf(msg, 64, "FILESIZE %ld", fsize);
             write(client_sock, msg, strlen(msg));
             fprintf(stderr, "[S2] Sent to S1: '%s'\n", msg);
 
             // Wait for "READY" from S1
             char buffer[BUF_SIZE];
             memset(buffer, 0, BUF_SIZE);
             int r = read(client_sock, buffer, BUF_SIZE - 1);
             fprintf(stderr, "[S2] Received from S1: '%s'\n", buffer);
             if (r > 0 && strncmp(buffer, "READY", 5) == 0) {
                 // Send file data in chunks
                 while (!feof(fp)) {
                     int n = fread(buffer, 1, BUF_SIZE, fp);
                     if (n > 0) {
                         write(client_sock, buffer, n);
                     }
                 }
                 fprintf(stderr, "[S2] Finished sending file data.\n");
             } else {
                 fprintf(stderr, "[S2] Did not receive READY from S1.\n");
             }
             fclose(fp);
             // Wait for final response from S1
        } else if (strcmp(cmd, "REMOVE") == 0) {
             char *path = strtok(NULL, " ");
             if (!path) {
                 write(client_sock, "ERROR Invalid REMOVE", 19);
                 continue;
             }
             char expanded[512];
             expand_path_s2(path, expanded, sizeof(expanded));
 
             if (remove(expanded) == 0) {
                 write(client_sock, "REMOVE_OK", 9);
             } else {
                 write(client_sock, "ERROR Remove failed", 19);
             }
 
         } else if (strcmp(cmd, "CREATETAR") == 0) {
            // Remove any old temporary files first
            system("rm -f pdf_list.txt pdffiles.tar");

            // Find all PDF files in the S2 directory and create a list
            FILE *fp_find = popen("find ./S2 -type f -iname '*.pdf'", "r");
            if (fp_find == NULL) {
                write(client_sock, "ERROR running find", strlen("ERROR running find"));
                continue;
            }
            
            // Write the output of the find command into pdf_list.txt
            FILE *list_fp = fopen("pdf_list.txt", "w");
            if (list_fp == NULL) {
                write(client_sock, "ERROR opening pdf_list.txt", strlen("ERROR opening pdf_list.txt"));
                pclose(fp_find);
                continue;
            }
            
            char line[512];
            int fileCount = 0;
            while (fgets(line, sizeof(line), fp_find) != NULL) {
                fputs(line, list_fp);
                fileCount++;
            }
            fclose(list_fp);
            pclose(fp_find);
            
            // If no files were found, send a "NO_PDFS" message to S1 and clean up.
            if (fileCount == 0) {
                write(client_sock, "NO_PDFS", strlen("NO_PDFS"));
                system("rm -f pdf_list.txt");
                continue;
            }

            // Create tar archive using the file list
            // Redirect STDIN from /dev/null so tar doesn't wait for input
            system("tar -cf pdffiles.tar -T pdf_list.txt < /dev/null > /dev/null 2>&1");

            FILE *fp_tar = fopen("pdffiles.tar", "rb");
            if (!fp_tar) {
                write(client_sock, "ERROR Cannot open tar file", strlen("ERROR Cannot open tar file"));
                continue;
            }

            // Send READY signal to S1 that the tar file is ready
            write(client_sock, "READY_FOR_FILE", strlen("READY_FOR_FILE"));

            // Get file size, send it to S1
            fseek(fp_tar, 0, SEEK_END);
            long tsize = ftell(fp_tar);
            fseek(fp_tar, 0, SEEK_SET);
            char sz[64];
            snprintf(sz, sizeof(sz), "%ld", tsize);
            write(client_sock, sz, strlen(sz));

            // Wait for the OK signal from S1
            char ack[BUF_SIZE];
            memset(ack, 0, sizeof(ack));
            int ackBytes = read(client_sock, ack, BUF_SIZE - 1);
            if (ackBytes <= 0 || strncmp(ack, "OK", 2) != 0) {
                fclose(fp_tar);
                continue;
            }

            // Send the tar archive to S1
            while (!feof(fp_tar)) {
                char buf[BUF_SIZE];
                int r = fread(buf, 1, BUF_SIZE, fp_tar);
                if (r > 0) {
                    write(client_sock, buf, r);
                }
            }
            fclose(fp_tar);

            // Clean up temporary files
            system("rm -f pdffiles.tar pdf_list.txt");
        }
        //DISPNAMES command
        
        else if (strcmp(cmd, "DISPNAMES") == 0) {
            char *path = strtok(NULL, " ");
            if (!path) {
                write(client_sock, "ERROR usage: DISPNAMES <path>\n", strlen("ERROR usage: DISPNAMES <path>\n"));
                continue;
            }
            // Expand "~S2" to "./S2" for S2
            char expanded[512];
            if (strncmp(path, "~S2", 3) == 0) {
                snprintf(expanded, sizeof(expanded), "./S2%s", path + 3);
            } else {
                strncpy(expanded, path, sizeof(expanded));
            }
            // Build the find command to list only file names
            char findCmd[1024];
            snprintf(findCmd, sizeof(findCmd), "find %s -type f -iname '*.pdf' -printf '%%f\n' | sort", expanded);
            FILE *fp = popen(findCmd, "r");
            char result[2048];
            memset(result, 0, sizeof(result));
            if (fp) {
                char line[256];
                while (fgets(line, sizeof(line), fp)) {
                    strcat(result, line);
                }
                pclose(fp);
            }
            write(client_sock, result, strlen(result));
        } else {
             write(client_sock, "ERROR Unknown command", 21);
        }
     }
 }
    // create socket, bind, listen, accept connections
 int main() {
     int sockfd, client_sock;
     struct sockaddr_in serv_addr, client_addr;
     socklen_t client_len = sizeof(client_addr);
 
     // Create socket
     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd < 0) {
         perror("socket");
         exit(1);
     }
     memset(&serv_addr, 0, sizeof(serv_addr));
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(SERVER_PORT);
     // Bind socket to port
     if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
         perror("bind");
         close(sockfd);
         exit(1);
     }
 
        // Listen for incoming connections
     if (listen(sockfd, 5) < 0) {
         perror("listen");
         close(sockfd);
         exit(1);
     }
     printf("S2 server listening on port %d...\n", SERVER_PORT);
 
     while (1) {
        // Accept incoming connection
         client_sock = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
         if (client_sock < 0) {
             perror("accept");
             continue;
         }
         pid_t pid = fork();
         if (pid < 0) {
             perror("fork");
             close(client_sock);
             continue;
         }
         if (pid == 0) {
             close(sockfd);
             handle_client(client_sock);
             close(client_sock);
             exit(0);
         } else {
             close(client_sock);
         }
         while (waitpid(-1, NULL, WNOHANG) > 0) {}
     }
 
     close(sockfd);
     return 0;
 }
