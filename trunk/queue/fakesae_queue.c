/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * fakesae_queue.c
 *
 * Copyright (C) 2012 - Dr.NP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/epoll.h>

#include <hiredis/hiredis.h>
#include <curl/curl.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "fakesae_queue.h"

/* Globals */
int log_fd = 0;

int stop_signal = 0;
redisContext *re = NULL;
struct csm_setting settings;
struct static_worker worker_list[WORKERS];
struct queue_t *queue_list = NULL;
size_t queue_list_size = 0;
size_t nqueues = 0;

pthread_mutex_t redis_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_write_lock = PTHREAD_MUTEX_INITIALIZER;

/* Function definations */
int proc_daemonize();
int error_log(const char *);
int init_redis();
int init_workers();
int is_worker_free(int);
int next_static_worker();
int read_queue_list();
char * read_queue(const char *);

void sig_handler(const int);
void set_sig(void (*)(int));

void * parallel_proc(void *);
int main_loop();
int jhash(const char *, size_t);

FILE *data_hole;

char **task_blocks = NULL;
int *free_task_blocks = NULL;
size_t ntasks = 0;
size_t nfreetasks = 0;

int push_task(const char *);
int pop_task(int);

int logger(const char *, ...);

/* * * Main * * */
int main(int argc, char **argv)
{
	// Init settings
	settings.redis_host = DEFAULT_REDIS_HOST;
	settings.redis_port = DEFAULT_REDIS_PORT;
	settings.redis_db = DEFAULT_REDIS_DB;
	settings.redis_password = NULL;
	settings.daemonize = DEFAULT_DAEMONIZE;
	settings.queue_consummer_sleep = DEFAULT_QUEUE_CONSUMMER_SLEEP;
	settings.queue_direction = DEFAULT_QUEUE_DIRECTION;
	settings.error_log_file = DEFAULT_ERROR_LOG_FILE;
	settings.conf_file = DEFAULT_CONF_FILE;
	
	int c;
	
	while (-1 != (c = getopt(argc, argv, "h:p:D:a:s:c:dl:q:H")))
	{
		switch (c)
		{
			case 'H' : 
				// Display help information
				printf("\n");
				printf("FakeSAE.queue %s\n", APP_VERSION);
				printf("By Dr.NP <zhanghao@9173.com>\n");
				printf("******************************************************************************\n");
				printf("Useage : fakesae_queue [OPTIONS]\n");
				printf("OPTIONS:\n");
				printf("\t-h : Redis server host. (Default : %s)\n", DEFAULT_REDIS_HOST);
				printf("\t-p : Redis server port. (Default : %d)\n", DEFAULT_REDIS_PORT);
				printf("\t-D : Redis Database ID. (Default : %d)\n", DEFAULT_REDIS_DB);
				printf("\t-a : Redis authentication password.\n");
				printf("\t-d : Run as a background daemon process.\n");
				printf("\t-s : Empty queue read delay time in millisecond. (Default %d)\n", DEFAULT_QUEUE_CONSUMMER_SLEEP);
				printf("\t-c : Configuration(Queue list) file. (Default : %s)\n", DEFAULT_CONF_FILE);
				printf("\t-l : Error log filename. (Default : %s)\n", DEFAULT_ERROR_LOG_FILE);
				printf("\t-q : Queue direction. (1 for left read / 2 for right read) (Default : %d)\n", DEFAULT_QUEUE_DIRECTION);
				printf("\t-H : Display this help topic.\n\n");

				return RTN_SUCCESS;
				
				break;
			
			case 'h' : 
				// Redis server host
				settings.redis_host = strdup(optarg);
				break;
			
			case 'p' : 
				// Redis server port
				settings.redis_port = atoi(optarg);
				break;

			case 'D' : 
				// Redis database
				settings.redis_db = atoi(optarg);
				break;
			
			case 'a' : 
				// Redis password
				settings.redis_password = strdup(optarg);
				break;
			
			case 'd' : 
				// Daemonize
				settings.daemonize = 1;
				break;
			
			case 's' : 
				// Null queue sleep time(in millisecond)
				settings.queue_consummer_sleep = atoi(optarg);
				break;

			case 'q' : 
				// Queue direction
				settings.queue_direction = atoi(optarg);
				break;

			case 'c' : 
				// Queue list
				settings.conf_file = strdup(optarg);
				break;
			
			case 'l' : 
				// Error log file path
				settings.error_log_file = strdup(optarg);
				break;
			
			default : 
				// What the fuck...
				break;
		}
	}

	if (1 == settings.daemonize)
	{
		proc_daemonize();
	}
	
	data_hole = fopen("/dev/null", "w");
	set_sig(sig_handler);
	init_redis();
	init_workers();
	
	logger("Server started.");
	// Initializations
	read_queue_list();
	main_loop();
	fclose(data_hole);

	return RTN_SUCCESS;
}

