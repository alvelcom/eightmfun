#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BACK_LOG   -1
#define MAX_EVENTS 1000

#define RCV_BUF    2048

typedef enum qtype
{
    T_UNDEF = 0,
    T_GET0  = 1,
    T_GET1  = 2,
    T_KA    = 4,
    T_CLOSE = 8,

    T_UNSUPPORTED = 128
} qtype_t;

/*
 * Parsing states enum
 */
typedef enum state
{
    S_ = 1,
    S_G,
    S_GE,
    S_GET,
    S_URL, 
    S_URL_,
    S_H,
    S_HT,
    S_HTT,
    S_HTTP,
    S_HTTP_,
    S_HTTP_1,
    S_HTTP_1_,
    S_HTTP_1_X, 
    S_HTTP_1_Xr,
    S_NEWLINE,
    S_KEY,
    S_KEY_, 
    S_VALUE,
    S_VALUEr, 
    S_CVALUE, 
    S_CVALUEr, 
    S_R,
    S_RN,

    S_ERROR,
    S_LAST
} state_t;

typedef struct zstr
{
    char    *str;
    ssize_t size;
    ssize_t pos;
} zstr_t;

/*
 * struct mydata
 *
 * We pass it to epoll as user-defined payload.
 */
typedef struct mydata 
{
    int     fd;
    state_t state;
    zstr_t  filename;
    zstr_t  buf;

    /*
     * Really don't know why we should keep double-ended queue...
     * Maybe someone will implement comet/websockets and 
     * make one another chat-room-server? :)
     */
    struct  mydata *next, *prev;

    /*
     * qtype_t seems to be 8bit length
     * -- escape padding
     */
    qtype_t type;

} mydata_t;

/*
 * Head of DEQ
 */
mydata_t *head;

/*
 * Some finite state machine we use for parsing http.
 *
 * State transition table here
 */
state_t fsm[S_LAST][256];

/*
 * Forward declarations
 */
void init_fsm(void);
void handle_listen(int epollfd, int listen_fd);
int handle_worker(mydata_t *);
int answer_http(mydata_t *ptr);

/*
 * Macro for appending one character to the tail of zstr
 */
#define APPEND(c, zs)                                       \
    do {                                                    \
        if ((zs).pos + 1 >= (zs).size) {                    \
            if (0 == (zs).size) (zs).size = 64;             \
            (zs).str = realloc((zs).str, (zs).size *= 2);   \
        }                                                   \
        (zs).str[(zs).pos++] = (c);                         \
    } while (0);

int
error(int exit_code, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);

   fprintf(stderr, "\n==ERROR REPORT==EXITING WITH %d, errno = %d\n", exit_code, errno);
   perror("==ERROR");
   vfprintf(stderr, fmt, ap);
   fprintf(stderr, "\n");

   va_end(ap);

   /*
    * If we have a non-zero exit code
    * then we should exit
    */
   if (exit_code)
       exit(exit_code);

   return 0;
}

