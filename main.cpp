
// file: main.cpp
// author: guoqiang@funshion.com

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <getopt.h>

#include <string>
#include <map>
using namespace std;
#include "public.h"
#include "deque.h"
#include "curl_download.h"
#include "jsonlint.h"
#include "db.h"
#include "http_server.h"

#define MY_VERSION		"0.1"
#define SLEEP_INTERVAL	(10)
#define MAX_URL_LEN		1024
#define MAX_KEY_LEN		(MAX_HASH_ID_LEN + 16)

// just for debug.
#if 0
#define DEFAULT_OXEYE_GET_INTERVAL	(1*60)
#define DEFAULT_DB_SAVE_INTERVAL	(6*60)
#define DEFAULT_OXEYE_URL_PREFIX 	"http://test.funshion.com/_online/cacti_django/api/get_data/playnum"
#else
#define DEFAULT_OXEYE_GET_INTERVAL	(10*60)
#define DEFAULT_DB_SAVE_INTERVAL	(1*60*60)
#define DEFAULT_OXEYE_URL_PREFIX 	"http://traceall.funshion.com/_online/cacti_django/api/get_data/playnum"
#endif

#define DEFAULT_ROOT_PATH			"/heat"
#define DEFAULT_DB_HOST				"127.0.0.1"
#define DEFAULT_DB_USER				"admin"
#define DEFAULT_PASSWORD			"123456"
#define DEFAULT_DB_NAME				"heat"
#define DEFAULT_DB_TABLE			"hashid_playnum"
#define DEFAULT_DB_PORT				3306

#define MAX_HITS_NUM				(DEFAULT_DB_SAVE_INTERVAL/DEFAULT_OXEYE_GET_INTERVAL)

typedef struct config_t
{	
	char* root_path;
	char* oxeye_url_prefix;
	int	  oxeye_get_interval; // seconds.
	int	  db_save_interval;   // seconds.
} CONFIG_T;

DEQUE_NODE* 		g_job_list = NULL;
pthread_mutex_t 	g_job_mutex;
sem_t				g_job_sem;

HITS_STATISTICS_T	g_hits_stats[MAX_HITS_NUM] = {};
int					g_hits_index = 0;

CONFIG_T			g_config = 
{
	/* .root_path= */			DEFAULT_ROOT_PATH, 
	/* .oxeye_url_prefix= */	DEFAULT_OXEYE_URL_PREFIX,
	/* .oxeye_get_interval= */	DEFAULT_OXEYE_GET_INTERVAL,
	/* .db_save_interval= */	DEFAULT_DB_SAVE_INTERVAL,
};

DB_PARAM			g_db_param = 
{
    /* char db_host[MAX_PARAM_LEN]; */		DEFAULT_DB_HOST,
    /* char user_name[MAX_PARAM_LEN];  */ 	DEFAULT_DB_USER,
    /* char password[MAX_PARAM_LEN];   */ 	DEFAULT_PASSWORD,
    /* char db_name[MAX_PARAM_LEN];   */		DEFAULT_DB_NAME,
    /* char table[MAX_PARAM_LEN]; */ 			DEFAULT_DB_TABLE,
    /* unsigned short db_port; 		*/ 			DEFAULT_DB_PORT,
};

// hash table.
// DEQUE_NODE*		g_hashid_list = NULL;
// red_black_tree is better.
map<string, HITS_RECORD_T> g_play_records;
time_t					   g_start_time;

int job_in_queue(time_t job_time)
{
// just for debug.
#if 0
	job_time = 1374832200;
#endif
	
	int ret = 0;	
	// append queue, and send signal.
	fprintf(stdout, "%s %ld\n", __FUNCTION__, job_time);

	DEQUE_NODE* nodep = (DEQUE_NODE*)malloc(sizeof(DEQUE_NODE));
	if(nodep == NULL)
	{
		ret = -1;
		return ret;
	}
	memset(nodep, 0, sizeof(DEQUE_NODE));
	
	time_t* timep = new time_t;
	if(timep == NULL)
	{
		free(nodep);
		ret = -1;
		return ret;
	}
	*timep = job_time;
	nodep->datap = timep;

	pthread_mutex_lock(&g_job_mutex);
	g_job_list = deque_append(g_job_list, nodep);
	pthread_mutex_unlock(&g_job_mutex);

	sem_post(&g_job_sem);

	return ret;
}

