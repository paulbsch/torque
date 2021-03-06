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

#define _DARWIN_UNLIMITED_SELECT

#include <pbs_config.h>   /* the master config generated by configure */

#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>  /* added - CRI 9/05 */
#include <unistd.h>    /* added - CRI 9/05 */
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#if defined(FD_SET_IN_SYS_SELECT_H)
#include <sys/select.h>
#endif
#if defined(NTOHL_NEEDS_ARPA_INET_H) && defined(HAVE_ARPA_INET_H)
#include <arpa/inet.h>
#endif




#include "portability.h"
#include "server_limits.h"
#include "net_connect.h"
#include "log.h"
#include "dis.h" /* DIS_tcp_close */

extern int  LOGLEVEL;

/* External Functions Called */

extern void process_request(int);

extern time_t time(time_t *);

/* Global Data (I wish I could make it private to the library, sigh, but
 * C don't support that scope of control.)
 *
 * This array of connection structures is used by the server to maintain
 * a record of the open I/O connections, it is indexed by the socket number.
 */

struct connection svr_conn[PBS_NET_MAX_CONNECTIONS];

/*
 * The following data is private to this set of network interface routines.
 */

static int max_connection = PBS_NET_MAX_CONNECTIONS;
static int num_connections = 0;
static fd_set *GlobalSocketReadSet = NULL;
static void (*read_func[2])(int);

pbs_net_t pbs_server_addr;

/* Private function within this file */

static void accept_conn();


static struct netcounter nc_list[60];

void
netcounter_incr(void)
  {
  time_t now, lastmin;
  int i;

  now = time(NULL);
  lastmin = now - 60;

  if (nc_list[0].time == now)
    {
    nc_list[0].counter++;
    }
  else
    {
    memmove(&nc_list[1], &nc_list[0], sizeof(struct netcounter)*59);

    nc_list[0].time = now;
    nc_list[0].counter = 1;

    for (i = 0;i < 60;i++)
      {
      if (nc_list[i].time < lastmin)
        {
        nc_list[i].time = 0;
        nc_list[i].counter = 0;
        }
      }
    }
  }


int get_num_connections()
  {
  return(num_connections);
  }


int *
netcounter_get(void)
  {
  static int netrates[3];
  int netsums[3] = {0, 0, 0};
  int i;

  for (i = 0;i < 5;i++)
    {
    netsums[0] += nc_list[i].counter;
    netsums[1] += nc_list[i].counter;
    netsums[2] += nc_list[i].counter;
    }

  for (i = 5;i < 30;i++)
    {
    netsums[1] += nc_list[i].counter;
    netsums[2] += nc_list[i].counter;
    }

  for (i = 30;i < 60;i++)
    {
    netsums[2] += nc_list[i].counter;
    }

  if (netsums[0] > 0)
    {
    netrates[0] = netsums[0] / 5;
    netrates[1] = netsums[1] / 30;
    netrates[2] = netsums[2] / 60;
    }
  else
    {
    netrates[0] = 0;
    netrates[1] = 0;
    netrates[2] = 0;
    }

  return netrates;
  }





/**
 * init_network - initialize the network interface
 * allocate a socket and bind it to the service port,
 * add the socket to the readset for select(),
 * add the socket to the connection structure and set the
 * processing function to accept_conn()
 */

int init_network(

  unsigned int  port,
  void        (*readfunc)())

  {
  int   i;
  static int  initialized = 0;
  int    sock;

  int MaxNumDescriptors = 0;

  struct sockaddr_in socname;
  enum conn_type   type;
#ifdef ENABLE_UNIX_SOCKETS

  struct sockaddr_un unsocname;
  int unixsocket;
  memset(&unsocname, 0, sizeof(unsocname));
#endif
 
  MaxNumDescriptors = get_max_num_descriptors();

  memset(&socname, 0, sizeof(socname));

  if (initialized == 0)
    {
    for (i = 0;i < PBS_NET_MAX_CONNECTIONS;i++)
      svr_conn[i].cn_active = Idle;

    /* initialize global "read" socket FD bitmap */
    GlobalSocketReadSet = (fd_set *)calloc(1,sizeof(char) * get_fdset_size());

    type = Primary;
    }
  else if (initialized == 1)
    {
    type = Secondary;
    }
  else
    {
    /* FAILURE */

    return(-1); /* too many main connections */
    }

  /* save the routine which should do the reading on connections */
  /* accepted from the parent socket    */

  read_func[initialized++] = readfunc;

  if (port != 0)
    {
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
      {
      /* FAILURE */

      return(-1);
      }

  if (MaxNumDescriptors < PBS_NET_MAX_CONNECTIONS)
    max_connection = MaxNumDescriptors;

    i = 1;

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&i, sizeof(i));

    /* name that socket "in three notes" */

    socname.sin_port = htons((unsigned short)port);

    socname.sin_addr.s_addr = INADDR_ANY;

    socname.sin_family = AF_INET;

    if (bind(sock, (struct sockaddr *)&socname, sizeof(socname)) < 0)
      {
      /* FAILURE */

      close(sock);

      return(-1);
      }

    /* record socket in connection structure and select set */

    add_conn(sock, type, (pbs_net_t)0, 0, PBS_SOCK_INET, accept_conn);

    /* start listening for connections */

    if (listen(sock, 512) < 0)
      {
      /* FAILURE */

      return(-1);
      }
    } /* END if (port != 0) */

