/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * fakesae_queue.h
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

/**
 * Redis consummer for fakesae
 * By Dr.NP <zhanghao@9173.com>
 * Update: 05/14/2012
 */

#ifndef FAKESAE_QUEUE_H

#include <pthread.h>
#include <curl/curl.h>

// Definations
#define APP_VERSION						"0.1 Alpha"

#define RTN_SUCCESS						0
#define RTN_FATAL						255
#define RTN_ERROR_UNKNOWN				254
#define RTN_ERROR_GENERAL				1
#define RTN_ERROR_EPOLL					2
#define RTN_ERROR_THREAD				3
#define RTN_ERROR_SIGNAL				4
#define RTN_ERROR_PIPE					5
#define RTN_ERROR_REDIS_CONNECTION		11
#define RTN_ERROR_CURL_INIT				41
#define RTN_ERROR_LUA					71

#define WORKERS							64
#define QUEUES_INITIAL					1024
#define MAX_QUEUE_ITEM_LENGTH           16384
#define MAX_QUEUE_NAME_LENGTH			256
#define EPOLL_WAIT_FDS					16							// We do not need too much here
#define DATA_ALLOC_BLOCKS				1024						// In default, 4MB block alloc each time

#define DEFAULT_REDIS_HOST				"localhost"
#define DEFAULT_REDIS_PORT				6379						// hAhA~~~ MERZ
#define DEFAULT_REDIS_DB				1
#define DEFAULT_DAEMONIZE				0
#define DEFAULT_QUEUE_CONSUMMER_SLEEP   100							// Sleep for 0.1 second
#define DEFAULT_ERROR_LOG_FILE			"/var/log/fakesae_queue.log"
#define DEFAULT_CONF_FILE				"fakesae_queue.conf.lua"
#define DEFAULT_QUEUE_DIRECTION			1							// 1-left read / 2-right read

#define MAX_QUEUE_CONSUMMER_SLEEP		1048576
#define MIN_QUEUE_CONSUMMER_SLEEP		1							// CPU leak~~
#define MAX_ACCU_TASKS					4096
#define MAX_LOG_LINE_LENGTH				32768

#define REDIS_SIGNAL_KEY				"FAKESAE_QUEUE_SIGNAL"
#define QUEUE_LIST_KEY					"fakesae_queue_list"

#define QUEUE_TYPE_ORDINAL				1
#define QUEUE_TYPE_PARALLEL				2

// Structs
struct csm_setting
{
	char *redis_host;
	int redis_port;
	int redis_db;
	char *redis_password;
	int daemonize;
	int queue_consummer_sleep;										// In millisecond
	int queue_direction;
	char *error_log_file;
	char *conf_file;
};

struct static_worker
{
	int worker_id;
	pthread_t thread;
	pthread_attr_t attr;
	pthread_mutex_t status_lock;
	int epoll_fd;
	int notify_send_fd;
	int notify_recv_fd;
	struct epoll_event ev;
	CURL *curl;
	int is_free;
	char task[MAX_QUEUE_ITEM_LENGTH];
};

struct queue_t
{
	int queue_id;
	char queue_name[MAX_QUEUE_NAME_LENGTH];
	char queue_item[MAX_QUEUE_ITEM_LENGTH];
	int queue_type;
};

// Functions

#endif  /* fakesae_queue.h */