int job_out_queue(time_t* job_timep)
{
	int ret = 0;	
	// get head from queue, and remove it.

	pthread_mutex_lock(&g_job_mutex);
	
	if(g_job_list == NULL)
	{
		pthread_mutex_unlock(&g_job_mutex);
		return -1;
	}

	time_t* temp = (time_t*)(g_job_list->datap);
	*job_timep = *temp;
	free(temp);

	g_job_list = deque_remove_head(g_job_list);
	
	pthread_mutex_unlock(&g_job_mutex);

	fprintf(stdout, "%s %ld\n", __FUNCTION__, *job_timep);

	return ret;
}

int do_parse(char* json_file)
{
	int ret = 0;

	HITS_STATISTICS_T* statp = &(g_hits_stats[g_hits_index]);
	if(statp->start_time > 0)
	{
		hits_statistics_release(statp);
	}
	
	ret = json_parse(statp, json_file);
	if(ret != 0)
	{
		return ret;
	}
	
	g_hits_index++;
	if(g_hits_index >= MAX_HITS_NUM)
	{
		g_hits_index = 0;
	}
	
	return 0;
}

int hits_records_print(map<string, HITS_RECORD_T>& record_list)
{
	int index = 0;

	fprintf(stdout, "%s: =========================================\n", __FUNCTION__);
	
	fprintf(stdout, "%s: %ld\n", __FUNCTION__, g_start_time);

	map<string, HITS_RECORD_T>::iterator iter;
	for(iter=record_list.begin(); iter!=record_list.end();iter++)
	{
		string key = iter->first;
		HITS_RECORD_T& record = iter->second;
		
		fprintf(stdout, "%s: index=%d, key=%s, hash_id=%s, area_id=%d, hits_num_pc=%ld, hits_num_mobile=%ld\n", 
			__FUNCTION__, index, key.c_str(), record.hash_id, record.area_id, record.hits_num_pc, record.hits_num_mobile);
		index ++;
	}	

	fprintf(stdout, "%s: #########################################\n", __FUNCTION__);
	
	return 0;
}


int hits_records_merge(map<string, HITS_RECORD_T>& record_list, HITS_STATISTICS_T* statp)
{
	g_start_time = statp->start_time;

	char key[MAX_KEY_LEN];
	HITS_RECORD_T record;
	map<string, HITS_RECORD_T>::iterator iter;

	DEQUE_NODE* area_list = NULL;
	DEQUE_NODE* area_nodep = NULL;
	
	area_list = statp->pc_list;
	area_nodep = area_list;
	while(area_nodep != NULL)
	{
		AREA_STATISTICS_T* areap = (AREA_STATISTICS_T*)(area_nodep->datap);

		DEQUE_NODE* hashid_list = areap->hashid_list;
		DEQUE_NODE* hashid_nodep = hashid_list;
		while(hashid_nodep != NULL)
		{
			HASHID_STATISTICS_T* hashidp = (HASHID_STATISTICS_T*)(hashid_nodep->datap);
			sprintf(key, "%s:%d", hashidp->hash_id, areap->area_id);
			iter = record_list.find(key);
			if(iter == record_list.end())
			{
				strcpy(record.hash_id, hashidp->hash_id);
				record.area_id = areap->area_id;
				record.hits_num_pc = hashidp->play_num;
				record.hits_num_mobile = 0;
				record_list.insert(pair<string, HITS_RECORD_T>(key, record));
			}
			else
			{
				HITS_RECORD_T& record_ref = iter->second;
				record_ref.hits_num_pc += hashidp->play_num;				
			}

			if(hashid_nodep->nextp == hashid_list)
			{
				break;
			}
			hashid_nodep = hashid_nodep->nextp;
		}
	
		if(area_nodep->nextp == area_list)
		{
			break;
		}
		area_nodep = area_nodep->nextp;
	}

	area_list = statp->mobile_list;
	area_nodep = area_list;
	while(area_nodep != NULL)
	{
		AREA_STATISTICS_T* areap = (AREA_STATISTICS_T*)(area_nodep->datap);

		DEQUE_NODE* hashid_list = areap->hashid_list;
		DEQUE_NODE* hashid_nodep = hashid_list;
		while(hashid_nodep != NULL)
		{
			HASHID_STATISTICS_T* hashidp = (HASHID_STATISTICS_T*)(hashid_nodep->datap);
			sprintf(key, "%s:%d", hashidp->hash_id, areap->area_id);
			iter = record_list.find(key);
			if(iter == record_list.end())
			{
				strcpy(record.hash_id, hashidp->hash_id);
				record.area_id = areap->area_id;
				record.hits_num_pc = 0;
				record.hits_num_mobile = hashidp->play_num;
				record_list.insert(pair<string, HITS_RECORD_T>(key, record));
			}
			else
			{
				HITS_RECORD_T& record_ref = iter->second;
				record_ref.hits_num_mobile += hashidp->play_num;				
			}

			if(hashid_nodep->nextp == hashid_list)
			{
				break;
			}
			hashid_nodep = hashid_nodep->nextp;
		}
	
		if(area_nodep->nextp == area_list)
		{
			break;
		}
		area_nodep = area_nodep->nextp;
	}
	
	return 0;
}

