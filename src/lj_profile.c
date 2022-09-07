/*
** Low-overhead profiling.
** Copyright (C) 2005-2022 Mike Pall. See Copyright Notice in luajit.h
*/

#define lj_profile_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASPROFILE

#include "lj_buf.h"
#include "lj_frame.h"
#include "lj_debug.h"
#include "lj_dispatch.h"
#if LJ_HASJIT
#include "lj_jit.h"
#include "lj_trace.h"
#endif
#include "lj_profile.h"

#include "luajit.h"

#if LJ_PROFILE_SIGPROF

#include <sys/time.h>
#include <signal.h>
#define profile_lock(ps)	UNUSED(ps)
#define profile_unlock(ps)	UNUSED(ps)

#elif LJ_PROFILE_PTHREAD

#include <pthread.h>
#include <time.h>
#if LJ_TARGET_PS3
#include <sys/timer.h>
#endif
#define profile_lock(ps)	pthread_mutex_lock(&ps->lock)
#define profile_unlock(ps)	pthread_mutex_unlock(&ps->lock)

#elif LJ_PROFILE_WTHREAD

#define WIN32_LEAN_AND_MEAN
#if LJ_TARGET_XBOX360
#include <xtl.h>
#include <xbox.h>
#else
#include <windows.h>
#endif
typedef unsigned int (WINAPI *WMM_TPFUNC)(unsigned int);
#define profile_lock(ps)	EnterCriticalSection(&ps->lock)
#define profile_unlock(ps)	LeaveCriticalSection(&ps->lock)

#endif

/* Profiler state. */
typedef struct ProfileState {
  global_State *g;		/* VM state that started the profiler. */
  luaJIT_profile_callback cb;	/* Profiler callback. */
  void *data;			/* Profiler callback data. */
  SBuf sb;			/* String buffer for stack dumps. */
  int interval;			/* Sample interval in milliseconds. */
  int samples;			/* Number of samples for next callback. */
  int vmstate;			/* VM state when profile timer triggered. */
#if LJ_PROFILE_SIGPROF
  struct sigaction oldsa;	/* Previous SIGPROF state. */
#elif LJ_PROFILE_PTHREAD
  pthread_mutex_t lock;		/* g->hookmask update lock. */
  pthread_t thread;		/* Timer thread. */
  int abort;			/* Abort timer thread. */
#elif LJ_PROFILE_WTHREAD
#if LJ_TARGET_WINDOWS
  HINSTANCE wmm;		/* WinMM library handle. */
  WMM_TPFUNC wmm_tbp;		/* WinMM timeBeginPeriod function. */
  WMM_TPFUNC wmm_tep;		/* WinMM timeEndPeriod function. */
#endif
  CRITICAL_SECTION lock;	/* g->hookmask update lock. */
  HANDLE thread;		/* Timer thread. */
  int abort;			/* Abort timer thread. */
#endif
} ProfileState;

/* Sadly, we have to use a static profiler state.
**
** The SIGPROF variant needs a static pointer to the global state, anyway.
** And it would be hard to extend for multiple threads. You can still use
** multiple VMs in multiple threads, but only profile one at a time.
*/
static ProfileState profile_state;

/* Default sample interval in milliseconds. */
#define LJ_PROFILE_INTERVAL_DEFAULT	10

/* -- Profiler/hook interaction ------------------------------------------- */

#if !LJ_PROFILE_SIGPROF
void LJ_FASTCALL lj_profile_hook_enter(global_State *g)
{
  ProfileState *ps = &profile_state;
  if (ps->g) {
    profile_lock(ps);
    hook_enter(g);
    profile_unlock(ps);
  } else {
    hook_enter(g);
  }
}

void LJ_FASTCALL lj_profile_hook_leave(global_State *g)
{
  ProfileState *ps = &profile_state;
  if (ps->g) {
    profile_lock(ps);
    hook_leave(g);
    profile_unlock(ps);
  } else {
    hook_leave(g);
  }
}
#endif