int
main(int argc, char **argv) 
{
    int                 listen_s, conn_s;
    int                 epollfd, nfsd;
    int                 n;
    socklen_t           socklen;
    struct sockaddr_in  addr;
    struct epoll_event  ev, events[MAX_EVENTS];

    /*
     * Initialize http parser fsm
     */
    init_fsm();

    /*
     * Create socket
     */
    if (0 == (listen_s = socket(AF_INET, SOCK_STREAM, 0)))
        error(1, "Bad socket call");

    /*
     * Bind socket
     */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(9999);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen              = sizeof(addr);

    if (bind(listen_s, (struct sockaddr *) &addr, socklen))
        error(2, "Bad bind call");

    /*
     * And listen
     */
    if (-1 == listen(listen_s, BACK_LOG))
        error(2, "Bad listen call");

    /*
     * Set up epoll
     */
    if (-1 == (epollfd = epoll_create(10))) /* See NOTES of man epoll_create */
        error(3, "Bad epoll_create call");

    /*
     * Attach socket to epoll
     */
    ev.events   = EPOLLIN;  /* We wait for avail. read(2) operations */
    ev.data.ptr = 0; 
    if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_s, &ev))
        error(4, "Bad epoll_ctl call");

    for (;;)
    {
        /*
         * Wait for a new event
         * The last argument to epoll_wait is timeout (in ms)
         */
        if (-1 == (nfsd = epoll_wait(epollfd, events, MAX_EVENTS, -1)))
            error(5, "Bad epoll_wait call");

        /* 
         * The epoll_wait returns the number of events
         * which have been place to occur.
         *
         * So we need to track them all
         */
        
        for (n = 0; n < nfsd; ++n)
        {
            /*
             * Is event occurs on listen socket?
             * Let's accept it!
             */
            if (0 == events[n].data.ptr)
                handle_listen(epollfd, listen_s);

            /*
             * Event occurs on worker socket
             */
            else
                handle_worker(events[n].data.ptr);
        }
    }

    return 0;
}

void
handle_listen(int epollfd, int listen_fd)
{
    struct epoll_event  ev;
    socklen_t           socklen;
    struct sockaddr_in  addr;
    struct mydata       *data;
    int                 conn_s, flags;

    if (-1 == (conn_s = accept(listen_fd,
                    (struct sockaddr *) &addr, &socklen)))
        error(6, "Bad accept call");
    
    /*
     * Using epoll and blocking sockets seems to be not really
     * good idea.
     */
    if (-1 == fcntl(conn_s, F_SETFL, O_NONBLOCK))
        error(7, "Bad fcntl call: set conn_s O_NONBLOCK flag");

    flags = 1;
    setsockopt(conn_s, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags));

    /*
     * Now we have one more fd. What will we do with it?
     * We feed up it to epoll! 
     */
    data = calloc(1, sizeof(mydata_t));
    data->fd = conn_s;
    data->state = S_;

    /*
     * Insert to deq
     */
    if (data->prev = head)
        head->next = data;
    head = data;

    /*
     * See man epoll(4) about Level-Triggered and Edge-Triggered
     */
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = data;
    if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_s, &ev))
        error(7, "Bad epoll_ctl call on conn_s socket");
}

