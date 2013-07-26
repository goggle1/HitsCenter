//file: HTTPListenerSocket.cpp

#include "HTTPSession.h"
#include "HTTPListenerSocket.h"

Task*   HTTPListenerSocket::GetSessionTask(TCPSocket** outSocket)
{ 
    HTTPSession* theTask = new HTTPSession();
    *outSocket = theTask->GetSocket();  // out socket is not attached to a unix socket yet.
        
    return theTask;
}


Bool16 HTTPListenerSocket::OverMaxConnections(UInt32 buffer)
{
    return false;
}


