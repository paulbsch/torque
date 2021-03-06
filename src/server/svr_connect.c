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
 * svr_connect.c - contains routines to tie the structures used by
 * net_client and net_server together with those used by the
 * various PBS_*() routines in the API.
 *
 * svr_connect() opens a connection with can be used with the
 *  API routines and still be selected in wait_request().
 *
 *  Returns a connection handle ( >= 0 ).  Note that a value
 *  of PBS_LOCAL_CONNECTION is special, it means the server
 *  is talking to itself.
 *
 *  It is called by the server whenever we need to send
 *  a request to another server, or talk to MOM.
 *
 *  On an error, PBS_NET_RC_FATAL (-1) is retuned if the
 *    error is believed to be permanent,
 *        PBS_NET_RC_RETRY (-2) if the error is
 *    believed to be temporary, ie retry.
 *
 * svr_disconnect() closes the above connection.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include "libpbs.h"
#include "log.h"
#include "server_limits.h"
#include "net_connect.h"
#include "svrfunc.h"
#include "dis.h"


/* global data */


extern int   errno;

extern int   pbs_errno;
extern unsigned int  pbs_server_port_dis;

extern struct connection svr_conn[];
extern pbs_net_t  pbs_server_addr;
extern int               LOGLEVEL;

extern int     addr_ok(pbs_net_t);
extern void    bad_node_warning(pbs_net_t);
extern ssize_t read_blocking_socket(int, void *, ssize_t);
extern int get_num_connections();



int svr_connect(

  pbs_net_t      hostaddr,  /* host order */
  unsigned int   port,   /* I */
  void (*func)(int),
  enum conn_type cntype)

  {
  char         EMsg[1024];

  static char *id = "svr_connect";

  int handle;
  int sock;

  time_t STime;
  time_t ETime;

  if (LOGLEVEL >= 4)
    {
    sprintf(log_buffer, "attempting connect to %s %s port %d",
            (hostaddr == pbs_server_addr) ? "server" : "host",
            ((int)hostaddr != 0) ? netaddr_pbs_net_t(hostaddr) : "localhost",
            port);

    log_event(
      PBSEVENT_ADMIN,
      PBS_EVENTCLASS_SERVER,
      id,
      log_buffer);
    }

  /* First, determine if the request is to another server or ourselves */

  if ((hostaddr == pbs_server_addr) && (port == pbs_server_port_dis))
    {
    return(PBS_LOCAL_CONNECTION); /* special value for local */
    }

  time(&STime);

  /* obtain the connection to the other server */

  if (!addr_ok(hostaddr))
    {
    if (LOGLEVEL >= 4)
      {
      sprintf(log_buffer, "cannot connect to %s port %d - target is down",
              (hostaddr == pbs_server_addr) ? "server" : "host",
              port);

      log_event(
        PBSEVENT_ADMIN,
        PBS_EVENTCLASS_SERVER,
        id,
        log_buffer);
      }

    pbs_errno = EHOSTDOWN;

    return(PBS_NET_RC_RETRY);
    }

  /* establish socket connection to specified host */

  sock = client_to_svr(hostaddr, port, 1, EMsg);

  time(&ETime);

  if (LOGLEVEL >= 2)
    {
    if (ETime > STime)
      {
      /* NYI */
      }
    }

  if (sock < 0)
    {
    if (LOGLEVEL >= 4)
      {
      sprintf(log_buffer, "cannot connect to %s port %d - cannot establish connection (%s) - time=%ld seconds",
              (hostaddr == pbs_server_addr) ? "server" : "host",
              port,
              EMsg,
              (long)(ETime - STime));

      log_event(
        PBSEVENT_ADMIN,
        PBS_EVENTCLASS_SERVER,
        id,
        log_buffer);
      }

    bad_node_warning(hostaddr);

    pbs_errno = errno;

    return(sock);  /* PBS_NET_RC_RETRY or PBS_NET_RC_FATAL */
    }  /* END if (sock < 0) */

  if ((LOGLEVEL >= 2) && (ETime > STime))
    {
    sprintf(log_buffer, "successful connect to %s port %d - time=%ld seconds",
            (hostaddr == pbs_server_addr) ? "server" : "host",
            port,
            (long)(ETime - STime));

    log_event(
      PBSEVENT_ADMIN,
      PBS_EVENTCLASS_SERVER,
      id,
      log_buffer);
    }

  /* add the connection to the server connection table and select list */

  if (func != NULL)
    {
    /* connect attempt to XXX? */

    add_conn(sock, ToServerDIS, hostaddr, port, PBS_SOCK_INET, func);
    }

  svr_conn[sock].cn_authen = PBS_NET_CONN_AUTHENTICATED;

  /* find a connect_handle entry we can use and pass to the PBS_*() */

  handle = socket_to_handle(sock);

  if (handle == -1)
    {
    if (LOGLEVEL >= 4)
      {
      sprintf(log_buffer, "cannot connect to %s port %d - cannot get handle",
              (hostaddr == pbs_server_addr) ? "server" : "host",
              port);

      log_event(
        PBSEVENT_ADMIN,
        PBS_EVENTCLASS_SERVER,
        id,
        log_buffer);
      }

    close_conn(sock);

    return(PBS_NET_RC_RETRY);
    }

  return(handle);
  }  /* END svr_connect() */


