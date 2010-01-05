/*****************************************************************************
 * dvblast.c
 *****************************************************************************
 * Copyright (C) 2004, 2008-2009 VideoLAN
 * $Id$
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *          Andy Gatward <a.j.gatward@reading.ac.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "dvblast.h"
#include "version.h"

/*****************************************************************************
 * Local declarations
 *****************************************************************************/
output_t **pp_outputs = NULL;
int i_nb_outputs = 0;
output_t output_dup = { 0 };
static char *psz_conf_file = NULL;
char *psz_srv_socket = NULL;
int i_ttl = 64;
in_addr_t i_ssrc = 0;
static int i_priority = -1;
int i_adapter = 0;
int i_fenum = 0;
int i_frequency = 0;
int i_srate = 27500000;
int i_satnum = 0;
int i_voltage = 13;
int b_tone = 0;
int i_bandwidth = 8;
char *psz_modulation = NULL;
int b_budget_mode = 0;
int b_slow_cam = 0;
int b_output_udp = 0;
int b_enable_epg = 0;
int b_unique_tsid = 0;
volatile int b_hup_received = 0;
int i_verbose = DEFAULT_VERBOSITY;
uint16_t i_src_port = DEFAULT_PORT;
in_addr_t i_src_addr = { 0 };
int b_src_rawudp = 0;
int i_asi_adapter = 0;

void (*pf_Open)( void ) = NULL;
block_t * (*pf_Read)( void ) = NULL;
int (*pf_SetFilter)( uint16_t i_pid ) = NULL;
void (*pf_UnsetFilter)( int i_fd, uint16_t i_pid ) = NULL;

/*****************************************************************************
 * Configuration files
 *****************************************************************************/
