/* MI Command Set - stack commands.
   Copyright 2000, 2002 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions (a Red Hat company).

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

#include <ctype.h>
#include "defs.h"
#include "target.h"
#include "frame.h"
#include "value.h"
#include "mi-cmds.h"
#include "ui-out.h"
#include "varobj.h"
#include "wrapper.h"
#include "interpreter.h"
#include "symtab.h"
#include "symtab.h"
#include "objfiles.h"
#include "gdb_regex.h"

/* FIXME: these should go in some .h file but stack.c doesn't have a
   corresponding .h file. These wrappers will be obsolete anyway, once
   we pull the plug on the sanitization. */
extern void select_frame_command_wrapper (char *, int);

/* FIXME: There is no general mi header to put this kind of utility function.*/
extern void mi_report_var_creation (struct ui_out *uiout, struct varobj *var);

void mi_interp_stack_changed_hook (void);
void mi_interp_frame_changed_hook (int new_frame_number);
void mi_interp_context_hook (int thread_id);

/* This is the interpreter for the mi... */
extern struct gdb_interpreter *mi_interp;

/* This regexp pattern buffer is used for the file_list_statics
   and file_list_globals for the filter.  It doesn't look like the
   regexp package has an explicit pattern free, it tends to just reuse
   one buffer.  I don't want to use their global buffer because the
   psymtab->symtab code uses it to do C++ method detection.  So I am going
   to keep a separate one here.  */

struct re_pattern_buffer mi_symbol_filter;

/* Use this to print any extra info in the stack listing output that is
   not in the standard gdb printing */

void mi_print_frame_more_info (struct ui_out *uiout,
				struct symtab_and_line *sal,
				struct frame_info *fi);

static void list_args_or_locals (int locals, int values, 
				 struct frame_info *fi,
				 int all_blocks,
				 int create_varobj);

static void print_syms_for_block (struct block *block, 
				  struct frame_info *fi, 
				  struct ui_stream *stb,
				  int locals, 
				  int values,
				  int create_varobj,
				  struct re_pattern_buffer *filter);

/* Print a list of the stack frames. Args can be none, in which case
   we want to print the whole backtrace, or a pair of numbers
   specifying the frame numbers at which to start and stop the
   display. If the two numbers are equal, a single frame will be
   displayed. */
enum mi_cmd_result
mi_cmd_stack_list_frames (char *command, char **argv, int argc)
{
  int frame_low;
  int frame_high;
  int i;
  struct cleanup *cleanup_stack;
  struct frame_info *fi;

  if (!target_has_stack)
    error ("mi_cmd_stack_list_frames: No stack.");

  if (argc > 2 || argc == 1)
    error ("mi_cmd_stack_list_frames: Usage: [FRAME_LOW FRAME_HIGH]");

  if (argc == 2)
    {
      frame_low = atoi (argv[0]);
      frame_high = atoi (argv[1]);
    }
  else
    {
      /* Called with no arguments, it means we want the whole
         backtrace. */
      frame_low = -1;
      frame_high = -1;
    }

  /* Let's position fi on the frame at which to start the
     display. Could be the innermost frame if the whole stack needs
     displaying, or if frame_low is 0. */
  for (i = 0, fi = get_current_frame ();
       fi && i < frame_low;
       i++, fi = get_prev_frame (fi));

  if (fi == NULL)
    error ("mi_cmd_stack_list_frames: Not enough frames in stack.");

  cleanup_stack = make_cleanup_ui_out_list_begin_end (uiout, "stack");

  /* Now let;s print the frames up to frame_high, or until there are
     frames in the stack. */
  for (;
       fi && (i <= frame_high || frame_high == -1);
       i++, fi = get_prev_frame (fi))
    {
      QUIT;
      /* level == i: always print the level 'i'
         source == LOC_AND_ADDRESS: print the location and the address 
         always, even for level 0.
         args == 0: don't print the arguments. */
      print_frame_info (fi /* frame info */ ,
			i /* level */ ,
			LOC_AND_ADDRESS /* source */ ,
			0 /* args */ );
    }

  do_cleanups (cleanup_stack);
  if (i < frame_high)
    error ("mi_cmd_stack_list_frames: Not enough frames in stack.");

  return MI_CMD_DONE;
}

