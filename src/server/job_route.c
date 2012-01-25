/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/
/*
 * job_route.c - functions to route a job to another queue
 *
 * Included functions are:
 *
 * job_route() - attempt to route a job to a new destination.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <sys/param.h>
#include "pbs_ifl.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "libpbs.h"
#include "pbs_error.h"
#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "work_task.h"
#include "server.h"
#include "log.h"
#include "queue.h"
#include "pbs_job.h"
#include "credential.h"
#include "batch_request.h"
#include "resource.h"
#include "sched_cmds.h"
#if __STDC__ != 1
#include <memory.h>
#endif

/* External functions called */
extern int svr_movejob(job *, char *, struct batch_request *);
extern long count_proc(char *);
extern int svr_do_schedule;
extern int listener_command;

/* Local Functions */

int  job_route(job *);
void queue_route(pbs_queue *);

/* Global Data */
extern char *msg_routexceed;
extern char *msg_err_malloc;
extern char *msg_routexceed;
extern time_t  time_now;

/*
 * Add an entry to the list of bad destinations for a job.
 *
 * Return: pointer to the new entry if it is added, NULL if not.
 */

void add_dest(

  job *jobp)

  {
  char      *id = "add_dest";
  badplace  *bp;
  char      *baddest = jobp->ji_qs.ji_destin;

  bp = (badplace *)malloc(sizeof(badplace));

  if (bp == NULL)
    {
    log_err(errno, id, msg_err_malloc);

    return;
    }

  CLEAR_LINK(bp->bp_link);

  strcpy(bp->bp_dest, baddest);

  append_link(&jobp->ji_rejectdest, &bp->bp_link, bp);

  return;
  }  /* END add_dest() */




/*
 * Check the job for a match of dest in the list of rejected destinations.
 *
 * Return: pointer if found, NULL if not.
 */

badplace *is_bad_dest(

  job  *jobp,
  char *dest)

  {
  /* ji_rejectdest is set in add_dest if approved in ??? */

  badplace *bp;

  bp = (badplace *)GET_NEXT(jobp->ji_rejectdest);

  while (bp != NULL)
    {
    if (!strcmp(bp->bp_dest, dest))
      break;

    bp = (badplace *)GET_NEXT(bp->bp_link);
    }

  return(bp);
  }  /* END is_bad_dest() */


/* int initialize_procct - set pjob->procct plus the resource
 * procct in the Resource_List
 *  
 * Assumes the nodes resource has been set on the Resource_List. This should 
 * have been done in req_quejob with the set_nodes_attr() function or in 
 * set_node_ct and/or set_proc_ct. 
 *  
 * Returns 0 on success. Non-zero on failure
 */ 
