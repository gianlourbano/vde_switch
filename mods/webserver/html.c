#include "html.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <regex.h>
#include <ctype.h>
#include "cJSON.h"
#include "debug.h"

#define ROUTE_START() if (0) {
#define ROUTE(METHOD, URI)                                                     \
  }                                                                            \
  else if (strcmp(URI, uri) == 0 && strcmp(METHOD, method) == 0) {
#define GET(URI) ROUTE("GET", URI)
#define POST(URI) ROUTE("POST", URI)
#define ROUTE_END()   }                                                         \
  else 
     


extern Webserver_Support* support;
extern int webserver_support_size;
extern int webserver_support_capacity;

char *method, // "GET" or "POST"
    *uri,     // "/index.html" things before '?'
    *qs,      // "a=1&b=2" things after  '?'
    *prot,    // "HTTP/1.1"
    *payload; // for POST

int payload_size;

#define BUFFER_SIZE (1 << 17)
static char buffer[BUFFER_SIZE];

static void uri_unescape(char *uri)
{
    char chr = 0;
    char *src = uri;
    char *dst = uri;

    // Skip inital non encoded character
    while (*src && !isspace((int)(*src)) && (*src != '%'))
        src++;

    // Replace encoded characters with corresponding code.
    dst = src;
    while (*src && !isspace((int)(*src)))
    {
        if (*src == '+')
            chr = ' ';
        else if ((*src == '%') && src[1] && src[2])
        {
            src++;
            chr = ((*src & 0x0F) + 9 * (*src > '9')) * 16;
            src++;
            chr += ((*src & 0x0F) + 9 * (*src > '9'));
        }
        else
            chr = *src;
        *dst++ = chr;
        src++;
    }
    *dst = '\0';
}

static void sanitaze_uri(char *uri)
{
    char *src = uri;
    char *dst = uri;

    while (*src)
    {
        if (*src == '.' && src[1] == '.')
        {
            src += 2;
            continue;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
}

typedef struct {
  char *name, *value;
} header_t;
static header_t reqhdr[17] = {{"\0", "\0"}};

char *request_header(const char *name) {
  header_t *h = reqhdr;
  while (h->name) {
    if (strcmp(h->name, name) == 0)
      return h->value;
    h++;
  }
  return NULL;
}

void route(int);
int handle_request(int fd)
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd;

    // accept client connection
    if ((client_fd = accept(fd,
                             (struct sockaddr *)&client_addr,
                             &client_addr_len)) < 0)
    {
        ERROR("accept failed");
    }

    // create a new thread to handle client request
    // pthread_t thread_id;
    // pthread_create(&thread_id, NULL, &handle_client, (void*)client_fd);
    // pthread_detach(thread_id);

    int read_size = recv(client_fd, buffer, BUFFER_SIZE, 0);
    if (read_size < 0)
    {
        ERROR("read failed");
        close(client_fd);
    }

    buffer[read_size] = '\0';

    method = strtok(buffer, " \t\r\n");
    uri = strtok(NULL, " \t");
    prot = strtok(NULL, " \t\r\n");

    uri_unescape(uri);
    sanitaze_uri(uri);

    LOG("\x1b[32m + [%s] %s\x1b[0m\n", method, uri);

    qs = strchr(uri, '?');

    if (qs)
        *qs++ = '\0'; // split URI
    else
        qs = uri - 1; // use an empty string

    header_t *h = reqhdr;
    char *t, *t2;
    while (h < reqhdr + 16)
    {
        char *key, *val;

        key = strtok(NULL, "\r\n: \t");
        if (!key)
            break;

        val = strtok(NULL, "\r\n");
        while (*val && *val == ' ')
            val++;

        h->name = key;
        h->value = val;
        h++;
        //fprintf(stderr, "[H] %s: %s\n", key, val);
        t = val + 1 + strlen(val);
        if (t[1] == '\r' && t[2] == '\n')
            break;
    }
    t = strtok(NULL, "\r\n");
    t2 = request_header("Content-Length"); // and the related header if there is
    payload = t;
    payload_size = t2 ? atol(t2) : (read_size - (t - buffer));

    route(client_fd);

    close(client_fd);

    return 0;
}

void route(int client_fd) {
    ROUTE_START()

    GET("/")
    {
        char response[] = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n<html><body><h1>Hello, World!</h1></body></html>";
        write(client_fd, response, strlen(response));
    }

    GET("/actions")
    {
        // build json
        char response_header[] = "HTTP/1.1 200 OK\nContent-Type: application/json\n\n";
        write(client_fd, response_header, strlen(response_header));

        cJSON *root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "actions", cJSON_CreateArray());

        char out[256];
        for(int i = 0; i < webserver_support_size; i++) {
            Webserver_Action* actions = support[i].actions;
            for(Webserver_Action* action = actions; action->action_name; action++) {
                cJSON *action_json = cJSON_CreateObject();
                cJSON_AddStringToObject(action_json, "name", action->action_name);
                cJSON_AddItemToArray(cJSON_GetObjectItem(root, "actions"), action_json);
            }
        }

        char *json = cJSON_PrintUnformatted(root);
        write(client_fd, json, strlen(json));
        cJSON_Delete(root);
        free(json);
    }

    POST("/action")
    {
        if(payload_size > 0) {
            LOG("Payload: %s\n", payload);
        }

        // perform action
        cJSON *root = cJSON_Parse(payload);
        if(!root) {
            char response[] = "HTTP/1.1 400 Bad Request\nContent-Type: application/json\n\n{\"status\": \"error\"}";
            write(client_fd, response, strlen(response));
            cJSON_Delete(root);
            return;
        }

        cJSON *action = cJSON_GetObjectItem(root, "action");
        if(!action) {
            char response[] = "HTTP/1.1 400 Bad Request\nContent-Type: application/json\n\n{\"status\": \"error\"}";
            write(client_fd, response, strlen(response));
            cJSON_Delete(root);
            return;
        }

        char *action_name = action->valuestring;
        LOG("Performing Action: %s\n", action_name);

        int found = 0;

        for(int i = 0; i < webserver_support_size; i++) {
            Webserver_Action* actions = support[i].actions;
            for(Webserver_Action* action = actions; action->action_name; action++) {
                if(strcmp(action->action_name, action_name) == 0) {
                    action->action();
                    found = 1;
                    break;
                }
            }
        }

        if(!found) {
            char response[] = "HTTP/1.1 404 Not Found\nContent-Type: application/json\n\n{\"status\": \"error\"}";
            write(client_fd, response, strlen(response));
            cJSON_Delete(root);
            return;
        }

        char response[] = "HTTP/1.1 200 OK\nContent-Type: application/json\n\n{\"status\": \"ok\"}";
        write(client_fd, response, strlen(response));

        cJSON_Delete(root);
    }

    ROUTE_END() 
    {                                                                      
        char response[] = "HTTP/1.1 404 Not Found\nContent-Type: text/html\n\n<html><body><h1>404 Not Found</h1></body></html>"; 
        write(client_fd, response, strlen(response));                              
    }
    
}