static void ReadConfiguration( char *psz_file )
{
    FILE *p_file;
    char psz_line[2048];
    int i;

    if ( psz_file == NULL )
    {
        msg_Err( NULL, "no config file" );
        return;
    }

    if ( (p_file = fopen( psz_file, "r" )) == NULL )
    {
        msg_Err( NULL, "can't fopen config file %s", psz_file );
        return;
    }

    while ( fgets( psz_line, sizeof(psz_line), p_file ) != NULL )
    {
        output_t *p_output = NULL;
        char *psz_parser, *psz_token, *psz_token2, *psz_token3;
        struct addrinfo *p_addr;
        struct addrinfo ai_hints;
        char sz_port[6];
        char *psz_displayname;
        uint16_t i_sid = 0;
        uint16_t *pi_pids = NULL;
        int i_nb_pids = 0;
        uint8_t i_config = 0;

        snprintf( sz_port, sizeof( sz_port ), "%d", DEFAULT_PORT );

        if ( !strncmp( psz_line, "#", 1 ) )
            continue;

        psz_token = strtok_r( psz_line, "\t\n ", &psz_parser );
        if ( psz_token == NULL )
            continue;

        if ( (psz_token3 = strrchr( psz_token, '/' )) != NULL )
        {
            *psz_token3 = '\0';
            if( strncasecmp( psz_token3 + 1, "udp", 3 ) == 0 )
                i_config |= OUTPUT_UDP;
        }

        if ( !strncmp( psz_token, "[", 1 ) )
        {
            if ( (psz_token2 = strchr( psz_token, ']' ) ) == NULL )
              continue;

            char *psz_maddr = malloc( psz_token2 - psz_token );
            memset( psz_maddr, '\0', ( psz_token2 - psz_token ) );
            strncpy( psz_maddr, psz_token + 1, ( psz_token2 - psz_token - 1 ));

            if ( (psz_token2 = strchr( psz_token2, ':' )) != NULL )
            {
                *psz_token2 = '\0';
                snprintf( sz_port, sizeof( sz_port ), "%d", atoi( psz_token2 + 1 ) );
            }

            p_addr = malloc( sizeof( p_addr ) );

            memset( &ai_hints, 0, sizeof( ai_hints ) );
            ai_hints.ai_socktype = SOCK_DGRAM;
            ai_hints.ai_flags    = AI_ADDRCONFIG | AI_NUMERICHOST | AI_NUMERICSERV;
            ai_hints.ai_family   = AF_INET6;
  
            int i_ai = getaddrinfo( psz_maddr, sz_port, &ai_hints, &p_addr );
            if ( i_ai != 0 )
            {
                msg_Err( NULL, "Cannot configure output [%s]:%s: %s", psz_maddr,
                         sz_port, gai_strerror( i_ai ) );
                continue;
            }

            psz_displayname = malloc( INET6_ADDRSTRLEN + 8 );
            snprintf( psz_displayname, ( INET6_ADDRSTRLEN + 8 ), "[%s]:%s",
                      psz_maddr, sz_port );

            free( psz_maddr );
        }
        else
        {
            if ( (psz_token2 = strrchr( psz_token, ':' )) != NULL )
            {
                *psz_token2 = '\0';
                snprintf( sz_port, sizeof( sz_port ), "%d",  atoi( psz_token2 + 1 ) );
            }

            p_addr = malloc( sizeof( p_addr ) );

            memset( &ai_hints, 0, sizeof( ai_hints ) );
            ai_hints.ai_socktype = SOCK_DGRAM;
            ai_hints.ai_flags    = AI_ADDRCONFIG | AI_NUMERICHOST | AI_NUMERICSERV;
            ai_hints.ai_family   = AF_INET;

            int i_ai = getaddrinfo( psz_token, sz_port, &ai_hints, &p_addr );
            if ( i_ai != 0 )
            {
                msg_Err( NULL, "Cannot configure output %s:%s: %s", psz_token,
                         sz_port, gai_strerror( i_ai ) );
                continue;
            }

            psz_displayname = malloc( INET_ADDRSTRLEN + 6 );
            snprintf( psz_displayname, ( INET_ADDRSTRLEN + 6 ), "%s:%s",
                      psz_token, sz_port );
        }

        psz_token = strtok_r( NULL, "\t\n ", &psz_parser );
        if ( psz_token == NULL )
            continue;
        if( atoi( psz_token ) )
          i_config |= OUTPUT_WATCH;
        else
          i_config &= ~OUTPUT_WATCH;

        psz_token = strtok_r( NULL, "\t\n ", &psz_parser );
        if ( psz_token == NULL )
            continue;
        i_sid = strtol(psz_token, NULL, 0);

        psz_token = strtok_r( NULL, "\t\n ", &psz_parser );
        if ( psz_token != NULL )
        {
            psz_parser = NULL;
            for ( ; ; )
            {
                psz_token = strtok_r( psz_token, ",", &psz_parser );
                if ( psz_token == NULL )
                    break;
                pi_pids = realloc( pi_pids,
                                   (i_nb_pids + 1) * sizeof(uint16_t) );
                pi_pids[i_nb_pids++] = strtol(psz_token, NULL, 0);
                psz_token = NULL;
            }
        }

        msg_Dbg( NULL, "conf: %s w=%d sid=%d pids[%d]=%d,%d,%d,%d,%d...",
                 psz_displayname,
                 ( i_config & OUTPUT_WATCH ) ? 1 : 0, i_sid, i_nb_pids,
                 i_nb_pids < 1 ? -1 : pi_pids[0],
                 i_nb_pids < 2 ? -1 : pi_pids[1],
                 i_nb_pids < 3 ? -1 : pi_pids[2],
                 i_nb_pids < 4 ? -1 : pi_pids[3],
                 i_nb_pids < 5 ? -1 : pi_pids[4] );

        for ( i = 0; i < i_nb_outputs; i++ )
        {
            if ( pp_outputs[i]->p_addr->ss_family == AF_INET )
            {
                struct sockaddr_in *p_esad = (struct sockaddr_in *)pp_outputs[i]->p_addr;
                struct sockaddr_in *p_nsad = (struct sockaddr_in *)p_addr->ai_addr;

                if ( ( p_esad->sin_addr.s_addr == p_nsad->sin_addr.s_addr ) &&
                     ( p_esad->sin_port == p_nsad->sin_port ) )
                {
                    p_output = pp_outputs[i];
                    output_Init( p_output, i_config, psz_displayname, (void *)p_addr );
                    break;
                }
            }
            else if ( pp_outputs[i]->p_addr->ss_family == AF_INET6 )
            {
                struct sockaddr_in6 *p_esad = (struct sockaddr_in6 *)pp_outputs[i]->p_addr;
                struct sockaddr_in6 *p_nsad = (struct sockaddr_in6 *)p_addr->ai_addr;

                if ( ( p_esad->sin6_addr.s6_addr32[0] == p_nsad->sin6_addr.s6_addr32[0] ) &&
                     ( p_esad->sin6_addr.s6_addr32[1] == p_nsad->sin6_addr.s6_addr32[1] ) &&
                     ( p_esad->sin6_addr.s6_addr32[2] == p_nsad->sin6_addr.s6_addr32[2] ) &&
                     ( p_esad->sin6_addr.s6_addr32[3] == p_nsad->sin6_addr.s6_addr32[3] ) &&
                     ( p_esad->sin6_port == p_nsad->sin6_port ) )
                {
                    p_output = pp_outputs[i];
                    output_Init( p_output, i_config, psz_displayname, (void *)p_addr );
                    break;
                }
            }
        }

        if ( i == i_nb_outputs )
            p_output = output_Create( i_config, psz_displayname, (void *)p_addr );

        if ( p_output != NULL )
        {
            demux_Change( p_output, i_sid, pi_pids, i_nb_pids );
            p_output->i_config |= OUTPUT_STILL_PRESENT;
        }

        free( pi_pids );
        freeaddrinfo( p_addr );
    }

    fclose( p_file );

    for ( i = 0; i < i_nb_outputs; i++ )
    {
        if ( ( pp_outputs[i]->i_config & OUTPUT_VALID ) && 
            !( pp_outputs[i]->i_config & OUTPUT_STILL_PRESENT ) )
        {
            msg_Dbg( NULL, "closing %s", pp_outputs[i]->psz_displayname );
            demux_Change( pp_outputs[i], 0, NULL, 0 );
            output_Close( pp_outputs[i] );
        }

        pp_outputs[i]->i_config &= ~OUTPUT_STILL_PRESENT;
    }
}

