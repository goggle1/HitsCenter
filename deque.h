#ifndef __DEQUE_H__
#define __DEQUE_H__

#include <stdlib.h>

typedef struct deque_node
{
    struct deque_node* prevp;
    struct deque_node* nextp;
    void* datap;
} DEQUE_NODE;

inline DEQUE_NODE* deque_remove_head(DEQUE_NODE* headp)
{
    if(headp == NULL)
    {
        return NULL;
    }
    
    DEQUE_NODE* nextp = headp->nextp;
    if(nextp == headp)
    {
        free(headp);
        headp = NULL;
        return NULL;
    }

    DEQUE_NODE* tailp = headp->prevp;
    nextp->prevp = tailp;
    tailp->nextp = nextp;

    free(headp);
    headp = NULL;
        
    headp = nextp;
    return headp;
}


inline DEQUE_NODE* deque_remove_node(DEQUE_NODE* headp, DEQUE_NODE* nodep)
{
    if(headp == NULL)
    {
        return NULL;
    }
    if(nodep == NULL)
    {
        return headp;
    }

    if(nodep == headp)
    {
        return deque_remove_head(headp);
    }

    DEQUE_NODE* prevp = nodep->prevp;
    DEQUE_NODE* nextp = nodep->nextp;

    prevp->nextp = nextp;
    nextp->prevp = prevp;

    free(nodep);    
    
    return headp;
}


inline DEQUE_NODE* deque_append(DEQUE_NODE* headp, DEQUE_NODE* nodep)
{
    if(nodep == NULL)
    {
        return headp;
    }
    
    if(headp == NULL)
    {
        nodep->nextp = nodep;
        nodep->prevp = nodep;
        return nodep;
    }

    DEQUE_NODE* tailp = headp->prevp;

    tailp->nextp = nodep;
    nodep->prevp = tailp;
    nodep->nextp = headp;
    headp->prevp = nodep;
    
    return headp;
    
}

inline DEQUE_NODE* deque_link(DEQUE_NODE* list1p, DEQUE_NODE* list2p)
{
    if(list2p == NULL)
    {
        return list1p;
    }
    
    if(list1p == NULL)
    {        
        return list2p;
    }

    DEQUE_NODE* head1p = list1p;
    DEQUE_NODE* tail1p = list1p->prevp;
    DEQUE_NODE* head2p = list2p;
    DEQUE_NODE* tail2p = list2p->prevp;

    tail1p->nextp = head2p;
    head2p->prevp = tail1p;
    tail2p->nextp = head1p;
    head1p->prevp = tail2p;
    
    return head1p;
    
}

typedef void (*DATA_RELEASE_FUNC)(void* datap);

inline void deque_release(DEQUE_NODE* headp, DATA_RELEASE_FUNC func)
{
    if(headp == NULL)
    {
        return;
    }
    if(func == NULL)
    {
        return;
    }
        

    DEQUE_NODE* nodep = headp;
    DEQUE_NODE* tempp = nodep;
    while(nodep)
    {
        tempp = nodep->nextp;

        if(func)
        {
        	func(nodep->datap);
       	}
        free(nodep);

        if(tempp == headp)
        {
            break;
        }
        nodep = tempp;
    }
    
}


#endif