/* Helper print function for mi_cmd_stack_list_frames_lite */
static void
mi_print_frame_info_lite (struct ui_out *uiout,
			  int frame_num,
			  CORE_ADDR pc,
			  CORE_ADDR fp)
{
  char num_buf[8];

  sprintf (num_buf, "%d", frame_num);
  ui_out_text (uiout, "Frame ");
  ui_out_text(uiout, num_buf);
  ui_out_text(uiout, ": ");
  ui_out_list_begin (uiout, num_buf);
  ui_out_field_core_addr (uiout, "pc", pc);
  ui_out_field_core_addr (uiout, "fp", fp);
  ui_out_text (uiout, "\n");
  ui_out_list_end (uiout);

}

/* Print a list of the PC and Frame Pointers for each frame in the stack;
   also return the total number of frames. An optional argument "-limit"
   can be give to limit the number of frames printed.
  */

enum mi_cmd_result
mi_cmd_stack_list_frames_lite (char *command, char **argv, int argc)
{
    int limit = 0;
    int valid;
    int count = 0;
#ifndef FAST_COUNT_STACK_DEPTH
    int i;
    struct frame_info *fi;
#endif

    if (!target_has_stack)
        error ("mi_cmd_stack_list_frames_lite: No stack.");

    if ((argc > 2) || (argc == 1))
        error ("mi_cmd_stack_list_frames_lite: Usage: [-limit max_frame_number]");

    if (argc == 2)
      {
	if (strcmp (argv[0], "-limit") != 0)
	  error ("mi_cmd_stack_list_frames_lite: Invalid option.");
	
	if (! isnumber (argv[1][0]))
	  error ("mi_cmd_stack_list_frames_lite: Invalid argument to -limit.");

	limit = atoi (argv[1]);
      }
    else
      limit = -1;
	
#ifdef FAST_COUNT_STACK_DEPTH
    valid = FAST_COUNT_STACK_DEPTH (1, 0, -1, limit, &count, mi_print_frame_info_lite);
#else
    /* Start at the inner most frame */
    for (fi = get_current_frame (); fi ; fi = get_next_frame(fi))
        ;

    fi = get_current_frame ();
    
    if (fi == NULL)
        error ("mi_cmd_stack_list_frames_lite: No frames in stack.");

    ui_out_list_begin (uiout, "frames");

    for (i = 0; fi != NULL; (fi = get_prev_frame (fi)), i++) 
      {
        QUIT;

        if ((limit == 0) || (i < limit))
          {
	    mi_print_frame_info_lite (uiout, i, fi->pc, get_frame_base(fi));
          }
      }

    count = i;
    valid = 1;
    ui_out_list_end (uiout);
#endif
    
    ui_out_text (uiout, "Valid: ");
    ui_out_field_int (uiout, "valid", valid);
    ui_out_text (uiout, "\nCount: ");
    ui_out_field_int (uiout, "count", count);
    ui_out_text (uiout, "\n");
    
    return MI_CMD_DONE;
}

void 
mi_print_frame_more_info (struct ui_out *uiout,
				struct symtab_and_line *sal,
				struct frame_info *fi)
{
  /* I would feel happier if we used ui_out_field_skip for all the fields
     that we don't know how to set (like the file if we don't have symbols)
     but the rest of print_frame just omits the fields if they are not known,
     so I will do the same here...  */

  if (sal && sal->symtab && sal->symtab->dirname)
    ui_out_field_string (uiout, "dir", sal->symtab->dirname);
}

