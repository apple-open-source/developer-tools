/* APPLE LOCAL file checkpoints */
/* Checkpoints for GDB.
   Copyright 2005
   Free Software Foundation, Inc.

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
#include "symtab.h"
#include "target.h"
#include "frame.h"
#include "command.h"
#include "gdbcmd.h"
#include "regcache.h"
#include "inferior.h"
extern void re_execute_command (char *args, int from_tty);

#include "checkpoint.h"

extern struct checkpoint *rx_cp;

int auto_checkpointing;

/* Memory cache stuff.  */

/* Get a block from the inferior and save it.  */

void
memcache_get (struct checkpoint *cp, ULONGEST addr, int len)
{
  struct memcache *mc;
  int actual;

  mc = (struct memcache *) xmalloc (sizeof (struct memcache));
  mc->startaddr = addr;
  mc->len = len;
  mc->cache = (gdb_byte *) xmalloc (len);

  mc->next = cp->mem;
  cp->mem = mc;

  actual = target_read_partial (&current_target, TARGET_OBJECT_MEMORY,
				NULL, mc->cache, addr, len);
  /*  printf ("cached %d (orig %d) bytes at 0x%llx\n", actual, len, addr); */

  mc->cache = (gdb_byte *) xrealloc (mc->cache, actual);

  mc->len = actual;
}

void
memcache_put (struct checkpoint *cp)
{
  struct memcache *mc;

  for (mc = cp->mem; mc != NULL; mc = mc->next)
    target_write_partial (&current_target, TARGET_OBJECT_MEMORY,
			  NULL, mc->cache, mc->startaddr, mc->len);

#ifdef NM_NEXTSTEP /* in lieu of target vectory */
 {
   void fork_memcache_put (struct checkpoint *);
   if (cp->pid != 0)
     fork_memcache_put (cp);
 }
#endif
}

/* The count of checkpoints that have been created so far.  */

int checkpoint_count = 1;

/* List of all checkpoints.  */
struct checkpoint *checkpoint_list;
struct checkpoint *last_checkpoint;

/* The current checkpoint is the one corresponding to the actual state of
   the inferior, or to the last point of checkpoint creation.  */

struct checkpoint *current_checkpoint;

/* Latest checkpoint on original line of execution. The program can be continued
   from this one with no ill effects on state of the world.  */

struct checkpoint *original_latest_checkpoint;

/* This is true if we're already in the midst of creating a
   checkpoint, just in case something thinks it wants to make another
   one.  */

int collecting_checkpoint = 0;

int rolling_back = 0;

int rolled_back = 0;

/* This variable is true when the user has asked for a checkpoint to
   be created at each stop.  */

int auto_checkpointing = 0;

int warned_cpfork = 0;

/* Manually create a checkpoint.  */

/* Note that multiple checkpoints may be created at the same point; it
   may be that the user has manually touched registers or memory in
   between. Rolling back to identical checkpoints is not interesting,
   but not harmful either.  */

static void
create_checkpoint_command (char *args, int from_tty)
{
  struct checkpoint *cp;

  if (!target_has_execution)
    {
      error ("Cannot create checkpoints before the program has run.");
      return;
    }

  cp = create_checkpoint ();

  if (cp)
    cp->type = manual;

  if (cp)
    printf ("Checkpoint %d created\n", cp->number);
  else
    printf ("Checkpoint creation failed.\n");
}

struct checkpoint *
create_checkpoint ()
{
  struct checkpoint *cp;

  if (!target_has_execution)
    return NULL;

  cp = collect_checkpoint ();

  return finish_checkpoint (cp);
}

/* Create a content-less checkpoint.  */

struct checkpoint *
start_checkpoint ()
{
  struct checkpoint *cp;

  cp = (struct checkpoint *) xmalloc (sizeof (struct checkpoint));
  memset (cp, 0, sizeof (struct checkpoint));

  cp->regs = regcache_xmalloc (current_gdbarch);
  cp->mem = NULL;

  cp->pid = 0;

  return cp;
}

