/*

Copyright (C) 2011 stoyan shopov

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

#include <stdbool.h>


/*! error reporting macro */
#define eprintf(format, ...) fprintf (stderr, "file %s, line %i, in function %s(): ", __FILE__, __LINE__, __func__), fprintf (stderr, format, ##__VA_ARGS__)

/*! opaque libgdb context data structure */
struct libgdb_ctx;

/*!
 *	\fn	void libgdb_send_ack(struct libgdb_ctx * ctx)
 *	\brief	sends an acknowledge (the '+') chaacter to a connected gdbserver
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	none */
void libgdb_send_ack(struct libgdb_ctx * ctx);

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
int libgdb_set_max_nr_words_xferred(struct libgdb_ctx * ctx, int maxwords);

/*!
 *	\fn	struct libgdb_ctx * libgdb_init(void)
 *	\brief	initializes the libgdb library
 *
 *	\param	none
 *	\return	a pointer to a library internal context data structure
 *		that is to be passed to the library functions on
 *		subsequent use; null pointer is returned in case of
 *		some error */
struct libgdb_ctx * libgdb_init(void);

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
int libgdb_connect(struct libgdb_ctx * ctx, const char * host, int port_nr);

/*!
 *	\fn	int libgdb_readwords(struct libgdb_ctx * ctx, uint32_t addr, int wordcnt, uint32_t * buf)
 *	\brief	reads words from a target controlled by a connected gdb server
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	addr	target address to read from
 *	\param	wordcnt	number of words to read
 *	\param	buf	buffer where to store the memory read
 *	\return	0 on success, -1 if an error occurs */
int libgdb_readwords(struct libgdb_ctx * ctx, uint32_t addr, int wordcnt, uint32_t * buf);

/*!
 *	\fn	int libgdb_writewords(struct libgdb_ctx * ctx, uint32_t addr, int wordcnt, uint32_t * buf)
 *	\brief	writes words to a target controlled by a connected gdb server
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	addr	target address to write to
 *	\param	wordcnt	number of words to write
 *	\param	buf	buffer containing the data to be written
 *	\return	0 on success, -1 if an error occurs */
int libgdb_writewords(struct libgdb_ctx * ctx, uint32_t addr, int wordcnt, uint32_t * buf);

/*!
 *	\fn	int libgdb_readreg(struct libgdb_ctx * ctx, int reg_nr, uint32_t * reg)
 *	\brief	reads a target register
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	reg_nr	the number of the register to read
 *	\param	reg	a pointer to where to store the register value retrieved
 *	\return	0 on success, -1 if an error occurs */
int libgdb_readreg(struct libgdb_ctx * ctx, int reg_nr, uint32_t * reg);

/*!
 *	\fn	int libgdb_writereg(struct libgdb_ctx * ctx, int reg_nr, uint32_t reg_val)
 *	\brief	writes a target register
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	reg_nr	the number of the register to write
 *	\param	reg_val	the register value to write
 *	\return	0 on success, -1 if an error occurs */
int libgdb_writereg(struct libgdb_ctx * ctx, int reg_nr, uint32_t reg_val);

/*!
 *	\fn	int libgdb_insert_hw_bkpt(struct libgdb_ctx * ctx, uint32_t addr, int len)
 *	\brief	inserts a hardware breakpoint at a given address
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	addr	the address at which to insert the hardware breakpoint
 *	\param	len	the length of the breakpoint, in bytes
 *	\return	0 on success, -1 if an error occurs */
int libgdb_insert_hw_bkpt(struct libgdb_ctx * ctx, uint32_t addr, int len);

/*!
 *	\fn	int libgdb_remove_hw_bkpt(struct libgdb_ctx * ctx, uint32_t addr, int len)
 *	\brief	removes a hardware breakpoint at a given address
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	addr	the address from which to remove the hardware breakpoint
 *	\param	len	the length of the breakpoint, in bytes
 *	\return	0 on success, -1 if an error occurs */
int libgdb_remove_hw_bkpt(struct libgdb_ctx * ctx, uint32_t addr, int len);

/*!
 *	\fn	void libgdb_sendpacket(struct libgdb_ctx * ctx, const char * packet_data)
 *	\brief	sends a packet to a connected gdbserver
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	packet_data	a null-terminated string that contains
 *				the packet payload
 *	\return	none */
void libgdb_sendpacket(struct libgdb_ctx * ctx, const char * packet_data);

/*!
 *	\fn	void libgdb_sendpacketraw(struct libgdb_ctx * ctx, const char * packet_data)
 *	\brief	sends a packet to a connected gdbserver without waiting for confirmation
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\param	packet_data	a null-terminated string that contains
 *				the packet payload
 *	\return	none */
void libgdb_sendpacketraw(struct libgdb_ctx * ctx, const char * packet_data);

/*!
 *	\fn	void libgdb_sendbreak(struct libgdb_ctx * ctx)
 *	\brief	sends a break character (ascii ETX - 03) to a target
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	none */
void libgdb_sendbreak(struct libgdb_ctx * ctx);

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
 *	are console output ('O') packets, and stop-reply ('S') packets;
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
const char * libgdb_async_get_packet(struct libgdb_ctx * ctx, char incoming_char);

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
int libgdb_get_gdbserver_socket_desc(struct libgdb_ctx * ctx);

/*!
 *	\fn	void libgdb_waithalted(struct libgdb_ctx * ctx)
 *	\brief	waits for the target to halt by expecting a gdbserver stop packet
 *
 *	\param	ctx	libgdb library context as returned by libgdb_init
 *	\return	none */
void libgdb_waithalted(struct libgdb_ctx * ctx);

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
		uint32_t halt_addr, uint32_t * result, uint32_t param0, uint32_t param1, uint32_t param2, uint32_t param3);

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
bool libgdb_set_annotation(struct libgdb_ctx * ctx, bool enable_annotation);
