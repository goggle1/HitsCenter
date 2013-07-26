//file: HTTPSession.cpp

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
       
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "BaseServer/StringParser.h"

#include "HTTPSession.h"

#define BASE_SERVER_NAME 	"TeslaStreamingServer"
#define BASE_SERVER_VERSION "1.0"
#define ROOT_PATH			"/home/html"

#define CONTENT_TYPE_TEXT_HTML	"text/html"
#define CONTENT_TYPE_VIDEO_MP4	"video/mp4"

#define CHARSET_UTF8		"utf-8"


static char template_response_http_error[] = 
            "HTTP/1.0 %s %s\r\n" //error_code error_reason
            "Server: %s/%s\r\n" //ServerName ServerVersion
            "\r\n";

Bool16 file_exist(char* abs_path)
{
	int ret = 0;
	ret = access(abs_path, R_OK);
	if(ret == 0)
	{
		return true;
	}

	
	return false;
}

char* file_suffix(char* abs_path)
{
	char* suffix = strstr(abs_path, ".");
	return suffix;
}

char* content_type_by_suffix(char* suffix)
{
	if(suffix == NULL)
	{
		return CONTENT_TYPE_TEXT_HTML;
	}
	
	if(strcasecmp(suffix, ".htm") == 0)
	{
		return CONTENT_TYPE_TEXT_HTML;
	}
	else if(strcasecmp(suffix, ".html") == 0)
	{
		return CONTENT_TYPE_TEXT_HTML;
	}
	else if(strcasecmp(suffix, ".mp4") == 0)
	{
		return CONTENT_TYPE_VIDEO_MP4;
	}
	else
	{
		return CONTENT_TYPE_TEXT_HTML;
	}
}

HTTPSession::HTTPSession():
    fSocket(NULL, Socket::kNonBlockingSocketType),
    fStrReceived((char*)fRequestBuffer, 0),
    fStrRequest(fStrReceived),
    fStrResponse((char*)fResponseBuffer, 0),
    fStrRemained(fStrResponse),
    fResponse(NULL, 0)    
{
	fprintf(stdout, "%s %s[%d][0x%016lX] \n", __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this);
	fFd	= -1;
    fStatusCode     = 0;
}

HTTPSession::~HTTPSession()
{
	if(fFd != -1)
	{
		close(fFd);
		fFd = -1;
	}
    fprintf(stdout, "%s %s[%d][0x%016lX] \n", __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this);
}

TCPSocket* HTTPSession::GetSocket() 
{ 
    return &fSocket;
}

SInt64     HTTPSession::Run()
{ 
    Task::EventFlags events = this->GetEvents();    
    if(events == 0x00000000)
    {
    	fprintf(stdout, "%s %s[%d][0x%016lX] events=0x%08X\n",
			__FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, events);
		Disconnect();
    	return -1;
    }
    
    if(events & Task::kKillEvent)
    {
        fprintf(stdout, "%s %s[%d][0x%016lX]: get kKillEvent[0x%08X] \n", 
            __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, events); 
        return -1;
    } 

	int willRequestEvent = 0;
	
    if(events & Task::kWriteEvent)
    {
    	Bool16 sendDone = SendData();    
    	if(!sendDone)
    	{
    		willRequestEvent = willRequestEvent | EV_WR;
    	}
    	else if(fFd != -1)
    	{
    		Bool16 haveContent = ReadFileContent();
    		if(haveContent)
    		{
    			willRequestEvent = willRequestEvent | EV_WR;
    		}
    	}
   	}

    if(events & Task::kReadEvent)
    {
	    QTSS_Error theErr = this->RecvData();
	    if(theErr == QTSS_RequestArrived)
	    {
	        QTSS_Error ok = this->ProcessRequest();  
	        if(ok != QTSS_RequestFailed)
	        {
	        	willRequestEvent = willRequestEvent | EV_WR;
	        	//fSocket.RequestEvent(EV_WR);
	       	}
	       	else
	       	{
	       		willRequestEvent = willRequestEvent | EV_RE;
	       		//fSocket.RequestEvent(EV_RE);
	       	}
	        //return 0;
	    }
	    else if(theErr == QTSS_NoErr)
	    {
	    	willRequestEvent = willRequestEvent | EV_RE;
	        //fSocket.RequestEvent(EV_RE);
	        //return 0;
	    }
	    else if(theErr == EAGAIN)
	    {
	        fprintf(stderr, "%s %s[%d][0x%016lX] theErr == EAGAIN %u, %s \n", 
	            __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, theErr, strerror(theErr));
	        willRequestEvent = willRequestEvent | EV_RE;
	        //fSocket.RequestEvent(EV_RE);
	        //return 0;
	    }
	    else
	    {
	        fprintf(stderr, "%s %s[%d][0x%016lX] theErr %u, %s \n", 
	            __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, theErr, strerror(theErr));
	        Disconnect();    
	        return -1;
	    }   
    }

	if(willRequestEvent == 0)
	{
		// strange.
		// it will be never happened.
		fprintf(stdout, "%s %s[%d][0x%016lX] events=0x%08X\n",
			__FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, events);
		Disconnect();    
	    return -1;		
	}
	else
	{
		fSocket.RequestEvent(willRequestEvent);
	}
	
    return 0;
}

