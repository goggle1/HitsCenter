
#ifndef __DB_H__
#define __DB_H__

/*
#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif 
*/

#include "public.h"

#define MAX_PARAM_LEN 64

typedef struct db_param_t
{
    char db_host[MAX_PARAM_LEN];   
    char user_name[MAX_PARAM_LEN];
    char password[MAX_PARAM_LEN]; 
    char db_name[MAX_PARAM_LEN];
    char table[MAX_PARAM_LEN]; 
    unsigned short db_port; 	
} DB_PARAM;

typedef struct hits_record_t
{
	char 	hash_id[MAX_HASH_ID_LEN];
	int  	area_id;
	long 	hits_num_pc;
	long	hits_num_mobile;
	//time_t	start_time;
} HITS_RECORD_T;


int db_test();
int db_save(map<string, HITS_RECORD_T>& record_list, time_t start_time);

/*
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif 
*/

#endif /* __DB_H__ */

