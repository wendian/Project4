/* csci4061 S2016 Assignment 4
* date: 5/2/16
* names: Wendi An, Ly Nguyen
* (anxxx102, 4638209) (nguy1848, 4551642)
*/
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include "util.h"

#define MAX_THREADS 100
#define MAX_QUEUE_SIZE 100
#define MAX_REQUEST_LENGTH 64
#define MAX_CACHE 100
//Cache will not store anything larger than this many bytes:
#define MAX_STORAGE 131072

int num_dispatcher;
int num_workers;
int queue_length;
char path[1024];
int num_requests = 0;
int cache_size = 0;
//Keeps track of next free cache slot
int cur_cache = 0;
pthread_t disps[MAX_THREADS];
pthread_t works[MAX_THREADS];
int reqs;
struct sigaction intr;
//This lock is used to protect the request queue when being used by dispatcher thread
static pthread_mutex_t nqing = PTHREAD_MUTEX_INITIALIZER;
//This lock is used to protect the request queue when being used by a worker thread
static pthread_mutex_t dqing = PTHREAD_MUTEX_INITIALIZER;
//This lock is used to protect the log file
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
//This lock is used to protect the cache
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

//Condition variable used to wake up a sleeping dispatcher thread that there is space in the queue
static pthread_cond_t queue_space = PTHREAD_COND_INITIALIZER;
//Condition variable used to wake up a sleeping worker thread that there are unfulfilled requests in queue
static pthread_cond_t available_req = PTHREAD_COND_INITIALIZER;


typedef struct request_queue
{
  int m_socket;
  char m_szRequest[MAX_REQUEST_LENGTH];
} request_queue_t;

request_queue_t requests[MAX_QUEUE_SIZE];
int front = 0;
int back = 0;

//Array that keeps track of the number of requests that thread i has fulfilled
int request_count[MAX_THREADS];
int log_file;

//Structure that represents one cache entry and stores all useful information associated the file
typedef struct cache
{
  char name[128];
  char buf[MAX_STORAGE];
  char c_type[10];
  int size;
} cache_t;

//This array represents the entire cache
cache_t cach[MAX_CACHE];

//--------------------------------------------------------------------------

//Dispatcher function that enqueues requests to the request queue and wakes up workers
void * dispatch(void * arg)
{
  int fd;
  char *fileName;
  while (1)
  {
    fd = accept_connection();
    fileName = (char *) malloc(1024);
    if(get_request(fd, fileName) != 0)
    {
      //get_request failed, just repeat loop
      continue;
    }
    pthread_mutex_lock(&nqing);
    while(num_requests == queue_length)
    {
      pthread_cond_wait(&queue_space, &nqing);
    }
    //Enqueueing request to the end of the request queue
    requests[back].m_socket = fd;
    strcpy(requests[back].m_szRequest, fileName);
    back = (back + 1) % queue_length;
    num_requests++;
    pthread_mutex_unlock(&nqing);
    //Signals random sleeping worker that there is an available request in the queue.
    pthread_cond_signal(&available_req);
  }
  return NULL;
}

//Worker thread function that dequeues from the request queue
void * worker(void * arg)
{
  int * tID = (int *) arg;
  char * buf;
  char mess[128];
  char c_type[20];
  char requ[1024];
  char fullpath[1024];
  char err_mess[1024];
  char logs[128];
  struct stat item;
  int nStat, szReq, numread, fd, reqfd, i, hit;
  clock_t start, end;

  while(1)
  {
    pthread_mutex_lock(&dqing);
    if (num_requests == 0)
    {
      pthread_cond_wait(&available_req, &dqing);
    }
    num_requests--;
    //Extracting information about current request
    fd = requests[front].m_socket;
    strcpy(requ, requests[front].m_szRequest);
    front = (front + 1) % queue_length;
    if (num_requests == queue_length - 1)
    {
      //Signaling sleeping dispatcher that there is space in the request queue
      pthread_cond_signal(&queue_space);
    }
    pthread_mutex_unlock(&dqing);
    hit = 0;
    request_count[*tID]++;
    start = clock();

    /*Checking if the request is in stored in the cache. If found in the cache, we grab information from
    the cache instead reading the file in a buffer, extracting the content type, and getting numbytes.
    */
    for (i = 0; i < cache_size; i++)
    {
      if (strcmp(requ, cach[i].name) == 0)
      {
        pthread_mutex_lock(&cache_lock);
        //Using request information stored in cache
        if (return_result(fd, cach[i].c_type, cach[i].buf, cach[i].size) != 0)
        {
          free(buf);
          printf("ERROR returning result\n");
          exit(1);
        }
        end = clock();
        sprintf(logs, "[%d][%d][%d][%s][%d][%dus][HIT]\n", *tID, request_count[*tID], fd, requ, cach[i].size, end - start);
        pthread_mutex_unlock(&cache_lock);
        pthread_mutex_lock(&log_lock);
        if (write(log_file, logs, strlen(logs)) < 0)
        {
          printf("failed to write to log\n");
          exit(1);
        }
        pthread_mutex_unlock(&log_lock);
        hit = 1;
        break;
      }
    }
    if (hit)
    {
      continue;
    }
    //Restart the time for a miss
    start = clock();
    strcpy(fullpath, path);
    //removes the extra "/" at the end of the path
    fullpath[strlen(fullpath) - 1] = '\0';
    strcat(fullpath, requ);
    //Checking if the file exists
  	if(access(fullpath, F_OK) != 0)
  	{
      strcpy(mess, "File not found");
      char final[1024];
      if (return_error(fd, mess) != 0)
      {
        printf("ERROR returning error\n");
        exit(1);
      }
      sprintf(logs, "[%d][%d][%d][%s][%s]\n", *tID, request_count[*tID], fd, requ, mess);
      pthread_mutex_lock(&log_lock);
      if (write(log_file, logs, strlen(logs)) < 0)
      {
        printf("failed to write to log\n");
        exit(1);
      }
      pthread_mutex_unlock(&log_lock);
      continue;
  	}
    //Extracting information from current request to use return_result
    nStat = stat(fullpath, &item);
    szReq = item.st_size;
    if (strstr(requ, ".html") != NULL)
    {
      strcpy(c_type, "text/html");
    }
    else if (strstr(requ, ".gif") != NULL)
    {
      strcpy(c_type, "image/gif");
    }
    else if (strstr(requ, ".jpeg") != NULL)
    {
      strcpy(c_type, "image/jpeg");
    }
    else
    {
      strcpy(c_type, "text/plain");
    }
    buf = (char *) malloc(szReq);
    //opening and reading entire file to buffer
    reqfd = open(fullpath, O_RDONLY);
    numread = read(reqfd, buf, szReq);
    if (numread != szReq)
    {
      printf("Reading file failed\n");
      exit(1);
    }
    if (return_result(fd, c_type, buf, szReq) != 0)
    {
      free(buf);
      printf("ERROR returning result\n");
      exit(1);
    }
    end = clock();
    if (cache_size > 0)
    {
      sprintf(logs, "[%d][%d][%d][%s][%d][%dus][MISS]\n", *tID, request_count[*tID], fd, requ, szReq, end - start);
    }
    else
    {
      sprintf(logs, "[%d][%d][%d][%s][%d][%dus]\n", *tID, request_count[*tID], fd, requ, szReq, end - start);
    }
    pthread_mutex_lock(&log_lock);
    if (write(log_file, logs, strlen(logs)) < 0)
    {
      printf("failed to write to log\n");
      exit(1);
    }
    pthread_mutex_unlock(&log_lock);
    if ((cache_size > 0)  && (szReq < MAX_STORAGE))
    {
      //Adding request information to cache from a miss
      pthread_mutex_lock(&cache_lock);
      strcpy(cach[cur_cache].name, requ);
      strcpy(cach[cur_cache].buf, buf);
      strcpy(cach[cur_cache].c_type, c_type);
      cach[cur_cache].size = szReq;
      cur_cache = (cur_cache + 1) % cache_size;
      pthread_mutex_unlock(&cache_lock);
    }
    free(buf);
  }
}