QTSS_Error  HTTPSession::RecvData()
{
    QTSS_Error theErr = QTSS_NoErr;
    StrPtrLen lastReceveid(fStrReceived);

    char* start_pos = NULL;
    UInt32 blank_len = 0;
    start_pos = (char*)fRequestBuffer;
    start_pos += lastReceveid.Len;
    blank_len = kRequestBufferSizeInBytes - lastReceveid.Len;

    UInt32 read_len = 0;
    theErr = fSocket.Read(start_pos, blank_len, &read_len);
    if(theErr != QTSS_NoErr)
    {
        fprintf(stderr, "%s %s[%d][0x%016lX] errno=%d, %s, recv %u, \n", 
            __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, errno, strerror(errno), read_len);
        return theErr;
    }
    
    fprintf(stdout, "%s %s[%d][0x%016lX] recv %u, \n%s", 
        __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, read_len, start_pos);

    fStrReceived.Len += read_len;
   
    bool check = IsFullRequest();
    if(check)
    {
        return QTSS_RequestArrived;
    }
    
    return QTSS_NoErr;
}

Bool16 HTTPSession::SendData()
{  	
    if(fStrRemained.Len <= 0)
    {
    	return true;
    }
    
    OS_Error theErr;
    UInt32 send_len = 0;
    theErr = fSocket.Send(fStrRemained.Ptr, fStrRemained.Len, &send_len);
    if(send_len > 0)
    {        
    	fprintf(stdout, "%s %s[%d][0x%016lX] send %u, return %u\n", 
        __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, fStrRemained.Len, send_len);
        fStrRemained.Ptr += send_len;
        fStrRemained.Len -= send_len;
        ::memmove(fResponseBuffer, fStrRemained.Ptr, fStrRemained.Len);
        fStrRemained.Ptr = fResponseBuffer;        
    }
    else
    {
        fprintf(stderr, "%s %s[%d][0x%016lX] send %u, return %u, errno=%d, %s\n", 
            __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, fStrRemained.Len, send_len, 
            errno, strerror(errno));
    }
    
    if(theErr == EAGAIN)
    {
        fprintf(stderr, "%s %s[%d][0x%016lX] theErr[%d] == EAGAIN[%d], errno=%d, %s \n", 
            __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, theErr, EAGAIN, 
            errno, strerror(errno));
        // If we get this error, we are currently flow-controlled and should
        // wait for the socket to become writeable again
     //   fSocket.RequestEvent(EV_WR);
        //I don't understand, mutexes? where or which?
     //   this->ForceSameThread();    // We are holding mutexes, so we need to force
                                    // the same thread to be used for next Run()
		return false;
    }
    else if(theErr == EPIPE) //Connection reset by peer
    {
    	Disconnect();
    }
    else if(theErr == ECONNRESET) //Connection reset by peer
    {
        Disconnect();
    }

    return true;
}

bool  HTTPSession::IsFullRequest()
{
    fStrRequest.Ptr = fStrReceived.Ptr;
    fStrRequest.Len = 0;
    
    StringParser headerParser(&fStrReceived);
    while (headerParser.GetThruEOL(NULL))
    {        
        if (headerParser.ExpectEOL())
        {
            fStrRequest.Len = headerParser.GetDataParsedLen();
            return true;
        }
    }
    
    return false;
}


Bool16 HTTPSession::Disconnect()
{
    fprintf(stdout, "%s %s[%d][0x%016lX]: Signal kKillEvent \n", 
            __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this);             
    
    this->Signal(Task::kKillEvent);    
    
    return true;
}

