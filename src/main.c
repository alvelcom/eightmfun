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

typedef enum state
{
    S_ = 1,
    S_G,
    S_GE,
    S_GET,
    S_URL, //
    S_URL_,
    S_H,
    S_HT,
    S_HTT,
    S_HTTP,
    S_HTTP_,
    S_HTTP_1,
    S_HTTP_1_,
    S_HTTP_1_X, //
    S_HTTP_1_Xr,
    S_NEWLINE,
    S_KEY,
    S_KEY_, //
    S_VALUE,
    S_VALUEr, 
    S_CVALUE, //
    S_CVALUEr, //
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

typedef struct mydata 
{
    int     fd;
    state_t state;
    zstr_t  filename;
    zstr_t  buf;

    struct  mydata *next, *prev;

    qtype_t type;

} mydata_t;

mydata_t *head;

state_t automata[S_LAST][256];

// forwards
void init_automata();
int handle(mydata_t *);
int answerHTTP(mydata_t *ptr);

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
    * if have non-zero exit code
    * we should exit
    */
   if (exit_code)
       exit(exit_code);

   return 0;
}

int
main() 
{
    int                 listen_s, conn_s;
    int                 epollfd, nfsd, flags;
    int                 n;
    socklen_t           socklen;
    struct sockaddr_in  addr;
    struct epoll_event  ev, events[MAX_EVENTS];
    struct mydata       *data;

    // Init http parser automata
    init_automata();

    // Create socket
    if (0 == (listen_s = socket(AF_INET, SOCK_STREAM, 0)))
        error(1, "Bad socket call");

    // Bind socket
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(9999);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen              = sizeof(addr);

    if (bind(listen_s, (struct sockaddr *) &addr, socklen))
        error(2, "Bad bind call");

    // And listen
    if (-1 == listen(listen_s, BACK_LOG))
        error(2, "Bad listen call");

    // Set up epoll
    if (-1 == (epollfd = epoll_create(10))) /* See NOTES of man epoll_create */
        error(3, "Bad epoll_create call");

    // Attach socket to epoll
    ev.events   = EPOLLIN;  // We wait for avail. read(2) operations
    ev.data.ptr = 0; 
    if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_s, &ev))
        error(4, "Bad epoll_ctl call");

    for (;;)
    {
        // Wait for a new event
        // The last argument to epoll_wait is timeout (in ms)
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
            {
                if (-1 == (conn_s = accept(listen_s,
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
                /*
                 * See man epoll(4) about Level-Triggered and Edge-Triggered
                 */
                ev.events = EPOLLIN | EPOLLET;

                data = calloc(1, sizeof(mydata_t));
                data->fd = conn_s;
                data->state = S_;
                if (data->prev = head)
                    head->next = data;
                head = data;

                ev.data.ptr = data;
                if (-1 == epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_s, &ev))
                    error(7, "Bad epoll_ctl call on conn_s socket");
            }
            else
            {
                /*
                 * Event occurs on worker socket
                 */
                handle(events[n].data.ptr);
            }
        }
    }

    return 0;
}

