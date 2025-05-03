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

#define SERVER_PORT 5003
#define BUF_SIZE 1024

// expand ~S3 => ./S3
void expand_path_s3(const char *input, char *output, size_t outSize) {
    if (strncmp(input, "~S3", 3) == 0) {
        snprintf(output, outSize, "./S3%s", input + 3);
    } else {
        snprintf(output, outSize, "%s", input);
    }
}

// Create directories recursively
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

// Handle client commands
void handle_client(int client_sock) {
    char buffer[BUF_SIZE];

    while (1) {
        memset(buffer, 0, BUF_SIZE);
        int bytes_read = read(client_sock, buffer, BUF_SIZE - 1);
        if (bytes_read <= 0) break;

        // Remove trailing newline
        printf("S3 received command: %s\n", buffer);
        char *cmd = strtok(buffer, " ");
        if (!cmd) {
            write(client_sock, "ERROR no command", 16);
            continue;
        }
        // Handle commands
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
            expand_path_s3(destPath, expanded, sizeof(expanded));
            mkpath(expanded);

            char fullFile[512];
            snprintf(fullFile, sizeof(fullFile), "%s/%s", expanded, fileName);

            FILE *fp = fopen(fullFile, "wb");
            if (!fp) {
                write(client_sock, "ERROR Cannot open file", 22);
                continue;
            }
            write(client_sock, "READY", 5);

            // Read file data in chunks
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

        } 
        // Check if the command is DOWNLOAD
        else if (strcmp(cmd, "DOWNLOAD") == 0) {
            char *path = strtok(NULL, " ");
            if (!path) {
                write(client_sock, "ERROR Invalid DOWNLOAD", 22);
                continue;
            }
            char expanded[512];
            expand_path_s3(path, expanded, sizeof(expanded));
            fprintf(stderr, "[S3] Expanded download path: %s\n", expanded);
        
            // Open the file for reading
            FILE *fp = fopen(expanded, "rb");
            if (!fp) {
                fprintf(stderr, "[S3] Cannot open '%s'\n", expanded);
                write(client_sock, "ERROR Cannot open file", 22);
                continue;
            }
            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            
            char msg[64];
            snprintf(msg, 64, "FILESIZE %ld", fsize);
            write(client_sock, msg, strlen(msg));
            fprintf(stderr, "[S3] Sent to S1: '%s'\n", msg);
        
            // Wait for "READY" from S1
            char buffer[BUF_SIZE];
            memset(buffer, 0, BUF_SIZE);
            int r = read(client_sock, buffer, BUF_SIZE - 1);
            fprintf(stderr, "[S3] Received from S1: '%s'\n", buffer);
            if (r > 0 && strncmp(buffer, "READY", 5) == 0) {
                // Send file data in chunks
                while (!feof(fp)) {
                    int n = fread(buffer, 1, BUF_SIZE, fp);
                    if (n > 0) {
                        write(client_sock, buffer, n);
                    }
                }
                fprintf(stderr, "[S3] Finished sending file data.\n");
            } else {
                fprintf(stderr, "[S3] Did not receive READY from S1.\n");
            }
            fclose(fp);
        }

        // Check if the command is REMOVE
        else if (strcmp(cmd, "REMOVE") == 0) {
            char *path = strtok(NULL, " ");
            if (!path) {
                write(client_sock, "ERROR Invalid REMOVE", 19);
                continue;
            }
            char expanded[512];
            expand_path_s3(path, expanded, sizeof(expanded));

            if (remove(expanded) == 0) {
                write(client_sock, "REMOVE_OK", 9);
            } else {
                write(client_sock, "ERROR Remove failed", 19);
            }

            // Check if the command is CREATETAR
        }else if (strcmp(cmd, "CREATETAR") == 0) {
            fprintf(stderr, "[S3 Debug] Received CREATETAR command\n");
        
            // Remove old temporary files.
            system("rm -f txt_list.txt txtfiles.tar");
        
            // Use popen() to recursively find all .txt files in ./S3
            FILE *fp_find = popen("find ./S3 -type f -iname '*.txt'", "r");
            if (fp_find == NULL) {
                write(client_sock, "ERROR running find", strlen("ERROR running find"));
                fprintf(stderr, "[S3 Debug] popen() failed for find command\n");
                continue;
            }
        
            // Write the output into a temporary list file
            FILE *list_fp = fopen("txt_list.txt", "w");
            if (list_fp == NULL) {
                write(client_sock, "ERROR opening txt_list.txt", strlen("ERROR opening txt_list.txt"));
                pclose(fp_find);
                fprintf(stderr, "[S3 Debug] Cannot open txt_list.txt for writing\n");
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
        
            fprintf(stderr, "[S3 Debug] Found %d .txt files\n", fileCount);
        
            // If no .txt files were found, notify S1 and clean up
            if (fileCount == 0) {
                write(client_sock, "NO_TXTS", strlen("NO_TXTS"));
                system("rm -f txt_list.txt");
                fprintf(stderr, "[S3 Debug] No .txt files found. Exiting CREATETAR branch.\n");
                continue;
            }
        
            // Create a tar archive using the file list.
            // Redirect standard input from /dev/null so tar doesn't wait for input
            int tar_ret = system("tar -cf txtfiles.tar -T txt_list.txt < /dev/null > /dev/null 2>&1");
            if (tar_ret != 0) {
                write(client_sock, "ERROR creating tar", strlen("ERROR creating tar"));
                system("rm -f txt_list.txt");
                fprintf(stderr, "[S3 Debug] Tar command failed with return code %d\n", tar_ret);
                continue;
            }
            fprintf(stderr, "[S3 Debug] Tar archive created successfully\n");
        
            FILE *fp_tar = fopen("txtfiles.tar", "rb");
            if (!fp_tar) {
                write(client_sock, "ERROR Cannot open tar file", strlen("ERROR Cannot open tar file"));
                fprintf(stderr, "[S3 Debug] Failed to open txtfiles.tar for reading\n");
                continue;
            }
        
            // Notify S1 that the tar file is ready for transfer
            write(client_sock, "READY_FOR_FILE", strlen("READY_FOR_FILE"));
        
            // Get the tar file’s size and send it
            fseek(fp_tar, 0, SEEK_END);
            long tsize = ftell(fp_tar);
            fseek(fp_tar, 0, SEEK_SET);
            char sz[64];
            snprintf(sz, sizeof(sz), "%ld", tsize);
            write(client_sock, sz, strlen(sz));
            fprintf(stderr, "[S3 Debug] Tar file size: %s bytes\n", sz);
        
            // Wait for S1’s "OK" before sending the tar file
            char ack[BUF_SIZE];
            memset(ack, 0, sizeof(ack));
            int ackBytes = read(client_sock, ack, BUF_SIZE - 1);
            fprintf(stderr, "[S3 Debug] Received ACK: '%s' (%d bytes)\n", ack, ackBytes);
            if (ackBytes <= 0 || strncmp(ack, "OK", 2) != 0) {
                fclose(fp_tar);
                fprintf(stderr, "[S3 Debug] Did not receive proper OK from S1\n");
                continue;
            }
        
            // Send the tar archive in chunks
            while (!feof(fp_tar)) {
                char buffer[BUF_SIZE];
                int r = fread(buffer, 1, BUF_SIZE, fp_tar);
                if (r > 0) {
                    write(client_sock, buffer, r);
                }
            }
            fclose(fp_tar);
            fprintf(stderr, "[S3 Debug] Finished sending tar file\n");
        
            // Clean up temporary files
            system("rm -f txtfiles.tar txt_list.txt");
            fprintf(stderr, "[S3 Debug] Cleaned up temporary files\n");
        }        
        else if (strcmp(cmd, "DISPNAMES") == 0) {
            char *path = strtok(NULL, " ");
            if (!path) {
                write(client_sock, "ERROR usage: DISPNAMES <path>\n", strlen("ERROR usage: DISPNAMES <path>\n"));
                continue;
            }
            // Expand "~S3" to "./S3"
            char expanded[512];
            if (strncmp(path, "~S3", 3) == 0) {
                snprintf(expanded, sizeof(expanded), "./S3%s", path + 3);
            } else {
                strncpy(expanded, path, sizeof(expanded));
            }
            // Build the find command to list only file names of .txt files.
            char findCmd[1024];
            snprintf(findCmd, sizeof(findCmd), "find %s -type f -iname '*.txt' -printf '%%f\n' | sort", expanded);
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
        }    
        else {
            write(client_sock, "ERROR Unknown command", 21);
        }
    }
}

int main() {
    int sockfd, client_sock;
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    // Set up server address
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(SERVER_PORT);
    // Bind the socket to the address
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
    printf("S3 server listening on port %d...\n", SERVER_PORT);

    while (1) {
        // Accept a new client connection
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