QTSS_Error  HTTPSession::ProcessRequest()
{
    QTSS_Error theError;
    
	fRequest.Clear();
	theError = fRequest.Parse(&fStrRequest);   
	if(theError != QTSS_NoErr)
	{
		fprintf(stderr, "%s %s[%d][0x%016lX] HTTPRequest Parse error: %d\n", 
			__FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, theError);

		this->MoveOnRequest();
		return QTSS_RequestFailed;
	}

	Bool16 ok = false;	  
	switch(fRequest.fMethod)
	{
		case httpGetMethod:
			ok = ResponseGet();
			break;		
		default:
		   //unhandled method.
			fprintf(stderr, "%s %s[%d][0x%016lX] unhandled method: %d", 
					__FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, fRequest.fMethod);
			fStatusCode = qtssClientBadRequest;
			ResponseError(fStatusCode);
			break;		  
	}	

    if(ok)
    {
        this->MoveOnRequest();
    }
 
    if(!ok)
    {
        return QTSS_RequestFailed;
    }   
    
    return QTSS_NoErr;
}

Bool16 HTTPSession::ResponseGet()
{
	Bool16 ret = true;

	if(fRequest.fAbsoluteURI.Len == 0)
	{
		return false;
	}
	
	char absolute_uri[fRequest.fAbsoluteURI.Len + 1];
	strncpy(absolute_uri, fRequest.fAbsoluteURI.Ptr, fRequest.fAbsoluteURI.Len);
	absolute_uri[fRequest.fAbsoluteURI.Len] = '\0';

	if(fRequest.fAbsoluteURI.Len == 1 && absolute_uri[0] == '/')
	{
		ret = ResponseHello();
		return ret;
	}
	
	if(fRequest.fAbsoluteURI.Len >= 2 && absolute_uri[0] == '/' && absolute_uri[1] == '?')
	{
		// ?cmd=get_hotest&N=10
		// ?cmd=get_coldest&N=10
		// ?cmd=get_hot&hashid=xxx
		// ?cmd=get_hot&hashid=xxx&ip_group_id=y
		// ?cmd=get_cold&hashid=xxx
		// ?cmd=get_cold&hashid=xxx&ip_group_id=y
		ret = ResponseCmd(absolute_uri);
		return ret;
	}

	char abs_path[PATH_MAX];
	snprintf(abs_path, PATH_MAX-1, "%s%s", ROOT_PATH, absolute_uri);
	abs_path[PATH_MAX-1] = '\0';
	
	if(file_exist(abs_path))
	{
		ret = ResponseFileContent(abs_path);
	}
	else
	{
		ret = ResponseFileNotFound(absolute_uri);
	}

	return ret;

}

Bool16 HTTPSession::ReadFileContent()
{
	if(fFd == -1)
	{
		return false;
	}
	
	ssize_t count = read(fFd, fBuffer, kReadBufferSize);
	if(count < kReadBufferSize)
	{
		close(fFd);
		fFd = -1;
	}

	if(count <= 0)
	{
		return false;
	}

	fprintf(stdout, "%s %s[%d][0x%016lX] read %u, return %lu\n", 
        __FILE__, __PRETTY_FUNCTION__, __LINE__, (long)this, kReadBufferSize, count);

    fResponse.Set(fStrRemained.Ptr+fStrRemained.Len, kResponseBufferSizeInBytes-fStrRemained.Len);
    fResponse.Put(fBuffer, count);

    fStrResponse.Set(fResponse.GetBufPtr(), fResponse.GetBytesWritten());
    //append to fStrRemained
    fStrRemained.Len += fStrResponse.Len;  
    //clear previous response.
    fStrResponse.Set(fResponseBuffer, 0);
	
	
	return true;
}