enum mi_cmd_result
mi_cmd_stack_info_depth (char *command, char **argv, int argc)
{
  int frame_high;
  int i;
  struct frame_info *fi;

  if (!target_has_stack)
    error ("mi_cmd_stack_info_depth: No stack.");

  if (argc > 1)
    error ("mi_cmd_stack_info_depth: Usage: [MAX_DEPTH]");

  if (argc == 1)
    frame_high = atoi (argv[0]);
  else
    /* Called with no arguments, it means we want the real depth of
       the stack. */
    frame_high = -1;

#ifdef FAST_COUNT_STACK_DEPTH
  if (! FAST_COUNT_STACK_DEPTH (0, 0, frame_high, frame_high, &i, NULL))
#endif
    {
      for (i = 0, fi = get_current_frame ();
	   fi && (i < frame_high || frame_high == -1);
	   i++, fi = get_prev_frame (fi))
	QUIT;
    }
  ui_out_field_int (uiout, "depth", i);
  
  return MI_CMD_DONE;
}

/* Print a list of the locals for the current frame. With argument of
   0, print only the names, with argument of 1 print also the
   values. */
enum mi_cmd_result
mi_cmd_stack_list_locals (char *command, char **argv, int argc)
{
  int values;
  int all_blocks;
  int create_varobj;

  if (argc < 1 || argc > 2)
    error ("mi_cmd_stack_list_locals: Usage: PRINT_VALUES [ALL_BLOCKS]");

  values = atoi (argv[0]);
  create_varobj = (values == 2);

  if (argc >= 2)
    all_blocks = atoi (argv[1]);
  else
    all_blocks = 0;

  list_args_or_locals (1, values, deprecated_selected_frame,
		       all_blocks, create_varobj);
  return MI_CMD_DONE;
}

/* Print a list of the arguments for the current frame. With argument
   of 0, print only the names, with argument of 1 print also the
   values, with argument of 2, create varobj for the arguments. */

enum mi_cmd_result
mi_cmd_stack_list_args (char *command, char **argv, int argc)
{
  int frame_low;
  int frame_high;
  int i;
  int values;
  int create_varobj;
  struct frame_info *fi;
  struct cleanup *cleanup_stack_args;

  if (argc < 1 || argc > 3 || argc == 2)
    error ("mi_cmd_stack_list_args: Usage: PRINT_VALUES [FRAME_LOW FRAME_HIGH]");

  if (argc == 3)
    {
      frame_low = atoi (argv[1]);
      frame_high = atoi (argv[2]);
    }
  else
    {
      /* Called with no arguments, it means we want args for the whole
         backtrace. */
      frame_low = -1;
      frame_high = -1;
    }

  values = atoi (argv[0]);
  create_varobj = (values == 2);

  /* Let's position fi on the frame at which to start the
     display. Could be the innermost frame if the whole stack needs
     displaying, or if frame_low is 0. */
  for (i = 0, fi = get_current_frame ();
       fi && i < frame_low;
       i++, fi = get_prev_frame (fi));

  if (fi == NULL)
    error ("mi_cmd_stack_list_args: Not enough frames in stack.");

  cleanup_stack_args = make_cleanup_ui_out_list_begin_end (uiout, "stack-args");

  /* Now let's print the frames up to frame_high, or until there are
     frames in the stack. */
  for (;
       fi && (i <= frame_high || frame_high == -1);
       i++, fi = get_prev_frame (fi))
    {
      struct cleanup *cleanup_frame;
      QUIT;
      cleanup_frame = make_cleanup_ui_out_tuple_begin_end (uiout, "frame");
      ui_out_field_int (uiout, "level", i); 
      list_args_or_locals (0, values, fi, 0, create_varobj);
      do_cleanups (cleanup_frame);
    }

  do_cleanups (cleanup_stack_args);
  if (i < frame_high)
    error ("mi_cmd_stack_list_args: Not enough frames in stack.");

  return MI_CMD_DONE;
}

/* Print a list of the locals or the arguments for the currently
   selected frame.  If the argument passed is 0, printonly the names
   of the variables, if an argument of 1 is passed, print the values
   as well. If ALL_BLOCKS == 1, then print the symbols for ALL lexical
   blocks in the function that is in frame FI.*/

