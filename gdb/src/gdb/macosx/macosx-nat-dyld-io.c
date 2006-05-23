/* Mac OS X support for GDB, the GNU debugger.
   Copyright 1997, 1998, 1999, 2000, 2001, 2002
   Free Software Foundation, Inc.

   Contributed by Apple Computer, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "inferior.h"
#include "symfile.h"
#include "symtab.h"
#include "gdbcmd.h"
#include "objfiles.h"
#include "target.h"
#include "exceptions.h"
#include "gdbcore.h"

#include <string.h>
#include <sys/stat.h>

#include <mach-o/nlist.h>
#include <mach-o/loader.h>
#include <mach-o/dyld_debug.h>

#include "libbfd.h"

#include "mach-o.h"

#include "macosx-nat-inferior.h"
#include "macosx-nat-mutils.h"
#include "macosx-nat-dyld-info.h"

struct inferior_info
{
  bfd_vma addr;
  bfd_vma offset;
  bfd_vma len;
  void *read;
};

static void *
inferior_open (bfd *abfd, void *open_closure)
{
  struct inferior_info *in = NULL;
  struct inferior_info *ret = NULL;

  in = (struct inferior_info *) open_closure;
  ret = bfd_zalloc (abfd, sizeof (struct inferior_info));

  *ret = *in;

  return ret;
}

static file_ptr
inferior_read_generic (bfd *abfd, void *stream, void *data, file_ptr nbytes, file_ptr offset)
{
  struct inferior_info *iptr = (struct inferior_info *) stream;
  volatile struct gdb_exception except;

  CHECK_FATAL (iptr != NULL);

  /* The target may be "macos-child" in the case of a regular process,
     or it may be "remote" in the case of a translated (Rosetta)
     application where we're controlling it via remote protocol.  */

  if (strcmp (current_target.to_shortname, "macos-child") != 0
      && strcmp (current_target.to_shortname, "remote") != 0)
    {
      bfd_set_error (bfd_error_no_contents);
      return 0;
    }

  if (offset > iptr->len)
    {
      bfd_set_error (bfd_error_no_contents);
      return 0;
    }

  TRY_CATCH (except, RETURN_MASK_ERROR)
    {
      read_memory (iptr->addr + offset, data, nbytes);
    }

  if (except.reason < 0)
    {
      bfd_set_error (bfd_error_system_call);
      return 0;
    }

  return nbytes;
}

static file_ptr
inferior_read_mach_o (bfd *abfd, void *stream, void *data, file_ptr nbytes, file_ptr offset)
{
  volatile struct gdb_exception except;
  struct inferior_info *iptr = (struct inferior_info *) stream;
  unsigned int i;
  int ret;

  CHECK_FATAL (iptr != NULL);

  /* The target may be "macos-child" in the case of a regular process,
     or it may be "remote" in the case of a translated (Rosetta)
     application where we're controlling it via remote protocol.  */

  if (strcmp (current_target.to_shortname, "macos-child") != 0
      && strcmp (current_target.to_shortname, "remote") != 0)
    {
      bfd_set_error (bfd_error_no_contents);
      return 0;
    }

  if ((strcmp (bfd_get_target (abfd), "mach-o-be") != 0)
      && (strcmp (bfd_get_target (abfd), "mach-o-le") != 0)
      && (strcmp (bfd_get_target (abfd), "mach-o") != 0))
    {
      bfd_set_error (bfd_error_invalid_target);
      return 0;
    }

  {
    struct mach_o_data_struct *mdata = NULL;
    CHECK_FATAL (bfd_mach_o_valid (abfd));
    mdata = abfd->tdata.mach_o_data;
    for (i = 0; i < mdata->header.ncmds; i++)
      {
        struct bfd_mach_o_load_command *cmd = &mdata->commands[i];
        if (cmd->type == BFD_MACH_O_LC_SEGMENT
	    || cmd->type == BFD_MACH_O_LC_SEGMENT_64)
          {
            struct bfd_mach_o_segment_command *segment =
              &cmd->command.segment;
            if ((offset  >= segment->fileoff)
                && (offset < (segment->fileoff + segment->filesize)))
              {
                bfd_vma infaddr =
                  (segment->vmaddr + iptr->offset +
                   (offset - segment->fileoff));
		
		TRY_CATCH (except, RETURN_MASK_ERROR)
		  {
		    read_memory (infaddr, data, nbytes);
		  }

                if (ret <= 0)
                  {
                    bfd_set_error (bfd_error_system_call);
                    return 0;
                  }
                return ret;
              }
          }
      }
  }

  bfd_set_error (bfd_error_no_contents);
  return 0;
}

static file_ptr
inferior_read (bfd *abfd, void *stream, void *data, file_ptr nbytes, file_ptr offset)
{
  if ((strcmp (bfd_get_target (abfd), "mach-o-be") == 0)
      || (strcmp (bfd_get_target (abfd), "mach-o-le") == 0)
      || (strcmp (bfd_get_target (abfd), "mach-o") == 0))
    {
      if (bfd_mach_o_valid (abfd))
	return inferior_read_mach_o (abfd, stream, data, nbytes, offset);
    }

  return inferior_read_generic (abfd, stream, data, nbytes, offset);
}

static int
inferior_close (bfd *abfd, void *stream)
{
  return 0;
}

static bfd *
inferior_bfd_generic (const char *name, CORE_ADDR addr, CORE_ADDR offset, 
                      CORE_ADDR len)
{
  struct inferior_info info;
  char *filename = NULL;
  bfd *abfd = NULL;

  info.addr = addr;
  info.offset = offset;
  info.len = len;
  info.read = 0;

  if (name != NULL)
    {
      xasprintf (&filename, "[memory object \"%s\" at 0x%s for 0x%s]",
                 name, paddr_nz (addr), paddr_nz (len));
    }
  else
    {
      xasprintf (&filename, "[memory object at 0x%s for 0x%s]",
                 paddr_nz (addr), paddr_nz (len));
    }

  if (filename == NULL)
    {
      warning ("unable to allocate memory for filename for \"%s\"", name);
      return NULL;
    }

  abfd = bfd_openr_iovec (filename, NULL, 
			 inferior_open, &info,
			 inferior_read, inferior_close);
  if (abfd == NULL)
    {
      warning ("Unable to open memory image for \"%s\"; skipping", name);
      return NULL;
    }

  /* FIXME: should check for errors from bfd_close (for one thing, on
     error it does not free all the storage associated with the bfd).  */

  if (!bfd_check_format (abfd, bfd_object))
    {
      warning ("Unable to read symbols from %s: %s.", bfd_get_filename (abfd),
               bfd_errmsg (bfd_get_error ()));
      bfd_close (abfd);
      return NULL;
    }

  return abfd;
}

bfd *
inferior_bfd (const char *name, CORE_ADDR addr, CORE_ADDR offset, CORE_ADDR len)
{
  bfd *abfd = inferior_bfd_generic (name, addr, offset, len);
  if (abfd == NULL)
    return abfd;

  return abfd;
}
