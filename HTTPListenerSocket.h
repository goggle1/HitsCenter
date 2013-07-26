//file: HTTPListenerSocket.h

#ifndef __HTTPListenerSocket_H__
#define __HTTPListenerSocket_H__

#include "BaseServer/TCPListenerSocket.h"

class HTTPListenerSocket : public TCPListenerSocket
{
    public:
    
        HTTPListenerSocket() {}
        virtual ~HTTPListenerSocket() {}
        
        //sole job of this object is to implement this function
        virtual Task*   GetSessionTask(TCPSocket** outSocket);
        
        //check whether the Listener should be idling
        Bool16 OverMaxConnections(UInt32 buffer);

};

#endif