struct checkpoint *
collect_checkpoint ()
{
  struct checkpoint *cp;
  struct value *forkfn;
  struct value *val;
  int retval;

  cp = start_checkpoint ();

  /* Always collect the registers directly.  */
  regcache_cpy (cp->regs, current_regcache);

  /* (The following should be target-specific) */
  if (lookup_minimal_symbol("_cpfork", 0, 0)
      && (forkfn = find_function_in_inferior ("cpfork", builtin_type_int)))
    {
      val = call_function_by_hand_expecting_type (forkfn,
						  builtin_type_int, 0, NULL, 1);
  
      retval = value_as_long (val);

      /* Keep the pid around, only dig through fork when rolling back.  */
      cp->pid = retval;

    }
  else
    {
      if (!warned_cpfork)
	{
	  warning ("cpfork not found, falling back to memory reads to make checkpoints\n");
	  warned_cpfork = 1;
	}

#ifdef NM_NEXTSTEP /* in lieu of target vectory */
      {
	void direct_memcache_get (struct checkpoint *);
	if (cp->pid != 0)
	  direct_memcache_get (cp);
      }
#endif
    }
  return cp;
}

struct checkpoint *
finish_checkpoint (struct checkpoint *cp)
{
  cp->number = checkpoint_count++;

  /* Link into the end of the list.  */
  if (last_checkpoint)
    {
      last_checkpoint->next = cp;
      cp->prev = last_checkpoint;
      last_checkpoint = cp;
    }
  else
    {
      checkpoint_list = last_checkpoint = cp;
      cp->prev = NULL;
    }

  cp->next = NULL;

  /* By definition, any newly-created checkpoint is the current one.  */
  if (current_checkpoint)
    current_checkpoint->lnext = cp;
  cp->lprev = current_checkpoint;
  current_checkpoint = cp;

  /* If we're on the original no-rollback line of execution, record
     this checkpoint as the most recent.  */
  if (!rolled_back)
    original_latest_checkpoint = cp;

  /*  print_checkpoint_info (cp); */

  return cp;
}

int
checkpoint_compare (struct checkpoint *cp1, struct checkpoint *cp2)
{
  int regcache_compare (struct regcache *rc1, struct regcache *rc2);

  if (regcache_compare (cp1->regs, cp2->regs) == 0)
    return 0;

  return 1;
}

/* This is called from infrun.c:normal_stop() to auto-generate checkpoints.  */

void
maybe_create_checkpoint ()
{
  struct checkpoint *tmpcp, *lastcp;

  if (!auto_checkpointing)
    return;

  if (collecting_checkpoint || rolling_back)
    return;
  collecting_checkpoint = 1;

  lastcp = current_checkpoint;

  tmpcp = collect_checkpoint ();

#if 0 /* used for re-execution */
  for (cp = checkpoint_list; cp != NULL; cp = cp->next)
    if (checkpoint_compare (cp, tmpcp))
      {
	current_checkpoint = cp;
	if (rx_cp && lastcp)
	  current_checkpoint->immediate_prev = lastcp;
	return;
      }
#endif

  finish_checkpoint (tmpcp);

  tmpcp->type = autogen;

  /*
  if ((rx_cp && lastcp) || step_range_end == 1)
    current_checkpoint->immediate_prev = lastcp;
  */

  collecting_checkpoint = 0;
}

static void
rollback_to_checkpoint_command (char *args, int from_tty)
{
  char *p;
  int num = 1;
  struct checkpoint *cp;

  if (!checkpoint_list)
    {
      error ("No checkpoints to roll back to!\n");
      return;
    }

  if (args)
    {
      p = args;
      num = atoi (p);
    }

  cp = find_checkpoint (num);

  if (cp == NULL)
    {
      error ("checkpoint %d not found\n", num);
      return;
    }

  rollback_to_checkpoint (cp);
}

/* Given a checkpoint, make it the current state.  */

void
rollback_to_checkpoint (struct checkpoint *cp)
{
  regcache_cpy (current_regcache, cp->regs);

  memcache_put (cp);

  current_checkpoint = cp;

  /* Prevent a bit of recursion.  */
  rolling_back = 1;

  flush_cached_frames ();

  if (1)
    normal_stop ();
  else
    {
      deprecated_update_frame_pc_hack (get_current_frame (), read_pc ());
      select_frame (get_current_frame ());
      print_stack_frame (get_selected_frame (NULL), 0, SRC_AND_LOC);
      do_displays ();
    }

  rolling_back = 0;

  /* Mark that we have been messing around with the program's state.  */
  rolled_back = (cp != original_latest_checkpoint);
}

/* Find a checkpoint given its number.  */

struct checkpoint *
find_checkpoint (int num)
{
  struct checkpoint *cp;

  /* Use a linear search, may need to improve on this when large numbers
     of checkpoints are in use.  */
  for (cp = checkpoint_list; cp != NULL; cp = cp->next)
    {
      if (cp->number == num)
	return cp;
    }
  return NULL;
}