int initialize_procct(job *pjob)
  { 
  char id[] = "initialize_procct";
  resource     *pnodesp = NULL;
  resource_def *pnodes_def = NULL;
  resource     *pprocsp = NULL;
  resource_def *pprocs_def = NULL;
  resource     *procctp = NULL;
  resource_def *procct_def = NULL;
  attribute    *pattr = NULL;

  pattr = &pjob->ji_wattr[JOB_ATR_resource];
  if(pattr == NULL)
    {
    /* Something is really wrong. ji_wattr[JOB_ATR_resource] should always be set
       by the time this function is called */
    sprintf(log_buffer, "%s: Resource_List is NULL. Cannot proceed", id);
    log_event(PBSEVENT_JOB,
              PBS_EVENTCLASS_JOB,
              pjob->ji_qs.ji_jobid,
              log_buffer);
    pbs_errno = PBSE_INTERNAL;
    return(ROUTE_PERM_FAILURE);
    }

  /* Has nodes been initialzed */
  if(pattr->at_flags & ATR_VFLAG_SET)
    {
    /* get the node spec from the nodes resource */
    pnodes_def = find_resc_def(svr_resc_def, "nodes", svr_resc_size);
    if(pnodes_def == NULL)
      {
      sprintf(log_buffer, "%s: Could not get nodes resource definition. Cannot proceed", id);
      log_event(PBSEVENT_JOB,
                PBS_EVENTCLASS_JOB,
                pjob->ji_qs.ji_jobid,
                log_buffer);
      pbs_errno = PBSE_INTERNAL;
      return(ROUTE_PERM_FAILURE);
      }
    pnodesp = find_resc_entry(pattr, pnodes_def);

    /* Get the procs count if the procs resource attribute is set */
    pprocs_def = find_resc_def(svr_resc_def, "procs", svr_resc_size);
    if(pprocs_def != NULL)
      {
      /* if pprocs_def is NULL we just go on. Otherwise we will get its value now */
      pprocsp = find_resc_entry(pattr, pprocs_def);
      /* We will evaluate pprocsp later. If it is null we do not care */
      }

    /* if neither pnodesp nor pprocsp are set, terminate */
    if(pnodesp == NULL && pprocsp == NULL)
      {
      /* nodes and procs were not set. Hopefully req_quejob set procct to 1 for us already */
      procct_def = find_resc_def(svr_resc_def, "procct", svr_resc_size);
      if(procct_def == NULL)
        {
        sprintf(log_buffer, "%s: Could not get procct resource definition. Cannot proceed", id);
        log_event(PBSEVENT_JOB,
                PBS_EVENTCLASS_JOB,
                pjob->ji_qs.ji_jobid,
                log_buffer);
        pbs_errno = PBSE_INTERNAL;
        return(ROUTE_PERM_FAILURE);
        }
      procctp = find_resc_entry(pattr, procct_def);
      if(procctp == NULL)
        {
        sprintf(log_buffer, "%s: Could not get nodes nor procs entry from Resource_List. Cannot proceed", id);
        log_event(PBSEVENT_JOB,
                PBS_EVENTCLASS_JOB,
                pjob->ji_qs.ji_jobid,
                log_buffer);
        pbs_errno = PBSE_INTERNAL;
        return(ROUTE_PERM_FAILURE);
        }
      }

    /* we now set pjob->procct and we also set the resource attribute procct */
    procct_def = find_resc_def(svr_resc_def, "procct", svr_resc_size);
    if(procct_def == NULL)
      {
      sprintf(log_buffer, "%s: Could not get procct resource definition. Cannot proceed", id);
      log_event(PBSEVENT_JOB,
                PBS_EVENTCLASS_JOB,
                pjob->ji_qs.ji_jobid,
                log_buffer);
      pbs_errno = PBSE_INTERNAL;
      return(ROUTE_PERM_FAILURE);
      }
    procctp = find_resc_entry(pattr, procct_def);
    if(procctp == NULL)
      {
      procctp = add_resource_entry(pattr, procct_def);
      if(procctp == NULL)
        {
        sprintf(log_buffer, "%s: Could not add procct resource. Cannot proceed", id);
        log_event(PBSEVENT_JOB,
                  PBS_EVENTCLASS_JOB,
                  pjob->ji_qs.ji_jobid,
                  log_buffer);
        pbs_errno = PBSE_INTERNAL;
        return(ROUTE_PERM_FAILURE);
        }
      }

    /* Finally the moment of truth. We have the nodes and procs resources. Add them
       to the procct resoruce*/
    procctp->rs_value.at_val.at_long = 0;
    if(pnodesp != NULL)
      {
      procctp->rs_value.at_val.at_long = count_proc(pnodesp->rs_value.at_val.at_str);
      }

    if(pprocsp != NULL)
      {
      procctp->rs_value.at_val.at_long += pprocsp->rs_value.at_val.at_long;
      }
    procctp->rs_value.at_flags |= ATR_VFLAG_SET;
    }
  else
    {
    /* Something is really wrong. ji_wattr[JOB_ATR_resource] should always be set
       by the time this function is called */
    sprintf(log_buffer, "%s: Resource_List not set. Cannot proceed", id);
    log_event(PBSEVENT_JOB,
              PBS_EVENTCLASS_JOB,
              pjob->ji_qs.ji_jobid,
              log_buffer);
    pbs_errno = PBSE_INTERNAL;
    return(ROUTE_PERM_FAILURE);
    }

  return(PBSE_NONE);
  } /* END initialize_procct */