static void
list_args_or_locals (int locals, int values, struct frame_info *fi, 
		     int all_blocks, int create_varobj)
{
  struct block *block = NULL;
  struct cleanup *cleanup_list;
  static struct ui_stream *stb = NULL;

  stb = ui_out_stream_new (uiout);
  
  cleanup_list = make_cleanup_ui_out_list_begin_end (uiout, locals ? "locals" : "args");

  if (all_blocks)
    {
      CORE_ADDR fstart;
      CORE_ADDR endaddr;
      int index;
      int nblocks;
      struct blockvector *bv;
      
      /* CHECK - I assume that the function block in the innermost
	 lexical block that starts at the start function of the
	 PC.  If this is not correct, then I will have to run through
	 the blockvector to match it to the block I get by:
      */   
	 
      fstart = get_pc_function_start (get_frame_pc (fi));
      if (fstart == 0)
	{
	  /* Can't find the containing function for this PC.  Sigh... */
	  fstart = fi->pc;
	}

      bv = blockvector_for_pc (fstart, &index);
      if (bv == NULL)
	{
	  error ("list_args_or_locals: Couldn't find block vector for pc %s.",
		 paddr_nz (fstart));
	}
      nblocks = BLOCKVECTOR_NBLOCKS (bv);

      block = BLOCKVECTOR_BLOCK (bv, index);
      endaddr = BLOCK_END (block);

      while (BLOCK_END (block) <= endaddr)
	{
	  print_syms_for_block (block, fi, stb, locals, 
				values, create_varobj, NULL);
	  index++;
	  if (index == nblocks)
	    break;
	  block = BLOCKVECTOR_BLOCK (bv, index);
	}
    }
  else
    {
      block = get_frame_block (fi, 0);

      while (block != 0)
	{
	  print_syms_for_block (block, fi, stb, locals, values, create_varobj, NULL);
	  
	  if (BLOCK_FUNCTION (block))
	    break;
	  else
	    block = BLOCK_SUPERBLOCK (block);
	}
    }

  do_cleanups (cleanup_list);
  ui_out_stream_delete (stb);
}

/* This implements the command -file-list-statics.  It takes three or four
   arguments, a filename, a shared library, and the standard PRINT_VALUES
   argument, and an optional filter.  
   It prints all the static variables in the given file, in
   the given shared library.  If the shared library name is empty, then it
   looks in all shared libraries for the file.

   If the file name is the special cookie *CURRENT FRAME* then it prints
   the statics for the currently selected frame.

   If PRINT_VALUES is 0, only the variable names are printed.
   If PRINT_VALUES is 1, the values are also printed.
   If PRINT_VALUES is 2, then varobj's are made for all the variables as
   well.
   If the filter string is provided, only symbols that DON'T match
   the filter will be printed.
   */

#define CURRENT_FRAME_COOKIE "*CURRENT FRAME*"