int
handle_worker(mydata_t *ptr)
{
    char        buf[RCV_BUF];
    ssize_t     size;
    char        *pos;
    state_t     oldstate, newstate;

    newstate = ptr->state;

    /*
     * Reader cycle
     */
    while (size = read(ptr->fd, buf, RCV_BUF))
    {
        if (-1 == size)
        {
            /*
             * I want to catch up the situation when
             * no available data in socket exists... ughm...
             *
             * How to do it right?
             */
            if (11 == errno)
                break;

            error(0, "Bad read call");
            goto close_conn;
        }

        for (pos = buf; pos < buf + size; ++pos)
        {
            /*
             * Lookup for a new state
             */
            oldstate = newstate;
            newstate = fsm[oldstate][(unsigned char)*pos];

            switch (newstate)
            {
            case S_URL:
                if (oldstate == newstate)
                {
                    APPEND(*pos, ptr->filename);
                }
                else
                {
                    ptr->type = T_GET0;
                    ptr->filename.pos = 0;
                }
                break;
            case S_URL_:
                APPEND(0, ptr->filename);
                break;
            case S_HTTP_1_X:
                if ('1' == *pos)
                    ptr->type = T_GET1;
                break;
            case S_NEWLINE:
                ptr->buf.pos = 0;
                break;
            case S_KEY:
                APPEND(tolower(*pos), ptr->buf);
                break;
            case S_KEY_:
                APPEND(0, ptr->buf);
                if (0 == strcmp("connection", ptr->buf.str))
                    newstate = S_CVALUE;
                ptr->buf.pos = 0;
                break;
            case S_CVALUE:
                APPEND(tolower(*pos), ptr->buf);
                break;
            case S_CVALUEr:
                APPEND(0, ptr->buf);
                if (0 == strcmp("keep-alive", ptr->buf.str))
                    ptr->type |= T_KA;
                else if (0 == strcmp("close", ptr->buf.str))
                    ptr->type |= T_CLOSE;
                break;
            default:
                break;
            }

            if (S_ERROR == newstate)
                goto bad_method;

            /*
             * if the final state reached
             * then we should generate answer!
             */
            if (S_RN == newstate)
            {
                /*
                 * If answer_http return zero then we should
                 * reset current state and wait for next query.
                 *
                 * Otherwise we close connection
                 */
                if (answer_http(ptr))
                    goto close_conn;
                else
                {
                    ptr->type = T_UNDEF;
                    ptr->buf.pos = 0;
                    ptr->filename.pos = 0;
                    newstate = S_;
                }
            }
        }
    }

    /*
     * Save the state
     */
    ptr->state = newstate;

    return 0;
bad_method:

    if (ptr->type & T_GET0)
    {
        const char ans0[] =
            "HTTP/1.0 400 Bad Request\r\n"
            "Server: eightmfun/0.0.0.0.0.0.1\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Bad Request\r\n";
        if (-1 == write(ptr->fd, ans0, sizeof(ans0)))
            error(0, "Bad write");
    }
    else
    {
        const char ans1[] =
            "HTTP/1.1 400 Bad Request\r\n"
            "Server: eightmfun/0.0.0.0.0.0.1\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Bad Request\r\n";
        if (-1 == write(ptr->fd, ans1, sizeof(ans1)))
            error(0, "Bad write");
    }

close_conn:
    /*
     * Remove from deq
     */
    if (ptr->next)
        ptr->next->prev = ptr->prev;
    if (ptr->prev)
        ptr->prev->next = ptr->next;
    
    if (head == ptr)
        head = ptr->prev;

    /*
     * Close the connection and free the memory
     */
    close(ptr->fd);
    if (ptr->filename.str) free(ptr->filename.str);
    if (ptr->buf.str)      free(ptr->buf.str);
    free(ptr);

    return 0;
}

/**********/
void
fill(state_t state, state_t to_state)
{
    int i;
    for (i = 0; i < 256; i++)
        fsm[state][i] = to_state;
}

void
init_fsm(void)
{
    int i;
    for (i = 0; i < S_LAST; i++)
        fill(S_, S_ERROR);

    fsm[S_         ]['G' ] = S_G;
    fsm[S_G        ]['E' ] = S_GE;
    fsm[S_GE       ]['T' ] = S_GET;
    fsm[S_GET      ][' ' ] = S_URL;

    fill(S_URL,              S_URL);
    fsm[S_URL      ][' ' ] = S_URL_;

    fsm[S_URL_     ]['H' ] = S_H;
    fsm[S_H        ]['T' ] = S_HT;
    fsm[S_HT       ]['T' ] = S_HTT;
    fsm[S_HTT      ]['P' ] = S_HTTP;
    fsm[S_HTTP     ]['/' ] = S_HTTP_;
    fsm[S_HTTP_    ]['1' ] = S_HTTP_1;
    fsm[S_HTTP_1   ]['.' ] = S_HTTP_1_;
    fsm[S_HTTP_1_  ]['0' ] = S_HTTP_1_X;
    fsm[S_HTTP_1_  ]['1' ] = S_HTTP_1_X;
    fsm[S_HTTP_1_X ]['\r'] = S_HTTP_1_Xr;
    fsm[S_HTTP_1_Xr]['\n'] = S_NEWLINE;

    fill(S_NEWLINE,          S_KEY);
    fsm[S_NEWLINE  ]['\r'] = S_R;
    fsm[S_R        ]['\n'] = S_RN; /* the end */

    fill(S_KEY,              S_KEY);
    fsm[S_KEY      ][':' ] = S_KEY_;
    fsm[S_KEY_     ][' ' ] = S_VALUE;
    /* but if KEY == 'Connection' then newstate = S_CVALUE */
    fill(S_VALUE,            S_VALUE);
    fill(S_CVALUE,           S_CVALUE);
    fsm[S_VALUE    ]['\r'] = S_VALUEr;
    fsm[S_CVALUE   ]['\r'] = S_CVALUEr;
    fsm[S_VALUEr   ]['\n'] = S_NEWLINE;
    fsm[S_CVALUEr  ]['\n'] = S_NEWLINE;
}

