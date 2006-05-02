/*-------------------------------------------------------------

$Id: cond.c,v 1.13 2006-05-02 11:56:10 shagkur Exp $

cond.c -- Thread subsystem V

Copyright (C) 2004
Michael Wiedenbauer (shagkur)
Dave Murphy (WinterMute)

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.	The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.	Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.	This notice may not be removed or altered from any source
distribution.

$Log: not supported by cvs2svn $
Revision 1.12  2006/05/02 09:32:18  shagkur
- changed object handling and handle typedef

Revision 1.11  2005/12/09 09:35:45  shagkur
no message

Revision 1.10  2005/11/21 12:15:46  shagkur
no message


-------------------------------------------------------------*/


#include <stdlib.h>
#include <errno.h>
#include "asm.h"
#include "mutex.h"
#include "lwp_threadq.h"
#include "lwp_objmgr.h"
#include "lwp_config.h"
#include "cond.h"

typedef struct _cond_st {
	lwp_obj object;
	mutex_t lock;
	lwp_thrqueue wait_queue;
} cond_st;

static lwp_objinfo _lwp_cond_objects;

extern int clock_gettime(struct timespec *tp);
extern void timespec_substract(const struct timespec *tp_start,const struct timespec *tp_end,struct timespec *result);

void __lwp_cond_init()
{
	__lwp_objmgr_initinfo(&_lwp_cond_objects,LWP_MAX_CONDVARS,sizeof(cond_st));
}

static __inline__ cond_st* __lwp_cond_open(cond_t cond)
{
	return (cond_st*)__lwp_objmgr_get(&_lwp_cond_objects,cond);
}

static __inline__ void __lwp_cond_free(cond_st *cond)
{
	__lwp_objmgr_close(&_lwp_cond_objects,&cond->object);
	__lwp_objmgr_free(&_lwp_cond_objects,&cond->object);
}

static cond_st* __lwp_cond_allocate()
{
	cond_st *cond;

	__lwp_thread_dispatchdisable();
	cond = (cond_st*)__lwp_objmgr_allocate(&_lwp_cond_objects);
	if(cond) {
		__lwp_objmgr_open(&_lwp_cond_objects,&cond->object);
		return cond;
	}
	__lwp_thread_dispatchenable();
	return NULL;
}

static s32 __lwp_cond_waitsupp(cond_t cond,mutex_t mutex,u32 timeout,u8 timedout)
{
	u32 status,mstatus,level;
	cond_st *thecond;

	thecond = __lwp_cond_open(cond);
	if(!thecond) return -1;
		
	if(thecond->lock!=LWP_MUTEX_NULL && thecond->lock!=mutex) {
		__lwp_thread_dispatchenable();
		return EINVAL;
	}

	LWP_MutexUnlock(mutex);
	if(!timedout) {
		thecond->lock = mutex;
		_CPU_ISR_Disable(level);
		__lwp_threadqueue_csenter(&thecond->wait_queue);
		_thr_executing->wait.ret_code = 0;
		_thr_executing->wait.queue = &thecond->wait_queue;
		_thr_executing->wait.id = thecond->object.id;
		_CPU_ISR_Restore(level);
		__lwp_threadqueue_enqueue(&thecond->wait_queue,timeout);
		__lwp_thread_dispatchenable();
		
		status = _thr_executing->wait.ret_code;
		if(status && status!=ETIMEDOUT)
			return status;
	} else {
		__lwp_thread_dispatchenable();
		status = ETIMEDOUT;
	}
	mstatus = LWP_MutexLock(mutex);
	if(mstatus)
		return EINVAL;

	return status;
}

static s32 __lwp_cond_signalsupp(cond_t cond,u8 isbroadcast)
{
	lwp_cntrl *thethread;
	cond_st *thecond;
	
	thecond = __lwp_cond_open(cond);
	if(!thecond) return -1;

	do {
		thethread = __lwp_threadqueue_dequeue(&thecond->wait_queue);
		if(!thethread) thecond->lock = LWP_MUTEX_NULL;
	} while(isbroadcast && thethread);
	__lwp_thread_dispatchenable();
	return 0;
}

s32 LWP_CondInit(cond_t *cond)
{
	cond_st *ret;
	
	if(!cond) return -1;
	
	ret = __lwp_cond_allocate();
	if(!ret) return ENOMEM;

	ret->lock = LWP_MUTEX_NULL;
	__lwp_threadqueue_init(&ret->wait_queue,LWP_THREADQ_MODEFIFO,LWP_STATES_WAITING_FOR_CONDVAR,ETIMEDOUT);

	*cond = (cond_t)ret->object.id;
	__lwp_thread_dispatchenable();

	return 0;
}

s32 LWP_CondWait(cond_t cond,mutex_t mutex)
{
	return __lwp_cond_waitsupp(cond,mutex,LWP_THREADQ_NOTIMEOUT,FALSE);
}

s32 LWP_CondSignal(cond_t cond)
{
	return __lwp_cond_signalsupp(cond,FALSE);
}

s32 LWP_CondBroadcast(cond_t cond)
{
	return __lwp_cond_signalsupp(cond,TRUE);
}

s32 LWP_CondTimedWait(cond_t cond,mutex_t mutex,const struct timespec *abstime)
{
	u64 timeout;
	struct timespec curr_time;
	struct timespec diff;
	boolean timedout = FALSE;
	
	clock_gettime(&curr_time);
	timespec_substract(&curr_time,abstime,&diff);
	if(diff.tv_sec<0 || (diff.tv_sec==0&& diff.tv_nsec<0)) timedout = TRUE;

	timeout = __lwp_wd_calc_ticks(&diff);
	return __lwp_cond_waitsupp(cond,mutex,timeout,timedout);
}

s32 LWP_CondDestroy(cond_t cond)
{
	cond_st *ptr;

	ptr = __lwp_cond_open(cond);
	if(!ptr) return -1;

	if(__lwp_threadqueue_first(&ptr->wait_queue)) {
		__lwp_thread_dispatchenable();
		return EBUSY;
	}
	__lwp_thread_dispatchenable();

	__lwp_cond_free(ptr);
	return 0;
}
