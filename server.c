#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

int init_socket(u_short *);  //初始化 httpd 服务
void listen_req(int);   //处理请求功能
void running_cgi(int, const char *, const char *, const char *);//运行 cgi 程序的处理
void read_socket(int, FILE *); //读取服务器某个文件写到socket套接字
void server_to_client(int, const char *); //服务器文件返回给浏览器
void http_to_socket(int, const char *); // 把 HTTP 响应的头部写到套接字
int executing(int, char *, int);  //读取套接字的一行，把回车换行等情况都统一为换行符结束
void bad_req(int);     // 400错误码处理
void error_cgi(int);  //处理执行cgi程序时出现的错误
void errormessage_die(const char *); //把错误信息写到perror并退出
void not_found(int);  //处理找不到请求的文件时的情况
void no_support_method(int); //返回给浏览器表明收到的 HTTP 请求所用的 method 不被支持

int init_socket(u_short *port)
{
    int httpd = 0;
    struct sockaddr_in name;
    /*建立 socket */
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        errormessage_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        errormessage_die("bind");
    if (*port == 0)  
    {
        int namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, (void *)&namelen) == -1)
            errormessage_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    /*开始监听*/
    if (listen(httpd, 5) < 0)
        errormessage_die("listen");
    return(httpd);
}

void listen_req(int client)
{
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      
    char *query_string = NULL;

    numchars = executing(client, buf, sizeof(buf));
    i = 0; j = 0;
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[j];
        i++; j++;
    }
    method[i] = '\0';
    /*如果既不是 GET 又不是 POST 则无法处理 */
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        no_support_method(client);
        return;
    }
    /* POST 的时候开启 cgi */
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;
    /*读取 url 地址*/
    i = 0;
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';
    /*处理 GET 方法*/
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }
    /*格式化 url 到 path 数组，html 文件都在 server_file 中*/
    sprintf(path, "server_file%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "a.html");
    if (stat(path, &st) == -1) {
        while ((numchars > 0) && strcmp("\n", buf))  
            numchars = executing(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/a.html");
      if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH)    )
          cgi = 1;
      if (!cgi)
          server_to_client(client, path);
      else
          running_cgi(client, path, method, query_string);
    }
    close(client);
}

void running_cgi(int client, const char *path, const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
        while ((numchars > 0) && strcmp("\n", buf))  
            numchars = executing(client, buf, sizeof(buf));
    else    
    {
        numchars = executing(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = executing(client, buf, sizeof(buf));
        }
        if (content_length == -1) {
            bad_req(client);
            return;
        }
    }
    /* 正确，HTTP 状态码 200 */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    if (pipe(cgi_output) < 0) {
        error_cgi(client);
        return;
    }
    if (pipe(cgi_input) < 0) {
        error_cgi(client);
        return;
    }
    if ((pid = fork()) < 0 ) {
        error_cgi(client);
        return;
    }
    if (pid == 0) 
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];
		
        dup2(cgi_output[1], 1);
        dup2(cgi_input[0], 0);
        close(cgi_output[0]);
        close(cgi_input[1]);
        /*设置 request_method 的环境变量*/
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, path, NULL);
        exit(0);
    } else {   
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);
        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

void read_socket(int client, FILE *resource)
{
    char buf[1024];
	
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

void server_to_client(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  
        numchars = executing(client, buf, sizeof(buf));
	
    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        http_to_socket(client, filename);
        read_socket(client, resource);
    }
    fclose(resource);
}

void http_to_socket(int client, const char *filename)
{
    char buf[1024];
    (void)filename; 
	
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

int executing(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';
    return(i);
}

void bad_req(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>你访问的页面域名不存在或者请求错误! ");
    send(client, buf, sizeof(buf), 0);
}

void error_cgi(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>执行CGI时发生错误！\r\n");
    send(client, buf, strlen(buf), 0);
}

void errormessage_die(const char *sc)
{
    perror(sc);
    exit(1);
}

void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><div align='center' style='font-size:50px'>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<p><strong style='color:red;'>404</strong></p>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<strong>The Page Not Found </strong>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</div></BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void no_support_method(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

int main(void)
{
    int server_sock = -1;
    u_short port = 0;
    int client_sock = -1;
    struct sockaddr_in client_name;
    int client_name_len = sizeof(client_name);
    pthread_t newthread;

    server_sock = init_socket(&port);
    printf("服务器运行端口： %d\n", port);

    while (1)
    {
        client_sock = accept(server_sock,(struct sockaddr *)&client_name,(void *)&client_name_len);
        if (client_sock == -1)
            errormessage_die("accept");
        /*派生新线程用 listen_req 函数处理新请求*/
        /* listen_req(client_sock); */
        if (pthread_create(&newthread , NULL,(void *)listen_req,(void *)client_sock) != 0)
            perror("pthread_create");
    }
    close(server_sock);
    return(0);
}