/**********/
int
answer_http(mydata_t *ptr)
{
    char        answer[2048];
    char        *pos;
    char        *filename;
    char        *ans;
    int         filed;
    struct      stat stat;
    ssize_t     content_length;
    char        closeafter;

    /*
     * Build answer headers in 
     *   char answer[2048];
     * After appending don't forget to move `pos` forward.
     *
     * If requested file exists then we should send it.
     * HINT: try to request /../../../../../../etc/passwd
     */

    closeafter = 0;
    pos = answer;

    /*
     * Proto version
     */
    pos = stpcpy(pos, ptr->type & T_GET0 ? "HTTP/1.0 " : "HTTP/1.1 ");

    /*
     * Here we decide what to do.
     * Maybe we should send a file?
     * Maybe just an index string?
     */
    filename = ptr->filename.str + 1;
    filed = -1;
    if (*filename) /* we have filename (i.e. filename == "") */
    {
        /*
         * Trying to open file
         *
         * At this moment we don't bother about permissions 
         * and other stuff. Just think that if it un-openable
         * that it doesnot exists :)
         */
        if (-1 == (filed = open(filename, O_RDONLY)))
        {
            pos = stpcpy(pos, "404 File not found\r\n");
            ans = "File not found\r\n";
            content_length = strlen(ans);
        }
        /*
         * We also need know about file size
         */
        else if (-1 == fstat(filed, &stat))
        {
            error(0, "Bad fstat call");
            return 1;
        }
        else
        {
            pos = stpcpy(pos, "200 OK\r\n");
            content_length = stat.st_size;
        }
    }
    else
    {
        /*
         * "GET / HTTP" requested
         * Just print an index string
         */
        pos = stpcpy(pos, "200 OK\r\n");
        ans = "Some page here\r\n";
        content_length = strlen(ans);
    }

    /*
     * Sing the server
     */
    pos = stpcpy(pos, "Server: eightmfun/0.0.0.0.0.1\r\nContent-Type: ");

    /*
     * Spec. content type ...
     */
    if (-1 != filed)
        pos = stpcpy(pos, "application/octet-stream\r\n");
    else
        pos = stpcpy(pos, "text/plain\r\n");

    /*
     * ... and content length.
     *
     * Since this super server doesnot support chunked encoding
     * we must supply content length.
     */
    pos += sprintf(pos, "Content-Length: %zu\r\n", content_length);

    /*
     * What we will do with connection after sending answer?
     * Browser should wondering after.
     */
    closeafter = 1;
    if (ptr->type & T_KA || ptr->type & T_GET1)
        closeafter = 0;
    if (ptr->type & T_CLOSE)
        closeafter = 1;

    pos = stpcpy(pos, "Connection: ");
    pos = stpcpy(pos, closeafter ? "close \r\n" : "keep-alive\r\n");
    
    /*
     * Empty line -- end of headers here
     */
    pos = stpcpy(pos, "\r\n");

    /*
     * We have a little body... Send it with headers
     */
    if (-1 == filed)
        pos = stpcpy(pos, ans);

    /*
     * Sending headers (and a little body possibly) itself
     */
    if (-1 == write(ptr->fd, answer, pos - answer))
        error(0, "Bad write");

    /*
     * Here we sending file over net.
     *
     * We using sendfile (works like a pump from one fd to another)
     */
    if (-1 != filed)
    {
        sendfile(ptr->fd, filed, NULL, stat.st_size);
        close(filed);
    }
    return closeafter;
}