enum mi_cmd_result
mi_cmd_file_list_statics (char *command, char **argv, int argc)
{
  int values;
  int create_varobj;
  char *shlibname, *filename;
  struct block *block;
  struct partial_symtab *file_ps;
  struct symtab *file_symtab;
  struct cleanup *cleanup_list;
  struct ui_stream *stb;
  struct re_pattern_buffer *filterp = NULL;

  if (argc < 3 || argc > 4)
    {
      error ("mi_cmd_file_list_statics: Usage: FILE SHLIB PRINT_VALUES"
	     " [FILTER]");
    }
  
  
  values = atoi (argv[2]);
  create_varobj = (values == 2);
  
  filename = argv[0];
  shlibname = argv[1];

  if (argc == 4)
    {
      const char *msg;

      msg = re_compile_pattern (argv[3], strlen (argv[3]), &mi_symbol_filter);
      if (msg)
	error ("Error compiling regexp: \"%s\"", msg);
      filterp = &mi_symbol_filter;
    }
  else
    filterp = NULL;

  /* Probably better to not restrict the objfile search, while doing the 
     PSYMTAB to SYMTAB conversion to miss some types that are defined outside the
     current shlib.  So get the psymtab first, and then convert after cleaning up.  */

  if (strcmp (filename, CURRENT_FRAME_COOKIE) == 0) 
    {
      CORE_ADDR pc;
      struct obj_section *objsec;

      pc = get_frame_pc (get_selected_frame ());
      objsec = find_pc_section (pc);
      if (objsec != NULL && objsec->objfile != NULL)
	cleanup_list = make_cleanup_restrict_to_objfile (objsec->objfile);
      else
	cleanup_list = make_cleanup (null_cleanup, NULL);

      file_ps = find_pc_psymtab (pc);
      do_cleanups (cleanup_list);
    }
  else
    {
      if (*shlibname != '\0')
	{
	  cleanup_list = make_cleanup_restrict_to_shlib (shlibname);
	  if (cleanup_list == (void *) -1)
	    {
	      error ("mi_cmd_file_list_statics: Could not find shlib \"%s\".", shlibname);
	    }
	  
	}
      else
	cleanup_list = make_cleanup (null_cleanup, NULL);
      
      file_ps = lookup_partial_symtab (filename);
      
      /* FIXME: dbxread.c only uses the SECOND N_SO stab when making psymtabs.  It discards
         the first one.  But that means that if filename is an absolute path, it is likely
         lookup_partial_symtab will fail.  If it did, try again with the base name.  */

      if (file_ps == NULL)
        if (lbasename(filename) != filename)
          file_ps = lookup_partial_symtab (lbasename (filename));
      
      do_cleanups (cleanup_list);

    }

  /* If the user passed us a real filename and we couldn't find it, that is an error.  But
     "" or current frame, could point to a file or objfile with no debug info.  In which
     case we should just return an empty list.  */
  
  if (file_ps == NULL)
    {
      if (filename[0] == '\0' || strcmp (filename, CURRENT_FRAME_COOKIE) == 0)
	{
	  cleanup_list = make_cleanup_ui_out_list_begin_end (uiout, "statics");
	  do_cleanups (cleanup_list);
	  return MI_CMD_DONE;
	}
      else
	error ("mi_cmd_file_list_statics: Could not get symtab for file \"%s\".", filename);
    }
  
  file_symtab = PSYMTAB_TO_SYMTAB (file_ps);

  if (file_symtab == NULL)
    error ("Could not convert psymtab to symtab for file \"%s\"", filename);

  block = BLOCKVECTOR_BLOCK (file_symtab->blockvector, STATIC_BLOCK);

  stb = ui_out_stream_new (uiout);
  
  cleanup_list = make_cleanup_ui_out_list_begin_end (uiout, "statics");

  print_syms_for_block (block, NULL, stb, -1, values, create_varobj, filterp);

  do_cleanups (cleanup_list);
  ui_out_stream_delete (stb);

  return MI_CMD_DONE;
}


void
print_globals_for_symtab (struct symtab *file_symtab, 
			  struct ui_stream *stb,
			  int values, int create_varobj, 
			  struct re_pattern_buffer *filter)
{
  struct block *block;
  struct cleanup *cleanup_list;
 
  block = BLOCKVECTOR_BLOCK (file_symtab->blockvector, GLOBAL_BLOCK);

  cleanup_list = make_cleanup_ui_out_list_begin_end (uiout, "globals");

  print_syms_for_block (block, NULL, stb, -1, values, create_varobj, filter);

  do_cleanups (cleanup_list);
}

/* This implements the command -file-list-globals.  It takes three or
   four arguments, filename, a shared library, the standard
   PRINT_VALUES argument and an optional filter regexp.  It prints all
   the global variables in the given file, in the given shared
   library.  If the shared library name is empty, then it looks in all
   shared libraries for the file.  If the filename is empty, then it
   looks in all files in the given shared library.  If both are empty
   then it prints ALL globals.  
   If PRINT_VALUES is 0, only the variable names are printed.  
   If PRINT_VALUES is 1, the values are also printed.  
   If PRINT_VALUES is 2, then varobj's are made for all the variables 
   as well.  
   Finally, if there are four arguments, the last is a regular expression,
   to filter OUT all varobj's matching the regexp.  */