Bool16 HTTPSession::ResponseFileContent(char* abs_path)
{
	fFd = open(abs_path, O_RDONLY);
	if(fFd == -1)
	{
		return false;
	}
	off_t file_len = lseek(fFd, 0L, SEEK_END);	
	lseek(fFd, 0L, SEEK_SET);

	char* suffix = file_suffix(abs_path);
	char* content_type = content_type_by_suffix(suffix);
	
	fResponse.Set(fStrRemained.Ptr+fStrRemained.Len, kResponseBufferSizeInBytes-fStrRemained.Len);
    fResponse.Put("HTTP/1.0 200 OK\r\n");
    fResponse.PutFmtStr("Server: %s/%s\r\n", BASE_SERVER_NAME, BASE_SERVER_VERSION);
    fResponse.PutFmtStr("Content-Length: %ld\r\n", file_len);
    //fResponse.PutFmtStr("Content-Type: %s; charset=utf-8\r\n", content_type);
    fResponse.PutFmtStr("Content-Type: %s", content_type);
    if(strcmp(content_type, CONTENT_TYPE_TEXT_HTML) == 0)
    {
    	fResponse.PutFmtStr(";charset=%s\r\n", CHARSET_UTF8);
    }
    else
    {
    	fResponse.Put("\r\n");
    }
    fResponse.Put("\r\n"); 
    
    fStrResponse.Set(fResponse.GetBufPtr(), fResponse.GetBytesWritten());
    //append to fStrRemained
    fStrRemained.Len += fStrResponse.Len;  
    //clear previous response.
    fStrResponse.Set(fResponseBuffer, 0);	
	
	return true;
}

Bool16 HTTPSession::ResponseHello()
{
	char	buffer[1024];
	StringFormatter content(buffer, sizeof(buffer));
	
	content.Put("<HTML>\n");
	content.Put("<BODY>\n");

	content.Put("<h2 align=\"center\">");
	content.PutFmtStr("hello, I am %s/%s\n", BASE_SERVER_NAME, BASE_SERVER_VERSION);
	content.Put("</h2>");
	
	content.Put("</BODY>\n");
	content.Put("</HTML>\n");
        
    fResponse.Set(fStrRemained.Ptr+fStrRemained.Len, kResponseBufferSizeInBytes-fStrRemained.Len);
    fResponse.Put("HTTP/1.0 200 OK\r\n");
    fResponse.PutFmtStr("Server: %s/%s\r\n", BASE_SERVER_NAME, BASE_SERVER_VERSION);
    fResponse.PutFmtStr("Content-Length: %d\r\n", content.GetBytesWritten());
    fResponse.PutFmtStr("Content-Type: %s;charset=%s\r\n", CONTENT_TYPE_TEXT_HTML, CHARSET_UTF8);
    fResponse.Put("\r\n"); 
    fResponse.Put(content.GetBufPtr(), content.GetBytesWritten());

    fStrResponse.Set(fResponse.GetBufPtr(), fResponse.GetBytesWritten());
    //append to fStrRemained
    fStrRemained.Len += fStrResponse.Len;  
    //clear previous response.
    fStrResponse.Set(fResponseBuffer, 0);
    
    //SendData();
    
	return true;
}


Bool16 HTTPSession::ResponseCmd(char* absolute_uri)
{
	char	buffer[1024];
	StringFormatter content(buffer, sizeof(buffer));
	
	content.Put("<HTML>\n");
	content.Put("<BODY>\n");

	content.PutFmtStr("cmd=%s\n", absolute_uri);	
	//todo:
		
	content.Put("</BODY>\n");
	content.Put("</HTML>\n");
        
    fResponse.Set(fStrRemained.Ptr+fStrRemained.Len, kResponseBufferSizeInBytes-fStrRemained.Len);
    fResponse.Put("HTTP/1.0 200 OK\r\n");
    fResponse.PutFmtStr("Server: %s/%s\r\n", BASE_SERVER_NAME, BASE_SERVER_VERSION);
    fResponse.PutFmtStr("Content-Length: %d\r\n", content.GetBytesWritten());
    fResponse.PutFmtStr("Content-Type: %s;charset=%s\r\n", CONTENT_TYPE_TEXT_HTML, CHARSET_UTF8);
    fResponse.Put("\r\n"); 
    fResponse.Put(content.GetBufPtr(), content.GetBytesWritten());

    fStrResponse.Set(fResponse.GetBufPtr(), fResponse.GetBytesWritten());
    //append to fStrRemained
    fStrRemained.Len += fStrResponse.Len;  
    //clear previous response.
    fStrResponse.Set(fResponseBuffer, 0);
    
    //SendData();
    
	return true;
}

