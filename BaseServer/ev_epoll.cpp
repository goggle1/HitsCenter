#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/errno.h>

#include <sys/epoll.h>

#include "MyAssert.h"

#include "OS.h"
#include "OSThread.h"
#include "OSMutex.h"
#include "ev.h"
#include "ev_epoll.h"

#define EPOLL_SIZE 		(1024*1024)

static int                s_epoll_fd = -1;
static struct epoll_event s_epoll_events[EPOLL_SIZE];

static void**   s_CookieArray = NULL;
static int*     s_FDsToCloseArray = NULL;
static int 		s_Pipes[2];
static OSMutex  s_MaxFDPosMutex;
static UInt32   s_NumFDsProcessed = 0;
static int      s_NumFDsBackFromEpoll = 0;
//static bool     s_InReadSet = true;
static int 		s_CurrentFDPos = 0;

int construct_event_req(struct eventreq* req, int fd_pos, int event)
{	
	int fd = s_epoll_events[fd_pos].data.fd;

	if(event == 0x00000000)
	{
		fprintf(stdout, "%s: fd %d, events=0x00000000\n", __FUNCTION__, fd);
	}
	
	#if 0
    Assert(fd < (int)(sizeof(fd_set) * 8));
    if (fd >=(int)(sizeof(fd_set) * 8) )
    {
        #if EV_DEBUGGING
                qtss_printf("construct_event_req: invalid fd=%d\n", fd);
        #endif
        return 0;
    }    
    #endif
    req->er_handle = fd;
    req->er_eventbits = event;
    req->er_data = s_CookieArray[fd];
    s_CurrentFDPos++;
    s_NumFDsProcessed++;
    
    //don't want events on this fd until modwatch is called.
    #if 0
    FD_CLR(fd, &sWriteSet);
    FD_CLR(fd, &sReadSet);
    #endif

	#if 0    
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = 0;
    int ret = epoll_ctl(s_epoll_fd, EPOLL_CTL_MOD, fd, &ev);    
    fprintf(stdout, "%s: epoll_ctl mod %d, return %d, errno=%d, %s\n", __FUNCTION__, fd, ret, errno, strerror(errno));
    #endif
    
    return 0;
}

void pipe_has_event()
{
	char theBuffer[4096]; 
    (void)::read(s_Pipes[0], &theBuffer[0], 4096);
    
    {
        //Check the fds to close array, and if there are any in it, close those descriptors
        //OSMutexLocker locker(&s_MaxFDPosMutex);
        for (UInt32 theIndex = 0; ((s_FDsToCloseArray[theIndex] != -1) && (theIndex < EPOLL_SIZE)); theIndex++)
        {
            (void)::close(s_FDsToCloseArray[theIndex]);
            s_FDsToCloseArray[theIndex] = -1;
        }
    }
}

bool epoll_has_data()
{
    if (s_NumFDsBackFromEpoll < 0)
    {
        int err=OSThread::GetErrno();
        
#if EV_DEBUGGING
        if (err == ENOENT) 
        {
             qtss_printf("selectHasdata: found error ENOENT==2 \n");
        }
#endif

        if ( 
#if __solaris__
            err == ENOENT || // this happens on Solaris when an HTTP fd is closed
#endif      
            err == EBADF || //this might happen if a fd is closed right before calling select
            err == EINTR 
           ) // this might happen if select gets interrupted
             return false;
        return true;//if there is an error from select, we want to make sure and return to the caller
    }
        
    if (s_NumFDsBackFromEpoll == 0)
        return false;//if select returns 0, we've simply timed out, so recall select

    #if 0
    if (FD_ISSET(s_Pipes[0], &sReturnedReadSet))
    {
#if EV_DEBUGGING
        qtss_printf("epoll_has_data: Got some data on the pipe fd\n");
#endif
        //we've gotten data on the pipe file descriptor. Clear the data.
        // increasing the select buffer fixes a hanging problem when the Darwin server is under heavy load
        // CISCO contribution
        char theBuffer[4096]; 
        (void)::read(s_Pipes[0], &theBuffer[0], 4096);

        FD_CLR(s_Pipes[0], &sReturnedReadSet);
        s_NumFDsBackFromEpoll--;
        
        {
            //Check the fds to close array, and if there are any in it, close those descriptors
            OSMutexLocker locker(&s_MaxFDPosMutex);
            for (UInt32 theIndex = 0; ((s_FDsToCloseArray[theIndex] != -1) && (theIndex < sizeof(fd_set) * 8)); theIndex++)
            {
                (void)::close(s_FDsToCloseArray[theIndex]);
                s_FDsToCloseArray[theIndex] = -1;
            }
        }
    }
    Assert(!FD_ISSET(s_Pipes[0], &sReturnedWriteSet));
    #endif
    
    if (s_NumFDsBackFromEpoll == 0)
        return false;//if the pipe file descriptor is the ONLY data we've gotten, recall select
    else
        return true;//we've gotten a real event, return that to the caller
}



