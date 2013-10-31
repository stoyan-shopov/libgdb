/*

Copyright (C) 2012 stoyan shopov

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

#include "libgdb.h"

/*! constant definitions */
enum
{
	/*! invalid/nonpresent memory area */
	MEM_TYPE_INVALID	= 0,
	/*! ram memory area */
	MEM_TYPE_RAM	= 1,
	/*! flash memory area */
	MEM_TYPE_FLASH	= 2,
};

/*! memory area description data structure */
struct struct_memarea
{
	/*! memory area start */
	uint32_t	start;
	/*! memory area length, in bytes */
	uint32_t	len;
	/*! for flash areas - a list of the sizes of the flash sectors
	 *
	 * the list is terminated with a zero-size entry */
	uint32_t	* sizes;
};


/*! a data structure facilitating the automation of passing target specific command line options to target access routines
 *
 * the main() routine shall attempt to parse unknown command line options
 * of the kind "option=parameter" according to the command line parameter
 * strings and types specified in this structure
 */
struct cmdline_option_info
{
	/*! a description of the comand line option and its arguments */
	const char * description;
	/*! comand line option string, e.g. "--target-xtal-freq-hz" */
	const char * cmdstr;
	/*! parameter type */
	enum
	{
		/*! invalid */
		PARAM_TYPE_INVALID = 0,
		/*! numeric parameter type
		 *
		 * the main() routine shall attempt
		 * to parse a numeric argument and if
		 * successful will put its value in the
		 * 'num' field below */
		PARAM_TYPE_NUMERIC,
		/*! string parameter type
		 *
		 * the main() routine shall put a pointer
		 * to the string argument in the 'str'
		 * field below */
		PARAM_TYPE_STRING,
	}
	type;
	/*! if true, then specifying the value for this parameter is mandatory, otherwise it may be left omitted */
	bool	is_mandatory;
	/*! if true, then this parameter has been specified and holds a parsed value */
	bool	is_specified;
	/*! a union holding the command line option argument, depending on the value of the 'type' field above */
	union
	{
		/*! see the description for 'PARAM_TYPE_NUMERIC' above */
		uint32_t num;
		/*! see the description for 'PARAM_TYPE_STRING' above */
		char * str;
	};
};

/*! device control data structure
 *
 * this roughly describes a target processor device and
 * contains things like core name, ram/flash memory areas,
 * function pointers to flash access routines, etc. */
struct struct_devctl
{
	/*! a link to the next node in a linked list of devices */
	struct struct_devctl	* next;
	/*! a string identifying this core */
	const char * name;
	/*! a pointer to a table describing any target specific command line options
	 *
	 * see the description for 'struct cmdline_option_info' above for details;
	 * if present (non-null) this table must be terminated with an entry
	 * having a null-pointer in its 'cmdstr' field */
	struct cmdline_option_info * cmdline_options;
	/*! a pointer to a table describing the target ram memory areas
	 *
	 * this is an array terminated by an entry of zero length */
	const struct struct_memarea * ram_areas;
	/*! a pointer to a table describing the target flash memory areas
	 *
	 * this is an array terminated by an entry of zero length */
	const struct struct_memarea * flash_areas;
	/*! a pointer to a function for performing device initialization, if any
	 *
	 * this can be null if no such routine is available
	 *
	 * normally - this is the place to perform any device
	 * specific setup (such as clock setup, etc.)
	 *
	 * this function returns zero on success, nonzero on error */
	int (* dev_open)(struct struct_devctl * dev, struct libgdb_ctx * ctx);
	/*! a pointer to a function for performing device deinitialization/shutdown, if any
	 *
	 * this can be null if no such routine is available
	 *
	 * this function returns zero on success, nonzero on error */
	int (* dev_close)(struct struct_devctl * dev, struct libgdb_ctx * ctx);
	/*! a pointer to a function for unlocking the target flash
	 *
	 * this can be null if no such routine is available; in this
	 * case, it is assumed that target flash does not need any
	 * special handling in order to unlock it for erasing and
	 * programming
	 *
	 * this function returns zero on success, nonzero on error */
	int (* flash_unlock_area)(struct struct_devctl * dev, struct libgdb_ctx * ctx, const struct struct_memarea * area);
	/*! a pointer to a function for erasing target flash area
	 *
	 * this can be null if no such routine is available
	 *
	 * this function returns zero on success, nonzero on error */
	int (* flash_erase_area)(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t start_addr, uint32_t len);
	/*! a pointer to a function for erasing a target flash sector number (sector numbering starts from zero(0))
	 *
	 * this can be null if no such routine is available
	 *
	 * this function returns zero on success, nonzero on error */
	int (* flash_erase_sector)(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t sector_nr);
	/*! a pointer to a function for mass erasing a target device
	 *
	 * this can be null if no such routine is available
	 *
	 * this function returns zero on success, nonzero on error */
	int (* flash_mass_erase)(struct struct_devctl * dev, struct libgdb_ctx * ctx);
	/*! a pointer to a function for writing 32 bit words to target flash
	 *
	 * this can be null if no such routine is available
	 *
	 * this function returns zero on success, nonzero on error */
	int (* flash_program_words)(struct struct_devctl * dev, struct libgdb_ctx * ctx, uint32_t dest, uint32_t * src, int wordcnt);
	/*! a pointer to a function for validating command line options for the specific target
	 *
	 * this can be null if no such routine is available
	 *
	 * this function returns zero on success, nonzero on error */
	int (* validate_cmdline_options)(struct struct_devctl * dev, struct libgdb_ctx * ctx);
	/*! a generic pointer field available for general-purpose use */
	void	* pdev;
};


/*! a sample data structure that can be used as the 'pdev' member in the 'struct_devctl' above
 *
 * this is just an example, this can be redefined by flash loaders
 * for their specific needs; often, however, the information put
 * in here may be sufficient for a flash loader
 *
 * a common use of this is to put the target residing flash code
 * at the start of targer ram memory (i.e. to set the 'code_load_addr'
 * field below equal to the beginning of target ram), then set the
 * 'write_buf_addr' field below to some address in target ram after
 * the target residing flash code, then set the 'write_buf_size' field
 * below to an appropriate value (this depends on available target
 * ram memory, generally the larger this - the better, as flash
 * writing will generally be faster than using smaller buffer sizes),
 * then set the 'stack_size'field below to an appropriate value -
 * then the stack base address for the target flash code can be
 * set to the end of the write buffer + the desired stack size
 * (field 'stack_size'); generally, it is best to reserve an
 * adequate amount of memory for the stack, and use all remaining
 * available memory for the write buffer */
struct pdev
{
	/*! base address in the target for loading flash-writing specific code */
	uint32_t	code_load_addr;
	/*! the base address of the write buffer in the target used for programming target flash */
	uint32_t	write_buf_addr;
	/*! the size of the write buffer described above (write_buf_addr), in bytes */
	uint32_t	write_buf_size;
	/*! the amount of memory to use for the target stack, in bytes */
	uint32_t	stack_size;
};
