
// file: main.cpp
// author: guoqiang@funshion.com

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>

#include <string>
#include <map>

#include "public.h"
#include "deque.h"
#include "curl_download.h"
#include "jsonlint.h"

using namespace std;

#include "db.h"

#define SLEEP_INTERVAL	(10)
//#define TIME_INTERVAL	(10*60)
#define TIME_INTERVAL	(1*60)
#define MAX_URL_LEN		1024
#define URL_PREFIX 		"http://traceall.funshion.com/_online/cacti_django/api/get_data/playnum"
#define ROOT_PATH		"/heat"
#define MAX_STATS_NUM	6
#define MAX_KEY_LEN		(MAX_HASH_ID_LEN + 16)

DEQUE_NODE* 		g_job_list = NULL;
pthread_mutex_t 	g_job_mutex;
sem_t				g_job_sem;

PLAY_STATISTICS_T	g_play_stats[MAX_STATS_NUM] = {};
int					g_stat_index = 0;

DB_PARAM			g_db_param = 
{
	"127.0.0.1",
	"admin",
	"123456",
	"heat",
	"hashid_playnum", 
	3306,
};

// hash table.
// DEQUE_NODE*		g_hashid_list = NULL;
// rb_tree is better.
map<string, PLAY_RECORD_T> g_play_records;
time_t					   g_start_time;

int job_in_queue(time_t job_time)
{
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

	PLAY_STATISTICS_T* statp = &(g_play_stats[g_stat_index]);
	if(statp->start_time > 0)
	{
		play_statistics_release(statp);
	}
	
	ret = json_parse(statp, json_file);
	if(ret != 0)
	{
		return ret;
	}
	
	g_stat_index++;
	if(g_stat_index >= MAX_STATS_NUM)
	{
		g_stat_index = 0;
	}
	
	return 0;
}

int play_records_print(map<string, PLAY_RECORD_T>& record_list)
{
	int index = 0;

	fprintf(stdout, "%s: =========================================\n", __FUNCTION__);
	
	fprintf(stdout, "%s: %ld\n", __FUNCTION__, g_start_time);

	map<string, PLAY_RECORD_T>::iterator iter;
	for(iter=record_list.begin(); iter!=record_list.end();iter++)
	{
		string key = iter->first;
		PLAY_RECORD_T& record = iter->second;
		
		fprintf(stdout, "%s: index=%d, key=%s, hash_id=%s, area_id=%d, play_num_pc=%ld, play_num_mobile=%ld\n", 
			__FUNCTION__, index, key.c_str(), record.hash_id, record.area_id, record.play_num_pc, record.play_num_mobile);
		index ++;
	}	

	fprintf(stdout, "%s: #########################################\n", __FUNCTION__);
	
	return 0;
}


int play_records_merge(map<string, PLAY_RECORD_T>& record_list, PLAY_STATISTICS_T* statp)
{
	g_start_time = statp->start_time;

	char key[MAX_KEY_LEN];
	PLAY_RECORD_T record;
	map<string, PLAY_RECORD_T>::iterator iter;

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
				// difference
				record.play_num_pc = hashidp->play_num;
				record.play_num_mobile = 0;
				record_list.insert(pair<string, PLAY_RECORD_T>(key, record));
			}
			else
			{
				PLAY_RECORD_T& record_ref = iter->second;
				// difference
				record_ref.play_num_pc += hashidp->play_num;				
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
				// difference
				record.play_num_pc = 0;
				record.play_num_mobile = hashidp->play_num;
				record_list.insert(pair<string, PLAY_RECORD_T>(key, record));
			}
			else
			{
				PLAY_RECORD_T& record_ref = iter->second;
				// difference
				record_ref.play_num_mobile += hashidp->play_num;				
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
	// the oldest statistics, maybe
	PLAY_STATISTICS_T* statp = &(g_play_stats[g_stat_index]);
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
		int index = g_stat_index;
		
		while(count < MAX_STATS_NUM)
		{
			PLAY_STATISTICS_T* statp = &(g_play_stats[index]);
			if(statp->start_time > 0)
			{
				play_records_merge(g_play_records, statp);	
				play_records_print(g_play_records);
			}
			
			index ++;
			if(index >= MAX_STATS_NUM)
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
	snprintf(url, MAX_URL_LEN-1, "%s?timestamp=%ld", URL_PREFIX, job_time);
	url[MAX_URL_LEN-1] = '\0';

	char job_file[PATH_MAX];
	snprintf(job_file, PATH_MAX-1, "%s/%ld.json", ROOT_PATH, job_time);
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

int main(int argc, char* argv[])
{
	// 1. timer, every 10 minutes produce 1 job
	// 2. thread_job, wait for job, and do job.
	int ret = 0;

	// just for test.
	do_parse("./hu.json");
	do_parse("./hu.json");
	do_parse("./hu.json");
	do_parse("./hu.json");
	do_parse("./hu.json");
	do_parse("./hu.json");
	do_parse("./hu.json");	

	// just for test.
	do_save();

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
		if(now - last_time >= TIME_INTERVAL)
		{
			// produce 1 job.
			time_t job_time = now/TIME_INTERVAL*TIME_INTERVAL;
			job_in_queue(job_time);			
			
			last_time = now;
		}
		
		sleep(SLEEP_INTERVAL);
	}
	
	return 0;
}