void epoll_startevents()
{
	s_epoll_fd = epoll_create(EPOLL_SIZE);

	//qtss_printf("FD_SETSIZE=%d sizeof(fd_set) * 8 ==%ld\n", FD_SETSIZE, sizeof(fd_set) * 8);
    //We need to associate cookies (void*)'s with our file descriptors.
    //We do so by storing cookies in this cookie array. Because an fd_set is
    //a big array of bits, we should have as many entries in the array as
    //there are bits in the fd set  
    s_CookieArray = new void*[EPOLL_SIZE];
    ::memset(s_CookieArray, 0, sizeof(void *) * EPOLL_SIZE);

	//We need to close all fds from the select thread. Once an fd is passed into
    //removeevent, its added to this array so it may be deleted from the select thread
    s_FDsToCloseArray = new int[EPOLL_SIZE];
    for (int i = 0; i < EPOLL_SIZE; i++)
        s_FDsToCloseArray[i] = -1;
    
    //We need to wakeup select when the masks have changed. In order to do this,
    //we create a pipe that gets written to from modwatch, and read when select returns
    int theErr = ::pipe((int*)&s_Pipes);
    Assert(theErr == 0);
    
    //Add the read end of the pipe to the read mask
    #if 0
    FD_SET(s_Pipes[0], &sReadSet);
    sMaxFDPos = s_Pipes[0];
	#endif
	struct epoll_event ev;
	ev.data.fd = s_Pipes[0];
	ev.events = EPOLLIN|EPOLLET;
	epoll_ctl(s_epoll_fd, EPOLL_CTL_ADD, s_Pipes[0], &ev);
    
}

int epoll_modwatch(struct eventreq *req, int which)
{
    {
        //Manipulating sMaxFDPos is not pre-emptive safe, so we have to wrap it in a mutex
        //I believe this is the only variable that is not preemptive safe....
        OSMutexLocker locker(&s_MaxFDPosMutex);

		struct epoll_event ev;
		ev.data.fd = req->er_handle;
		ev.events = EPOLLET;
	
        //Add or remove this fd from the specified sets
        if (which & EV_RE)
        {
        	ev.events = ev.events | EPOLLIN;
        }
        
        if (which & EV_WR)
        {
            ev.events = ev.events | EPOLLOUT;
        }

        int ret = epoll_ctl(s_epoll_fd, EPOLL_CTL_MOD, req->er_handle, &ev);
        fprintf(stdout, "%s: epoll_ctl mod %d[%d], return %d\n", 
        	__FUNCTION__, req->er_handle, which, ret);
        if(ret != 0)
        {
        	fprintf(stderr, "%s: epoll_ctl mod %d[%d], return %d, errno=%d, %s\n", 
	        	__FUNCTION__, req->er_handle, which, ret, errno, strerror(errno));
        }

		#if 0
        if (req->er_handle > sMaxFDPos)
            sMaxFDPos = req->er_handle;
        #endif

        //
        // Also, modifying the cookie is not preemptive safe. This must be
        // done atomically wrt setting the fd in the set. Otherwise, it is
        // possible to have a NULL cookie on a fd.
        Assert(req->er_handle < EPOLL_SIZE);
        Assert(req->er_data != NULL);
        s_CookieArray[req->er_handle] = req->er_data;
    }
    
    //write to the pipe so that select wakes up and registers the new mask
    int theErr = ::write(s_Pipes[1], "p", 1);
    Assert(theErr == 1);

    return 0;
}


