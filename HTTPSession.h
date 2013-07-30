//file: HTTPSession.h
#ifndef __HTTPSESSION_H__
#define __HTTPSESSION_H__

#include <map>
#include <string>

#include "BaseServer/OSHeaders.h"
#include "BaseServer/StringFormatter.h"
#include "BaseServer/Task.h"
#include "BaseServer/TCPSocket.h"

#include "HTTPRequest.h"

using namespace std;

class HTTPSession : public Task
{
public:
            HTTPSession();
virtual    ~HTTPSession();
            TCPSocket*  GetSocket();
            //inherited from Task
virtual     SInt64      Run();

protected:
			QTSS_Error      RecvData();
        	Bool16          SendData();
       	 	bool            IsFullRequest();
        	Bool16          Disconnect();
        	QTSS_Error      ProcessRequest();
        	Bool16          ResponseGet();
        	Bool16 			ReadFileContent();
        	Bool16 			ResponseFileContent(char* absolute_path);
        	Bool16 			ResponseHello();
        	Bool16 			ResponseCmd(char* absolute_uri);
        	Bool16 			ResponseFileNotFound(char* absolute_uri);
        	Bool16 			ResponseError(QTSS_RTSPStatusCode StatusCode);
	        void            MoveOnRequest();
	        Bool16			ParseParams(char* params);

			TCPSocket	fSocket;
		
			//CONSTANTS:
			enum
			{
				kRequestBufferSizeInBytes = 2048,		 //UInt32			 
			};
			char		fRequestBuffer[kRequestBufferSizeInBytes];
			//received all the data, maybe one and half rtsp request.
			StrPtrLen	fStrReceived;
			//one full rtsp request.
			StrPtrLen	fStrRequest; 
			//
			HTTPRequest fRequest;
	
			//CONSTANTS:
			enum
			{
				kResponseBufferSizeInBytes = 2048*10,		 //UInt32	
				kReadBufferSize = 1024*16,
			};
			char		fResponseBuffer[kResponseBufferSizeInBytes];
			//one full rtsp response.
			StrPtrLen	fStrResponse; 
			//rtsp response left, which will be sended again.
			StrPtrLen	fStrRemained;
			// 
			StringFormatter 	fResponse;
			// 
			int			fFd;
			char		fBuffer[kReadBufferSize];

			// from RTSP, 
	        QTSS_RTSPStatusCode fStatusCode;  

			// ?cmd=get_cmd&N=10
	        // map params.
	        // key1=value1&key2=value2
	        // red_black_tree.
	        map<string, string> fParams;


};

#endif