/*****************************************************************************
 * Signal Handler
 *****************************************************************************/
static void SigHandler( int i_signal )
{
    b_hup_received = 1;
}

/*****************************************************************************
 * Version
 *****************************************************************************/
static void DisplayVersion()
{
    msg_Raw( NULL, "DVBlast %d.%d.%d%s", VERSION_MAJOR, VERSION_MINOR,
                                         VERSION_REVISION, VERSION_EXTRA );
}

/*****************************************************************************
 * Entry point
 *****************************************************************************/
void usage()
{
    msg_Raw( NULL, "Usage: dvblast [-q] [-c <config file>] [-r <remote socket>] [-t <ttl>] [-o <SSRC IP>] [-i <RT priority>] [-a <adapter>] [-n <frontend number>] [-S <diseqc>] [-f <frequency>|-D <src mcast>:<port>|-A <ASI adapter>] [-s <symbol rate>] [-v <0|13|18>] [-p] [-b <bandwidth>] [-m <modulation] [-u] [-W] [-U] [-d <dest IP:port>] [-e] [-T]" );
    msg_Raw( NULL, "    -q: be quiet (less verbosity, repeat or use number for even quieter)" );
    msg_Raw( NULL, "    -v: voltage to apply to the LNB (QPSK)" );
    msg_Raw( NULL, "    -p: force 22kHz pulses for high-band selection (DVB-S)" );
    msg_Raw( NULL, "    -S: satellite number for diseqc (0: no diseqc, 1-4, A or B)" );
    msg_Raw( NULL, "    -m: DVB-C  qpsk|qam_16|qam_32|qam_64|qam_128|qam_256 (default qam_auto)" );
    msg_Raw( NULL, "        DVB-T  qam_16|qam_32|qam_64|qam_128|qam_256 (default qam_auto)" );
    msg_Raw( NULL, "        DVB-S2 qpsk|psk_8 (default legacy DVB-S)" );
    msg_Raw( NULL, "    -u: turn on budget mode (no hardware PID filtering)" );
    msg_Raw( NULL, "    -W: add extra delays for slow CAMs" );
    msg_Raw( NULL, "    -U: use raw UDP rather than RTP (required by some IPTV set top boxes)" );
    msg_Raw( NULL, "    -d: duplicate all received packets to a given destination" );
    msg_Raw( NULL, "    -D: read packets from a multicast address instead of a DVB card" );
    msg_Raw( NULL, "    -A: read packets from an ASI adapter (0-n)" );
    msg_Raw( NULL, "    -e: enable EPG pass through (EIT data)" );
    msg_Raw( NULL, "    -T: generate unique TS ID for each program" );
    msg_Raw( NULL, "    -h: display this full help" );
    msg_Raw( NULL, "    -V: only display the version" );
    exit(1);
}

