/* The system call implemented in this file:
 *   m_type:	SYS_MEMSET
 *
 * The parameters for this system call are:
 *    m5_l1:	CP_SRC_ADDR	(virtual address)	
 *    m5_l2:	CP_DST_ADDR	(returns physical address)	
 *    m5_l3:	CP_NR_BYTES	(size of datastructure) 	
 */

#include "../system.h"

#if USE_MEMSET

/*===========================================================================*
 *				do_memset				     *
 *===========================================================================*/
PUBLIC int do_memset(m_ptr)
register message *m_ptr;
{
/* Handle sys_memset(). */
  unsigned long pat;
  unsigned char c = m_ptr->MEM_CHAR;
  pat = c | (c << 8) | (c << 16) | (c << 24);
  phys_fill((phys_bytes) m_ptr->MEM_PTR, (phys_bytes) m_ptr->MEM_COUNT, pat);
  return(OK);
}

#endif /* USE_MEMSET */

