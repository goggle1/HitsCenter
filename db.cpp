
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <map>

using namespace std;

#include "mysql.h"
#include "db.h"

#define MAX_BUF_SIZE 1024 

void db_print_error(const char *msg, MYSQL *db_conn) 
{ 
    if (msg)
    {
        fprintf(stderr, "%s: %s\n", msg, mysql_error(db_conn));
    }
    else
    {
        fprintf(stderr, "%s\n", mysql_error(db_conn));
    }
}

int db_execute_sql(const char * sql, MYSQL *db_conn)
{
    /*query the database according the sql*/
    if (mysql_real_query(db_conn, sql, strlen(sql)))
    {
        return -1;
    }

    return 0; 
}

int db_init_mysql(DB_PARAM* paramp, MYSQL **db_conn ) 
{ 
    // init the database connection
    *db_conn = mysql_init(NULL);

    /* connect the database */
    if(!mysql_real_connect(*db_conn,
                            paramp->db_host,
                            paramp->user_name, 
                            paramp->password, 
                            paramp->db_name, 
                            paramp->db_port,
                            NULL, 
                            0)) 
    {
        return -1;
    }    
  
    if (db_execute_sql("set names utf8",*db_conn))
    {
        return -1;
    }
    
    return 0;
}

int db_test()
{
    int ret = -1;    
    MYSQL *db_conn = NULL; 
	extern DB_PARAM g_db_param;
   
    ret = db_init_mysql(&g_db_param, &db_conn);
    if (ret != 0)
    {
        db_print_error(NULL, db_conn);
        return ret;
    }

	char sql[MAX_BUF_SIZE];
	//sprintf(sql, "INSERT INTO `test`(`num`, `name`) VALUES('123', 'testname')");
	//sprintf(sql, "INSERT INTO test(num, name) VALUES('456', 'guoqiang')");
	snprintf(sql, MAX_BUF_SIZE-1, 
		"INSERT INTO %s(hash_id, area_id, play_num_pc, play_num_mobile, start_time) "
		"VALUES('%s', '%d', '%ld', '%ld', '%s')", 
		g_db_param.table,
		"1234567890123456789012345678901234567890",
		123, 456L, 789L, "2013-07-24 00:11:22");
	sql[MAX_BUF_SIZE-1] = '\0';

	ret = db_execute_sql(sql, db_conn); 
	if (ret != 0)
	{
	    db_print_error(NULL, db_conn);
	}

    mysql_close(db_conn);

    return 0;
}

int db_save(map<string, HITS_RECORD_T>& record_list, time_t start_time)
{
    int ret = -1;    
    MYSQL *db_conn = NULL; 
	extern DB_PARAM g_db_param;
   
    ret = db_init_mysql(&g_db_param, &db_conn);
    if (ret != 0)
    {
        db_print_error(NULL, db_conn);
        return ret;
    }

	char sql[MAX_BUF_SIZE];	
	//sprintf(sql, "INSERT INTO `test`(`num`, `name`) VALUES('123', 'testname')");
	//sprintf(sql, "INSERT INTO test(num, name) VALUES('456', 'guoqiang')");

	char time_str[MAX_TIME_LEN];
	struct tm result = {};
	localtime_r(&start_time, &result);
	snprintf(time_str, MAX_TIME_LEN-1, "%04d-%02d-%02d %02d:%02d:%02d", 
		result.tm_year+1900,
		result.tm_mon +1,
		result.tm_mday,
		result.tm_hour,
		result.tm_min,
		result.tm_sec);
	time_str[MAX_TIME_LEN-1] = '\0';

	map<string, HITS_RECORD_T>::iterator iter;
	for(iter=record_list.begin(); iter!=record_list.end(); iter++)
	{
		HITS_RECORD_T& record = iter->second;		
		snprintf(sql, MAX_BUF_SIZE-1, 
			"INSERT INTO %s(hash_id, area_id, play_num_pc, play_num_mobile, start_time) "
			"VALUES('%s', '%d', '%ld', '%ld', '%s')", 
			g_db_param.table,
			record.hash_id, record.area_id, record.hits_num_pc, record.hits_num_mobile, time_str
			);
		sql[MAX_BUF_SIZE-1] = '\0';

		ret = db_execute_sql(sql, db_conn); 
		if (ret != 0)
		{
		    db_print_error(NULL, db_conn);
		}
	}

    mysql_close(db_conn);

    return 0;
}