#ifdef ENABLE_UNIX_SOCKETS
  if (port == 0)
    {
    /* setup unix domain socket */

    unixsocket = socket(AF_UNIX, SOCK_STREAM, 0);

    if (unixsocket < 0)
      {
      return(-1);
      }

    unsocname.sun_family = AF_UNIX;

    strncpy(unsocname.sun_path, TSOCK_PATH, sizeof(unsocname.sun_path) - 1);  

    unlink(TSOCK_PATH);  /* don't care if this fails */

    if (bind(unixsocket,
             (struct sockaddr *)&unsocname,
             sizeof(unsocname)) < 0)
      {
      close(unixsocket);

      return(-1);
      }

    if (chmod(TSOCK_PATH, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) != 0)
      {
      close(unixsocket);

      return(-1);
      }

    add_conn(unixsocket, type, (pbs_net_t)0, 0, PBS_SOCK_UNIX, accept_conn);

    if (listen(unixsocket, 512) < 0)
      {
      /* FAILURE */

      return(-1);
      }
    }   /* END if (port == 0) */

#endif  /* END ENABLE_UNIX_SOCKETS */

  if (port != 0)
    {
    /* allocate a minute's worth of counter structs */

    for (i = 0;i < 60;i++)
      {
      nc_list[i].time = 0;
      nc_list[i].counter = 0;
      }
    }

  return(0);
  }  /* END init_network() */





/*
 * wait_request - wait for a request (socket with data to read)
 * This routine does a select on the readset of sockets,
 * when data is ready, the processing routine associated with
 * the socket is invoked.
 */

int wait_request(

  time_t  waittime,   /* I (seconds) */
  long   *SState)     /* I (optional) */

  {
  extern char *PAddrToString(pbs_net_t *);
  void close_conn();

  int i;
  int n;

  time_t now;

  fd_set *SelectSet = NULL;
  int SelectSetSize = 0;

  int MaxNumDescriptors = 0;

  char id[] = "wait_request";
  char tmpLine[1024];

  struct timeval timeout;

  long OrigState = 0;

  if (SState != NULL)
    OrigState = *SState;

  timeout.tv_usec = 0;

  timeout.tv_sec  = waittime;

  SelectSetSize = sizeof(char) * get_fdset_size();
  SelectSet = (fd_set *)calloc(1,SelectSetSize);

  memcpy(SelectSet,GlobalSocketReadSet,SelectSetSize);
 
  /* selset = readset;*/  /* readset is global */
  MaxNumDescriptors = get_max_num_descriptors();

  n = select(MaxNumDescriptors, SelectSet, (fd_set *)0, (fd_set *)0, &timeout);

  if (n == -1)
    {
    if (errno == EINTR)
      {
      n = 0; /* interrupted, cycle around */
      }
    else
      {
      int i;

      struct stat fbuf;

      /* check all file descriptors to verify they are valid */

      /* NOTE:  selset may be modified by failed select() */

      for (i = 0;i < MaxNumDescriptors;i++)
        {
        if (FD_ISSET(i, GlobalSocketReadSet) == 0)
          continue;

        if (fstat(i, &fbuf) == 0)
          continue;

        /* clean up SdList and bad sd... */

        FD_CLR(i, GlobalSocketReadSet);
        }    /* END for (i) */

      free(SelectSet);
      return(-1);
      }  /* END else (errno == EINTR) */
    }    /* END if (n == -1) */

  for (i = 0;(i < max_connection) && (n != 0);i++)
    {
    if (FD_ISSET(i, SelectSet))
      {
      /* this socket has data */

      n--;

      svr_conn[i].cn_lasttime = time((time_t *)0);

      if (svr_conn[i].cn_active != Idle)
        {
        netcounter_incr();

        svr_conn[i].cn_func(i);

        /* NOTE:  breakout if state changed (probably received shutdown request) */

        if ((SState != NULL) && (OrigState != *SState))
          break;
        }
      else
        {
        FD_CLR(i, GlobalSocketReadSet);
        close_conn(i);
        sprintf(tmpLine,"closed connection to fd %d - num_connections=%d (select bad socket)",
          i,
          num_connections);

        log_err(-1,id,tmpLine);
        }
      }
    }    /* END for (i) */

  /* NOTE:  break out if shutdown request received */

  if ((SState != NULL) && (OrigState != *SState))
    {
    free(SelectSet);
    return(0);
    }

  /* have any connections timed out ?? */

  now = time((time_t *)0);

  for (i = 0;i < max_connection;i++)
    {

    struct connection *cp;

    cp = &svr_conn[i];

    if (cp->cn_active != FromClientDIS)
      continue;

    if ((now - cp->cn_lasttime) <= PBS_NET_MAXCONNECTIDLE)
      continue;

    if (cp->cn_authen & PBS_NET_CONN_NOTIMEOUT)
      continue; /* do not time-out this connection */

    /* NOTE:  add info about node associated with connection - NYI */

    snprintf(tmpLine, sizeof(tmpLine), "connection %d to host %s has timed out after %d seconds - closing stale connection\n",
             i,
             PAddrToString(&cp->cn_addr),
             PBS_NET_MAXCONNECTIDLE);

    log_err(-1, "wait_request", tmpLine);

    /* locate node associated with interface, mark node as down until node responds */

    /* NYI */

    close_conn(i);
    }  /* END for (i) */

  free(SelectSet);
  return(0);
  }  /* END wait_request() */





