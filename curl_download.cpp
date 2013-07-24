
#include <stdio.h>
#include <curl/curl.h>

/*
 * This is an example showing how to get a single file from an FTP server.
 * It delays the actual destination file creation until the first write
 * callback so that it won't create an empty file in case the remote file
 * doesn't exist or something else fails.
 */ 
 
struct FtpFile 
{
	const char *filename;
  	FILE *stream;
};

static size_t my_fwrite(void *buffer, size_t size, size_t nmemb, void *stream)
{
	struct FtpFile *out = (struct FtpFile *)stream;
  	if(out && !out->stream) 
  	{
    	/* open file for writing */ 
    	out->stream = fopen(out->filename, "wb");
    	if(!out->stream)
    	{
    		fprintf(stderr, "%s error in fopen %s\n", __FUNCTION__, out->filename);
      		return -1; /* failure, can't open file to write */ 
      	}
  	}
  	return fwrite(buffer, size, nmemb, out->stream);
}
 
 
//int main(void)
int curl_download(char* srcfile, char* dstfile)
{
	int ret = 0;
	
  	CURL* curl = NULL;
  	CURLcode res = CURLE_OK;
  	struct FtpFile ftpfile =
  		{
    		dstfile, /* name to store the file as if succesful */ 
    		NULL
  		};
 
  	curl_global_init(CURL_GLOBAL_DEFAULT);
 
  	curl = curl_easy_init();
  	if(curl != NULL) 
  	{
    	//超时设置（单位：秒），如果在指定时间内没数据可接收则超时
    	curl_easy_setopt( curl, CURLOPT_TIMEOUT, 6);
        //连接超时，不过只适用于Unix系统，Windows系统应该就是使用CURLOPT_TIMEOUT
    	curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 2);
    	/*
     		* You better replace the URL with one that works!
     		*/ 
    	curl_easy_setopt(curl, CURLOPT_URL, srcfile);
    	/* Define our callback to get called when there's data to be written */ 
	    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, my_fwrite);	    
    	/* Set a pointer to our struct to pass to the callback */ 
    	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ftpfile);
 
    	/* Switch on full protocol/debug output */ 
    	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
 
    	res = curl_easy_perform(curl); 
    	if(CURLE_OK == res)
    	{
        	long http_res_code = 0;
        	res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_res_code);
        	if(http_res_code != 200)
        	{
            	ret = -1;
        	}
        	else
        	{
            	// CURLINFO_CONTENT_LENGTH_DOWNLOAD == CURLINFO_SIZE_DOWNLOAD
            	double content_length = 0.0;
            	double download_length = 0.0;
            	res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
            	res = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &download_length);
	            if(content_length!=0 && download_length==content_length)
	            {
	                //do nothing.
	            }
	            else
	            {
	                ret = -1;
	            }           
        	}
        
    	}
    	else
    	{
        	ret = -1;
      		/* we failed */ 
      		fprintf(stderr, "curl told us %d\n", res);
      		//fprintf(stdout, "curl told us %d\n", res);
    	}

    	/* always cleanup */ 
    	curl_easy_cleanup(curl);
  	}
 
  	if(ftpfile.stream)
    	fclose(ftpfile.stream); /* close the local file */ 
 
  	curl_global_cleanup();
 
  	return ret;
}