Bool16 HTTPSession::ResponseFileNotFound(char* absolute_uri)
{
	char	buffer[1024];
	StringFormatter content(buffer, sizeof(buffer));
	
	content.Put("<HTML>\n");
	content.Put("<BODY>\n");
	content.Put("<TABLE border=\"1\" align=\"center\" cellpadding=\"5\">\n");

	content.Put("<TR>\n");	
	content.Put("<TH>\n");
	content.Put("uri is not found! \n");
	content.Put("</TH>\n");
	content.Put("<TD>\n");
	content.PutFmtStr("%s\n", absolute_uri);
	content.Put("</TD>\n");	
	content.Put("</TR>\n");

	content.Put("<TR>\n");	
	content.Put("<TH>\n");
	content.Put("your ip:");
	content.Put("</TH>\n");
	content.Put("<TD>\n");
	UInt32  remote_ip =  fSocket.GetRemoteAddr();
	content.PutFmtStr("%u.%u.%u.%u\n", 
		(remote_ip & 0xFF000000) >> 24,
		(remote_ip & 0x00FF0000) >> 16,
		(remote_ip & 0x0000FF00) >> 8,
		(remote_ip & 0x000000FF) >> 0
		);
	content.Put("</TD>\n");	
	content.Put("</TR>\n");
	
	content.Put("<TR>\n");	
	content.Put("<TH>\n");
	content.Put("your port:\n");
	content.Put("</TH>\n");
	content.Put("<TD>\n");
	content.PutFmtStr("%u\n", fSocket.GetRemotePort());	
	content.Put("</TD>\n");	
	content.Put("</TR>\n");


	content.Put("<TR>\n");	
	content.Put("<TH>\n");
	content.Put("server:\n");
	content.Put("</TH>\n");
	content.Put("<TD>\n");
	content.PutFmtStr("%s/%s\n", BASE_SERVER_NAME, BASE_SERVER_VERSION);	
	content.Put("</TD>\n");	
	content.Put("</TR>\n");

	content.Put("</TABLE>\n");		
	content.Put("</BODY>\n");
	content.Put("</HTML>\n");
        
    fResponse.Set(fStrRemained.Ptr+fStrRemained.Len, kResponseBufferSizeInBytes-fStrRemained.Len);
    fResponse.Put("HTTP/1.0 200 OK\r\n");
    fResponse.PutFmtStr("Server: %s/%s\r\n", BASE_SERVER_NAME, BASE_SERVER_VERSION);
    fResponse.PutFmtStr("Content-Length: %d\r\n", content.GetBytesWritten());
    fResponse.PutFmtStr("Content-Type: %s;charset=%s\r\n", CONTENT_TYPE_TEXT_HTML, CHARSET_UTF8);
    fResponse.Put("\r\n"); 
    fResponse.Put(content.GetBufPtr(), content.GetBytesWritten());

    fStrResponse.Set(fResponse.GetBufPtr(), fResponse.GetBytesWritten());
    //append to fStrRemained
    fStrRemained.Len += fStrResponse.Len;  
    //clear previous response.
    fStrResponse.Set(fResponseBuffer, 0);
    
    //SendData();
    
	return true;
}

Bool16 HTTPSession::ResponseError(QTSS_RTSPStatusCode status_code)
{
    StrPtrLen blank;
    blank.Set(fStrRemained.Ptr+fStrRemained.Len, kResponseBufferSizeInBytes-fStrRemained.Len);
    int response_len = 0;
    
    response_len = snprintf(blank.Ptr, blank.Len-1, template_response_http_error,
            HTTPProtocol::GetStatusCodeAsString(status_code)->Ptr,  
            HTTPProtocol::GetStatusCodeString(status_code)->Ptr,  
            BASE_SERVER_NAME,
            BASE_SERVER_VERSION); 

    fStrResponse.Set(blank.Ptr, response_len);
    
    //append to fStrRemained
    fStrRemained.Len += fStrResponse.Len;  

    //clear previous response.
    fStrResponse.Set(fResponseBuffer, 0);

    //bool ok = SendData();
    //return ok;
    return true;
}



void HTTPSession::MoveOnRequest()
{
    StrPtrLen   strRemained;
    strRemained.Set(fStrRequest.Ptr+fStrRequest.Len, fStrReceived.Len-fStrRequest.Len);
        
    ::memmove(fRequestBuffer, strRemained.Ptr, strRemained.Len);
    fStrReceived.Set(fRequestBuffer, strRemained.Len);
    fStrRequest.Set(fRequestBuffer, 0);
}