/* -- Profile callbacks --------------------------------------------------- */

/* Callback from profile hook (HOOK_PROFILE already cleared). */
void LJ_FASTCALL lj_profile_interpreter(lua_State *L)
{
  ProfileState *ps = &profile_state;
  global_State *g = G(L);
  uint8_t mask;
  profile_lock(ps);
  mask = (g->hookmask & ~HOOK_PROFILE);
  if (!(mask & HOOK_VMEVENT)) {
    int samples = ps->samples;
    ps->samples = 0;
    g->hookmask = HOOK_VMEVENT;
    lj_dispatch_update(g);
    profile_unlock(ps);
    ps->cb(ps->data, L, samples, ps->vmstate);  /* Invoke user callback. */
    profile_lock(ps);
    mask |= (g->hookmask & HOOK_PROFILE);
  }
  g->hookmask = mask;
  lj_dispatch_update(g);
  profile_unlock(ps);
}

/* Trigger profile hook. Asynchronous call from OS-specific profile timer. */
static void profile_trigger(ProfileState *ps)
{
  global_State *g = ps->g;
  uint8_t mask;
  profile_lock(ps);
  ps->samples++;  /* Always increment number of samples. */
  mask = g->hookmask;
  if (!(mask & (HOOK_PROFILE|HOOK_VMEVENT|HOOK_GC))) {  /* Set profile hook. */
    int st = g->vmstate;
    ps->vmstate = st >= 0 ? 'N' :
		  st == ~LJ_VMST_INTERP ? 'I' :
		  st == ~LJ_VMST_C ? 'C' :
		  st == ~LJ_VMST_GC ? 'G' : 'J';
    g->hookmask = (mask | HOOK_PROFILE);
    lj_dispatch_update(g);
  }
  profile_unlock(ps);
}

/* -- OS-specific profile timer handling ---------------------------------- */

#if LJ_PROFILE_SIGPROF

/* SIGPROF handler. */
static void profile_signal(int sig)
{
  UNUSED(sig);
  profile_trigger(&profile_state);
}

/* Start profiling timer. */
static void profile_timer_start(ProfileState *ps)
{
  int interval = ps->interval;
  struct itimerval tm;
  struct sigaction sa;
  tm.it_value.tv_sec = tm.it_interval.tv_sec = interval / 1000;
  tm.it_value.tv_usec = tm.it_interval.tv_usec = (interval % 1000) * 1000;
  setitimer(ITIMER_PROF, &tm, NULL);
#if LJ_TARGET_QNX
  sa.sa_flags = 0;
#else
  sa.sa_flags = SA_RESTART;
#endif
  sa.sa_handler = profile_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGPROF, &sa, &ps->oldsa);
}

/* Stop profiling timer. */
static void profile_timer_stop(ProfileState *ps)
{
  struct itimerval tm;
  tm.it_value.tv_sec = tm.it_interval.tv_sec = 0;
  tm.it_value.tv_usec = tm.it_interval.tv_usec = 0;
  setitimer(ITIMER_PROF, &tm, NULL);
  sigaction(SIGPROF, &ps->oldsa, NULL);
}

#elif LJ_PROFILE_PTHREAD

/* POSIX timer thread. */
static void *profile_thread(ProfileState *ps)
{
  int interval = ps->interval;
#if !LJ_TARGET_PS3
  struct timespec ts;
  ts.tv_sec = interval / 1000;
  ts.tv_nsec = (interval % 1000) * 1000000;
#endif
  while (1) {
#if LJ_TARGET_PS3
    sys_timer_usleep(interval * 1000);
#else
    nanosleep(&ts, NULL);
#endif
    if (ps->abort) break;
    profile_trigger(ps);
  }
  return NULL;
}

/* Start profiling timer thread. */
static void profile_timer_start(ProfileState *ps)
{
  pthread_mutex_ini