int main( int i_argc, char **pp_argv )
{
    struct sched_param param;
    int i_error;
    int c;

    DisplayVersion();

    if ( i_argc == 1 )
        usage();

    while ( ( c = getopt(i_argc, pp_argv, "q::c:r:t:o:i:a:n:f:s:S:v:pb:m:uWUTd:D:A:ehV")) != -1 )
    {
        switch ( c )
        {
        case 'q':
            if ( optarg )
            {
                if ( *optarg == 'q' )  /* e.g. -qqq */
                {
                    i_verbose--;
                    while ( *optarg == 'q' )
                    {
                        i_verbose--;
                        optarg++;
                    }
                }
                else
                {
                    i_verbose -= atoi( optarg );  /* e.g. -q2 */
                }
            }
            else
            {
                i_verbose--;  /* -q */
            }
            break;

        case 'c':
            psz_conf_file = optarg;
            break;

        case 'r':
            if ( pf_Open != dvb_Open && pf_Open != NULL )
            {
                msg_Err( NULL, "-r is only available for linux-dvb input" );
                usage();
            }
            psz_srv_socket = optarg;
            break;

        case 't':
            i_ttl = strtol( optarg, NULL, 0 );
            break;

        case 'o':
        {
            struct in_addr maddr;
            if ( !inet_aton( optarg, &maddr ) )
                usage();
            i_ssrc = maddr.s_addr;
            break;
        }

        case 'i':
            i_priority = strtol( optarg, NULL, 0 );
            break;

        case 'a':
            i_adapter = strtol( optarg, NULL, 0 );
            break;

        case 'n':
            i_fenum = strtol( optarg, NULL, 0 );
            break;

        case 'f':
            i_frequency = strtol( optarg, NULL, 0 );
            if ( pf_Open != NULL )
                usage();
            pf_Open = dvb_Open;
            pf_Read = dvb_Read;
            pf_SetFilter = dvb_SetFilter;
            pf_UnsetFilter = dvb_UnsetFilter;
            break;

        case 's':
            i_srate = strtol( optarg, NULL, 0 );
            break;

        case 'S':
            i_satnum = strtol( optarg, NULL, 16 );
            break;

        case 'v':
            i_voltage = strtol( optarg, NULL, 0 );
            break;

        case 'p':
            b_tone = 1;
            break;

        case 'b':
            i_bandwidth = strtol( optarg, NULL, 0 );
            break;

        case 'm':
            psz_modulation = optarg;
            break;

        case 'u':
            b_budget_mode = 1;
            break;

        case 'W':
            b_slow_cam = 1;
            break;

        case 'U':
            b_output_udp = 1;
            break;

        case 'd':
        {
            char *psz_token, *psz_displayname;
            char sz_port[6];
            struct addrinfo *p_daddr;
            struct addrinfo ai_hints;
            int i_dup_config = 0;

            snprintf( sz_port, sizeof( sz_port ), "%d", DEFAULT_PORT );

            p_daddr = malloc( sizeof( p_daddr ) );
            memset( p_daddr, '\0', sizeof( p_daddr ) );

            if ( !strncmp( optarg, "[", 1 ) )
            {
                if ( (psz_token = strchr( optarg, ']' ) ) == NULL )
                {
                    msg_Err(NULL, "Invalid target address for -d switch");
                    break;
                }
    
                char *psz_maddr = malloc( psz_token - optarg );
                memset( psz_maddr, '\0', ( psz_token - optarg ) );
                strncpy( psz_maddr, optarg + 1, ( psz_token - optarg - 1 ));
    
                if ( (psz_token = strchr( psz_token, ':' )) != NULL )
                {
                    *psz_token = '\0';
                    snprintf( sz_port, sizeof( sz_port ), "%d", atoi( psz_token + 1 ) );
                }
    
    
                memset( &ai_hints, 0, sizeof( ai_hints ) );
                ai_hints.ai_socktype = SOCK_DGRAM;
                ai_hints.ai_flags    = AI_ADDRCONFIG | AI_NUMERICHOST | AI_NUMERICSERV;
                ai_hints.ai_family   = AF_INET6;
    
                int i_ai = getaddrinfo( psz_maddr, sz_port, &ai_hints, &p_daddr );
                if ( i_ai != 0 )
                {
                    msg_Err( NULL, "Cannot duplicate to [%s]:%s: %s", psz_maddr,
                             sz_port, gai_strerror( i_ai ) );
                    break;
                }

                psz_displayname = malloc( INET6_ADDRSTRLEN + 20 );
                snprintf( psz_displayname, ( INET6_ADDRSTRLEN + 20 ),
                          "duplicate ([%s]:%s)", psz_maddr, sz_port );

                i_dup_config |= OUTPUT_VALID;
    
                free( psz_maddr );
            }
            else
            {
                if ( (psz_token = strrchr( optarg, ':' )) != NULL )
                {
                    *psz_token = '\0';
                    snprintf( sz_port, sizeof( sz_port ), "%d",  atoi( psz_token + 1 ) );
                }

                memset( &ai_hints, 0, sizeof( ai_hints ) );
                ai_hints.ai_socktype = SOCK_DGRAM;
                ai_hints.ai_flags    = AI_ADDRCONFIG | AI_NUMERICHOST | AI_NUMERICSERV;
                ai_hints.ai_family   = AF_INET;

                int i_ai = getaddrinfo( optarg, sz_port, &ai_hints, &p_daddr );
                if ( i_ai != 0 )
                {
                    msg_Err( NULL, "Cannot duplicate to %s:%s: %s", optarg,
                             sz_port, gai_strerror( i_ai ) );
                    break;
                }

                psz_displayname = malloc( INET_ADDRSTRLEN + 18 );
                snprintf( psz_displayname, ( INET6_ADDRSTRLEN + 18 ),
                          "duplicate (%s:%s)", optarg, sz_port );

                i_dup_config |= OUTPUT_VALID;
            }

            if ( i_dup_config &= OUTPUT_VALID ) {
              output_Init( &output_dup, i_dup_config, psz_displayname, p_daddr );
            }
            else
              msg_Err( NULL, "Invalid configuration for -d switch: %s" , optarg);

            freeaddrinfo( p_daddr );
            break;
        }

        case 'D':
        {
            char *psz_token;
            struct in_addr maddr;
            if ( pf_Open != NULL )
                usage();
            if ( psz_srv_socket != NULL )
            {
                msg_Err( NULL, "-r is only available for linux-dvb input" );
                usage();
            }

            pf_Open = udp_Open;
            pf_Read = udp_Read;
            pf_SetFilter = udp_SetFilter;
            pf_UnsetFilter = udp_UnsetFilter;

            if ( (psz_token = strrchr( optarg, '/' )) != NULL )
            {
                *psz_token = '\0';
                b_src_rawudp = ( strncasecmp( psz_token + 1, "udp", 3 ) == 0 );
            }
            if ( (psz_token = strrchr( optarg, ':' )) != NULL )
            {
                *psz_token = '\0';
                i_src_port = atoi( psz_token + 1 );
            }
            if ( !inet_aton( optarg, &maddr ) )
                usage();
            i_src_addr = maddr.s_addr;
            break;
        }

        case 'A':
            i_asi_adapter = strtol( optarg, NULL, 0 );
            if ( pf_Open != NULL )
                usage();
            if ( psz_srv_socket != NULL )
            {
                msg_Err( NULL, "-r is only available for linux-dvb input" );
                usage();
            }
            pf_Open = asi_Open;
            pf_Read = asi_Read;
            pf_SetFilter = asi_SetFilter;
            pf_UnsetFilter = asi_UnsetFilter;
            break;

        case 'e':
            b_enable_epg = 1;
            break;

        case 'T':
            b_unique_tsid = 1;
            break;

        case 'V':
            exit(0);
            break;

        case 'h':
        default:
            usage();
        }
    }
    if ( optind < i_argc )
        usage();

    msg_Warn( NULL, "restarting" );

    if ( b_output_udp )
    {
        msg_Warn( NULL, "raw UDP output is deprecated.  Please consider using RTP." );
        msg_Warn( NULL, "for DVB-IP compliance you should use RTP." );
    }

    signal( SIGHUP, SigHandler );
    srand( time(NULL) * getpid() );

    demux_Open();

    if ( i_priority > 0 )
    {
        memset( &param, 0, sizeof(struct sched_param) );
        param.sched_priority = i_priority;
        if ( (i_error = pthread_setschedparam( pthread_self(), SCHED_RR,
                                               &param )) )
        {
            msg_Warn( NULL, "couldn't set thread priority: %s",
                      strerror(i_error) );
        }
    }

    ReadConfiguration( psz_conf_file );

    if ( psz_srv_socket != NULL )
        comm_Open();

    for ( ; ; )
    {
        if ( b_hup_received )
        {
            b_hup_received = 0;
            msg_Warn( NULL, "HUP received, reloading" );
            ReadConfiguration( psz_conf_file );
        }

        demux_Run();
    }
}