/* static */ void
checkpoints_info (char *args, int from_tty)
{
  struct checkpoint *cp;

  for (cp = checkpoint_list; cp != NULL; cp = cp->next)
    {
      print_checkpoint_info (cp);
    }
}

void
print_checkpoint_info (struct checkpoint *cp)
{
  gdb_byte reg_buf[MAX_REGISTER_SIZE];
  LONGEST pc;
  struct symtab_and_line sal;

  regcache_cooked_read (cp->regs, PC_REGNUM, reg_buf);
  pc = *((LONGEST *) reg_buf);
  sal = find_pc_line (pc, 0);

  if (cp->pid != 0)
    printf("[%d] ", cp->pid);

  printf ("%c%c%c%d: pc=0x%llx",
	  cp->type,
	  (current_checkpoint == cp ? '*' : ' '),
	  (original_latest_checkpoint == cp ? '!' : ' '),
	  cp->number, pc);

  printf (" (");
  if (cp->lprev)
    printf ("%d", cp->lprev->number);
  else
    printf (" ");
  printf ("<-)");
  printf (" (->");
  if (cp->lnext)
    printf ("%d", cp->lnext->number);
  else
    printf (" ");
  printf (")");

  if (sal.symtab)
    {
      printf (" -- ");
      print_source_lines (sal.symtab, sal.line, 1, 0);
    }
  else
    {
      printf ("\n");
    }
}

/* Checkpoint commands.  */

static void
undo_command (char *args, int from_tty)
{
  if (current_checkpoint == NULL)
    error ("No current checkpoint");
  else if (current_checkpoint->lprev == NULL)
    error ("No previous checkpoint to roll back to");
  else
    rollback_to_checkpoint (current_checkpoint->lprev);
}

static void
redo_command (char *args, int from_tty)
{
  if (current_checkpoint == NULL)
    error ("No current checkpoint");
  else if (current_checkpoint->lnext == NULL)
    error ("No next checkpoint to roll forward to");
  else
    rollback_to_checkpoint (current_checkpoint->lnext);
}

static void
now_command (char *args, int from_tty)
{
  if (original_latest_checkpoint == NULL)
    error ("No original latest checkpoint");

  rollback_to_checkpoint (original_latest_checkpoint);
}

/* Given a checkpoint, scrub it out of the system.  */

void
delete_checkpoint (struct checkpoint *cp)
{
  struct checkpoint *cpi;
  struct memcache *mc;

  /* First disentangle all the logical connections.  */
  for (cpi = checkpoint_list; cpi != NULL; cpi = cpi->next)
    {
      if (cpi->lnext == cp)
	cpi->lnext = cp->lnext;
      if (cpi->lprev == cp)
	cpi->lprev = cp->lprev;
    }

  if (current_checkpoint == cp)
    current_checkpoint = NULL;
  /* Note that we don't want to move this to any other checkpoint,
     because this is the only checkpoint that can restore to a
     pre-all-undo state.  */
  if (original_latest_checkpoint == cp)
    original_latest_checkpoint = NULL;

  /* Now disconnect from the main list.  */
  if (cp == checkpoint_list)
    checkpoint_list = cp->next;
  if (cp->prev)
    cp->prev->next = cp->next;
  if (cp->next)
    cp->next->prev = cp->prev;

  for (mc = cp->mem; mc != NULL; mc = mc->next)
    {
      /* (should dealloc caches) */
    }

  if (cp->pid)
    {
      kill (cp->pid, 9);
    }

  /* flagging for debugging purposes */
  cp->type = 'D';
}

/* Call FUNCTION on each of the checkpoints whose numbers are given in
   ARGS.  */

static void
map_checkpoint_numbers (char *args, void (*function) (struct checkpoint *))
{
  char *p = args;
  char *p1;
  int num;
  struct checkpoint *cpi;
  int match;

  if (p == 0)
    error_no_arg (_("one or more checkpoint numbers"));

  while (*p)
    {
      match = 0;
      p1 = p;

      num = get_number_or_range (&p1);
      if (num == 0)
	{
	  warning (_("bad checkpoint number at or near '%s'"), p);
	}
      else
	{
	  for (cpi = checkpoint_list; cpi != NULL; cpi = cpi->next)
	    if (cpi->number == num)
	      {
		match = 1;
		function (cpi);
	      }
	  if (match == 0)
	    printf_unfiltered (_("No checkpoint number %d.\n"), num);
	}
      p = p1;
    }
}