int
handle(mydata_t *ptr)
{
    char        buf[RCV_BUF];
    ssize_t     size;
    char        *pos;
    state_t     oldstate, newstate;

    newstate = ptr->state;

    while (size = read(ptr->fd, buf, RCV_BUF))
    {
        if (-1 == size)
        {
            if (11 == errno)
                break;
            error(0, "Bad read call");
            goto close_conn;
        }

        for (pos = buf; pos < buf + size; ++pos)
        {
            oldstate = newstate;
            newstate = automata[oldstate][(unsigned char)*pos];

            if (S_GET == oldstate && S_URL == newstate)
            {
                ptr->type = T_GET0;
                ptr->filename.pos = 0;
            }
            
            switch (newstate)
            {
            case S_URL:
                if (oldstate == newstate)
                    APPEND(*pos, ptr->filename);
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

            //printf("%d[%c = %02x] -> %d\n", oldstate, *pos, *pos, newstate);
            if (S_RN == newstate)
            {
                if (answerHTTP(ptr))
                    goto close_conn;
                else
                    newstate = S_;
            }
        }
    }

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
    if (ptr->next)
        ptr->next->prev = ptr->prev;
    if (ptr->prev)
        ptr->prev->next = ptr->next;
    
    if (head == ptr)
        head = ptr->prev;

    close(ptr->fd);
    if (ptr->filename.str) free(ptr->filename.str);
    if (ptr->buf.str)      free(ptr->buf.str);
    free(ptr);

    return 0;
}

void fill(state_t state, state_t to_state)
{
    int i;
    for (i = 0; i < 256; i++)
        automata[state][i] = to_state;
}

void init_automata()
{
    int i;
    for (i = 0; i < S_LAST; i++)
        fill(S_, S_ERROR);

    automata[S_         ]['G' ] = S_G;
    automata[S_G        ]['E' ] = S_GE;
    automata[S_GE       ]['T' ] = S_GET;
    automata[S_GET      ][' ' ] = S_URL;

    fill(    S_URL,               S_URL);
    automata[S_URL      ][' ' ] = S_URL_;

    automata[S_URL_     ]['H' ] = S_H;
    automata[S_H        ]['T' ] = S_HT;
    automata[S_HT       ]['T' ] = S_HTT;
    automata[S_HTT      ]['P' ] = S_HTTP;
    automata[S_HTTP     ]['/' ] = S_HTTP_;
    automata[S_HTTP_    ]['1' ] = S_HTTP_1;
    automata[S_HTTP_1   ]['.' ] = S_HTTP_1_;
    automata[S_HTTP_1_  ]['0' ] = S_HTTP_1_X;
    automata[S_HTTP_1_  ]['1' ] = S_HTTP_1_X;
    automata[S_HTTP_1_X ]['\r'] = S_HTTP_1_Xr;
    automata[S_HTTP_1_Xr]['\n'] = S_NEWLINE;

    fill(    S_NEWLINE,           S_KEY);
    automata[S_NEWLINE  ]['\r'] = S_R;
    automata[S_R        ]['\n'] = S_RN; // the end

    fill(    S_KEY,               S_KEY);
    automata[S_KEY      ][':' ] = S_KEY_;
    automata[S_KEY_     ][' ' ] = S_VALUE;
    // but if KEY == 'Connection' then newstate = S_CVALUE
    fill(    S_VALUE,             S_VALUE);
    fill(    S_CVALUE,            S_CVALUE);
    automata[S_VALUE    ]['\r'] = S_VALUEr;
    automata[S_CVALUE   ]['\r'] = S_CVALUEr;
    automata[S_VALUEr   ]['\n'] = S_NEWLINE;
    automata[S_CVALUEr  ]['\n'] = S_NEWLINE;
}

/////
int answerHTTP(mydata_t *ptr)
{
    char        answer[2048];
    char        *pos;
    char        *filename;
    char        *ans;
    int         filed;
    struct      stat stat;
    char        closeafter;

    closeafter = 0;
    pos = answer;

    if (ptr->type & T_GET0)
        strcpy(pos, "HTTP/1.0 ");
    else
        strcpy(pos, "HTTP/1.1 ");

    while (*pos) pos++;

    filename = ptr->filename.str + 1;
    filed = -1;
    if (*filename)
    {
        if (-1 == (filed = open(filename, O_RDONLY)))
        {
            strcpy(pos, "404 File not found\r\n");
            ans = "File not found\r\n";
        }
        else if (-1 == fstat(filed, &stat))
        {
            error(0, "Bad fstat call");
            return 1;
        }
        else
        {
            strcpy(pos, "200 OK\r\n");
        }
    }
    else
    {
        strcpy(pos, "200 OK\r\n");
        ans = "Some page here\r\n";
    }
    while (*pos) pos++;

    strcpy(pos, "Server: eightmfun/0.0.0.0.0.1\r\nContent-Type: ");
    while (*pos) pos++;

    if (-1 != filed)
        strcpy(pos, "application/octet-stream\r\n");
    else
        strcpy(pos, "text/plain\r\n");
    while (*pos) pos++;

    if (-1 != filed)
        sprintf(pos, "Content-Length: %zu\r\n", stat.st_size);
    else
        sprintf(pos, "Content-Length: %zu\r\n", strlen(ans));
    while (*pos) pos++;

    if (ptr->type & T_CLOSE || !(ptr->type & T_GET1))
        strcpy(pos, "Connection: close\r\n"), closeafter = 1;
    else
        strcpy(pos, "Connection: keep-alive\r\n");
    while (*pos) pos++;
    
    strcpy(pos, "\r\n");
    while (*pos) pos++;

    if (-1 == filed)
        strcpy(pos, ans);
    while (*pos) pos++;

    if (-1 == write(ptr->fd, answer, pos - answer))
        error(0, "Bad write");

    if (-1 != filed)
    {
        sendfile(ptr->fd, filed, NULL, stat.st_size);
        close(filed);
    }
    return closeafter;
}