//This function is used to clean up all the threads when the program is exited

void cleanup(int sig)
{
  int i;
  for (i = 0; i < num_workers; i++)
  {
    if (pthread_cancel(works[i]) != 0)
    {
      printf("failed to close worker thread\n");
      exit(1);
    }
  }
  for (i = 0; i < num_dispatcher; i++)
  {
    if (pthread_cancel(disps[i]) != 0)
    {
      printf("failed to close dispatcher thread\n");
      exit(1);
    }
  }
  if (close(log_file) < 0)
  {
    printf("failed to close file\n");
    exit(1);
  }
  printf("Exiting..\n");
  exit(0);
}


int main(int argc, char **argv)
{
  if(argc != 6 && argc != 7)
  {
    printf("usage: %s port path num_dispatcher num_workers queue_length [cache_size]\n", argv[0]);
    return -1;
  }
  printf("Awaiting requests\n");
  //Unpacking input
  int port = atoi(argv[1]);
  strcpy(path, argv[2]);
  num_dispatcher = atoi(argv[3]);
  if (num_dispatcher > MAX_THREADS)
  {
    printf("num_dispatchers scaled down to %d\n", MAX_THREADS);
    num_dispatcher = MAX_THREADS;
  }
  num_workers = atoi(argv[4]);
  if (num_workers > MAX_THREADS)
  {
    printf("num_workers scaled down to %d\n", MAX_THREADS);
    num_workers = MAX_THREADS;
  }
  queue_length = atoi(argv[5]);
  if (queue_length > MAX_QUEUE_SIZE)
  {
    printf("queue_length scaled down to %d\n", MAX_QUEUE_SIZE);
    queue_length = MAX_QUEUE_SIZE;
  }
  if (argc == 7)
  {
    cache_size = atoi(argv[6]);
    if (cache_size > MAX_CACHE)
    {
      printf("cache_size scaled down to %d\n", MAX_CACHE);
      cache_size = MAX_CACHE;
    }
    int i;
    for (i = 0; i < cache_size; i++)
    {
      cach[i].name[0] = '\0';
    }
  }

  log_file =  open("web_server_log", O_CREAT | O_RDWR | O_TRUNC, 0666);
  if (log_file < 0)
  {
    printf("Couldn't make log\n");
    exit(1);
  }

  intr.sa_handler = cleanup;
  intr.sa_flags = 0;
  if (sigaction(SIGINT,&intr,NULL) != 0)
  {
    perror("sig int handler failed to initialize");
    exit(1);
  }
  init(port);

  int i;
  for(i = 0; i < num_dispatcher; i++)
  {
    if(pthread_create(&disps[i], NULL, dispatch, NULL) != 0)
    {
      fprintf(stderr,"Error creating dispatcher thread\n");
      exit(1);
    }
  }

  int threadID[num_workers];
  for (i = 0; i < num_workers; i++)
  {
    threadID[i] = i;
    if(pthread_create(&works[i], NULL, worker, (void *)&threadID[i]) != 0)
    {
      fprintf(stderr,"Error creating worker thread\n");
      exit(1);
    }
  }
  while (1) pause();
  return 0;
}