int epoll_watchevent(struct eventreq *req, int which)
{
    {
        //Manipulating sMaxFDPos is not pre-emptive safe, so we have to wrap it in a mutex
        //I believe this is the only variable that is not preemptive safe....        
        OSMutexLocker locker(&s_MaxFDPosMutex);

		struct epoll_event ev;
		ev.data.fd = req->er_handle;
		ev.events = EPOLLET;
	
        //Add or remove this fd from the specified sets
        if (which & EV_RE)
        {
        	ev.events = ev.events | EPOLLIN;
        }
        
        if (which & EV_WR)
        {
            ev.events = ev.events | EPOLLOUT;
        }

        int ret = epoll_ctl(s_epoll_fd, EPOLL_CTL_ADD, req->er_handle, &ev);
        fprintf(stdout, "%s: epoll_ctl add %d[%d], return %d\n", 
        	__FUNCTION__, req->er_handle, which, ret);
		if(ret != 0)
        {
        	fprintf(stderr, "%s: epoll_ctl add %d[%d], return %d, errno=%d, %s\n", 
	        	__FUNCTION__, req->er_handle, which, ret, errno, strerror(errno));
        }
        
		#if 0
        if (req->er_handle > sMaxFDPos)
            sMaxFDPos = req->er_handle;
        #endif

        //
        // Also, modifying the cookie is not preemptive safe. This must be
        // done atomically wrt setting the fd in the set. Otherwise, it is
        // possible to have a NULL cookie on a fd.
        Assert(req->er_handle < EPOLL_SIZE);
        Assert(req->er_data != NULL);
        s_CookieArray[req->er_handle] = req->er_data;
    }
    
    //write to the pipe so that select wakes up and registers the new mask
    int theErr = ::write(s_Pipes[1], "p", 1);
    Assert(theErr == 1);

    return 0;
}

int epoll_removeevent(int which)
{

    {
        //Manipulating sMaxFDPos is not pre-emptive safe, so we have to wrap it in a mutex
        //I believe this is the only variable that is not preemptive safe....        
        OSMutexLocker locker(&s_MaxFDPosMutex);    
        
		struct epoll_event ev;
		ev.data.fd = which;
		ev.events = EPOLLET;
        int ret  = epoll_ctl(s_epoll_fd, EPOLL_CTL_DEL, which, &ev);
        fprintf(stdout, "%s: epoll_ctl del %d, return %d\n", 
        	__FUNCTION__, which, ret);
    	if(ret != 0)
        {
        	fprintf(stderr, "%s: epoll_ctl del %d, return %d, errno=%d, %s\n", 
	        	__FUNCTION__, which, ret, errno, strerror(errno));
        }
        
        s_CookieArray[which] = NULL; // Clear out the cookie

        //We also need to keep the mutex locked during any manipulation of the
        //s_FDsToCloseArray, because it's definitely not preemptive safe.
            
        //put this fd into the fd's to close array, so that when select wakes up, it will
        //close the fd
        UInt32 theIndex = 0;
        while ((s_FDsToCloseArray[theIndex] != -1) && (theIndex < EPOLL_SIZE) )
            theIndex++;
        Assert(s_FDsToCloseArray[theIndex] == -1);
        s_FDsToCloseArray[theIndex] = which;
#if EV_DEBUGGING
    qtss_printf("removeevent: Disabled %d \n", which);
#endif
    }
    
    //write to the pipe so that select wakes up and registers the new mask
    int theErr = ::write(s_Pipes[1], "p", 1);
    Assert(theErr == 1);

    return 0;
}