bool one_hour_complete()
{
	// just for debug.
#if 0
	return true;
#endif

	// the oldest statistics, maybe
	HITS_STATISTICS_T* statp = &(g_hits_stats[g_hits_index]);
	if(statp->start_time > 0 && (statp->start_time%3600==0))
	{
		return true;
	}

	return false;
}

int do_save()
{
	int ret = 0;
	
	// 10 minutes ,  1 statistics.
	// 1   hour,	save to database.	
	if(one_hour_complete())
	{
		int count = 0;
		int index = g_hits_index;
		
		while(count < MAX_HITS_NUM)
		{
			HITS_STATISTICS_T* statp = &(g_hits_stats[index]);
			if(statp->start_time > 0)
			{
				hits_records_merge(g_play_records, statp);	
				hits_records_print(g_play_records);
			}
			
			index ++;
			if(index >= MAX_HITS_NUM)
			{
				index = 0;
			}
			count ++;
		}
		
		ret = db_save(g_play_records, g_start_time);
	}

	return ret;
}

int do_job(time_t job_time)
{
	// http_get json from oxeye
	// parse the json
	// organize the data
	// insert into database.

	int ret = 0;
	
	fprintf(stdout, "%s %ld\n", __FUNCTION__, job_time);
	
	// http://traceall.funshion.com/_online/cacti_django/api/get_data/playnum?timestamp=
	char url[MAX_URL_LEN];
	snprintf(url, MAX_URL_LEN-1, "%s?timestamp=%ld", g_config.oxeye_url_prefix, job_time);
	url[MAX_URL_LEN-1] = '\0';

	char job_file[PATH_MAX];
	snprintf(job_file, PATH_MAX-1, "%s/%ld.json", g_config.root_path, job_time);
	job_file[PATH_MAX-1] = '\0';
	
	ret = curl_download(url, job_file);
	if(ret != 0)
	{
		return ret;
	}	

	ret = do_parse(job_file);
	if(ret != 0)
	{
		return ret;
	}

	ret = do_save();
	if(ret != 0)
	{
		return ret;
	}
	
	return 0;
}

void* thread_job(void* arg)
{

	while(1)
	{
		// wait for semaphore.
		// get one job
		// do job
		sem_wait(&g_job_sem);
		
		time_t job_time = 0;
		job_out_queue(&job_time);
		if(job_time > 0)
		{
			do_job(job_time);
		}
	}

	return NULL;
}

void print_usage(char* program)
{
	fprintf(stdout, "usage: %s --help\n", program);
    fprintf(stdout, "usage: %s -h\n", program);
    fprintf(stdout, "usage: %s --version\n", program);	   
    fprintf(stdout, "usage: %s -v\n", program);
  
    fprintf(stdout, "usage: %s --root_path=%s --oxeye_url_prefix=\"%s\" --oxeye_get_interval=%d --db_save_interval=%d "
    	"--db_host=%s --db_user=%s --db_password=%s --db_name=%s --db_port=%d\n", 
        program, 
        DEFAULT_ROOT_PATH, 
        DEFAULT_OXEYE_URL_PREFIX, 
        DEFAULT_OXEYE_GET_INTERVAL, 
        DEFAULT_DB_SAVE_INTERVAL,
        DEFAULT_DB_HOST,
        DEFAULT_DB_USER,
        DEFAULT_PASSWORD,
        DEFAULT_DB_NAME,
        DEFAULT_DB_PORT
        );
    fprintf(stdout, "usage: %s -r %s -x \"%s\" -g %d -s %d "
    	"-o %s -u %s -a %s -n %s -p %d\n", 
        program, 
        DEFAULT_ROOT_PATH, 
        DEFAULT_OXEYE_URL_PREFIX, 
        DEFAULT_OXEYE_GET_INTERVAL, 
        DEFAULT_DB_SAVE_INTERVAL,
        DEFAULT_DB_HOST,
        DEFAULT_DB_USER,
        DEFAULT_PASSWORD,
        DEFAULT_DB_NAME,
        DEFAULT_DB_PORT
        );
}

