/*****************************************************************************
 * dvblast.h
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

#include <netdb.h>
#include <sys/socket.h>

#define DEFAULT_PORT 3001
#define TS_SIZE 188
#define NB_BLOCKS 7
#define NB_BLOCKS_IPV6 6          // assume MTU of 1280 bytes for IPv6
#define RTP_SIZE 12
#define EMPTY_PID 8192
#define PADDING_PID 8191
#define WATCHDOG_WAIT 10000000LL
#define MAX_ERRORS 1000
#define DEFAULT_VERBOSITY 3
#define MAX_POLL_TIMEOUT 100000 /* 100 ms */
#define DEFAULT_OUTPUT_LATENCY 200000 /* 200 ms */
#define DEFAULT_MAX_RETENTION 40000 /* 40 ms */
#define MAX_EIT_RETENTION 500000 /* 500 ms */

/*****************************************************************************
 * Output configuration flags (for output_t -> i_config) - bit values
 * Bit  0 : Set for watch mode
 * Bit  1 : Set output still present
 * Bit  2 : Set if output is valid (replaces m_addr != 0 tests)
 * Bit  3 : Set for UDP, otherwise use RTP if a network stream
 * Bit  4 : Set for file / FIFO output, unset for network (future use)
 * Bit  5 : Set if DVB conformance tables are inserted
 * Bit  6 : Set if DVB EIT schedule tables are forwarded
 *****************************************************************************/

#define OUTPUT_WATCH         0x01
#define OUTPUT_STILL_PRESENT 0x02
#define OUTPUT_VALID         0x04
#define OUTPUT_UDP           0x08
#define OUTPUT_FILE          0x10
#define OUTPUT_DVB           0x20
#define OUTPUT_EPG           0x40

typedef int64_t mtime_t;

typedef struct block_t
{
    uint8_t p_ts[TS_SIZE];
    int i_refcount;
    mtime_t i_dts;
    struct block_t *p_next;
} block_t;

typedef struct packet_t packet_t;

typedef struct output_t
{
    /* address information, protocol agnostic */
    struct sockaddr_storage *p_addr;
    socklen_t i_addrlen;

    /* display string */
    char *psz_displayname;

    /* output */
    int i_handle;
    packet_t *p_packets, *p_last_packet;
    uint16_t i_cc;
    mtime_t i_ref_timestamp;
    mtime_t i_ref_wallclock;

    /* demux */
    int i_nb_errors;
    mtime_t i_last_error;
    uint8_t *p_pat_section;
    uint8_t i_pat_version, i_pat_cc;
    uint8_t *p_pmt_section;
    uint8_t i_pmt_version, i_pmt_cc;
    uint8_t *p_nit_section;
    uint8_t i_nit_version, i_nit_cc;
    uint8_t *p_sdt_section;
    uint8_t i_sdt_version, i_sdt_cc;
    block_t *p_eit_ts_buffer;
    uint8_t i_eit_ts_buffer_offset, i_eit_cc;

    /* configuration */
    uint16_t i_sid; /* 0 if raw mode */
    uint16_t *pi_pids;
    int i_nb_pids;
    int i_ttl;
    in_addr_t i_ssrc;
    uint16_t i_tsid;
    bool b_fixed_tsid;
    mtime_t i_output_latency, i_max_retention;
    uint8_t i_config;
} output_t;

extern int i_syslog;
extern int i_verbose;
extern output_t **pp_outputs;
extern int i_nb_outputs;
extern output_t output_dup;
extern char *psz_srv_socket;
extern in_addr_t i_ssrc;
extern int i_adapter;
extern int i_fenum;
extern int i_frequency;
extern int i_srate;
extern int i_satnum;
extern int i_fec;
extern int i_rolloff;
extern int i_voltage;
extern int b_tone;
extern int i_bandwidth;
extern char *psz_modulation;
extern int b_budget_mode;
extern int b_random_tsid;
extern uint16_t i_network_id;
extern const char *psz_network_name;
extern mtime_t i_wallclock;
extern volatile int b_hup_received;
extern int i_comm_fd;
extern uint16_t i_src_port;
extern in_addr_t i_src_addr;
extern int b_src_rawudp;
extern int i_asi_adapter;

extern void (*pf_Open)( void );
extern block_t * (*pf_Read)( mtime_t i_poll_timeout );
extern int (*pf_SetFilter)( uint16_t i_pid );
extern void (*pf_UnsetFilter)( int i_fd, uint16_t i_pid );

/*****************************************************************************
 * Prototypes
 *****************************************************************************/

/* Connect/Disconnect from syslogd */
void msg_Connect( const char *ident );
void msg_Disconnect( void );

/* */
void msg_Info( void *_unused, const char *psz_format, ... );
void msg_Err( void *_unused, const char *psz_format, ... );
void msg_Warn( void *_unused, const char *psz_format, ... );
void msg_Dbg( void *_unused, const char *psz_format, ... );
void msg_Raw( void *_unused, const char *psz_format, ... );

/* */
mtime_t mdate( void );
void msleep( mtime_t delay );
void hexDump( uint8_t *p_data, uint32_t i_len );

void dvb_Open( void );
void dvb_Reset( void );
block_t * dvb_Read( mtime_t i_poll_timeout );
int dvb_SetFilter( uint16_t i_pid );
void dvb_UnsetFilter( int i_fd, uint16_t i_pid );
uint8_t dvb_FrontendStatus( uint8_t *p_answer, ssize_t *pi_size );

void udp_Open( void );
block_t * udp_Read( mtime_t i_poll_timeout );
int udp_SetFilter( uint16_t i_pid );
void udp_UnsetFilter( int i_fd, uint16_t i_pid );

void asi_Open( void );
block_t * asi_Read( mtime_t i_poll_timeout );
int asi_SetFilter( uint16_t i_pid );
void asi_UnsetFilter( int i_fd, uint16_t i_pid );

void demux_Open( void );
void demux_Run( block_t *p_ts );
void demux_Change( output_t *p_output, int i_tsid, uint16_t i_sid,
                   uint16_t *pi_pids, int i_nb_pids );
void demux_ResendCAPMTs( void );
bool demux_PIDIsSelected( uint16_t i_pid );

output_t *output_Create( const char *psz_displayname, struct addrinfo *p_ai );
int output_Init( output_t *p_output, const char *psz_displayname,
                 struct addrinfo *p_ai );
void output_Close( output_t *p_output );
void output_Put( output_t *p_output, block_t *p_block );
mtime_t output_Send( void );
void output_SetTTL( output_t *p_output, int i_ttl );

void comm_Open( void );
void comm_Read( void );

/*****************************************************************************
 * block_New
 *****************************************************************************/
static inline block_t *block_New( void )
{
    block_t *p_block = malloc(sizeof(block_t));
    p_block->p_next = NULL;
    p_block->i_refcount = 1;
    return p_block;
}

/*****************************************************************************
 * block_Delete
 *****************************************************************************/
static inline void block_Delete( block_t *p_block )
{
    free( p_block );
}

/*****************************************************************************
 * block_DeleteChain
 *****************************************************************************/
static inline void block_DeleteChain( block_t *p_block )
{
    while ( p_block != NULL )
    {
        block_t *p_next = p_block->p_next;
        free( p_block );
        p_block = p_next;
    }
}
