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
 *
 *  qgpureset - Reset GPU EEC error counts on a MOM
 *
 * Authors:
 *      Al Taufer
 *      Adaptive Computing
 */

#include "cmds.h"
#include <pbs_config.h>   /* the master config generated by configure */

int pbs_gpureset(int c, char *node, char *gpuid, int permanent, int vol);
int exitstatus = 0; /* Exit Status */

static void execute(char *, char *, int, int, char *);

/*  qgpureset */

int main(

  int    argc,
  char **argv)

  {
  int   c;
  int   errflg = 0;
  char *gpuid = NULL;
  int   ecc_perm = 0;
  int   ecc_vol = 0;

  char *mom_node = NULL;

#define GETOPT_ARGS "H:g:pv"

  while ((c = getopt(argc, argv, GETOPT_ARGS)) != EOF)
    {
    switch (c)
      {

      case 'H':

        if (strlen(optarg) == 0)
          {
          fprintf(stderr, "qgpureset: illegal -H value\n");
          errflg++;
          break;
          }

        mom_node = optarg;

        break;

      case 'g':

        if (strlen(optarg) == 0)
          {
          fprintf(stderr, " qgpureset: illegal -g value\n");
          errflg++;
          break;
          }

        gpuid = optarg;

        break;

      case 'p':

        ecc_perm = 1;

        break;

      case 'v':

        ecc_vol = 1;

        break;

      default:

        errflg++;

        break;
      }
    }    /* END while (c) */

  if (errflg || (mom_node == NULL) || (gpuid == NULL) ||
      ((ecc_perm == 1) && (ecc_vol == 1)))
    {
    static char usage[] = "usage:  qgpureset -H host -g gpuid -p -v\n";

    fprintf(stderr, "%s", usage);

    fprintf(stderr, "       -p and -v are mutually exclusive\n");

    exit(2);
    }
  else if ((ecc_perm == 0) && (ecc_vol == 0))
    {
    fprintf(stderr, "%s", "-p or -v is required\n");

    exit(2);
    }

  execute(mom_node, gpuid, ecc_perm, ecc_vol, "");

  exit(exitstatus);

  /*NOTREACHED*/

  return(0);
  }  /* END main() */

/*
 * void execute( char *node, char *gpuid, int ecc_perm, int ecc_vol, char *server )
 *
 * node     The name of the MOM node.
 * gpuid    The id of the GPU.
 * ecc_perm The value for resetting the permanent ECC count.
 * ecc_vol  The value for resetting the volatile ECC count.
 * server   The name of the server to send to.
 *
 * Returns:
 *  None
 *
 * File Variables:
 *  exitstatus  Set to two if an error occurs.
 */
static void
execute(char *node, char *gpuid, int ecc_perm, int ecc_vol, char *server)
  {
  int ct;         /* Connection to the server */
  int merr;       /* Error return from pbs_manager */
  char *errmsg;   /* Error message from pbs_manager */

  /* The request to change mode */

  if ((ct = cnt2server(server)) > 0)
    {
    merr = pbs_gpureset(ct, node, gpuid, ecc_perm, ecc_vol);

    if (merr != 0)
      {
      errmsg = pbs_geterrmsg(ct);

      if (errmsg != NULL)
        {
        fprintf(stderr, " qgpureset: %s ", errmsg);
        }
      else
        {
        fprintf(stderr, " qgpureset: Error (%d - %s) resetting GPU ECC counts",
                pbs_errno,
                pbs_strerror(pbs_errno));
        }

      if (notNULL(server))
        fprintf(stderr, "@%s", server);

      fprintf(stderr, "\n");

      exitstatus = 2;
      }

    pbs_disconnect(ct);
    }
  else
    {
    fprintf(stderr, " qgpureset: could not connect to server %s (%d) %s\n",
            server,
            pbs_errno,
            pbs_strerror(pbs_errno));
    exitstatus = 2;
    }
  }