int remove_procct(job *pjob)
  {
  char id[] = "remove_procct";
  attribute    *pattr;
  resource_def *pctdef;
  resource     *pctresc;

  pattr = &pjob->ji_wattr[JOB_ATR_resource];
  if(pattr == NULL)
    {
    /* Something is really wrong. ji_wattr[JOB_ATR_resource] should always be set
       by the time this function is called */
    sprintf(log_buffer, "%s: Resource_List is NULL. Cannot proceed", id);
    log_event(PBSEVENT_JOB,
              PBS_EVENTCLASS_JOB,
              pjob->ji_qs.ji_jobid,
              log_buffer);
    pbs_errno = PBSE_INTERNAL;
    return(ROUTE_PERM_FAILURE);
    }

 /* unset the procct resource if it has been set */
    pctdef = find_resc_def(svr_resc_def, "procct", svr_resc_size);

    if ((pctresc = find_resc_entry(pattr, pctdef)) != NULL)
      pctdef->rs_free(&pctresc->rs_value);

  return(PBSE_NONE);
  } /* END remove_procct */

/*
 * default_router - basic function for "routing" jobs.
 * Does a round-robin attempt on the destinations as listed,
 * job goes to first destination that takes it.
 *
 * If no destination will accept the job, PBSE_ROUTEREJ is returned,
 * otherwise 0 is returned.
 */

int default_router(

  job              *jobp,
  struct pbs_queue *qp,
  long              retry_time)

  {

  struct array_strings *dest_attr = NULL;
  char       *destination;
  int        last;
  int        rc;

  if (qp->qu_attr[(int)QR_ATR_RouteDestin].at_flags & ATR_VFLAG_SET)
    {
    dest_attr = qp->qu_attr[(int)QR_ATR_RouteDestin].at_val.at_arst;

    last = dest_attr->as_usedptr;
    }
  else
    {
    last = 0;
    }

  /* loop through all possible destinations */

  jobp->ji_retryok = 0;

  while (1)
    {
    if (jobp->ji_lastdest >= last)
      {
      jobp->ji_lastdest = 0; /* have tried all */

      if (jobp->ji_retryok == 0)
        {
        log_event(
          PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          jobp->ji_qs.ji_jobid,
          pbse_to_txt(PBSE_ROUTEREJ));

        return(PBSE_ROUTEREJ);
        }
      else
        {
        /* set time to retry job */

        jobp->ji_qs.ji_un.ji_routet.ji_rteretry = retry_time;

        return(0);
        }
      }

    destination = dest_attr->as_string[jobp->ji_lastdest++];

    if (is_bad_dest(jobp, destination))
      continue;
  
    /* We need to manage the procct resource which is 
       part of the Resource_List attribute. At this point 
       we need to remember what the procct value is and also
	     make sure it is in the Resource_List before calling
	     svr_movejob. See ROUTE_RETRY to see what is done
	     if the job stays in the routing queue. */
    rc = initialize_procct(jobp);
    if(rc != PBSE_NONE)
      {
      return(rc);
      }

    switch (svr_movejob(jobp, destination, NULL))
      {

      case ROUTE_PERM_FAILURE: /* permanent failure */

        add_dest(jobp);

        break;

      case ROUTE_SUCCESS:  /* worked */

      case ROUTE_DEFERRED:  /* deferred */

        return(0);

        /*NOTREACHED*/

        break;

      case ROUTE_RETRY:  /* failed, but try destination again */
			/* There are no available queues for this job so it is 
			   going to stay in the routing queue. But we need to remove
         procct from the Resource_List so it does not get sent
				 to the scheduler as part of the job. procct is for 
				 TORQUE use only. */
        rc = remove_procct(jobp);
        if(rc != PBSE_NONE)
          {
          return(rc);
          }

        jobp->ji_retryok = 1;

        break;
      }
    }

  return(-1);
  }  /* END default_router() */


/*
 * job_route - route a job to another queue
 *
 * This is only called for jobs in a routing queue.
 * Loop over all the possible destinations for the route queue.
 * Check each one to see if it is ok to try it.  It could have been
 * tried before and returned a rejection.  If so, skip to the next
 * destination.  If it is ok to try it, look to see if it is a local
 * queue.  If so, it is an internal procedure to try/do the move.
 * If not, a child process is created to deal with it in the
 * function net_route(), see svr_movejob.c.
 *
 * Returns: 0 on success, non-zero (error number) on failure
 */