// Take me background. Be a daemon is a happy thing.
int proc_daemonize()
{
	int fd;

	switch (fork())
	{
		case -1 : 
			return RTN_FATAL;

		case 0 : 
			break;

		default : 
			_exit(RTN_SUCCESS);
	}

	if (-1 == setsid())
	{
		return RTN_FATAL;
	}

	fd = open("/dev/null", O_RDWR, 0);
	if (fd)
	{
		(void) dup2(fd, STDIN_FILENO);
		(void) dup2(fd, STDOUT_FILENO);
		(void) dup2(fd, STDERR_FILENO);

		if (fd > STDERR_FILENO)
		{
			(void) close(fd);
		}
	}

	return RTN_SUCCESS;
}

// Connect and initialize redis server
int init_redis()
{
	redisReply *reply = NULL;
	re = redisConnect(settings.redis_host, settings.redis_port);
	if (re->err)
	{
		// Connection failed
		fprintf(stderr, "Cannot connect to redis server %s:%d -  %s\n", settings.redis_host, settings.redis_port, re->errstr);
		exit(RTN_ERROR_REDIS_CONNECTION);
	}

	if (settings.redis_password)
	{
		reply = redisCommand(re, "AUTH %s", settings.redis_password);
		if (!reply || 0 != strcmp(reply->str, "OK"))
		{
			fprintf(stderr, "Redis server authenticate failed : %s\n", (reply) ? reply->str : "Unknown");
			logger("Redis server authenticate failed");
			_exit(RTN_ERROR_GENERAL);
		}
	}
	
	reply = redisCommand(re, "SELECT %d", settings.redis_db);
	if (!reply || 0 != strcmp(reply->str, "OK"))
	{
		fprintf(stderr, "Redis server error : %s\n", (reply) ? reply->str : "Connection error");
		_exit(RTN_ERROR_REDIS_CONNECTION);
	}
	
	return RTN_SUCCESS;
}

// Init static thread pool
int init_workers()
{
	int n;
	int pipe_fds[2];

	memset(worker_list, 0, sizeof(struct static_worker) * WORKERS);
	for (n = 0; n < WORKERS; n ++)
	{
		worker_list[n].worker_id = n;
		worker_list[n].epoll_fd = epoll_create(EPOLL_WAIT_FDS);
		if (-1 == worker_list[n].epoll_fd)
		{
			fprintf(stderr, "Epoll create failed.\n");

			exit(RTN_ERROR_EPOLL);
		}

		if (0 != pipe(pipe_fds))
		{
			fprintf(stderr, "Pipe error.\n");
			exit(RTN_ERROR_PIPE);
		}
		worker_list[n].notify_recv_fd = pipe_fds[0];
		worker_list[n].notify_send_fd = pipe_fds[1];

		worker_list[n].ev.data.fd = worker_list[n].notify_recv_fd;
		worker_list[n].ev.events = EPOLLIN;
		epoll_ctl(worker_list[n].epoll_fd, EPOLL_CTL_ADD, worker_list[n].notify_recv_fd, &worker_list[n].ev);
		
		pthread_attr_init(&worker_list[n].attr);
		
		if (0 != pthread_create(&worker_list[n].thread, &worker_list[n].attr, parallel_proc, (void *) &worker_list[n]))
		{
			fprintf(stderr, "Cannot create static worker thread.\n");

			exit(RTN_ERROR_THREAD);
		}
	}

	return n;
}

// If worker free now
int is_worker_free(int wid)
{
	if (wid < 0 || wid >= WORKERS)
	{
		return 0;
	}
	
	int is_free = 0;
	pthread_mutex_lock(&worker_list[wid].status_lock);
	is_free = worker_list[wid].is_free;
	pthread_mutex_unlock(&worker_list[wid].status_lock);
	
	return is_free;
}

