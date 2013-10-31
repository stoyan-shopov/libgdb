/*

Copyright (C) 2011-2012 stoyan shopov

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/


/*
 * include section follows
 */
#ifdef __LINUX__
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#else
#define _WIN32_WINNT	0x0501
#include <windows.h>
#include <wincon.h>
#include <winsock2.h>
#include <winuser.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>
#include <setjmp.h>
#include <stdio.h>

#include "libgdb.h"

/*
 * local constants follow
 */
enum
{
	/*! gdb break character ascii code */
	GDB_BREAK_CHAR	= 3,
	/*! reception buffer length, in bytes */
	RX_BUF_LEN	= 128,
	/*! transmission buffer length, in bytes */
	TX_BUF_LEN	= 128,
	/*! maximum length of received/transmitted packets from the gdbserver */
	MAX_PACKET_LEN	= 1024 * 8 - 16,
	/*! read timeout waiting for data from the gdbserver, seconds part */
	GDB_SERVER_READ_TIMEOUT_SEC	= 300,
	/*! read timeout waiting for data from the gdbserver, microseconds part */
	GDB_SERVER_READ_TIMEOUT_USEC	= 100000,
};

static const char hexchars[16] = "0123456789abcdef";

/*
 * local types follow
 */ 

/*! error code enumeration */
enum ENUM_LIBGDB_ERR
{
	/*! no error */
	LIBGDB_ERR_NO_ERROR = 0,
	/*! connection shutdown by the remote gdbserver */
	LIBGDB_ERR_CONNECTION_SHUTDOWN,
	/*! generic communication error with the remote gdbserver */
	LIBGDB_ERR_COMM_ERROR,
	/*! read timeout waiting data from the remote gdbserver */
	LIBGDB_ERR_READ_TIMEOUT,
};

/*! context data used by libgdb */
struct libgdb_ctx
{
	/*! exception handling jump buffer */
	jmp_buf	jmpbuf;
	/*! error code (most commonly initialized by code performing a longjmp()) */
	enum ENUM_LIBGDB_ERR err;
	/*! annotation enable flag
 	 *
	 * if set to true, libgdb will print annotated output that
 	 * is suitable for consuming by a machine interface reader */
	bool is_annotation_enabled;
	/*! maximum number of words to transfer in a single read/write memory packet request
	 *
	 * some targets may have small memory buffers that are unable
	 * to hold a whole memory read/write request packet when the request
	 * is for a large memory area
	 *
	 * if this field is nonzero, libgdb will take this field in
	 * account and will cause the transfer of no more than this amount of
	 * words in a single memory access request packet; if
	 * necessary - wbgdb ill split large memory read/write requests into
	 * smaller request packets, reading/writing at most this amount of
	 * words at a time
	 *
	 * if this field is zero, libgdb will transfer as much words
	 * as can fit in the 'rx/txpacket' buffers below - it is essential that these
	 * buffer are of the same size */
	int mem_access_max_nr_words;
	/*! socket over which to communicate with a gdb server */
	int socket;
	/*! reception buffer */
	char rxbuf[RX_BUF_LEN];
	/*! reception buffer read index */
	int rxidx;
	/*! reception buffer read count */
	int rxcnt;
	/*! transmission buffer */
	char txbuf[RX_BUF_LEN];
	/*! transmission buffer write index */
	int txidx;
	/*! buffer to hold the packet received from the gdbserver */
	char rxpacket[MAX_PACKET_LEN];
	/*! buffer to hold the packet sent to the gdbserver */
	char txpacket[MAX_PACKET_LEN];
	/*! a data structure holding the data needed for asynchronous packet reception from the target gdbserver probe */
	struct
	{
		/*! state enumeration variable for the asynchronous packet reception state machine */
		enum
		{
			/*! invalid state */
			ASYNC_RX_STATE_INVALID		= 0,
			/*! waiting for the start-of-packet character ('$') */
			ASYNC_RX_STATE_WAITING_START,
			/*! data receiving state */
			ASYNC_RX_STATE_READING_DATA,
			/*! waiting for the first cbhecksum character */
			ASYNC_RX_STATE_WAITING_FIRST_CKSUM_CHAR,
			/*! waiting for the second checksum character */
			ASYNC_RX_STATE_WAITING_SECOND_CKSUM_CHAR,
		}
		state;
		/*! checksum computed */
		uint8_t	cksum;
		/*! checksum received */
		uint8_t	rx_cksum;
		/*! incoming packet data buffer */
		char async_rxpacket[MAX_PACKET_LEN];
		/*! incoming packet data index */
		int idx;
	};
#ifndef __LINUX__
	/*! winsock specific data used on windows machines */
	WSADATA wsadata;
#endif
};

/*
 * local functions follow
 */


/*!
 *	\fn	static inline hex(char c)
 *	\brief	given an ascii hex digit, convert it to binary
 *
 *	\param	c	the ascii hex digit to convert to binary
 *	\return	the conversion result */
