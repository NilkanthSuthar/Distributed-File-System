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

#define SERVER_PORT 5004
#define BUF_SIZE 1024

// expand ~S4 => ./S4
void expand_path_s4(const char *input, char *output, size_t outSize) {
    if (strncmp(input, "~S4", 3) == 0) {
        snprintf(output, outSize, "./S4%s", input + 3);
    } else {
        snprintf(output, outSize, "%s", input);
    }
}

// create subfolders by creating all directories in the path if they do not exist
int mkpath(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp));
    tmp[511] = '\0';    // ensure null-termination

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {    // found a directory separator
            *p = '\0';  
            mkdir(tmp, 0777);
            *p = '/';   // restore the separator
        }
    }
    return mkdir(tmp, 0777);
}

// handle client commands
// Accept commands: UPLOAD, DOWNLOAD, REMOVE, DISPNAMES
void handle_client(int client_sock) {
    char buffer[BUF_SIZE];

    while (1) {
        memset(buffer, 0, BUF_SIZE);
        int bytes_read = read(client_sock, buffer, BUF_SIZE - 1);
        if (bytes_read <= 0) break;

        // Trim newline character
        printf("S4 received: %s\n", buffer);
        char *cmd = strtok(buffer, " ");
        if (!cmd) {
            write(client_sock, "ERROR no command", 16);
            continue;
        }

        // Process commands
        // UPLOAD <fname> <dest> <size>
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
            expand_path_s4(destPath, expanded, sizeof(expanded));
            mkpath(expanded);

            char fullFile[512];
            snprintf(fullFile, sizeof(fullFile), "%s/%s", expanded, fileName);

            // Open file for writing
            FILE *fp = fopen(fullFile, "wb");
            if (!fp) {
                write(client_sock, "ERROR Cannot open file", 22);
                continue;
            }
            write(client_sock, "READY", 5); // Notify client to start sending data
            
            // Read file data from client
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
        // DOWNLOAD <path>
        else if (strcmp(cmd, "DOWNLOAD") == 0) {
            char *path = strtok(NULL, " ");
            if (!path) {
                write(client_sock, "ERROR Invalid DOWNLOAD", 22);
                continue;
            }
            char expanded[512];
            expand_path_s4(path, expanded, sizeof(expanded));
            fprintf(stderr, "[S4] Expanded download path: %s\n", expanded);
        
            // Open file for reading
            FILE *fp = fopen(expanded, "rb");
            if (!fp) {
                fprintf(stderr, "[S4] Cannot open '%s'\n", expanded);
                write(client_sock, "ERROR Cannot open file", 22);
                continue;
            }
            // Notify client that file is ready for transfer
            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            
            char msg[64];
            snprintf(msg, 64, "FILESIZE %ld", fsize);
            write(client_sock, msg, strlen(msg));
            fprintf(stderr, "[S4] Sent to S1: '%s'\n", msg);
        
            // Wait for READY from S1
            char buffer[BUF_SIZE];
            memset(buffer, 0, BUF_SIZE);
            int r = read(client_sock, buffer, BUF_SIZE - 1);
            fprintf(stderr, "[S4] Received from S1: '%s'\n", buffer);
            if (r > 0 && strncmp(buffer, "READY", 5) == 0) {
                // Send file data in chunks
                while (!feof(fp)) {
                    int n = fread(buffer, 1, BUF_SIZE, fp);
                    if (n > 0) {
                        write(client_sock, buffer, n);
                    }
                }
                fprintf(stderr, "[S4] Finished sending file data.\n");
            } else {
                fprintf(stderr, "[S4] Did not receive READY from S1.\n");
            }
            fclose(fp);
        }         

        // REMOVE <path>
        else if (strcmp(cmd, "REMOVE") == 0) {
            char *path = strtok(NULL, " ");
            if (!path) {
                write(client_sock, "ERROR Invalid REMOVE", 19);
                continue;
            }

            // Expand "~S4" to "./S4"
            char expanded[512];
            expand_path_s4(path, expanded, sizeof(expanded));

            if (remove(expanded) == 0) {
                write(client_sock, "REMOVE_OK", 9);
            } else {
                write(client_sock, "ERROR Remove failed", 19);
            }
        } 

        // DISPNAMES <path>
        else if (strcmp(cmd, "DISPNAMES") == 0) {
            char *path = strtok(NULL, " ");
            if (!path) {
                write(client_sock, "ERROR usage: DISPNAMES <path>\n", strlen("ERROR usage: DISPNAMES <path>\n"));
                continue;
            }
            // Expand "~S4" to "./S4"
            char expanded[512];
            if (strncmp(path, "~S4", 3) == 0) {
                snprintf(expanded, sizeof(expanded), "./S4%s", path + 3);
            } else {
                strncpy(expanded, path, sizeof(expanded));
            }
            // Build the find command to list only file names of .zip files.
            char findCmd[1024];
            snprintf(findCmd, sizeof(findCmd), "find %s -type f -iname '*.zip' -printf '%%f\n' | sort", expanded);
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

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    // Initialize server address structure
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(SERVER_PORT);

    // Bind socket to address
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
    printf("S4 server listening on port %d...\n", SERVER_PORT);

    while (1) { // Accept incoming connections
        client_sock = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {  // error accepting connection
            perror("accept");
            continue;
        }
        pid_t pid = fork(); // Create a child process
        if (pid < 0) {
            perror("fork");
            close(client_sock);
            continue;
        }
        if (pid == 0) { // Child process
            close(sockfd);
            handle_client(client_sock);
            close(client_sock);
            exit(0);
        } else {
            close(client_sock);
        }
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }

    close(sockfd); // Close the server socket
    return 0;
}