/*
 * accept_conn - accept request for new connection
 * this routine is normally associated with the main socket,
 * requests for connection on the socket are accepted and
 * the new socket is added to the select set and the connection
 * structure - the processing routine is set to the external
 * function: process_request(socket)
 */

static void accept_conn(

  int sd)  /* main socket with connection request pending */

  {
  int newsock;

  struct sockaddr_in from;

  struct sockaddr_un unixfrom;

  torque_socklen_t fromsize;

  from.sin_addr.s_addr = 0;
  from.sin_port = 0;

  /* update lasttime of main socket */

  svr_conn[sd].cn_lasttime = time((time_t *)0);

  if (svr_conn[sd].cn_socktype == PBS_SOCK_INET)
    {
    fromsize = sizeof(from);
    newsock = accept(sd, (struct sockaddr *) & from, &fromsize);
    }
  else
    {
    fromsize = sizeof(unixfrom);
    newsock = accept(sd, (struct sockaddr *) & unixfrom, &fromsize);
    }

  if (newsock == -1)
    {
    return;
    }

  if ((num_connections >= max_connection) ||
      (newsock >= PBS_NET_MAX_CONNECTIONS))
    {
    close(newsock);

    return;  /* too many current connections */
    }

  /* add the new socket to the select set and connection structure */

  add_conn(
    newsock,
    FromClientDIS,
    (pbs_net_t)ntohl(from.sin_addr.s_addr),
    (unsigned int)ntohs(from.sin_port),
    svr_conn[sd].cn_socktype,
    read_func[(int)svr_conn[sd].cn_active]);

  return;
  }  /* END accept_conn() */




/*
 * add_conn - add a connection to the svr_conn array.
 * The params addr and port are in host order.
 *
 * NOTE:  This routine cannot fail.
 */

void add_conn(

  int            sock,    /* socket associated with connection */
  enum conn_type type,    /* type of connection */
  pbs_net_t      addr,    /* IP address of connected host */
  unsigned int   port,    /* port number (host order) on connected host */
  unsigned int   socktype, /* inet or unix */
  void (*func)(int))  /* function to invoke on data rdy to read */

  {
  num_connections++;

  FD_SET(sock, GlobalSocketReadSet);

  svr_conn[sock].cn_active   = type;
  svr_conn[sock].cn_addr     = addr;
  svr_conn[sock].cn_port     = (unsigned short)port;
  svr_conn[sock].cn_lasttime = time((time_t *)0);
  svr_conn[sock].cn_func     = func;
  svr_conn[sock].cn_oncl     = 0;
  svr_conn[sock].cn_socktype = socktype;

#ifndef NOPRIVPORTS

  if ((socktype == PBS_SOCK_INET) && (port < IPPORT_RESERVED))
    {
    svr_conn[sock].cn_authen = PBS_NET_CONN_FROM_PRIVIL;
    }
  else
    {
    /* AF_UNIX sockets */
    svr_conn[sock].cn_authen = 0;
    }

#else /* !NOPRIVPORTS */

  if (socktype == PBS_SOCK_INET)
    {
    /* All TCP connections are privileged */
    svr_conn[sock].cn_authen = PBS_NET_CONN_FROM_PRIVIL;
    }
  else
    {
    /* AF_UNIX sockets */
    svr_conn[sock].cn_authen = 0;
    }

#endif /* !NOPRIVPORTS */

  return;
  }  /* END add_conn() */