static inline unsigned int hex(char c)
{
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/*!
 *	\fn	static void hex_to_mem(char * dest, char * src, int cnt)
 *	\brief	converts data from ascii hex to a binary representation
 *
 *	\param	dest	the buffer where to store the result
 *	\param	src	the buffer where to read data from
 *	\param	cnt	convert no more than this amount of bytes
 *	\return	none */
static void hex_to_mem(char * dest, char * src, int cnt)
{
int i;
unsigned int h, l;

	for (i = 0; i < cnt; i ++)
	{
		h = hex(src[i << 1]);
		l = hex(src[(i << 1) + 1]);
		if (h < 0 || l < 0)
			/*!	\todo	error - is it necessary to do anything special here? */
			break;
		* dest ++ = (h << 4) | l;
	}
}

/*!
 *	\fn	static void mem_to_hex(char * dest, char * src, int cnt)
 *	\brief	converts data from binary to ascii hex representation
 *
 *	\param	dest	the buffer where to store the result
 *	\param	src	the buffer where to read data from
 *	\param	cnt	convert no more than this amount of bytes
 *	\return	none */
static void mem_to_hex(char * dest, char * src, int cnt)
{
int i;
unsigned int x;

	for (i = 0; i < cnt; i ++)
	{
		x = ((unsigned char *) src)[i];
		* dest ++ = hexchars[x >> 4];
		* dest ++ = hexchars[x & 15];
	}
}

/*!
 *	\fn	static char get_char(struct libgdb_ctx * ctx)
 *	\brief	retrieves the next character sent by a connected gdbserver
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	the next character sent by a connected gdbserver
 *	\note	in case of an error, this function will longjmp()
 *		to the context saved in ctx->jmpbuf */
static char get_char(struct libgdb_ctx * ctx)
{
	if (ctx->rxidx == ctx->rxcnt)
	{
		/* input buffer empty - refill it */
		int i;
		fd_set fd;
		struct timeval tout;

		FD_ZERO(&fd);
		FD_SET(ctx->socket, &fd);
		tout.tv_sec = GDB_SERVER_READ_TIMEOUT_SEC;
		tout.tv_usec = GDB_SERVER_READ_TIMEOUT_USEC;
		i = select(ctx->socket + 1, & fd, 0, 0, & tout);
		if (i == 1 && FD_ISSET(ctx->socket, &fd))
		{
			i = recv(ctx->socket, ctx->rxbuf, sizeof ctx->rxbuf, 0);
			if (i == 0)
			{
				ctx->err = LIBGDB_ERR_CONNECTION_SHUTDOWN;
				longjmp(ctx->jmpbuf, ctx->err);
			}
			else if (i < 0)
			{
				ctx->err = LIBGDB_ERR_COMM_ERROR;
				longjmp(ctx->jmpbuf, ctx->err);
			}
			ctx->rxcnt = i;
			ctx->rxidx = 0;
		}
		else
		{
			eprintf("timeout receiving data from the gdbserver\n");
			ctx->err = LIBGDB_ERR_READ_TIMEOUT;
			longjmp(ctx->jmpbuf, ctx->err);
		}

	}
	if (0) printf ("#%c ", ctx->rxbuf[ctx->rxidx]);
	return ctx->rxbuf[ctx->rxidx ++];
}

/*!
 *	\fn	static void txsync(struct libgdb_ctx * ctx)
 *	\brief	flushes any pending data to a connected gdb server
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	none */
static void txsync(struct libgdb_ctx * ctx)
{
	int i;
	if (ctx->txidx == 0)
		/* nothing to send */
		return;
	i = send(ctx->socket, ctx->txbuf, ctx->txidx, 0);
	if (i < 0 || i != ctx->txidx)
	{
		ctx->err = LIBGDB_ERR_COMM_ERROR;
		longjmp(ctx->jmpbuf, ctx->err);
	}
	ctx->txidx = 0;
}

/*!
 *	\fn	static void send_char(struct libgdb_ctx * ctx, char c)
 *	\brief	sends a character to a connected gdbserver
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	none
 *	\none	in case of an error, this function will longjmp()
 *		to the context saved in ctx->jmpbuf */
static void send_char(struct libgdb_ctx * ctx, char c)
{
	ctx->txbuf[ctx->txidx ++] = c;
	/* if the buffer gets full - send it */
	if (ctx->txidx == sizeof ctx->txbuf)
		txsync(ctx);
}

/*!
 *	\fn	static int getpacket(struct libgdb_ctx * ctx, bool ignore_stop_packets)
 *	\brief	receives a packet from a connected gdbserver
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	ignore_stop_packets	if true, stop packets received
 *					from the gdbserver will be
 *					ignored
 *	\return	0 on success, -1 if a packet too large to be held
 *		in the ctx->rxpacket buffer was received
 *	\note	retransmission from the gdbserver is requested
 *		for packets with checksum errors */
static int getpacket(struct libgdb_ctx * ctx, bool ignore_stop_packets)
{
	unsigned char cksum;
	unsigned char xcksum;
	char c;
	int i;

	while (1)
	{
		/* wait around for the start character, ignore all other characters */
		while ((c = get_char(ctx)) != '$')
			;
retry:
		cksum = 0;
		i = 0;

		/* now, read until a '#' or the end of the buffer is reached */
		while (1)
		{
			c = get_char(ctx);
			if (c == '$')
				goto retry;
			if (c == '#')
				break;
			cksum = cksum + c;
			if (i < sizeof ctx->rxpacket - 1)
				ctx->rxpacket[i ++] = c;
		}
		ctx->rxpacket[i ++] = 0;

		/* read the checksum */
		xcksum = hex(get_char(ctx)) << 4;
		xcksum |= hex(get_char(ctx));

		if (cksum != xcksum)
		{
			send_char(ctx, '-');	/* failed checksum */
			txsync(ctx);
		}
		else
		{
			send_char(ctx, '+');	/* successful transfer */
			txsync(ctx);

			/* if a sequence char is present, reply the sequence ID */
			if (ctx->rxpacket[2] == ':')
			{
				send_char(ctx, ctx->rxpacket[0]);
				send_char(ctx, ctx->rxpacket[1]);
				txsync(ctx);

				/* discard the sequence number */
				memmove(ctx->rxpacket, ctx->rxpacket + 3, i - 3);
			}
			/* packet received successfully - check for packet overflow */
			if (i == sizeof ctx->rxpacket)
			{
				eprintf("packet received too long, packet will be discarded\n");
				return -1;
			}
			else if (!ignore_stop_packets || (ctx->rxpacket[0] != 'S' && ctx->rxpacket[0] != 'T'))
				return 0;

		}
	}
}


/*!
 *	\fn	static void putpacket(struct libgdb_ctx * ctx, bool wait_confirmation)
 *	\brief	sends a packet to a connected gdbserver
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	wait_confirmation	if true, wait for a confirmation
 *					that the packet was correctly received
 *					by the gdb server; if false - do
 *					not wait for confirmation
 *	\return	none */
static void putpacket(struct libgdb_ctx * ctx, bool wait_confirmation)
{
	unsigned char cksum;
	int i;
	char c;

	c = 0;
	/*  $<packet info>#<checksum>. */
	do
	{
		if (c)
		{
			fd_set fd;
			struct timeval tout;
			printf("RETRYING TRANSFER\n");
			if (0) exit(1);
			/* most probably a protocol error/desync - discard
			 * received data and any data currently pending to be
			 * read */
			send_char(ctx, '+');
			txsync(ctx);
			FD_ZERO(&fd);
			FD_SET(ctx->socket, &fd);
			tout.tv_sec = GDB_SERVER_READ_TIMEOUT_SEC;
			tout.tv_usec = GDB_SERVER_READ_TIMEOUT_USEC;
			i = select(ctx->socket + 1, & fd, 0, 0, & tout);
			if (i == 1 && FD_ISSET(ctx->socket, &fd))
			{
				recv(ctx->socket, ctx->rxbuf, sizeof ctx->rxbuf, 0);
			}
		}
		ctx->rxidx = ctx->rxcnt = 0;
		send_char(ctx, '$');
		cksum = 0;
		i = 0;

		while ((c = ctx->txpacket[i]))
		{
			send_char(ctx, c);
			cksum += c;
			i ++;
		}

		send_char(ctx, '#');
		send_char(ctx, hexchars[cksum >> 4]);
		send_char(ctx, hexchars[cksum & 0xf]);

		txsync(ctx);

	}
	while (wait_confirmation && (c = (get_char(ctx)) != '+'));
}



/*!
 *	\fn	static int is_error_packet(struct libgdb_ctx * ctx)
 *	\brief	determines if a packet received from a connected gdb server is an error code packet
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	0, if the last received packet does not contain error
 *		status information, 1 - if the packet is an 'OK' response,
 *		a negative number corresponding to the error code in case
 *		the last received packet is an 'Exxx' error status packet */
static int is_error_packet(struct libgdb_ctx * ctx)
{
char * s;

	s = ctx->rxpacket;
	if (s[0] == 'O' && s[1] == 'K')
		return 1;
	else if (s[0] == 'E')
	{
		int errcode;
		errcode = strtol(s + 1, 0, 16);
		eprintf("gdbserver error code: %i\n", errcode);
		return - errcode; 
	}
	else
		return 0;
}

/*!
 *	\fn	static inline int get_max_mem_xfer_words(struct libgdb_ctx * ctx)
 *	\brief	computes the maximum number of words that can fit in a single packet for memory write request packets
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	the maximum number of words that can fit in a single
 *		packet for memory write request packets */
static inline int get_max_mem_xfer_words(struct libgdb_ctx * ctx)
{
	return (MAX_PACKET_LEN
			- /* one byte for the null terminator */ 1
			- /* ... and one more, the checks for packet overflow depend on this */ 1
			- /* maximum length of the 'Mxxx,xxx:' command string */ 19)
			/ (sizeof(uint32_t) << /* 2 ascii hex characters per byte */ 1);
}

/*
 * exported functions follow
 */

/*!
 *	\fn	void libgdb_send_ack(struct libgdb_ctx * ctx)
 *	\brief	sends an acknowledge (the '+') chaacter to a connected gdbserver
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	none */
void libgdb_send_ack(struct libgdb_ctx * ctx)
{
	send_char(ctx, '+');
	txsync(ctx);
}


/*!
 *	\fn	int libgdb_set_max_nr_words_xferred(struct libgdb_ctx * ctx, int maxwords)
 *	\brief	sets the maximum number of words to be transferred at a time in memory write request packets
 *
 *	see the comments about the 'mem_access_max_nr_words' field in
 *	'struct libgdb_ctx' for explanation why this function is
 *	necessary/useful
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	maxwords	new value for the maximum number of
 *				words to transfer in a single memory
 *				access request packet; if this is zero,
 *				libgdb will transfer as many words as
 *				can fit in its buffers allocated for
 *				packets to be sent to a connected
 *				gdbserver - please note that this is
 *				*not* a failsafe setting
 *	\return	previous value of the maximum number of words that are
 *		transferred in a single memory access request packet, -1
 *		if this request cannot be satisfied (e.g. because the
 *		allocated packet buffers are not large enough to hold
 *		the amount of words requested) */
int libgdb_set_max_nr_words_xferred(struct libgdb_ctx * ctx, int maxwords)
{
int i;

	/* sanity checks */
	i = get_max_mem_xfer_words(ctx);
	if (i <= 0)
		/* ??? */
		return -1;
	if (i < maxwords)
		return -1;
	i = ctx->mem_access_max_nr_words;
	ctx->mem_access_max_nr_words = maxwords;
	return i;
}

/*!
 *	\fn	struct libgdb_ctx * libgdb_init(void)
 *	\brief	initializes the libgdb library
 *
 *	\param	none
 *	\return	a pointer to a library internal context data structure
 *		that is to be passed to the library functions on
 *		subsequent use; null pointer is returned in case of
 *		some error */
struct libgdb_ctx * libgdb_init(void)
{
struct libgdb_ctx * s;

	s = (struct libgdb_ctx *) calloc(1, sizeof(struct libgdb_ctx));
	if (!s)
		return 0;
	s->is_annotation_enabled = false;
	s->state = ASYNC_RX_STATE_WAITING_START;
#ifndef __LINUX__
	{
		int err;
		err = WSAStartup(MAKEWORD(1, 1), & s->wsadata);
		if (err)
		{
			eprintf("%s(): error initializing the winsock2 library, error %i\n", __func__, err);
			free(s);
			return 0;
		}
	}
#endif
	return s;
}

/*!
 *	\fn	int libgdb_connect(struct libgdb_ctx * ctx, const char * host, int port_nr)
 *	\brief	attempts connection to a gdb server
 *
 *	attempts connecting to a gdb server running on machine 'host',
 *	and listening on the specified port
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	host	the host to connect to
 *	\port	port	the port to connect to
 *	\return	0 on success, -1 on error
 *	\todo	only inet dot addresses are supported right now */
int libgdb_connect(struct libgdb_ctx * ctx, const char * host, int port_nr)
{
struct sockaddr_in addr;

	if ((ctx->socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		eprintf("socket() error\n");
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port_nr);
	addr.sin_addr.s_addr = inet_addr(host);

	if (connect(ctx->socket, & addr, sizeof addr))
	{
		close(ctx->socket);
		eprintf("connect() error\n");
		return -1;
	}
	send_char(ctx, '+');
	txsync(ctx);
	return 0;
}

/*!
 *	\fn	int libgdb_readwords(struct libgdb_ctx * ctx, uint32_t addr, int wordcnt, uint32_t * buf)
 *	\brief	reads words from a target controlled by a connected gdb server
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	addr	target address to read from
 *	\param	wordcnt	number of words to read
 *	\param	buf	buffer where to store the memory read
 *	\return	0 on success, -1 if an error occurs */
int libgdb_readwords(struct libgdb_ctx * ctx, uint32_t addr, int wordcnt, uint32_t * buf)
{
int maxwords, x;
int total, cur;

	/* see how many words can be transferred in one run */
	if (ctx->mem_access_max_nr_words == 0)
	{
		maxwords = get_max_mem_xfer_words(ctx);
		if (maxwords <= 0)
			/* ??? */
			return -1;
	}
	else
		maxwords = ctx->mem_access_max_nr_words;
	cur = 0;
	total = wordcnt * sizeof(uint32_t);
	while (wordcnt)
	{
		x = (maxwords > wordcnt) ? wordcnt : maxwords;
		snprintf(ctx->txpacket, sizeof ctx->txpacket, "m%x,%x", addr, x * sizeof(uint32_t));
		putpacket(ctx, true);
		if (getpacket(ctx, true))
		{
			eprintf("%s(): error getting packet\n", __func__);
			return -1;
		}
		if (is_error_packet(ctx))
		{
			eprintf("%s(): error reading target memory\n", __func__);
			return -1;
		}
		hex_to_mem((char *) buf, ctx->rxpacket, x * sizeof(uint32_t));

		addr += x * sizeof(uint32_t);
		buf += x;
		wordcnt -= x;
		cur += x * sizeof(uint32_t);
		if (ctx->is_annotation_enabled)
		{
			printf("[VX-MEM-READ-PROGRESS]\t%i\t%i\n", cur, total);
			fflush(stdout);
		}
	}
	return 0;
}

/*!
 *	\fn	int libgdb_writewords(struct libgdb_ctx * ctx, uint32_t addr, int wordcnt, uint32_t * buf)
 *	\brief	writes words to a target controlled by a connected gdb server
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	addr	target address to write to
 *	\param	wordcnt	number of words to write
 *	\param	buf	buffer containing the data to be written
 *	\return	0 on success, -1 if an error occurs */
int libgdb_writewords(struct libgdb_ctx * ctx, uint32_t addr, int wordcnt, uint32_t * buf)
{
int maxwords, x, i;
int total, cur;

	/* see how many words can be transferred in one run */
	if (ctx->mem_access_max_nr_words == 0)
	{
		maxwords = get_max_mem_xfer_words(ctx);
		if (maxwords <= 0)
			/* ??? */
			return -1;
	}
	else
		maxwords = ctx->mem_access_max_nr_words;
	cur = 0;
	total = wordcnt * sizeof(uint32_t);
	while (wordcnt)
	{
		x = (maxwords > wordcnt) ? wordcnt : maxwords;
		i = snprintf(ctx->txpacket, sizeof ctx->txpacket, "M%x,%x:", addr, x * sizeof(uint32_t));
		mem_to_hex(ctx->txpacket + i, (char *) buf, x * sizeof(uint32_t));
		putpacket(ctx, true);
		if (getpacket(ctx, true))
		{
			eprintf("%s(): error getting packet\n", __func__);
			return -1;
		}
		if (is_error_packet(ctx) != 1)
		{
			eprintf("%s(): error writing target memory\n", __func__);
			return -1;
		}

		addr += x * sizeof(uint32_t);
		buf += x;
		wordcnt -= x;
		cur += x * sizeof(uint32_t);
		if (ctx->is_annotation_enabled)
		{
			printf("[VX-MEM-WRITE-PROGRESS]\t%i\t%i\n", cur, total);
			fflush(stdout);
		}
	}
	return 0;
}

/*!
 *	\fn	int libgdb_readreg(struct libgdb_ctx * ctx, int reg_nr, uint32_t * reg)
 *	\brief	reads a target register
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	reg_nr	the number of the register to read
 *	\param	reg	a pointer to where to store the register value retrieved
 *	\return	0 on success, -1 if an error occurs */
int libgdb_readreg(struct libgdb_ctx * ctx, int reg_nr, uint32_t * reg)
{

	snprintf(ctx->txpacket, sizeof ctx->txpacket, "p%x", reg_nr);
	putpacket(ctx, true);
	if (getpacket(ctx, true))
	{
		eprintf("%s(): error getting packet\n", __func__);
		return -1;
	}
	if (is_error_packet(ctx))
	{
		eprintf("%s(): error reading target register %i\n", __func__, reg_nr);
		return -1;
	}
	hex_to_mem((char *) reg, ctx->rxpacket, 1 * sizeof(uint32_t));

	return 0;
}

/*!
 *	\fn	int libgdb_writereg(struct libgdb_ctx * ctx, int reg_nr, uint32_t reg_val)
 *	\brief	writes a target register
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	reg_nr	the number of the register to write
 *	\param	reg_val	the register value to write
 *	\return	0 on success, -1 if an error occurs */
int libgdb_writereg(struct libgdb_ctx * ctx, int reg_nr, uint32_t reg_val)
{
	/* the register value must be printed in target endian order,
	 * assume low-endian here */

	reg_val = (reg_val >> 16) | (reg_val << 16);
	reg_val = ((reg_val >> 8) & 0x00ff00ff) | ((reg_val << 8) & 0xff00ff00);

	snprintf(ctx->txpacket, sizeof ctx->txpacket, "P%x=%08x", reg_nr, reg_val);
	putpacket(ctx, true);
	if (getpacket(ctx, true))
	{
		eprintf("%s(): error getting packet\n", __func__);
		return -1;
	}
	if (is_error_packet(ctx) != 1)
	{
		eprintf("%s(): error writing target register %i\n", __func__, reg_nr);
		return -1;
	}

	return 0;
}


/*!
 *	\fn	int libgdb_insert_hw_bkpt(struct libgdb_ctx * ctx, uint32_t addr, int len)
 *	\brief	inserts a hardware breakpoint at a given address
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	addr	the address at which to insert the hardware breakpoint
 *	\param	len	the length of the breakpoint, in bytes
 *	\return	0 on success, -1 if an error occurs */
int libgdb_insert_hw_bkpt(struct libgdb_ctx * ctx, uint32_t addr, int len)
{
	snprintf(ctx->txpacket, sizeof ctx->txpacket, "Z1,%x,%x", addr, len);
	putpacket(ctx, true);
	if (getpacket(ctx, true))
	{
		eprintf("%s(): error getting packet\n", __func__);
		return -1;
	}
	if (is_error_packet(ctx) != 1)
	{
		eprintf("%s(): error setting breakpoint at address 0x%08x\n", __func__, addr);
		return -1;
	}

	return 0;
}

/*!
 *	\fn	int libgdb_remove_hw_bkpt(struct libgdb_ctx * ctx, uint32_t addr, int len)
 *	\brief	removes a hardware breakpoint at a given address
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	addr	the address from which to remove the hardware breakpoint
 *	\param	len	the length of the breakpoint, in bytes
 *	\return	0 on success, -1 if an error occurs */
int libgdb_remove_hw_bkpt(struct libgdb_ctx * ctx, uint32_t addr, int len)
{
	snprintf(ctx->txpacket, sizeof ctx->txpacket, "z1,%x,%x", addr, len);
	putpacket(ctx, true);
	if (getpacket(ctx, true))
	{
		eprintf("%s(): error getting packet\n", __func__);
		return -1;
	}
	if (is_error_packet(ctx) != 1)
	{
		eprintf("%s(): error removing breakpoint at address 0x%08x\n", __func__, addr);
		return -1;
	}

	return 0;
}

/*!
 *	\fn	void libgdb_sendpacket(struct libgdb_ctx * ctx, const char * packet_data)
 *	\brief	sends a packet to a connected gdbserver
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	packet_data	a null-terminated string that contains
 *				the packet payload
 *	\return	none */
void libgdb_sendpacket(struct libgdb_ctx * ctx, const char * packet_data)
{
	strncpy(ctx->txpacket, packet_data, sizeof ctx->txpacket - 1);
	ctx->txpacket[MAX_PACKET_LEN - 1] = 0;
	putpacket(ctx, true);
}

/*!
 *	\fn	void libgdb_sendpacketraw(struct libgdb_ctx * ctx, const char * packet_data)
 *	\brief	sends a packet to a connected gdbserver without waiting for confirmation
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	packet_data	a null-terminated string that contains
 *				the packet payload
 *	\return	none */
void libgdb_sendpacketraw(struct libgdb_ctx * ctx, const char * packet_data)
{
	strncpy(ctx->txpacket, packet_data, sizeof ctx->txpacket - 1);
	ctx->txpacket[MAX_PACKET_LEN - 1] = 0;
	putpacket(ctx, false);
}

/*!
 *	\fn	void libgdb_sendbreak(struct libgdb_ctx * ctx)
 *	\brief	sends a break character (ascii ETX - 03) to a target
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	none */
void libgdb_sendbreak(struct libgdb_ctx * ctx)
{
	send_char(ctx, GDB_BREAK_CHAR);
	txsync(ctx);
}

/*!
 *	\fn	const char * libgdb_async_get_packet(struct libgdb_ctx * ctx, char incoming_char)
 *	\brief	receive a packet from a remote gdbserver, asynchronously
 *
 *	the purpose of this routine is to handle asynchronous packet
 *	reception from a remote gdbserver; it does not directly read
 *	incoming characters from the target; this routine
 *	should *not* be used when a reply to some request packet
 *	from the target is expected; this routine should be used
 *	when expecting an asynchronous
 *	packet from the target; examples of such packets
 *	are console output ('O') packets, and stop-reply ('S' and 'T') packets;
 *	when an asynchronous packet is expected, this routine should be invoked
 *	whenever an incoming character is available; this character
 *	would have been obtained by code outside of this ('libgdb'), and
 *	whenever such a character has been obtained, this routine
 *	should be invoked (with the received character as a parameter -
 *	the 'incoming_char' parameter) in order to determine if a whole
 *	packet has arrived or not
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	incoming_char	the incoming character from the remote
 *				gdbserver
 *	\return	a pointer to the packet received; if null, a whole packet
 *			is not yet available */
const char * libgdb_async_get_packet(struct libgdb_ctx * ctx, char incoming_char)
{
	switch (ctx->state)
	{
		case ASYNC_RX_STATE_WAITING_START:
			if (incoming_char == '$')
			{
				ctx->state = ASYNC_RX_STATE_READING_DATA;
				ctx->idx = 0;
				ctx->cksum = 0;
			}
			break;
		case ASYNC_RX_STATE_READING_DATA:
			if (incoming_char == '#')
			{
				/* null terminate the packet received */
				ctx->async_rxpacket[ctx->idx] = '\0';
				ctx->state = ASYNC_RX_STATE_WAITING_FIRST_CKSUM_CHAR;
			}
			else
			{
				if (ctx->idx == sizeof ctx->async_rxpacket - /* reserve one byte for a null terminator */ 1)
				{
					/* incoming buffer overflow - abort current packet and start looking for next one */
					ctx->state = ASYNC_RX_STATE_WAITING_START;
					break;
				}
				ctx->async_rxpacket[ctx->idx ++] = incoming_char;
				ctx->cksum += incoming_char;
			}
			break;
		case ASYNC_RX_STATE_WAITING_FIRST_CKSUM_CHAR:
			ctx->rx_cksum = hex(incoming_char) << 4;
			ctx->state = ASYNC_RX_STATE_WAITING_SECOND_CKSUM_CHAR;
			break;
		case ASYNC_RX_STATE_WAITING_SECOND_CKSUM_CHAR:
			ctx->rx_cksum |= hex(incoming_char);
			ctx->state = ASYNC_RX_STATE_WAITING_START;
			if (ctx->cksum == ctx->rx_cksum)
				return ctx->async_rxpacket;
			break;
	}
	return 0;
}

/*!
 *	\fn	int libgdb_get_gdbserver_socket_desc(struct libgdb_ctx * ctx)
 *	\brief	retrieves the file descriptor of the socket that libgdb is using to communicate with the remote gdbserver
 *
 *	this routine retrieves the file descriptor that the libgd library is currently
 *	using to communicate with a remote gdbserver; this is mainly
 *	useful (and necessary) when waiting for asynchronous packets
 *	from the remote gdbserver (e.g. polling the socket file descriptor
 *	for incoming data)
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	the file descriptor of the socket that libgdb is currently
 *		using to communicate with the remote gdbserver */
int libgdb_get_gdbserver_socket_desc(struct libgdb_ctx * ctx)
{
	return ctx->socket;
}

/*!
 *	\fn	void libgdb_waithalted(struct libgdb_ctx * ctx)
 *	\brief	waits for the target to halt by expecting a gdbserver stop packet
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	none */
void libgdb_waithalted(struct libgdb_ctx * ctx)
{
	do
	{
		getpacket(ctx, false);
	}
	while (ctx->rxpacket[0] != 'S' && ctx->rxpacket[0] != 'T');
}

/*!
 *	\fn	int libgdb_armv7m_run_target_routine(struct libgdb_ctx * ctx, uint32_t routine_entry_point, uint32_t stack_ptr, uint32_t halt_addr, uint32_t * halt_addr, uint32_t param0, uint32_t param1, uint32_t param2, uint32_t param3)
 *	\brief	runs a routine on a connected target and wait for the target to halt
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	routine_entry_point	the routine entry point address
 *	\param	stack_ptr	stack pointer value to set for the routine
 *				being run
 *	\param	halt_addr	expected halt address; a hardware breakpoint
 *				will be inserted at this address, then
 *				the routine to be run will have its
 *				parameters set to the param0 .. param3
 *				values passed, the target will then be run,
 *				and this function will wait for the target
 *				to halt; when the target halts, the hardware
 *				breakpoint will be removed, and the result
 *				returned by the executed routine will be stored
 *				in the 'result' parameter passed
 *	\param	result	a pointer to where to store the result that the executed
 *			routine returns; can be null if this value is of no interest
 *	\param	param0	value for the first parameter to pass to the function
 *	\param	param1	value for the second parameter to pass to the function
 *	\param	param2	value for the third parameter to pass to the function
 *	\param	param3	value for the fourth parameter to pass to the function
 *	\return	0 on success, -1 if an error occurs
 *
 *	\note	the target must be halted prior to invoking this routine */
int libgdb_armv7m_run_target_routine(struct libgdb_ctx * ctx, uint32_t routine_entry_point, uint32_t stack_ptr,
		uint32_t halt_addr, uint32_t * result, uint32_t param0, uint32_t param1, uint32_t param2, uint32_t param3)
{
uint32_t reg;
void dump_target_regfile(void)
{
uint32_t x;
int i;

	if (libgdb_readreg(ctx, 15, &reg))
	{
		eprintf("error reading register %i\n", i);
		exit(2);
	}

	printf("pc: 0x%08x\n", reg);
	if (reg >= 0x20000010 && reg <= 0x20000062)
		return;
	for (i = 0; i < 16; ((++ i) & 3) ? 0 : printf("\n"))
	{
		if (libgdb_readreg(ctx, i, &reg))
		{
			eprintf("error reading register %i\n", i);
			exit(2);
		}
		printf("r%i: 0x%08x, ", i, reg);
	}
	/* read xpsr */
	if (libgdb_readreg(ctx, 16, &reg))
	{
		eprintf("error reading xpsr\n");
		exit(2);
	}
	printf("xpsr: 0x%08x, ", reg);
	/* read main stack pointer (msp) */
	if (libgdb_readreg(ctx, 17, &reg))
	{
		eprintf("error reading msp\n");
		exit(2);
	}
	printf("msp: 0x%08x, ", reg);
	/* read process stack pointer (psp) */
	if (libgdb_readreg(ctx, 18, &reg))
	{
		eprintf("error reading psp\n");
		exit(2);
	}
	printf("psp: 0x%08x\n", reg);
	/* read control, primask, faultmask, basepri */
	/*! \warn	!!! WARNING !!! ; UGLY SPECIAL CASE !!! ; SYNC WITH VX FIRMWARE !!! 19, NOT 20 !!! */
	if (libgdb_readreg(ctx, 19, &reg))
	{
		eprintf("error reading register 20\n");
		exit(2);
	}
	printf("control: 0x%08x, ", x = (reg >> 24));
	printf("thread mode has %s access; ", (x & 1) ? "UNPRIVILEDGED" : "priviledged");
	printf("current stack in use is %s; ", (x & 2) ? "THREAD" : "main");
	printf("floating point extensions are currently %s; ", (x & 4) ? "ACTIVE" : "inactive");
	printf("faultmask: 0x%08x, ", (reg >> 16) & 255);
	printf("basepri: 0x%08x, ", (reg >> 8) & 255);
	printf("primask: 0x%08x, ", (reg >> 0) & 255);
	printf("\n");
}

	/* read the target xpsr register and inspect the 'thumb' bit; armv7m cores cannot
	 * run if this bit is cleared (as they support thumb execution only); if this bit
	 * is cleared (possible if the target has executed some invalid code and is currently
	 * in a faulty state) */
	if (libgdb_readreg(ctx, 25, &reg))
	{
		eprintf("error reading register xpsr, aborting\n");
		exit(2);
	}
	if (!(reg & (1 << 24)))
	{
		printf("warning: thumb execution bit is currently detected as 'disabled'; will try to enable thumb execution...\n");

		/* enable thumb bit */
		reg |= 1 << 24;

		if (libgdb_writereg(ctx, 1, 0xa5))
		{
			eprintf("error writing register 1, aborting\n");
			exit(2);
		}
		//if (libgdb_writereg(ctx, 16, reg))
		if (libgdb_writereg(ctx, 25, reg))
		{
			eprintf("error writing register xpsr, aborting\n");
			exit(2);
		}
		//if (libgdb_readreg(ctx, 16, &reg))
		if (libgdb_readreg(ctx, 25, &reg))
		{
			eprintf("error reading register xpsr, aborting\n");
			exit(2);
		}
		if (!(reg & (1 << 24)))
		{
			eprintf("FAILED TO ENTER THUMB, ABORTING\n");
			exit(2);
		}
		else
			printf("thumb mode successfully reentered...\n");
	}

	/* insert a hardware breakpoint at the expected return address */
	if (libgdb_insert_hw_bkpt(ctx, halt_addr, 2))
		return -1;
	/* write the program counter */
	if (libgdb_writereg(ctx, 15, routine_entry_point /* set thumb execution bit */ | 1))
		return -1;
	/* write the stack pointer */
	if (libgdb_writereg(ctx, 13, stack_ptr))
		return -1;
	/* write the return address (link) register */
	if (libgdb_writereg(ctx, 14, halt_addr /* set thumb execution bit */ | 1))
		return -1;
	/* write param0 */
	if (libgdb_writereg(ctx, 0, param0))
		return -1;
	/* write param1 */
	if (libgdb_writereg(ctx, 1, param1))
		return -1;
	/* write param2 */
	if (libgdb_writereg(ctx, 2, param2))
		return -1;
	/* write param3 */
	if (libgdb_writereg(ctx, 3, param3))
		return -1;

	/* useful for debugging pieces of machine code run on the target */
	while (0)
	{
		int i;
		uint32_t x;
		dump_target_regfile();
		libgdb_sendpacket(ctx, "s");
		libgdb_waithalted(ctx);
	}

	/* request target run */
	libgdb_sendpacket(ctx, "c");
	/* wait for the target to halt */
	libgdb_waithalted(ctx);
	/* remove the hardware breakpoint */
	if (libgdb_remove_hw_bkpt(ctx, halt_addr, 2));
	if (result)
	{
		/* if the result (if any) returned by the routine just
		 * executed on the target is of interest - retrieve it */
		if (libgdb_readreg(ctx, 0, result))
			return -1;
	}
	return 0;
}

/*!
 *	\fn	bool libgdb_set_annotation(struct libgdb_ctx * ctx, bool enable_annotation)
 *	\brief	enables/disables libgdb output annotation
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	enable_annotation	the new value of the annotation
 *					enable flag; if true, annotation
 *					is being enabled, otherwise it
 *					is being disabled
 *	\return	the previous value of the annotation flag */
bool libgdb_set_annotation(struct libgdb_ctx * ctx, bool enable_annotation)
{
bool b;
	b = ctx->is_annotation_enabled;
	ctx->is_annotation_enabled = enable_annotation;
	return b;
}

