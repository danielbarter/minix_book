/* Miscellaneous system calls.				Author: Kees J. Bot
 *								31 Mar 2000
 * The entry points into this file are:
 *   do_reboot: kill all processes, then reboot system
 *   do_svrctl: memory manager control
 *   do_getsysinfo: request copy of MM data structure
 */

#include "mm.h"
#include <minix/callnr.h>
#include <signal.h>
#include <sys/svrctl.h>
#include <minix/com.h>
#include <minix/utils.h>
#include <string.h>
#include "mproc.h"
#include "param.h"

FORWARD _PROTOTYPE( char *find_key, (const char *params, const char *key));

/* MM gets a copy of all boot monitor parameters. */
PRIVATE char monitor_params[128*sizeof(char *)];

/*=====================================================================*
 *			    do_getsysinfo			       *
 *=====================================================================*/
PUBLIC int do_getsysinfo()
{
  return(OK);
}


/*=====================================================================*
 *			    do_reboot				       *
 *=====================================================================*/
PUBLIC int do_reboot()
{
  register struct mproc *rmp = mp;
  char monitor_code[32*sizeof(char *)];

  if (rmp->mp_effuid != SUPER_USER) return(EPERM);

  switch (m_in.reboot_flag) {
  case RBT_HALT:
  case RBT_REBOOT:
  case RBT_PANIC:
  case RBT_RESET:
	break;
  case RBT_MONITOR:
	if (m_in.reboot_size >= sizeof(monitor_code)) return(EINVAL);
	if (sys_datacopy(who, (vir_bytes) m_in.reboot_code,
		PM_PROC_NR, (vir_bytes) monitor_code,
		(phys_bytes) (m_in.reboot_size+1)) != OK) return(EFAULT);
	if (monitor_code[m_in.reboot_size] != 0) return(EINVAL);
	break;
  default:
	return(EINVAL);
  }

  check_sig(-1, SIGKILL); 		/* kill all processes except init */
  tell_fs(REBOOT,0,0,0);		/* tell FS to prepare for shutdown */

  sys_abort(m_in.reboot_flag, PM_PROC_NR, monitor_code, m_in.reboot_size);
  sys_exit(0);
}

/*=====================================================================*
 *			    do_svrctl				       *
 *=====================================================================*/
PUBLIC int do_svrctl()
{
  static int initialized = 0;
  int s, req;
  vir_bytes ptr;
  req = m_in.svrctl_req;
  ptr = (vir_bytes) m_in.svrctl_argp;

  /* Initialize private copy of monitor parameters on first call. */
  if (! initialized) {
      if ((s=sys_getmonparams(monitor_params, sizeof(monitor_params))) != OK)
          printf("MM: Warning couldn't get copy of monitor params: %d\n",s);
      else
          initialized = 1;
  }

  /* Binary compatibility check. */
  if (req == SYSGETENV) {
#if DEAD_CODE
	printf("SYSGETENV by %d (fix!)\n", who);
#endif
  	req = MMGETPARAM;
  }

  /* Is the request for the kernel? Forward it, except for SYSGETENV. */
  if (((req >> 8) & 0xFF) == 'S') {

      /* Simply forward call to the SYSTEM task. */
      return(sys_svrctl(who, req, mp->mp_effuid == SUPER_USER, ptr));
  }

  /* Control operations local to the MM. */
  switch(req) {
  case MMGETPARAM: {
      struct sysgetenv sysgetenv;
      char search_key[64];
      char *val_start;
      size_t val_len;
      size_t copy_len;

      /* Check if boot monitor parameters are in place. */
      if (! initialized) return(EAGAIN);

      /* Copy sysgetenv structure to MM. */
      if (sys_datacopy(who, ptr, SELF, (vir_bytes) &sysgetenv, 
              sizeof(sysgetenv)) != OK) return(EFAULT);  

      if (sysgetenv.keylen == 0) {	/* copy all parameters */
          val_start = monitor_params;
          val_len = sizeof(monitor_params);
      } 
      else {				/* lookup value for key */
          /* Try to get a copy of the requested key. */
          if (sysgetenv.keylen > sizeof(search_key)) return(EINVAL);
          if ((s = sys_datacopy(who, (vir_bytes) sysgetenv.key,
                  SELF, (vir_bytes) search_key, sysgetenv.keylen)) != OK)
              return(s);

          /* Make sure key is null-terminated and lookup value. */
          search_key[sysgetenv.keylen-1]= '\0';
          if ((val_start = find_key(monitor_params, search_key)) == NULL)
               return(ESRCH);
          val_len = strlen(val_start) + 1;
      }

      /* Value found, make the actual copy (as far as possible). */
      copy_len = MAX(val_len, sysgetenv.vallen); 
      if ((s=sys_datacopy(SELF, (vir_bytes) val_start, 
              who, (vir_bytes) sysgetenv.val, copy_len)) != OK)
          return(s);

      /* See if it fits in the client's buffer. */
      return (copy_len > sysgetenv.vallen) ? E2BIG : OK;
  }
  case MMSIGNON: {
	/* A user process becomes a task.  Simulate an exit by
	 * releasing a waiting parent and disinheriting children.
	 */
	struct mproc *rmp;
	pid_t pidarg;

	if (mp->mp_effuid != SUPER_USER) return(EPERM);

	rmp = &mproc[mp->mp_parent];
	tell_fs(EXIT, who, 0, 0);

	pidarg = rmp->mp_wpid;
	if ((rmp->mp_flags & WAITING) && (pidarg == -1
		|| pidarg == mp->mp_pid || -pidarg == mp->mp_procgrp))
	{
		/* Wake up the parent. */
		rmp->mp_reply.reply_res2 = 0;
		setreply(mp->mp_parent, mp->mp_pid);
		rmp->mp_flags &= ~WAITING;
	}

	/* Disinherit children. */
	for (rmp = &mproc[0]; rmp < &mproc[NR_PROCS]; rmp++) {
		if (rmp->mp_flags & IN_USE && rmp->mp_parent == who) {
			rmp->mp_parent = INIT_PROC_NR;
		}
	}

	/* Become like MM and FS. */
	mp->mp_pid = mp->mp_procgrp = 0;
	mp->mp_parent = 0;
	return(OK); }

#if ENABLE_SWAP
  case MMSWAPON: {
	struct mmswapon swapon;

	if (mp->mp_effuid != SUPER_USER) return(EPERM);

	if (sys_datacopy(who, (phys_bytes) ptr,
		PM_PROC_NR, (phys_bytes) &swapon,
		(phys_bytes) sizeof(swapon)) != OK) return(EFAULT);

	return(swap_on(swapon.file, swapon.offset, swapon.size)); }

  case MMSWAPOFF: {
	if (mp->mp_effuid != SUPER_USER) return(EPERM);

	return(swap_off()); }
#endif /* SWAP */

  default:
	return(EINVAL);
  }
}

/*==========================================================================*
 *				find_key					    *
 *==========================================================================*/
PRIVATE char *find_key(params,name)
const char *params;
const char *name;
{
  register const char *namep;
  register char *envp;

  for (envp = (char *) params; *envp != 0;) {
	for (namep = name; *namep != 0 && *namep == *envp; namep++, envp++)
		;
	if (*namep == '\0' && *envp == '=') 
		return(envp + 1);
	while (*envp++ != 0)
		;
  }
  return(NULL);
}