enum mi_cmd_result
mi_cmd_file_list_globals (char *command, char **argv, int argc)
{
  int values;
  int create_varobj;
  char *shlibname, *filename;
  struct partial_symtab *file_ps;
  struct symtab *file_symtab;
  struct ui_stream *stb;
  struct re_pattern_buffer *filterp;

  if (argc < 3 || argc > 4)
    {
      error ("mi_cmd_file_list_globals: Usage: FILE SHLIB PRINT_VALUES"
	     " [FILTER]");
    }
  
  
  values = atoi (argv[2]);
  create_varobj = (values == 2);
  
  filename = argv[0];
  shlibname = argv[1];

  if (argc == 4)
    {
      const char *msg;

      msg = re_compile_pattern (argv[3], strlen (argv[3]), &mi_symbol_filter);
      if (msg)
	error ("Error compiling regexp: \"%s\"", msg);
      filterp = &mi_symbol_filter;
    }
  else
    filterp = NULL;

  stb = ui_out_stream_new (uiout);

  if (*filename != '\0')
    {
      struct cleanup *cleanup_list;

      if (*shlibname != '\0')
	{
	  cleanup_list = make_cleanup_restrict_to_shlib (shlibname);
	  if (cleanup_list == (void *) -1)
	    {
	      error ("mi_cmd_file_list_statics: "
		     "Could not find shlib \"%s\".", 
		     shlibname);
	    }
	  
	}
      else
	cleanup_list = make_cleanup (null_cleanup, NULL);

      /* Probably better to not restrict the objfile search, while
	 doing the PSYMTAB to SYMTAB conversion to miss some types
	 that are defined outside the current shlib.  So get the
	 psymtab first, and then convert after cleaning up.  */

      file_ps = lookup_partial_symtab (filename);
      
      
      if (file_ps == NULL)
	error ("mi_cmd_file_list_statics: "
	       "Could not get symtab for file \"%s\".", 
	       filename);
      
      do_cleanups (cleanup_list);
      
      file_symtab = PSYMTAB_TO_SYMTAB (file_ps);
      print_globals_for_symtab (file_symtab, stb, values, 
				create_varobj, filterp);
    }
  else
    {
      if (*shlibname != '\0')
	{
	  struct objfile *ofile, *requested_ofile = NULL;
	  struct partial_symtab *ps;

	  ALL_OBJFILES (ofile)
	    {
	      if (objfile_matches_name (ofile, shlibname))
		{
		  requested_ofile = ofile;
		  break;
		}
	    }
	  if (requested_ofile == NULL)
	    error ("mi_file_list_globals: "
		   "Couldn't find shared library \"%s\"\n", 
		   shlibname);

	  ALL_OBJFILE_PSYMTABS (requested_ofile, ps)
	    {
	      struct symtab *file_symtab;

	      file_symtab = PSYMTAB_TO_SYMTAB (ps);
	      if (!file_symtab)
		continue;
	      
	      
	      if (file_symtab->primary)
		{
		  struct cleanup *file_cleanup;
		  file_cleanup = 
		    make_cleanup_ui_out_list_begin_end (uiout, "file");
		  ui_out_field_string (uiout, "filename", 
				       file_symtab->filename);
		  print_globals_for_symtab (file_symtab, stb, values, 
					    create_varobj, filterp);
		  do_cleanups (file_cleanup);
		}
	    }
	}
      else
	{
	  struct objfile *ofile;
	  struct partial_symtab *ps;

	  /* Okay, you want EVERYTHING...  */

	  ALL_OBJFILES (ofile)
	    {
	      struct cleanup *ofile_cleanup;

	      ofile_cleanup = make_cleanup_ui_out_list_begin_end (uiout, "image");
	      if (ofile->name != NULL)
		ui_out_field_string (uiout, "imagename", ofile->name);
	      else
		ui_out_field_string (uiout, "imagename", "<unknown>");

	      ALL_OBJFILE_PSYMTABS (ofile, ps)
		{
		  struct symtab *file_symtab;
		  
		  file_symtab = PSYMTAB_TO_SYMTAB (ps);
		  if (!file_symtab)
		    continue;
		  
		  if (file_symtab->primary)
		    {
		      struct cleanup *file_cleanup;
		      file_cleanup = 
			make_cleanup_ui_out_list_begin_end (uiout, "file");
		      ui_out_field_string (uiout, "filename", 
					   file_symtab->filename);
		      print_globals_for_symtab (file_symtab, stb, 
						values, create_varobj, 
						filterp);
		      do_cleanups (file_cleanup);
		    }
		}
	      do_cleanups (ofile_cleanup);
	    }
	}
    }
      
  ui_out_stream_delete (stb);

  return MI_CMD_DONE;

}

