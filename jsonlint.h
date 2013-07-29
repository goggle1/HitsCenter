#ifndef __JSONLINT_H__
#define __JSONLINT_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "public.h"

#define PLATFORM_PC		1
#define PLATFORM_MOBILE	2

typedef struct hashid_statistics_t
{
	char	hash_id[MAX_HASH_ID_LEN];
	long	hits_num;	
} HASHID_STATISTICS_T;

typedef struct area_statistics_t
{
	int 	area_id;	
	DEQUE_NODE* hashid_list;	
} AREA_STATISTICS_T;

//#define MAX_TIME_LEN	32
typedef struct hits_statistics_t
{
	time_t 	start_time;
	//char 	start_time[MAX_TIME_LEN];	
	DEQUE_NODE* pc_list;
	DEQUE_NODE* mobile_list;
} HITS_STATISTICS_T;


int 	json_parse(HITS_STATISTICS_T* statp, char* json_file);
void 	hits_statistics_release(HITS_STATISTICS_T* statp);


#ifdef __cplusplus
}
#endif

#endif