/*
 * localalm() - alarm handler for svr_disconnect()
 */

static void localalm(

  int sig)  /* I */

  {

  log_ext(-1,"svr_disconnect","alarm fired",LOG_DEBUG);
  return;
  }  /* END localalm() */







/*
 * svr_disconnect - close a connection made with svr_connect()
 *
 * In addition to closing the actual connection, both the
 * server's connection table and the handle table used by
 * the API routines must be cleaned-up.
 */

void svr_disconnect(

  int handle)  /* I */

  {
  int sock;
  int x;

  if ((handle >= 0) && (handle < PBS_LOCAL_CONNECTION))
    {
    sock = connection[handle].ch_socket;

    DIS_tcp_setup(sock);

    if ((encode_DIS_ReqHdr(sock, PBS_BATCH_Disconnect, pbs_current_user) == 0) &&
        (DIS_tcp_wflush(sock) == 0))
      {
      struct sigaction act;
      struct sigaction oldact;
      /* wait for other server to close connection */
      act.sa_handler = localalm;

      sigemptyset(&act.sa_mask);

      act.sa_flags = 0;

      sigaction(SIGALRM, &act, &oldact);

      ualarm(100000, 0); /* 1/10 of second */

      while (1)
        {
        /* don't call the non-blocking function */
        if (read_blocking_socket(sock, &x, 1) < 1)
          break;
        }

      ualarm(0, 0);

      /* restore the previous handler */

      sigaction(SIGALRM, &oldact, 0);

      }

    shutdown(connection[handle].ch_socket, 2);

    close_conn(connection[handle].ch_socket);

    if (connection[handle].ch_errtxt != NULL)
      {
      free(connection[handle].ch_errtxt);
      connection[handle].ch_errtxt = NULL;
      }

    connection[handle].ch_errno = 0;

    connection[handle].ch_inuse = 0;
    }

  return;
  }  /* END svr_disconnect() */




/*
 * socket_to_handle() - turn a socket into a connection handle
 * as used by the libpbs.a routines.
 *
 * Returns: >=0 connection handle if successful, or
 *    -1 if error, error number set in pbs_errno.
 */

int socket_to_handle(

  int sock)  /* opened socket */

  {
  char *id = "socket_to_handle";

  int i;

  for (i = 0;i < PBS_NET_MAX_CONNECTIONS;i++)
    {
    if (connection[i].ch_inuse != 0)
      continue;

    connection[i].ch_stream = 0;
    connection[i].ch_inuse  = 1;
    connection[i].ch_errno  = 0;
    connection[i].ch_socket = sock;
    connection[i].ch_errtxt = 0;

    /* SUCCESS - save handle for later close */

    svr_conn[sock].cn_handle = i;

    if (i >= (PBS_NET_MAX_CONNECTIONS/2))
      {
      sprintf(log_buffer,"internal socket table is half-full at %d (expected num_connections is %d)!",
        i,
        get_num_connections());

      log_ext(-1,id,log_buffer,LOG_CRIT);
      }

    return(i);
    }  /* END for (i) */

  sprintf(log_buffer,"internal socket table full (%d) - num_connections is %d",
    i,
    get_num_connections());

  log_ext(-1,id,log_buffer,LOG_ALERT);

  pbs_errno = PBSE_NOCONNECTS;

  return(-1);
  }  /* END socket_to_handle() */





/*
 * parse_servername - parse a server/mom name in the form:
 * hostname[:service_port][/#][+hostname...]
 *
 * Returns ptr to the host name as the function value and the service_port
 * number (int) into service if :port is found, otherwise port is unchanged
 * host name is also terminated by a '+' or '/' in string
 *
 * Warning: as written, Not reentrient/thread safe
 */

char *parse_servername(

  char         *name,   /* server name in form name[:port] */
  unsigned int *service)  /* RETURN: service_port if :port */

  {
  static char  buf[PBS_MAXSERVERNAME + PBS_MAXPORTNUM + 2];

  int   i = 0;
  char *pc;

  buf[0] = '\0';

  /* look for a ':', '+' or '/' in the string */

  pc = name;

  if (name == NULL)
    {
    /* invalid name specified */

    return(buf);
    }

  while (*pc && (i < PBS_MAXSERVERNAME + PBS_MAXPORTNUM + 2))
    {
    if ((*pc == '+') || (*pc == '/'))
      {
      break;
      }

    if (*pc == ':')
      {
      *service = (unsigned int)atoi(pc + 1);

      break;
      }

    buf[i++] = *pc++;
    }

  buf[i] = '\0';

  return(buf);
  }  /* END parse_servername() */


/* END svr_connect.c */