// Find a free static worker
int next_static_worker()
{
	static int id = 0;
	if (++ id >= WORKERS)
	{
		id = 0;
	}
	
	return id;
}

// Read queue list from redis server
int read_queue_list()
{
	int queue_type = 0;
	int l = 0, i = 0;
	//redisReply *reply = NULL;
	nqueues = 0;
	lua_State *lua = NULL;
	

	if (!queue_list)
	{
		queue_list = malloc(sizeof(struct queue_t) * QUEUES_INITIAL);
		if (!queue_list)
		{
			fprintf(stderr, "Cannot alloc memory for queue list.\n");
			exit(RTN_FATAL);
		}

		queue_list_size = QUEUES_INITIAL;
	}

	// Lua needed here
	lua = luaL_newstate();

	if (!lua)
	{
		fprintf(stderr, "LUA state error.\n");
		exit(RTN_ERROR_LUA);
	}

	luaL_openlibs(lua);
	if (luaL_loadfile(lua, settings.conf_file) != 0 || lua_pcall(lua, 0, 0, 0) != 0)
	{
		// Lua file error
		fprintf(stderr, "Load lua script %s error : %s\n", settings.conf_file, lua_tostring(lua, -1));
		lua_close(lua);
		return RTN_ERROR_LUA;
	}

	lua_getglobal(lua, QUEUE_LIST_KEY);
	if (!lua_istable(lua, -1))
	{
		fprintf(stderr, "Global table <%s> does not exists in lua script %s.\n", QUEUE_LIST_KEY, settings.conf_file);
		return RTN_ERROR_LUA;
	}
	
	memset(queue_list, 0, sizeof(struct queue_t) * queue_list_size);
	lua_pushnil(lua);

	while (lua_next(lua, -2) != 0)
	{
		if (QUEUE_TYPE_PARALLEL == atoi(lua_tostring(lua, -1)))
		{
			queue_type = QUEUE_TYPE_PARALLEL;
		}

		else
		{
			queue_type = QUEUE_TYPE_ORDINAL;
		}

		while (i >= queue_list_size)
		{
			// Enlarge queue list
			struct queue_t *tmp = realloc(queue_list, sizeof(struct queue_t) * queue_list_size * 2);
			if (tmp)
			{
				queue_list = tmp;
				queue_list_size *= 2;
			}

			else
			{
				fprintf(stderr, "Enlarge queue list error.\n");
				exit(RTN_FATAL);
			}
		}

		l = strlen(lua_tostring(lua, -2));
		l = (l > MAX_QUEUE_NAME_LENGTH - 1 ? MAX_QUEUE_NAME_LENGTH : l);
		struct queue_t *q = &queue_list[i];
		q->queue_id = i;
		strncpy(q->queue_name, lua_tostring(lua, -2), l);
		q->queue_name[l] = 0x0;
		q->queue_type = queue_type;

		i ++;
		
		lua_pop(lua, 1);
	}

	nqueues = i;
	lua_close(lua);
	
	return i;
}