int job_route(

  job *jobp) /* job to route */

  {
  int      bad_state = 0;
  int      rc;
  char     *id = "job_route";
  time_t     life;

  struct pbs_queue *qp;
  long              retry_time;

  /* see if the job is able to be routed */

  switch (jobp->ji_qs.ji_state)
    {

    case JOB_STATE_TRANSIT:

      return(0);  /* already going, ignore it */

      /*NOTREACHED*/

      break;

    case JOB_STATE_QUEUED:

      /* NO-OP */

      break;   /* ok to try */

    case JOB_STATE_HELD:

      /* job may be acceptable */

      bad_state = !jobp->ji_qhdr->qu_attr[QR_ATR_RouteHeld].at_val.at_long;

      break;

    case JOB_STATE_WAITING:

      /* job may be acceptable */

      bad_state = !jobp->ji_qhdr->qu_attr[QR_ATR_RouteWaiting].at_val.at_long;

      break;

    default:

      sprintf(log_buffer, "%s %d", pbse_to_txt(PBSE_BADSTATE),
              jobp->ji_qs.ji_state);

      strcat(log_buffer, id);

      log_event(
        PBSEVENT_DEBUG,
        PBS_EVENTCLASS_JOB,
        jobp->ji_qs.ji_jobid,
        log_buffer);

      return(0);

      /*NOTREACHED*/

      break;
    }

  /* check the queue limits, can we route any (more) */

  qp = jobp->ji_qhdr;

  if (qp->qu_attr[QA_ATR_Started].at_val.at_long == 0)
    {
    /* queue not started - no routing */

    return(0);
    }

  if ((qp->qu_attr[QA_ATR_MaxRun].at_flags & ATR_VFLAG_SET) &&
      (qp->qu_attr[QA_ATR_MaxRun].at_val.at_long <= qp->qu_njstate[JOB_STATE_TRANSIT]))
    {
    /* max number of jobs being routed */

    return(0);
    }

  /* what is the retry time and life time of a job in this queue */

  if (qp->qu_attr[QR_ATR_RouteRetryTime].at_flags & ATR_VFLAG_SET)
    {
    retry_time =
      (long)time_now +
      qp->qu_attr[QR_ATR_RouteRetryTime].at_val.at_long;
    }
  else
    {
    retry_time = (long)time_now + PBS_NET_RETRY_TIME;
    }

  if (qp->qu_attr[QR_ATR_RouteLifeTime].at_flags & ATR_VFLAG_SET)
    {
    life =
      jobp->ji_qs.ji_un.ji_routet.ji_quetime +
      qp->qu_attr[QR_ATR_RouteLifeTime].at_val.at_long;
    }
  else
    {
    life = 0; /* forever */
    }

  if (life && (life < time_now))
    {
    log_event(
      PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      jobp->ji_qs.ji_jobid,
      msg_routexceed);

    /* job too long in queue */

    return(PBSE_ROUTEEXPD);
    }

  if (bad_state)
    {
    /* not currently routing this job */

    return(0);
    }

  if (qp->qu_attr[QR_ATR_AltRouter].at_val.at_long == 0)
    {
    rc = default_router(jobp, qp, retry_time);
    if(rc == ROUTE_SUCCESS)
      {
      svr_do_schedule = SCH_SCHEDULE_NEW;
      listener_command = SCH_SCHEDULE_NEW;
      }
    return(rc);
    }

  rc = site_alt_router(jobp, qp, retry_time);
  if(rc == ROUTE_SUCCESS)
    {
    svr_do_schedule = SCH_SCHEDULE_NEW;
    listener_command = SCH_SCHEDULE_NEW;
    }

  return(rc);
  }  /* END job_route() */





/*
 * queue_route - route any "ready" jobs in a specific queue
 *
 * look for any job in the queue whose route retry time has
 * passed.

 * If the queue is "started" and if the number of jobs in the
 * Transiting state is less than the max_running limit, then
 * attempt to route it.
 */

void queue_route(

  pbs_queue *pque)

  {
  job *nxjb;
  job *pjob;
  int  rc;

  pjob = (job *)GET_NEXT(pque->qu_jobs);

  while (pjob != NULL)
    {
    nxjb = (job *)GET_NEXT(pjob->ji_jobque);

    if (pjob->ji_qs.ji_un.ji_routet.ji_rteretry <= time_now)
      {
      if ((rc = job_route(pjob)) == PBSE_ROUTEREJ)
        job_abt(&pjob, pbse_to_txt(PBSE_ROUTEREJ));
      else if (rc == PBSE_ROUTEEXPD)
        job_abt(&pjob, msg_routexceed);
      }

    pjob = nxjb;
    }

  return;
  }


