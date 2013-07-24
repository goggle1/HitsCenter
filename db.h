
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
    char db_ip[MAX_PARAM_LEN];      // "127.0.0.1"
    char user_name[MAX_PARAM_LEN];  // "root"
    char password[MAX_PARAM_LEN];   // "123456"
    char db_name[MAX_PARAM_LEN];    // "heat"
    char table[MAX_PARAM_LEN]; 	  	// "hashid"
    unsigned short db_port; 		// 3306
} DB_PARAM;

typedef struct play_record_t
{
	char 	hash_id[MAX_HASH_ID_LEN];
	int  	area_id;
	long 	play_num_pc;
	long	play_num_mobile;
	//time_t	start_time;
} PLAY_RECORD_T;


int db_test();
int db_save(map<string, PLAY_RECORD_T>& record_list, time_t start_time);

/*
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif 
*/

#endif /* __DB_H__ */