static void
delete_checkpoint_command (char *args, int from_tty)
{
  struct checkpoint *cpi;

  dont_repeat ();

  if (args == 0)
    {
      /* Ask user only if there are some breakpoints to delete.  */
      if (!from_tty
	  || (checkpoint_list && query (_("Delete all checkpoints? "))))
	{
	  for (cpi = checkpoint_list; cpi != NULL; cpi = cpi->next)
	    delete_checkpoint (cpi);
	}
    }
  else
    map_checkpoint_numbers (args, delete_checkpoint);

}

#if 0 /* comment out for now */
static void
reverse_step_command (char *args, int from_tty)
{
  struct checkpoint *cp, *prev;
  gdb_byte reg_buf[MAX_REGISTER_SIZE];
  LONGEST pc, prev_pc;
  struct symtab_and_line sal, prev_sal;

  if (current_checkpoint == NULL)
    {
      printf ("no cp!\n");
      return;
    }

  if (current_checkpoint->immediate_prev == NULL)
    {
      /* re-execute to find old state */
    }

  if (current_checkpoint->immediate_prev)
    {
      cp = current_checkpoint->immediate_prev;
      prev = cp->immediate_prev;
      while (prev)
	{
	  regcache_cooked_read (cp->regs, PC_REGNUM, reg_buf);
	  pc = *((LONGEST *) reg_buf);
	  sal = find_pc_line (pc, 0);
	  regcache_cooked_read (prev->regs, PC_REGNUM, reg_buf);
	  prev_pc = *((LONGEST *) reg_buf);
	  prev_sal = find_pc_line (prev_pc, 0);
	  if (prev_sal.line != sal.line)
	    {
	      rollback_to_checkpoint (cp);
	      return;
	    }
	  cp = prev;
	  prev = cp->immediate_prev;
	  if (cp == prev)
	    {
	      printf("weird...");
	      return;
	    }
	}
      printf ("no prev line found\n");
    }
  else
    printf("no cp?\n");
}

static void
reverse_stepi_command (char *args, int from_tty)
{
  if (current_checkpoint == NULL)
    {
      printf ("no cp!\n");
      return;
    }

  if (current_checkpoint->immediate_prev == NULL)
    {
      /* re-execute to find old state */
    }

  if (current_checkpoint->immediate_prev)
    rollback_to_checkpoint (current_checkpoint->immediate_prev);
  else
    printf("no cp?\n");
}
#endif

/* Clear out all accumulated checkpoint stuff.  */

void
clear_all_checkpoints ()
{
  struct checkpoint *cp;

  for (cp = checkpoint_list; cp != NULL; cp = cp->next)
    {
      delete_checkpoint (cp);
    }
  checkpoint_list = last_checkpoint = NULL;
  current_checkpoint = NULL;
  checkpoint_count = 1;
}

void
_initialize_checkpoint (void)
{
  add_com ("create-checkpoint", class_obscure, create_checkpoint_command,
	   "create a checkpoint");
  add_com_alias ("cc", "create-checkpoint", class_obscure, 1);
  add_com ("rollback", class_obscure, rollback_to_checkpoint_command,
	   "roll back to a checkpoint");
  add_com ("undo", class_obscure, undo_command,
	   "back to last checkpoint");
  add_com ("redo", class_obscure, redo_command,
	   "forward to next checkpoint");
  add_com ("now", class_obscure, now_command,
	   "go to latest original execution line");
#if 0 /* commented out until working on again */
  add_com ("rs", class_obscure, reverse_step_command,
	   "reverse-step by one line");
  add_com ("rsi", class_obscure, reverse_stepi_command,
	   "reverse-step by one instruction");
  add_com ("rx", class_obscure, re_execute_command,
	   "execute up to a given place");
#endif

  add_cmd ("checkpoints", class_obscure, delete_checkpoint_command, _("\
Delete specified checkpoints.\n\
Arguments are checkpoint numbers, separated by spaces.\n\
No argument means delete all checkpoints."),
	   &deletelist);

  add_info ("checkpoints", checkpoints_info, "help");

  add_setshow_boolean_cmd ("checkpointing", class_support, &auto_checkpointing, _("\
Set automatic creation of checkpoints."), _("\
Show automatic creation of checkpoints."), NULL,
			   NULL,
			   NULL,
			   &setlist, &showlist);
}