/* * * Processor * * */
// Static worker thread loop
void * parallel_proc(void *arg)
{
	struct static_worker *me = (struct static_worker *) arg;
	struct epoll_event *events = malloc(sizeof(struct epoll_event) * EPOLL_WAIT_FDS);
	int n, nfds;
	CURLcode res;
	long http_code;
	char buff[32768];

	if (!events)
	{
		fprintf(stderr, "Event malloc error.\n");

		exit(RTN_FATAL);
	}

	me->curl = curl_easy_init();
	if (!me->curl)
	{
		fprintf(stderr, "cURL handler create failed.\n");
		exit(RTN_ERROR_CURL_INIT);
	}

	me->is_free = 0;
	me->task[0] = 0;
	pthread_mutex_init(&me->status_lock, NULL);
	
	while (1)
	{
		pthread_mutex_lock(&me->status_lock);
		me->is_free = 1;
		me->task[0] = 0;
		//fprintf(stderr, "Worker %d set free\n", me->worker_id);
		pthread_mutex_unlock(&me->status_lock);
		memset(events, 0, sizeof(struct epoll_event) * EPOLL_WAIT_FDS);
		nfds = epoll_wait(me->epoll_fd, events, EPOLL_WAIT_FDS, -1);
		pthread_mutex_lock(&me->status_lock);
		me->is_free = 0;
		//fprintf(stderr, "Worker %d set busy\n", me->worker_id);
		pthread_mutex_unlock(&me->status_lock);

		for (n = 0; n < nfds; n ++)
		{
			if ((!events[n].events & EPOLLIN) || events[n].data.fd < 0)
			{
				continue;
			}

			if (events[n].data.fd == me->notify_recv_fd)
			{
				
				// Read all data
				read(me->notify_recv_fd, buff, 32768);
				
				if (strlen(me->task))
				{
					curl_easy_setopt(me->curl, CURLOPT_URL, me->task);
					curl_easy_setopt(me->curl, CURLOPT_WRITEDATA, data_hole);
					curl_easy_setopt(me->curl, CURLOPT_NOSIGNAL, 1);
					res = curl_easy_perform(me->curl);
					if (0 == res)
					{
						curl_easy_getinfo(me->curl, CURLINFO_RESPONSE_CODE, &http_code);

						if (200 != http_code)
						{
							// An error occured
							logger("URL access error : %d - %s", http_code, me->task);
						}
					}
					
					else
					{
						logger("Invalid URL : %s", me->task);
					}
					
					me->task[0] = 0;
				}
				
				else
				{
					fprintf(stderr, "Failed\n");
				}
			}
		}
	}

	curl_easy_cleanup(me->curl);
	
	return NULL;
}

// Main thread loop
// Read signal queue per second
int main_loop()
{
	char cmd[MAX_QUEUE_ITEM_LENGTH];
	char **argv = malloc(sizeof(char *) * (nqueues + 2));
	if (!argv)
	{
		_exit(RTN_FATAL);
	}
	
	redisReply *reply;
	int i, qid;
	int worker_id;
	int cnt_loop = 0;
	size_t l;
	uint64_t cnt = 0;
	
	// Wait for all threads prepaired
	sleep(1);
	while (1)
	{
		if (!re)
		{
			init_redis();
			if (!re)
			{
				logger("Redis connection failed");
				break;
			}
		}
		
		// Read command first
		if (settings.queue_direction == 1)
		{
			reply = redisCommand(re, "LPOP %s", REDIS_SIGNAL_KEY);
		}
		
		else
		{
			reply = redisCommand(re, "RPOP %s", REDIS_SIGNAL_KEY);
		}
		
		if (reply && REDIS_REPLY_STRING == reply->type && reply->len > 0)
		{
			l = (reply->len < MAX_QUEUE_ITEM_LENGTH - 1 ? reply->len : MAX_QUEUE_ITEM_LENGTH - 1);
			strncpy(cmd, reply->str, l);
			cmd[l] = 0x0;
			
			if (0 == strcmp(cmd, "stop"))
			{
				// Close apps
				stop_signal = 1;
			}

			if (0 == strcmp(cmd, "resume"))
			{
				stop_signal = 0;
			}

			else if (0 == strcmp(cmd, "reload"))
			{
				// Reload queue list
				read_queue_list();
				argv = realloc(argv, sizeof(char *) * (nqueues + 2));
				if (!argv)
				{
					return RTN_FATAL;
				}
			}

			else if (0 == strcmp(cmd, "shutdown"))
			{
				// End main loop and exit.
				break;
			}
		}
		
		freeReplyObject(reply);
		
		if (1 == stop_signal)
		{
			sleep(1);
		}
		
		else
		{
			// BL(R)POP queue
			argv[0] = (settings.queue_direction == 1 ? "BLPOP" : "BRPOP");
			
			for (i = 0; i < nqueues; i ++)
			{
				qid = (cnt_loop + i) % nqueues;
				argv[i + 1] = queue_list[qid].queue_name;
			}
			
			cnt_loop ++;
			argv[nqueues + 1] = "1";
			reply = redisCommandArgv(re, nqueues + 2, (const char **) argv, NULL);
			if (reply && REDIS_REPLY_ARRAY == reply->type && reply->elements == 2)
			{
				cnt ++;
				qid = -1;
				for (i = 0; i < nqueues; i ++)
				{
					// Find me
					if (0 == strcmp(reply->element[0]->str, queue_list[i].queue_name))
					{
						qid = i;
						break;
					}
				}
				
				if (qid >= 0 && qid < nqueues)
				{
					if (QUEUE_TYPE_PARALLEL == queue_list[qid].queue_type)
					{
						while (1)
						{
							worker_id = next_static_worker();
							if (1 == is_worker_free(worker_id) && 0 == strlen(worker_list[worker_id].task))
							{
								break;
							}
							
							__asm__ __volatile__("pause");
						}
					}
					
					else
					{
						worker_id = jhash(queue_list[qid].queue_name, -1);
						while (1)
						{
							if (1 == is_worker_free(worker_id) && 0 == strlen(worker_list[worker_id].task))
							{
								break;
							}
							
							__asm__ __volatile__("pause");
						}
					}
					
					// Copy data
					l = (strlen(reply->element[1]->str) > MAX_QUEUE_ITEM_LENGTH - 1) ? (MAX_QUEUE_ITEM_LENGTH - 1) : strlen(reply->element[1]->str);
					strncpy(worker_list[worker_id].task, reply->element[1]->str, l);
					worker_list[worker_id].task[l] = 0x0;
					
					write(worker_list[worker_id].notify_send_fd, "", 1);
					
					//fprintf(stderr, "Now %llu => %d\n", (long long unsigned int) cnt, worker_id);
				}
			}
			
			freeReplyObject(reply);
		}
	}
	
	if (re)
	{
		redisFree(re);
	}

	if (log_fd)
	{
		logger("Server closed.");
		close(log_fd);
	}
	
	if (argv)
	{
		free(argv);
	}
	
	return RTN_SUCCESS;
}