int epoll_waitevent(struct eventreq *req, void* /*onlyForMacOSX*/)
{
    //Check to see if we still have some select descriptors to process
    int theFDsProcessed = (int)s_NumFDsProcessed;
    bool isSet = false;
    
    if (theFDsProcessed < s_NumFDsBackFromEpoll)
    {
        //if (s_InReadSet)
        {
            OSMutexLocker locker(&s_MaxFDPosMutex);
#if EV_DEBUGGING
            qtss_printf("waitevent: Looping through readset starting at %d\n", s_CurrentFDPos);
#endif		
			#if 0
            while((!(isSet = FD_ISSET(s_CurrentFDPos, &sReturnedReadSet))) && (s_CurrentFDPos < sMaxFDPos)) 
                s_CurrentFDPos++;        
            #endif
            while( s_CurrentFDPos<s_NumFDsBackFromEpoll )
            {
            	if((s_epoll_events[s_CurrentFDPos].events | EPOLLIN) || (s_epoll_events[s_CurrentFDPos].events | EPOLLOUT))
            	{
            		if(s_epoll_events[s_CurrentFDPos].data.fd == s_Pipes[0])
            		{
            			pipe_has_event();   
					    s_NumFDsProcessed++;
            		}
            		else
            		{
            			isSet = true;
            			break;
            		}
            	}
            	s_CurrentFDPos++;            
           	}

            if (isSet)
            {   
#if EV_DEBUGGING
                qtss_printf("waitevent: Found an fd: %d in readset max=%d\n", s_CurrentFDPos, sMaxFDPos);
#endif				
                //FD_CLR(s_CurrentFDPos, &sReturnedReadSet);
                int event = 0;
                if(s_epoll_events[s_CurrentFDPos].events & EPOLLIN)
                {
                	event |= EV_RE;
                }
                if(s_epoll_events[s_CurrentFDPos].events & EPOLLOUT)
                {
                	event |= EV_WR;
                }
                return construct_event_req(req, s_CurrentFDPos, event);
            }
            else
            {
#if EV_DEBUGGING
                qtss_printf("waitevent: Stopping traverse of readset at %d\n", s_CurrentFDPos);
#endif
                //s_InReadSet = false;
                s_CurrentFDPos = 0;
            }
        }
#if 0
        if (!s_InReadSet)
        {
            OSMutexLocker locker(&s_MaxFDPosMutex);
#if EV_DEBUGGING
            qtss_printf("waitevent: Looping through writeset starting at %d\n", s_CurrentFDPos);
#endif
			#if 0
            while((!(isSet = FD_ISSET(s_CurrentFDPos, &sReturnedWriteSet))) && (s_CurrentFDPos < sMaxFDPos))
                s_CurrentFDPos++;            
            #endif
			//while((!(isSet = s_epoll_events[s_CurrentFDPos].events | EPOLLOUT)) && (s_CurrentFDPos<s_NumFDsBackFromEpoll) )
			//				s_CurrentFDPos++;	
			while( s_CurrentFDPos<s_NumFDsBackFromEpoll )
            {
            	if(s_epoll_events[s_CurrentFDPos].events | EPOLLOUT)
            	{
            		if(s_epoll_events[s_CurrentFDPos].data.fd == s_Pipes[0])
            		{
            			//pipe_has_event();
            			s_NumFDsProcessed ++;
            		}
            		else
            		{
            			isSet = true;
            			break;
            		}
            	}
            	s_CurrentFDPos++;            
           	}

            if (isSet)
            {
#if EV_DEBUGGING
                qtss_printf("waitevent: Found an fd: %d in writeset\n", s_CurrentFDPos);
#endif
                //FD_CLR(s_CurrentFDPos, &sReturnedWriteSet);
                return construct_event_req(req, s_CurrentFDPos, EV_WR);
            }
            else
            {
                // This can happen if another thread calls select_removeevent at just the right
                // time, setting sMaxFDPos lower than it was when select() was last called.
                // Becase sMaxFDPos is used as the place to stop iterating over the read & write
                // masks, setting it lower can cause file descriptors in the mask to get skipped.
                // If they are skipped, that's ok, because those file descriptors were removed
                // by select_removeevent anyway. We need to make sure to finish iterating over
                // the masks and call select again, which is why we set s_NumFDsProcessed
                // artificially here.
                s_NumFDsProcessed = s_NumFDsBackFromEpoll;
                Assert(s_NumFDsBackFromEpoll > 0);
            }
        }
#endif
    }
    
    if (s_NumFDsProcessed > 0)
    {
        OSMutexLocker locker(&s_MaxFDPosMutex);
#if DEBUG
        //
        // In a very bizarre circumstance (sMaxFDPos goes down & then back up again, these
        // asserts could hit.
        //
        //for (int x = 0; x < sMaxFDPos; x++)
        //  Assert(!FD_ISSET(x, &sReturnedReadSet));
        //for (int y = 0; y < sMaxFDPos; y++)
        //  Assert(!FD_ISSET(y, &sReturnedWriteSet));
#endif  
#if EV_DEBUGGING
        qtss_printf("waitevent: Finished with all fds in set. Stopped traverse of writeset at %d maxFD = %d\n", s_CurrentFDPos,sMaxFDPos);
#endif
        //We've just cycled through one select result. Re-init all the counting states
        s_NumFDsProcessed = 0;
        s_NumFDsBackFromEpoll = 0;
        s_CurrentFDPos = 0;
        //s_InReadSet = true;
    }
    
    
    
    while(!epoll_has_data())
    {
    	#if 0
        {
            OSMutexLocker locker(&s_MaxFDPosMutex);
            //Prepare to call select. Preserve the read and write sets by copying their contents
            //into the corresponding "returned" versions, and then pass those into select
            ::memcpy(&sReturnedReadSet, &sReadSet, sizeof(fd_set));
            ::memcpy(&sReturnedWriteSet, &sWriteSet, sizeof(fd_set));
        }
        #endif

        SInt64  yieldDur = 0;
        SInt64  yieldStart;
        
        //Periodically time out the select call just in case we
        //are deaf for some reason
        // on platforw's where our threading is non-preemptive, just poll select

        struct timeval  tv;
        tv.tv_usec = 0;

    #if THREADING_IS_COOPERATIVE
        tv.tv_sec = 0;
        
        if ( yieldDur > 4 )
            tv.tv_usec = 0;
        else
            tv.tv_usec = 5000;
    #else
        tv.tv_sec = 15;
    #endif
    	int timeout = 15*1000;

#if EV_DEBUGGING
        qtss_printf("waitevent: about to call select\n");
#endif

        yieldStart = OS::Milliseconds();
        OSThread::ThreadYield();
        
        yieldDur = OS::Milliseconds() - yieldStart;
#if EV_DEBUGGING
        static SInt64   numZeroYields;
        
        if ( yieldDur > 1 )
        {
            qtss_printf( "select_waitevent time in OSThread::Yield() %i, numZeroYields %i\n", (SInt32)yieldDur, (SInt32)numZeroYields );
            numZeroYields = 0;
        }
        else
            numZeroYields++;

#endif

		#if 0
        s_NumFDsBackFromEpoll = ::select(sMaxFDPos+1, &sReturnedReadSet, &sReturnedWriteSet, NULL, &tv);
        #endif
        memset(s_epoll_events, 0, sizeof(s_epoll_events));
        s_NumFDsBackFromEpoll = ::epoll_wait(s_epoll_fd, s_epoll_events, EPOLL_SIZE, timeout);
        if(s_NumFDsBackFromEpoll == -1)
        {
        	if(errno != EINTR)
        	{
        		qtss_printf( "epoll_wait errno=%d, %s\n", errno, strerror(errno));
        	}
        }

#if EV_DEBUGGING
        qtss_printf("waitevent: back from select. Result = %d\n", s_NumFDsBackFromEpoll);
#endif
    }
    

    if (s_NumFDsBackFromEpoll >= 0)
        return EINTR;   //either we've timed out or gotten some events. Either way, force caller
                        //to call waitevent again.
    return s_NumFDsBackFromEpoll;
}