/* Print the variable symbols for block BLOCK.  If VALUES is 1 print
   the values as well as the names.  If CREATE_VAROBJ is 1, also make
   varobj's for each variable.

   LOCALS determines what scope of variables to print:
     1 - print locals AND statics.  
     0 - print args.  
     -1  - print statics. 
   STB is the ui-stream to which the results are printed.  
   And FI, if non-null, is the frame to bind the varobj to.  
   If FILTER is non-null, then we only print expressions matching
   that compiled regexp.  */

static void
print_syms_for_block (struct block *block, 
		      struct frame_info *fi, 
		      struct ui_stream *stb,
		      int locals, 
		      int values,
		      int create_varobj,
		      struct re_pattern_buffer *filter)
{
  int nsyms;
  int print_me;
  struct symbol *sym;
  int i;
  struct ui_stream *error_stb;
  struct cleanup *old_chain;
  
  nsyms = BLOCK_NSYMS (block);

  if (nsyms == 0) 
    return;

  error_stb = ui_out_stream_new (uiout);
  old_chain = make_cleanup_ui_out_stream_delete (error_stb);

  ALL_BLOCK_SYMBOLS (block, i, sym)
    {
      print_me = 0;

      switch (SYMBOL_CLASS (sym))
	{
	default:
	case LOC_UNDEF:	/* catches errors        */
	case LOC_CONST:	/* constant              */
	case LOC_TYPEDEF:	/* local typedef         */
	case LOC_LABEL:	/* local label           */
	case LOC_BLOCK:	/* local function        */
	case LOC_CONST_BYTES:	/* loc. byte seq.        */
	case LOC_UNRESOLVED:	/* unresolved static     */
	case LOC_OPTIMIZED_OUT:	/* optimized out         */
	  print_me = 0;
	  break;

	case LOC_ARG:	/* argument              */
	case LOC_REF_ARG:	/* reference arg         */
	case LOC_REGPARM:	/* register arg          */
	case LOC_REGPARM_ADDR:	/* indirect register arg */
	case LOC_LOCAL_ARG:	/* stack arg             */
	case LOC_BASEREG_ARG:	/* basereg arg           */
	  if (locals == 0)
	    print_me = 1;
	  break;

	case LOC_STATIC:	/* static                */
	  if (locals == -1 || locals == 1)
	    print_me = 1;
	  break;
	case LOC_LOCAL:	/* stack local           */
	case LOC_BASEREG:	/* basereg local         */
	case LOC_REGISTER:	/* register              */
	  if (locals == 1)
	    print_me = 1;
	  break;
	}

      if (print_me)
	{
	  struct symbol *sym2;
	  int len = strlen (SYMBOL_NAME (sym));

	  /* If we are about to print, compare against the regexp.  */
	  if (filter && re_search (filter, SYMBOL_NAME (sym), 
				   len, 0, len, 
				   (struct re_registers *) 0) >= 0)
	    continue;

	  if (!create_varobj && !values)
	    {
	      ui_out_tuple_begin (uiout, NULL);
	      ui_out_field_string (uiout, "name", SYMBOL_NAME (sym));
	      ui_out_tuple_end (uiout);
	      continue;
	    }

	  if (!locals)
	    sym2 = lookup_symbol (SYMBOL_NAME (sym),
				  block, VAR_NAMESPACE,
				  (int *) NULL,
				  (struct symtab **) NULL);
	  else
	    sym2 = sym;
	  
	  if (create_varobj)
	    {
	      struct varobj *new_var;
	      if (fi)
		new_var = varobj_create (varobj_gen_name (), 
					 SYMBOL_NAME (sym2),
					 fi->frame,
					 block,
					 USE_BLOCK_IN_FRAME);

	      else
		new_var = varobj_create (varobj_gen_name (), 
					 SYMBOL_NAME (sym2),
					 0,
					 block,
					 NO_FRAME_NEEDED);

	      /* FIXME: There should be a better way to report an error in 
		 creating a variable here, but I am not sure how to do it,
	         so I will just bag out for now. */

	      if (new_var == NULL)
		continue;

	      ui_out_tuple_begin (uiout, "varobj");
	      ui_out_field_string (uiout, "exp", SYMBOL_NAME (sym));
	      if (values)
		{
		  if (new_var != NULL)
		    {
		      char *value_str;
		      struct ui_file *save_stderr;
       
		      /* If we are using the varobj's, then print
			 the value as the varobj would. */
		      
		      save_stderr = gdb_stderr;
		      gdb_stderr = error_stb->stream;

		      if (gdb_varobj_get_value (new_var, &value_str))
			{
			  ui_out_field_string (uiout, "value", value_str);
			}
		      else
			{
			  /* FIXME: can I get the error string & put it here? */
			  ui_out_field_stream (uiout, "value", 
					       error_stb);
			}
		      gdb_stderr = save_stderr;
		    }
		  else
		    ui_out_field_skip (uiout, "value");
		}
	      mi_report_var_creation (uiout, new_var);
	    }	  
	  else
	    {
	      ui_out_tuple_begin (uiout, NULL);
	      ui_out_field_string (uiout, "name", SYMBOL_NAME (sym));
	      print_variable_value (sym2, fi, stb->stream);
	      ui_out_field_stream (uiout, "value", stb);
	    }
	  ui_out_tuple_end (uiout);
	}
    }