/*
 * close_conn - close a network connection
 * does physical close, also marks the connection table
 */

void close_conn(

  int sd) /* I */

  {

  if ((sd < 0) || (max_connection <= sd))
    {
    return;
    }

  if (svr_conn[sd].cn_active == Idle)
    {
    return;
    }

  close(sd);

  /* if there is a function to call on close, do it */

  if (svr_conn[sd].cn_oncl != 0)
    svr_conn[sd].cn_oncl(sd);

  /* 
   * In the case of a -t cold start, this will be called prior to
   * GlobalSocketReadSet being initialized
   */

  if (GlobalSocketReadSet != NULL)
  {
    FD_CLR(sd, GlobalSocketReadSet);
  }

  svr_conn[sd].cn_addr = 0;

  svr_conn[sd].cn_handle = -1;

  svr_conn[sd].cn_active = Idle;

  svr_conn[sd].cn_func = (void (*)())0;

  svr_conn[sd].cn_authen = 0;

  num_connections--;

  DIS_tcp_close(sd);


  return;
  }  /* END close_conn() */




/*
 * net_close - close all network connections but the one specified,
 * if called with impossible socket number (-1), all will be closed.
 * This function is typically called when a server is closing down and
 * when it is forking a child.
 *
 * We clear the cn_oncl field in the connection table to prevent any
 * "special on close" functions from being called.
 */

void net_close(

  int but)  /* I */

  {
  int i;

  for (i = 0;i < max_connection;i++)
    {
    if (i != but)
      {
      svr_conn[i].cn_oncl = 0;

      close_conn(i);
      }
    }    /* END for (i) */

  return;
  }  /* END net_close() */




/*
 * get_connectaddr - return address of host connected via the socket
 * This is in host order.
 */

pbs_net_t get_connectaddr(

  int sock)  /* I */

  {
  return(svr_conn[sock].cn_addr);
  }





int find_conn(

  pbs_net_t addr)  /* I */

  {
  int index;

  /* NOTE:  there may be multiple connections per addr (not handled) */

  for (index = 0;index < PBS_NET_MAX_CONNECTIONS;index++)
    {
    if (addr == svr_conn[index].cn_addr)
      {
      return(index);
      }
    }    /* END for (index) */

  return(-1);
  }  /* END find_conn() */





/*
 * get_connecthost - return name of host connected via the socket
 */

int get_connecthost(

  int   sock,     /* I */
  char *namebuf,  /* O (minsize=size) */
  int   size)     /* I */

  {

  struct hostent *phe;

  struct in_addr  addr;
  int             namesize = 0;

  static struct in_addr  serveraddr;
  static char           *server_name = NULL;

  if ((server_name == NULL) && (pbs_server_addr != 0))
    {
    /* cache local server addr info */

    serveraddr.s_addr = htonl(pbs_server_addr);

    if ((phe = gethostbyaddr(
                 (char *) & serveraddr,
                 sizeof(struct in_addr),
                 AF_INET)) == NULL)
      {
      server_name = strdup(inet_ntoa(serveraddr));
      }
    else
      {
      server_name = strdup(phe->h_name);
      }
    }

  size--;

  addr.s_addr = htonl(svr_conn[sock].cn_addr);

  if ((server_name != NULL) && (svr_conn[sock].cn_socktype & PBS_SOCK_UNIX))
    {
    /* lookup request is for local server */

    strcpy(namebuf, server_name);
    }
  else if ((server_name != NULL) && (addr.s_addr == serveraddr.s_addr))
    {
    /* lookup request is for local server */

    strcpy(namebuf, server_name);
    }
  else if ((phe = gethostbyaddr(
                    (char *) & addr,
                    sizeof(struct in_addr),
                    AF_INET)) == NULL)
    {
    strcpy(namebuf, inet_ntoa(addr));
    }
  else
    {
    namesize = strlen(phe->h_name);

    strncpy(namebuf, phe->h_name, size);

    *(namebuf + size) = '\0';
    }

  if (namesize > size)
    {
    /* FAILURE - buffer too small */

    return(-1);
    }

  /* SUCCESS */

  return(0);
  }  /* END get_connecthost() */




/*
** Put a human readable representation of a network address into
** a staticly allocated string.
*/
char *netaddr_pbs_net_t(

  pbs_net_t ipadd)

  {
  static char out[80];

  if (ipadd == 0)
    return "unknown";

  sprintf(out, "%ld.%ld.%ld.%ld",
          (ipadd & 0xff000000) >> 24,
          (ipadd & 0x00ff0000) >> 16,
          (ipadd & 0x0000ff00) >> 8,
          (ipadd & 0x000000ff));

  return (out);
  }




/* END net_server.c */

