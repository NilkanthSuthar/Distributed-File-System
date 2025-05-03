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

 #define S1_PORT 5001
 #define BUF_SIZE 1024

 #define S2_PORT 5002
 #define S3_PORT 5003
 #define S4_PORT 5004
 #define SERVER_IP "127.0.0.1"

 // Forward declarations
 void process_client(int client_sock);

 // send_error
 void send_error(int sock, const char *msg) {
     write(sock, msg, strlen(msg));
 }


 // e.g. ~S1/path/file.ext => ./S1/path/file.ext

 void expand_path_s1(const char *input, char *output, size_t outSize) {
     if (strncmp(input, "~S1", 3) == 0) {
         snprintf(output, outSize, "./S1%s", input + 3);
     } else {
         snprintf(output, outSize, "%s", input);
     }
 }

 // e.g. .pdf => replace "~S1" with "~S2"
 
 void replace_s1_with_sx(const char *original, char *output, size_t outSize, const char *replacement) {
     if (strncmp(original, "~S1", 3) == 0) {
         // e.g. replacement = "~S2"
         snprintf(output, outSize, "%s%s", replacement, original + 3);
     } else {
         snprintf(output, outSize, "%s", original);
     }
 }

 // create subfolders
 int mkpath(const char *path) {
     char tmp[512];
     strncpy(tmp, path, sizeof(tmp));
     tmp[511] = '\0';

     for (char *p = tmp + 1; *p; p++) {
         if (*p == '/') {
             *p = '\0';
             mkdir(tmp, 0777); // ignore if exists
             *p = '/';
         }
     }
     return mkdir(tmp, 0777);
 }

 // connect_to_servers S2, S3, S4
 int connect_to_server(int port) {
     int sockfd;
     struct sockaddr_in serv_addr;

     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd < 0) {
         perror("socket");
         return -1;
     }
     // set up server address
     memset(&serv_addr, 0, sizeof(serv_addr));
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_port = htons(port);
     serv_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

     if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
         perror("connect");
         close(sockfd);
         return -1;
     }
     return sockfd;
 }


 // handle all commands from w25clients
    
 void process_client(int client_sock) {
     char buffer[BUF_SIZE];

     while (1) {
         memset(buffer, 0, BUF_SIZE);
         int n = read(client_sock, buffer, BUF_SIZE - 1);
         if (n <= 0) {
             // client closed or error
             break;
         }
         fprintf(stderr, "[S1] Received from client: '%s'\n", buffer);

         char *cmd = strtok(buffer, " ");
         if (!cmd) {
             send_error(client_sock, "ERR no command\n");
             continue;
         }

     
          // uploadf
        
         if (strcmp(cmd, "uploadf") == 0) {
             char *fileName = strtok(NULL, " ");
             char *destPath = strtok(NULL, " ");
             if (!fileName || !destPath) {
                 send_error(client_sock, "ERR usage: uploadf <filename> <dest>\n");
                 continue;
             }
             // 1) S1 => client: "READY_FOR_FILE"
             write(client_sock, "READY_FOR_FILE", 14);

             // 2) read file size from client
             char sizeBuf[64];
             memset(sizeBuf, 0, 64);
             int readSize = read(client_sock, sizeBuf, 63);
             if (readSize <= 0) {
                 send_error(client_sock, "ERR cannot read size\n");
                 continue;
             }
             long fsize = atol(sizeBuf);

             // 3) "OK"
             write(client_sock, "OK", 2);

             // 4) read file data into temp
             FILE *fp = fopen("./temp_upload", "wb");
             if (!fp) {
                 send_error(client_sock, "ERR cannot open temp file\n");
                 continue;
             }
             long remain = fsize;
             while (remain > 0) {
                 char dataBuf[BUF_SIZE];
                 int chunk = (remain > BUF_SIZE) ? BUF_SIZE : (int)remain;
                 int rr = read(client_sock, dataBuf, chunk);
                 if (rr <= 0) break;
                 fwrite(dataBuf, 1, rr, fp);
                 remain -= rr;
             }
             fclose(fp);
             fprintf(stderr, "[S1] Done receiving file from client.\n");

             // check extension
             char *dot = strrchr(fileName, '.');
             if (!dot) {
                 send_error(client_sock, "ERR unknown file type\n");
                 remove("./temp_upload");
                 continue;
             }

             // .c => store local (~S1 => ./S1)
             // .pdf => forward to S2
             // .txt => forward to S3
             // .zip => forward to S4
             if (strcmp(dot, ".c") == 0) {
                 char expanded[512];
                 expand_path_s1(destPath, expanded, sizeof(expanded));
                 mkpath(expanded);

                    // check if file already exists
                 char finalFile[512];
                 snprintf(finalFile, sizeof(finalFile), "%s/%s", expanded, fileName);
                 rename("./temp_upload", finalFile);
                 write(client_sock, "UPLOAD_DONE", 11);
                 fprintf(stderr, "[S1] .c stored locally at '%s'\n", finalFile);

             } else {
                // for non .c file: determine the target server based on extension
                 int targetPort = 0;
                 char newPath[512];

                 if (strcmp(dot, ".pdf") == 0) {
                     targetPort = S2_PORT;
                     replace_s1_with_sx(destPath, newPath, sizeof(newPath), "~S2");
                 } else if (strcmp(dot, ".txt") == 0) {
                     targetPort = S3_PORT;
                     replace_s1_with_sx(destPath, newPath, sizeof(newPath), "~S3");
                 } else if (strcmp(dot, ".zip") == 0) {
                     targetPort = S4_PORT;
                     replace_s1_with_sx(destPath, newPath, sizeof(newPath), "~S4");
                 } else {
                     send_error(client_sock, "ERR unhandled file extension\n");
                     remove("./temp_upload");
                     continue;
                 }
                 int sSock = connect_to_server(targetPort); // connect to S2, S3, or S4
                 if (sSock < 0) {
                     send_error(client_sock, "ERR cannot connect subserver\n");
                     remove("./temp_upload");
                     continue;
                 }
                 // UPLOAD filename ~S2/.. or ~S3/.. or ~S4/..  <fsize>
                 char msg[512];
                 snprintf(msg, sizeof(msg), "UPLOAD %s %s %ld", fileName, newPath, fsize);
                 write(sSock, msg, strlen(msg));

                 // expect "READY"
                 char resp[BUF_SIZE];
                 memset(resp, 0, BUF_SIZE);
                 int r2 = read(sSock, resp, BUF_SIZE - 1);
                 if (r2 > 0 && strncmp(resp, "READY", 5) == 0) {
                     // send data from ./temp_upload
                     FILE *fp2 = fopen("./temp_upload", "rb");
                     if (!fp2) {
                         write(sSock, "ERROR read temp file", 20);
                     } else {
                         long remain2 = fsize;
                         while (remain2 > 0) {  // send data
                             char dbuf[BUF_SIZE];
                             int c = (remain2 > BUF_SIZE) ? BUF_SIZE : (int)remain2;   
                             int rd = fread(dbuf, 1, c, fp2);
                             if (rd <= 0) break;
                             write(sSock, dbuf, rd);
                             remain2 -= rd;
                         }
                         fclose(fp2);
                     }
                     // final ack
                     memset(resp, 0, BUF_SIZE);
                     int r3 = read(sSock, resp, BUF_SIZE - 1);
                     if (r3 > 0 && strncmp(resp, "UPLOAD_OK", 9) == 0) {
                         write(client_sock, "UPLOAD_DONE", 11);
                     } else {
                         send_error(client_sock, "ERR subserver upload\n");
                     }
                 } else {
                     fprintf(stderr, "[S1] did not get READY => '%s'\n", resp);
                     send_error(client_sock, "ERR subserver not ready\n");
                 }
                 close(sSock);
                 remove("./temp_upload");
             }

          // downlf
   
        } else if (strcmp(cmd, "downlf") == 0) {
            // "downlf ~S1/path/file.ext"
            char *path = strtok(NULL, " ");
            if (!path) {
                send_error(client_sock, "ERR usage: downlf <path>\n");
                continue;
            }
            char *dot = strrchr(path, '.');
            if (!dot) {
                send_error(client_sock, "ERR extension missing\n");
                continue;
            }
            if (strcmp(dot, ".c") == 0) {
                // local from S1
                char expanded[512];
                expand_path_s1(path, expanded, sizeof(expanded));
                FILE *fp = fopen(expanded, "rb");
                if (!fp) {
                    send_error(client_sock, "ERR cannot open local c-file\n");
                    continue;
                }
                // "READY_FOR_FILE"
                write(client_sock, "READY_FOR_FILE", 14);

                // get file size
                fseek(fp, 0, SEEK_END);
                long fsize = ftell(fp);
                fseek(fp, 0, SEEK_SET);

                char sz[64];
                snprintf(sz, 64, "%ld", fsize);
                write(client_sock, sz, strlen(sz));

                // wait "OK" from client
                char ack[BUF_SIZE];
                memset(ack, 0, BUF_SIZE);
                int ackBytes = read(client_sock, ack, BUF_SIZE - 1);
                // if an empty message is received, try to read again
                while (ackBytes > 0 && strlen(ack) == 0) {
                    memset(ack, 0, BUF_SIZE);
                    ackBytes = read(client_sock, ack, BUF_SIZE - 1);
                }

                // send data
                while (!feof(fp)) {
                    int r = fread(ack, 1, BUF_SIZE, fp);
                    if (r > 0) {
                        write(client_sock, ack, r);
                    }
                }
                fclose(fp);

            } else {
                // determine the target server based on extension
                int targetPort = 0;
                char newPath[512];

                if (strcmp(dot, ".pdf") == 0) {
                    targetPort = S2_PORT;
                    replace_s1_with_sx(path, newPath, sizeof(newPath), "~S2");
                } else if (strcmp(dot, ".txt") == 0) {
                    targetPort = S3_PORT;
                    replace_s1_with_sx(path, newPath, sizeof(newPath), "~S3");
                } else if (strcmp(dot, ".zip") == 0) {
                    targetPort = S4_PORT;
                    replace_s1_with_sx(path, newPath, sizeof(newPath), "~S4");
                } else {
                    send_error(client_sock, "ERR unknown extension\n");
                    continue;
                }
                int sSock = connect_to_server(targetPort);
                if (sSock < 0) {
                    send_error(client_sock, "ERR cannot connect subserver\n");
                    continue;
                }
                // Send DOWNLOAD command to subserver using the updated path
                char msg[512];
                snprintf(msg, sizeof(msg), "DOWNLOAD %s", newPath);
                write(sSock, msg, strlen(msg));
                fprintf(stderr, "[S1] Sent to subserver: %s\n", msg);

                // Expect "FILESIZE <num>" reply from subserver
                char resp[BUF_SIZE];
                memset(resp, 0, BUF_SIZE);
                int rd = read(sSock, resp, BUF_SIZE - 1);
                fprintf(stderr, "[S1] Read from subserver: '%s'\n", resp);
                if (rd > 0 && strncmp(resp, "FILESIZE", 8) == 0) {
                    long fsize = atol(resp + 9);
                    fprintf(stderr, "[S1] File size from subserver: %ld\n", fsize);

                    // Inform subserver that S1 is ready to receive the file
                    write(sSock, "READY", 5);
                    fprintf(stderr, "[S1] Sent 'READY' to subserver.\n");

                    // Inform the client that S1 is ready to send the file
                    write(client_sock, "READY_FOR_FILE", 14);
                    fprintf(stderr, "[S1] Sent 'READY_FOR_FILE' to client.\n");

                    // Send file size to the client
                    char sz[64];
                    snprintf(sz, 64, "%ld", fsize);
                    write(client_sock, sz, strlen(sz));
                    fprintf(stderr, "[S1] Sent file size '%s' to client.\n", sz);

                    // Wait for "OK" from client
                    memset(resp, 0, BUF_SIZE);
                    int clientAck = read(client_sock, resp, BUF_SIZE - 1);
                    // if empty response is received, try reading again
                    while (clientAck > 0 && strlen(resp) == 0) {
                        memset(resp, 0, BUF_SIZE);
                        clientAck = read(client_sock, resp, BUF_SIZE - 1);
                    }
                    fprintf(stderr, "[S1] Received from client: '%s'\n", resp);
                    if (clientAck <= 0 || strncmp(resp, "OK", 2) != 0) {
                        send_error(client_sock, "ERR client did not send OK\n");
                        close(sSock);
                        continue;
                    }

                    // Forward file data from subserver to client
                    long remain2 = fsize;
                    while (remain2 > 0) {
                        char chunkBuf[BUF_SIZE];
                        int chunk = (remain2 > BUF_SIZE) ? BUF_SIZE : (int)remain2;
                        int got = read(sSock, chunkBuf, chunk);
                        if (got <= 0) break;
                        write(client_sock, chunkBuf, got);
                        remain2 -= got;
                    }
                    fprintf(stderr, "[S1] Finished forwarding file data. Bytes remaining: %ld\n", remain2);
                } else {
                    fprintf(stderr, "[S1] Error: did not receive proper FILESIZE from subserver.\n");
                    send_error(client_sock, "ERR subserver no FILESIZE\n");
                }
                close(sSock);
            }


          // removef
         } else if (strcmp(cmd, "removef") == 0) {
             // "removef ~S1/path/file.ext"
             char *path = strtok(NULL, " ");
             if (!path) {
                 send_error(client_sock, "ERR usage: removef <path>\n");
                 continue;
             }
             char *dot = strrchr(path, '.');
             if (!dot) {
                 send_error(client_sock, "ERR no extension\n");
                 continue;
             }
             if (strcmp(dot, ".c") == 0) {
                 // local
                 char expanded[512];
                 expand_path_s1(path, expanded, sizeof(expanded));
                 if (remove(expanded) == 0) {
                     write(client_sock, "REMOVE_DONE", 11);
                 } else {
                     send_error(client_sock, "ERR remove local\n");
                 }
             } else {
                 int targetPort = 0;
                 char newPath[512];

                 if (strcmp(dot, ".pdf") == 0) {
                     targetPort = S2_PORT;
                     replace_s1_with_sx(path, newPath, sizeof(newPath), "~S2");
                 } else if (strcmp(dot, ".txt") == 0) {
                     targetPort = S3_PORT;
                     replace_s1_with_sx(path, newPath, sizeof(newPath), "~S3");
                 } else if (strcmp(dot, ".zip") == 0) {
                     targetPort = S4_PORT;
                     replace_s1_with_sx(path, newPath, sizeof(newPath), "~S4");
                 } else {
                     send_error(client_sock, "ERR ext unknown\n");
                     continue;
                 }
                 int sSock = connect_to_server(targetPort);
                 if (sSock < 0) {
                     send_error(client_sock, "ERR cannot connect subserver\n");
                     continue;
                 }
                 char msg[512];
                 snprintf(msg, sizeof(msg), "REMOVE %s", newPath);
                 write(sSock, msg, strlen(msg));

                 char resp[BUF_SIZE];
                 memset(resp, 0, BUF_SIZE);
                 int rd = read(sSock, resp, BUF_SIZE - 1);
                 if (rd > 0 && strncmp(resp, "REMOVE_OK", 9) == 0) {
                     write(client_sock, "REMOVE_DONE", 11);
                 } else {
                     send_error(client_sock, "ERR remove remote\n");
                 }
                 close(sSock);
             }

            // "downltar"
        } else if (strcmp(cmd, "downltar") == 0) {
            char *ftype = strtok(NULL, " ");
            if (!ftype) {
                send_error(client_sock, "ERR usage: downltar <.c/.pdf/.txt/.zip>\n");
                continue;
            }
            if (strcmp(ftype, ".c") == 0) {
                // Remove any old temporary files
                system("rm -f cfiles_list.txt cfiles.tar");
                
                // Use popen to recursively search for .c files in ./S1
                FILE *fp_find = popen("find ./S1 -type f -iname '*.c'", "r");
                if (fp_find == NULL) {
                    send_error(client_sock, "ERR running find");
                    continue;
                }
                
                // Save the list of .c files to a temporary file
                FILE *list_fp = fopen("cfiles_list.txt", "w");
                if (list_fp == NULL) {
                    send_error(client_sock, "ERR opening cfiles_list.txt");
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
                
                // If no .c files are found, notify the client and exit this branch
                if (fileCount == 0) {
                    write(client_sock, "NO_CFILES", strlen("NO_CFILES"));
                    system("rm -f cfiles_list.txt");
                    continue;
                }
                
                // Create a tar archive containing the list of .c files
                // Redirect standard input from /dev/null and suppress output
                system("tar -cf cfiles.tar -T cfiles_list.txt < /dev/null > /dev/null 2>&1");
                
                // Open the archive for reading.
                FILE *fp_tar = fopen("cfiles.tar", "rb");
                if (!fp_tar) {
                    send_error(client_sock, "ERR Cannot open tar file");
                    continue;
                }
                
                // Inform the client that the tar file is ready
                write(client_sock, "READY_FOR_FILE", strlen("READY_FOR_FILE"));
                
                // Determine the tar archive's size and send it
                fseek(fp_tar, 0, SEEK_END);
                long fsize = ftell(fp_tar);
                fseek(fp_tar, 0, SEEK_SET);
                char sz[64];
                snprintf(sz, sizeof(sz), "%ld", fsize);
                write(client_sock, sz, strlen(sz));
                
                // Wait for the client's "OK" before streaming data
                char ack[BUF_SIZE];
                memset(ack, 0, sizeof(ack));
                int ackBytes = read(client_sock, ack, BUF_SIZE - 1);
                if (ackBytes <= 0 || strncmp(ack, "OK", 2) != 0) {
                    fclose(fp_tar);
                    continue;
                }
                
                // Send the tar archive in chunks to the client
                while (!feof(fp_tar)) {
                    char buffer[BUF_SIZE];
                    int r = fread(buffer, 1, BUF_SIZE, fp_tar);
                    if (r > 0) {
                        write(client_sock, buffer, r);
                    }
                }
                fclose(fp_tar);
                
                // Clean up the temporary files
                system("rm -f cfiles.tar cfiles_list.txt");
            }    else if (strcmp(ftype, ".pdf") == 0) {
                int sSock = connect_to_server(S2_PORT);
                if (sSock < 0) {
                    send_error(client_sock, "ERR cannot connect S2\n");
                    continue;
                }
                write(sSock, "CREATETAR", strlen("CREATETAR"));

                char resp[BUF_SIZE];
                memset(resp, 0, BUF_SIZE);
                int n = read(sSock, resp, BUF_SIZE - 1);
                if (n <= 0 || strncmp(resp, "READY_FOR_FILE", 14) != 0) {
                    send_error(client_sock, "ERR S2 tar creation failed\n");
                    close(sSock);
                    continue;
                }
                // Read the tar file size from S2
                memset(resp, 0, BUF_SIZE);
                int sizeBytes = read(sSock, resp, BUF_SIZE - 1);
                if (sizeBytes <= 0) {
                    send_error(client_sock, "ERR S2 did not send tar file size\n");
                    close(sSock);
                    continue;
                }
                long fsize = atol(resp);
                write(sSock, "OK", 2);

                // Forward tar file info to client
                write(client_sock, "READY_FOR_FILE", 14);
                char sz[64];
                snprintf(sz, 64, "%ld", fsize);
                write(client_sock, sz, strlen(sz));

                // Wait for "OK" from client
                char clientAck[BUF_SIZE];
                memset(clientAck, 0, BUF_SIZE);
                int ackBytes = read(client_sock, clientAck, BUF_SIZE - 1);
                while (ackBytes > 0 && strlen(clientAck) == 0) {
                    memset(clientAck, 0, BUF_SIZE);
                    ackBytes = read(client_sock, clientAck, BUF_SIZE - 1);
                }
                if (ackBytes <= 0 || strncmp(clientAck, "OK", 2) != 0) {
                    send_error(client_sock, "ERR client did not send OK\n");
                    close(sSock);
                    continue;
                }
                // Read from S2 and forward to the client
                long remain = fsize;
                while (remain > 0) {
                    char buffer[BUF_SIZE];
                    int chunk = (remain > BUF_SIZE) ? BUF_SIZE : (int)remain;
                    int r = read(sSock, buffer, chunk);
                    if (r <= 0) break;
                    write(client_sock, buffer, r);
                    remain -= r;
                }
                close(sSock);

            } else if (strcmp(ftype, ".txt") == 0) {
                // Connect to S3
                int sSock = connect_to_server(S3_PORT);
                if (sSock < 0) {
                    send_error(client_sock, "ERR cannot connect S3\n");
                    continue;
                }
                // Request tar creation on S3
                write(sSock, "CREATETAR", strlen("CREATETAR"));
    
                // Wait for S3's READY signal
                char resp[BUF_SIZE];
                memset(resp, 0, BUF_SIZE);
                int n = read(sSock, resp, BUF_SIZE - 1);
                if (n <= 0 || strncmp(resp, "READY_FOR_FILE", 14) != 0) {
                    send_error(client_sock, "ERR S3 tar creation failed\n");
                    close(sSock);
                    continue;
                }
    
                // Read the tar file size from S3
                memset(resp, 0, BUF_SIZE);
                n = read(sSock, resp, BUF_SIZE - 1);
                if (n <= 0) {
                    send_error(client_sock, "ERR S3 did not send tar file size\n");
                    close(sSock);
                    continue;
                }
                long fsize = atol(resp);
                // Inform S3 we're ready to receive the tar file
                write(sSock, "OK", 2);
    
                // Forward the READY signal and tar file size to the client
                write(client_sock, "READY_FOR_FILE", strlen("READY_FOR_FILE"));
                char sz[64];
                snprintf(sz, sizeof(sz), "%ld", fsize);
                write(client_sock, sz, strlen(sz));
    
                // Wait for the client to send "OK"
                char clientAck[BUF_SIZE];
                memset(clientAck, 0, BUF_SIZE);
                int ackBytes = read(client_sock, clientAck, BUF_SIZE - 1);
                if (ackBytes <= 0 || strncmp(clientAck, "OK", 2) != 0) {
                    send_error(client_sock, "ERR client did not send OK\n");
                    close(sSock);
                    continue;
                }
    
                // Read from S3 and forward to the client
                long remain = fsize;
                while (remain > 0) {
                    char buffer[BUF_SIZE];
                    int chunk = (remain > BUF_SIZE) ? BUF_SIZE : (int)remain;
                    int r = read(sSock, buffer, chunk);
                    if (r <= 0) break;
                    write(client_sock, buffer, r);
                    remain -= r;
                }
    
                close(sSock);
                // Remove the temporary tar file
            }    else if (strcmp(ftype, ".zip") == 0) {
                int sSock = connect_to_server(S4_PORT);
                if (sSock < 0) {
                    send_error(client_sock, "ERR cannot connect S4\n");
                    continue;
                }
                write(sSock, "CREATETAR", strlen("CREATETAR"));
                // Wait for S4's READY signal
                char resp[BUF_SIZE];
                memset(resp, 0, BUF_SIZE);
                int n = read(sSock, resp, BUF_SIZE - 1);
                if (n <= 0 || strncmp(resp, "READY_FOR_FILE", 14) != 0) {
                    send_error(client_sock, "ERR S4 tar creation failed\n");
                    close(sSock);
                    continue;
                }
                // Read the tar file size from S4
                memset(resp, 0, BUF_SIZE);
                int sizeBytes = read(sSock, resp, BUF_SIZE - 1);
                if (sizeBytes <= 0) {
                    send_error(client_sock, "ERR S4 did not send tar file size\n");
                    close(sSock);
                    continue;
                }
                long fsize = atol(resp);
                write(sSock, "OK", 2);
                // Forward the READY signal and tar file size to the client
                write(client_sock, "READY_FOR_FILE", 14);
                char sz[64];
                snprintf(sz, 64, "%ld", fsize);
                write(client_sock, sz, strlen(sz));
                // Wait for the client to send "OK"
                char clientAck[BUF_SIZE];
                memset(clientAck, 0, BUF_SIZE);
                int ackBytes = read(client_sock, clientAck, BUF_SIZE - 1);
                while (ackBytes > 0 && strlen(clientAck) == 0) {
                    memset(clientAck, 0, BUF_SIZE);
                    ackBytes = read(client_sock, clientAck, BUF_SIZE - 1);
                }
                if (ackBytes <= 0 || strncmp(clientAck, "OK", 2) != 0) {
                    send_error(client_sock, "ERR client did not send OK\n");
                    close(sSock);
                    continue;
                }
                // Read from S4 and forward to the client
                long remain = fsize;
                while (remain > 0) {
                    char buffer[BUF_SIZE];
                    int chunk = (remain > BUF_SIZE) ? BUF_SIZE : (int)remain;
                    int r = read(sSock, buffer, chunk);
                    if (r <= 0) break;
                    write(client_sock, buffer, r);
                    remain -= r;
                }
                close(sSock);
            } else {
                send_error(client_sock, "ERR unknown filetype for tar\n");
            }
      }      // dispfnames
         else if (strcmp(cmd, "dispfnames") == 0) {
            char *path = strtok(NULL, " ");
            if (!path) {
                send_error(client_sock, "ERR usage: dispfnames <~S1/path>\n");
                continue;
            }
            
            // Consolidated result buffer.
            char result[4096];
            memset(result, 0, sizeof(result));
            
            // c files from S1
            char expanded[512];
            expand_path_s1(path, expanded, sizeof(expanded));
            // Use -printf '%f\n' to output only file names
            char findCmd[1024];
            snprintf(findCmd, sizeof(findCmd), "find %s -type f -iname '*.c' -printf '%%f\n' | sort", expanded);
            FILE *fp = popen(findCmd, "r");
            strcat(result, "[.c FILES]\n");
            if (fp) {
                char line[256];
                while (fgets(line, sizeof(line), fp)) {
                    strcat(result, line);
                }
                pclose(fp);
            } else {
                strcat(result, "Error executing local find command\n");
            }
            
            // .pdf Files from S2 
            char s2Path[512];
            replace_s1_with_sx(path, s2Path, sizeof(s2Path), "~S2");
            int s2Sock = connect_to_server(S2_PORT);
            strcat(result, "\n[.pdf FILES]\n");
            if (s2Sock >= 0) {
                char dispCmd[512];
                snprintf(dispCmd, sizeof(dispCmd), "DISPNAMES %s", s2Path);
                write(s2Sock, dispCmd, strlen(dispCmd));
                char buffer[1024];
                memset(buffer, 0, sizeof(buffer));
                int n = read(s2Sock, buffer, sizeof(buffer)-1);
                if (n > 0) {
                    strcat(result, buffer);
                } else {
                    strcat(result, "Error or no .pdf files found\n");
                }
                close(s2Sock);
            } else {
                strcat(result, "Error connecting to S2\n");
            }
            
            // ----- .txt Files from S3 -----
            char s3Path[512];
            replace_s1_with_sx(path, s3Path, sizeof(s3Path), "~S3");
            int s3Sock = connect_to_server(S3_PORT);
            strcat(result, "\n[.txt FILES]\n");
            if (s3Sock >= 0) {
                char dispCmd[512];
                snprintf(dispCmd, sizeof(dispCmd), "DISPNAMES %s", s3Path);
                write(s3Sock, dispCmd, strlen(dispCmd));
                char buffer[1024];
                memset(buffer, 0, sizeof(buffer));
                int n = read(s3Sock, buffer, sizeof(buffer)-1);
                if (n > 0) {
                    strcat(result, buffer);
                } else {
                    strcat(result, "Error or no .txt files found\n");
                }
                close(s3Sock);
            } else {
                strcat(result, "Error connecting to S3\n");
            }
            
            // ----- .zip Files from S4 -----
            char s4Path[512];
            replace_s1_with_sx(path, s4Path, sizeof(s4Path), "~S4");
            int s4Sock = connect_to_server(S4_PORT);
            strcat(result, "\n[.zip FILES]\n");
            if (s4Sock >= 0) {
                char dispCmd[512];
                snprintf(dispCmd, sizeof(dispCmd), "DISPNAMES %s", s4Path);
                write(s4Sock, dispCmd, strlen(dispCmd));
                char buffer[1024];
                memset(buffer, 0, sizeof(buffer));
                int n = read(s4Sock, buffer, sizeof(buffer)-1);
                if (n > 0) {
                    strcat(result, buffer);
                } else {
                    strcat(result, "Error or no .zip files found\n");
                }
                close(s4Sock);
            } else {
                strcat(result, "Error connecting to S4\n");
            }
            
            // Send the consolidated list back to the client.
            write(client_sock, result, strlen(result));
        } else {
             send_error(client_sock, "ERR unknown cmd\n");
         }
     }
 }

 int main() {
     int sockfd, client_sock;
     struct sockaddr_in serv_addr, client_addr;
     socklen_t client_len = sizeof(client_addr);
        // create socket
     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd < 0) {
         perror("socket");
         return 1;
     }
     // set up server address
     memset(&serv_addr, 0, sizeof(serv_addr));
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(S1_PORT);

     if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
         perror("bind");
         close(sockfd);
         return 1;
     }
     if (listen(sockfd, 5) < 0) {
         perror("listen");
         close(sockfd);
         return 1;
     }
     printf("S1 (main server) listening on port %d...\n", S1_PORT);

     while (1) {
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
             // child
             close(sockfd);
             process_client(client_sock);
             close(client_sock);
             return 0;
         } else {
             close(client_sock);
         }
         // cleanup children
         while (waitpid(-1, NULL, WNOHANG) > 0) {}
     }

     close(sockfd);
     return 0;
 }