  do_cleanups (old_chain);
}

enum mi_cmd_result
mi_cmd_stack_select_frame (char *command, char **argv, int argc)
{
  if (!target_has_stack)
    error ("mi_cmd_stack_select_frame: No stack.");

  if (argc > 1)
    error ("mi_cmd_stack_select_frame: Usage: [FRAME_SPEC]");

  /* with no args, don't change frame */
  if (argc == 0)
    select_frame_command_wrapper (0, 1 /* not used */ );
  else
    select_frame_command_wrapper (argv[0], 1 /* not used */ );
  return MI_CMD_DONE;
}

void 
mi_interp_stack_changed_hook (void)
{
  struct ui_out *saved_ui_out = uiout;

  uiout = gdb_interpreter_ui_out (mi_interp);

  ui_out_list_begin (uiout, "MI_HOOK_RESULT");
  ui_out_field_string (uiout, "HOOK_TYPE", "stack_changed");
  ui_out_list_end (uiout);
  uiout = saved_ui_out;
}

void 
mi_interp_frame_changed_hook (int new_frame_number)
{
  struct ui_out *saved_ui_out = uiout;

  uiout = gdb_interpreter_ui_out (mi_interp);

  ui_out_list_begin (uiout, "MI_HOOK_RESULT");
  ui_out_field_string (uiout, "HOOK_TYPE", "frame_changed");
  ui_out_field_int (uiout, "frame", new_frame_number);
  ui_out_list_end (uiout);
  uiout = saved_ui_out;

}

void
mi_interp_context_hook (int thread_id)
{
  struct ui_out *saved_ui_out = uiout;

  uiout = gdb_interpreter_ui_out (mi_interp);

  ui_out_list_begin (uiout, "MI_HOOK_RESULT");
  ui_out_field_string (uiout, "HOOK_TYPE", "thread_changed");
  ui_out_field_int (uiout, "thread", thread_id);
  ui_out_list_end (uiout);
  uiout = saved_ui_out;
}