int parse_cmd_line(int argc, char* argv[])
{
	int ret = 0;
	
	// begin: parse the command line.
	bool  have_unknown_opts = false;
	// command line
	// -r, --root_path  
	// -x, --oxeye_url_prefix
	// -g, --oxeye_get_interval
	// -s, --db_save_interval
	// -o, --db_host
	// -u, --db_user,
	// -a, --db_password
	// -n, --db_name,
	// -p, --db_port,	
	// -v, --version
	// -h, --help
	// parse_cmd_line();
	static struct option orig_options[] = 
	{		 
		{ "root_path",				1, 0, 'r' },
		{ "oxeye_url_prefix",		1, 0, 'x' },  
		{ "oxeye_get_interval",		1, 0, 'g' },
		{ "db_save_interval",		1, 0, 's' },
		{ "db_host",				1, 0, 'o' },
		{ "db_user",				1, 0, 'u' },
		{ "db_password",			1, 0, 'a' },
		{ "db_name",				1, 0, 'n' },
		{ "db_port",				1, 0, 'p' },
		{ "version", 				0, 0, 'v' },
		{ "help",	 				0, 0, 'h' },		 
		{ NULL, 	 				0, 0, 0   }
	};	
	while (true) 
	{
		int c = -1;
		int option_index = 0;
	  
		c = getopt_long_only(argc, argv, "r:x:g:s:o:u:a:n:p:vh", orig_options, &option_index);
		if (c == -1)
			break;

		switch (c) 
		{	
			case 'r':	
				g_config.root_path = strdup(optarg);
				break;
			case 'x':	
				g_config.oxeye_url_prefix = strdup(optarg);
				break;
			case 'g':	
				g_config.oxeye_get_interval = atoi(optarg);
				break;
			case 's':	
				g_config.db_save_interval = atoi(optarg);
				break;
			case 'o':	
				strcpy(g_db_param.db_host, optarg);
				break;
			case 'u':	
				strcpy(g_db_param.user_name, optarg);
				break;
			case 'a':	
				strcpy(g_db_param.password, optarg);
				break;
			case 'n':	
				strcpy(g_db_param.db_name, optarg);
				break;
			case 'p':	
				g_db_param.db_port = atoi(optarg);
				break;
			case 'h':
				print_usage(argv[0]);						
				exit(0);
				break;			  
			case 'v':
				fprintf(stdout, "%s: version %s\n", argv[0], MY_VERSION);
				exit(0);
				break;
			case '?':
			default:
				have_unknown_opts = true;
				break;
		}
	}
	
	if(have_unknown_opts)
	{
		print_usage(argv[0]);
		exit(-1);
	}	
	// end: parse the command line.

	return ret;
}

int main(int argc, char* argv[])
{
	// 1. timer, every 10 minutes produce 1 job
	// 2. thread_job, wait for job, and do job.
	int ret = 0;
		
#if 0
	// just for test.
	do_parse("./1.json");
	do_parse("./1.json");
	do_parse("./1.json");
	do_parse("./1.json");
	do_parse("./1.json");
	do_parse("./1.json");
	do_parse("./1.json");	
	do_save();
#endif
		
	ret = parse_cmd_line(argc, argv);
	if(ret < 0)
	{
		// error
		fprintf(stderr, "%s: parse_cmd_line failed\n", __FUNCTION__);
		return -1;
	}

	ret = start_http_server();
	if(ret < 0)
	{
		// error
		fprintf(stderr, "%s: start_http_server failed\n", __FUNCTION__);
		return -1;
	}

	ret = sem_init(&g_job_sem, 0, 0);
	pthread_mutex_init(&g_job_mutex, NULL);
	
	pthread_t thread_id = 0;
	ret = pthread_create(&thread_id, NULL, thread_job, NULL);
	if(ret < 0)
	{
		// error
		fprintf(stderr, "%s: pthread_create %s failed\n", __FUNCTION__, "thread_job");
		return -1;
	}
	

	time_t last_time = 0;
	while(1)
	{
		time_t now = time(NULL);
		fprintf(stdout, "%s now=%ld, last_time=%ld\n", __FUNCTION__, now, last_time);
		if(now - last_time >= g_config.oxeye_get_interval)
		{
			// produce 1 job.
			time_t job_time = now/g_config.oxeye_get_interval*g_config.oxeye_get_interval;
			job_in_queue(job_time);			
			
			last_time = now;
		}
		
		sleep(SLEEP_INTERVAL);
	}
	
	return 0;
}