/* * * Signals * * */
void sig_handler(const int sig)
{
	// Process terminated
	stop_signal = 1;
	fprintf(stderr, "\n\nProgram terminated by user.\n\n");

	exit(RTN_SUCCESS);
}

void set_sig(void (* func)(int))
{
	struct sigaction sa;

	if (!func)
	{
		func = SIG_DFL;
	}

	signal(SIGINT, func);
	signal(SIGTERM, func);
	signal(SIGQUIT, func);
	signal(SIGKILL, func);

	// Ignore SIGPIPE & SIGCLD
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	signal(SIGPIPE, SIG_IGN);
	if (-1 == sigemptyset(&sa.sa_mask) || -1 == sigaction(SIGPIPE, &sa, 0))
	{
		fprintf(stderr, "Ignore SIGPIPE failed.\n");
		exit(RTN_ERROR_SIGNAL);
	}

	signal(SIGALRM, SIG_IGN);
	if (-1 == sigemptyset(&sa.sa_mask) || -1 == sigaction(SIGALRM, &sa, 0))
	{
		fprintf(stderr, "Ignore SIGALRM failed.\n");
		exit(RTN_ERROR_SIGNAL);
	}
}

int jhash(const char *input, size_t len)
{
	uint32_t hash, i;
	if (!input)
	{
		input = "";
	}
	
	if (-1 == (signed) len)
	{
		len = strlen(input);
	}
	
	for (hash = i = 0; i < len; i ++)
	{
		hash += input[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash % WORKERS;
}

/* * * Log recorder * * */
int logger(const char *fmt, ...)
{
	time_t now = time((time_t *) NULL);
	va_list ap;
	static char data[MAX_LOG_LINE_LENGTH];
	static char line[MAX_LOG_LINE_LENGTH];
	static char tgdate[128];
	struct tm *loctime;
	size_t t = 0, len;

	pthread_mutex_lock(&log_write_lock);
	if (!log_fd)
	{
		log_fd = open(settings.error_log_file, O_CREAT | O_WRONLY | O_APPEND, 0644);
	}

	if (log_fd > 0)
	{
		memset(data, 0, MAX_LOG_LINE_LENGTH);
		memset(line, 0, MAX_LOG_LINE_LENGTH);
		
		va_start(ap, fmt);
		vsnprintf(data, MAX_LOG_LINE_LENGTH - 2, fmt, ap); 
		va_end(ap);

		loctime = localtime(&now);
		strftime(tgdate, 128, "%m/%d/%Y %H:%M:%S", loctime);
		snprintf(line, MAX_LOG_LINE_LENGTH - 1, "[%s] : %s\n", tgdate, data);

		len = strlen(line);

		while (t < len)
		{
			t += write(log_fd, line, len);
		}
	}
	pthread_mutex_unlock(&log_write_lock);

	return t;
}
