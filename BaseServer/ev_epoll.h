
#ifndef __EV_EPOLL_H__
#define __EV_EPOLL_H__

int epoll_watchevent(struct eventreq *req, int which);
int epoll_modwatch(struct eventreq *req, int which);
int epoll_waitevent(struct eventreq *req, void* onlyForMOSX);
void epoll_startevents();
int epoll_removeevent(int which);

#endif
