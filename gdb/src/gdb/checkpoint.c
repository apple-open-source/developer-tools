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
  if (cp->pid != 0)
    fork_memcache_put (cp);
#endif
}

int checkpoint_count;

int checkpoint_increment = 1;

int last_rollback_number;

struct checkpoint *checkpoint_list;
struct checkpoint *last_checkpoint;

struct checkpoint *active_checkpoint;

struct checkpoint *scratch_checkpoint;

static void
create_checkpoint_command (char *args, int from_tty)
{
  struct checkpoint *cp;

  cp = create_checkpoint ();

  /*  printf("checkpoint %d created\n", cp->number); */
}

struct checkpoint *
create_checkpoint ()
{
  struct checkpoint *cp;

  if (!target_has_execution)
    return NULL;

  cp = (struct checkpoint *) collect_checkpoint ();

  return finish_checkpoint (cp);
}

struct checkpoint *
collect_checkpoint ()
{
  struct checkpoint *cp;
  struct symbol *sym;

  cp = (struct checkpoint *) xmalloc (sizeof (struct checkpoint));
  memset (cp, 0, sizeof (cp));

  cp->regs = regcache_xmalloc (current_gdbarch);
  cp->mem = NULL;

  cp->pid = 0;

  regcache_cpy (cp->regs, current_regcache);

  /* Collect the stack directly.  */
  memcache_get (cp, (ULONGEST) (read_sp () - 2000), 10000);
  memcache_get (cp, (ULONGEST) (read_sp () - 4000), 2000);

  sym = lookup_symbol ("cpfork", 0, VAR_DOMAIN, 0, 0);
  if (sym)
    {
      static struct cached_value *cached_cpfork = NULL;
      struct value *val;
      int retval;

      printf("found cpfork\n");

      if (cached_cpfork == NULL)
	cached_cpfork = create_cached_function ("cpfork", builtin_type_int);

      val = call_function_by_hand_expecting_type (lookup_cached_function (cached_cpfork),
						  builtin_type_int, 0, NULL, 1);
  
      retval = value_as_long (val);

      printf ("returned %d\n", retval);

      /* Keep the pid around, only dig through fork when rolling back.  */
      cp->pid = retval;

    }
  else
    {
      sym = lookup_symbol ("NXArgc", 0, VAR_DOMAIN, 0, 0);

      if (sym)
	memcache_get (cp, (ULONGEST) SYMBOL_VALUE_ADDRESS (sym), 10000);
    }

  return cp;
}

struct checkpoint *
finish_checkpoint (struct checkpoint *cp)
{
  checkpoint_count += checkpoint_increment;
  cp->number = checkpoint_count;

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

  /*  print_checkpoint_info (cp); */

  return cp;
}

int
checkpoint_compare (struct checkpoint *cp1, struct checkpoint *cp2)
{
  if (regcache_compare (cp1->regs, cp2->regs) == 0)
    return 0;

  return 1;
}

int collecting_checkpoint = 0;

int auto_checkpointing = 0;

void
maybe_create_checkpoint ()
{
  struct checkpoint *tmpcp, *lastcp;

  if (!auto_checkpointing)
    return;

  if (collecting_checkpoint)
    return;
  collecting_checkpoint = 1;

  lastcp = active_checkpoint;

  tmpcp = collect_checkpoint ();

#if 0 /* used for re-execution */
  for (cp = checkpoint_list; cp != NULL; cp = cp->next)
    if (checkpoint_compare (cp, tmpcp))
      {
	active_checkpoint = cp;
	if (rx_cp && lastcp)
	  active_checkpoint->immediate_prev = lastcp;
	return;
      }
#endif

  active_checkpoint = finish_checkpoint (tmpcp);

  if ((rx_cp && lastcp) || step_range_end == 1)
    active_checkpoint->immediate_prev = lastcp;

  collecting_checkpoint = 0;
}

static void
rollback_to_checkpoint_command (char *args, int from_tty)
{
  char *p;
  int num = 1, sgn = 1;
  struct checkpoint *cp;

  if (!target_has_execution)
    {
      printf ("No rolling back now!\n");
      return;
    }

  if (args)
    {
      p = args;
      if (*p == '-')
	{
	  sgn = -1;
	  ++p;
	}
      num = atoi (p) * sgn;
    }

  if (num == -1)
    {
      if (active_checkpoint && active_checkpoint->prev)
	num = active_checkpoint->prev->number;
    }

  cp = find_checkpoint (num);

  if (cp == NULL)
    {
      printf ("checkpoint %d not found\n", num);
      return;
    }

  rollback_to_checkpoint (cp);
}

void
rollback_to_checkpoint (struct checkpoint *cp)
{
  regcache_cpy (current_regcache, cp->regs);

  memcache_put (cp);

  last_rollback_number = cp->number;

  active_checkpoint = cp;

  /* The following is conceptually similar to normal_stop() behavior.  */
  /*
  deprecated_update_frame_pc_hack (get_current_frame (), read_pc ());

  select_frame (get_current_frame ());
  */
  normal_stop ();

  /* step_once (0, 1, 1); */

  /*   print_stack_frame (get_selected_frame (NULL), 0, SRC_AND_LOC); */

  /*  do_displays (); */

}

struct checkpoint *
find_checkpoint (int num)
{
  struct checkpoint *cp;

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
  printf ("%c%d: pc=0x%llx",
	  (active_checkpoint == cp ? '*' : ' '),
	  cp->number, pc);
  if (cp->immediate_prev)
    {
      printf (" (%d<-)", cp->immediate_prev->number);
    }

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

void
clear_checkpoints ()
{
  checkpoint_list = last_checkpoint = NULL;
  active_checkpoint = NULL;
  checkpoint_count = 0;
  last_rollback_number = 0;
}

/* to infcmd eventually */

static void
undo_command (char *args, int from_tty)
{
  rollback_to_checkpoint_command ("-1", from_tty);
}

static void
reverse_step_command (char *args, int from_tty)
{
  struct checkpoint *cp, *prev;
  gdb_byte reg_buf[MAX_REGISTER_SIZE];
  LONGEST pc, prev_pc;
  struct symtab_and_line sal, prev_sal;

  if (active_checkpoint == NULL)
    {
      printf ("no cp!\n");
      return;
    }

  if (active_checkpoint->immediate_prev == NULL)
    {
      /* re-execute to find old state */
    }

  if (active_checkpoint->immediate_prev)
    {
      cp = active_checkpoint->immediate_prev;
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
  if (active_checkpoint == NULL)
    {
      printf ("no cp!\n");
      return;
    }

  if (active_checkpoint->immediate_prev == NULL)
    {
      /* re-execute to find old state */
    }

  if (active_checkpoint->immediate_prev)
    rollback_to_checkpoint (active_checkpoint->immediate_prev);
  else
    printf("no cp?\n");
}

void
_initialize_checkpoint (void)
{
  add_com ("cc", class_obscure, create_checkpoint_command,
	   "create a checkpoint");
  add_com ("rollback", class_obscure, rollback_to_checkpoint_command,
	   "roll back to a checkpoint");
  add_com ("undo", class_obscure, undo_command,
	   "back to last checkpoint");
  add_com ("rs", class_obscure, reverse_step_command,
	   "reverse-step by one line");
  add_com ("rsi", class_obscure, reverse_stepi_command,
	   "reverse-step by one instruction");
  add_com ("rx", class_obscure, re_execute_command,
	   "execute up to a given place");

  add_info ("checkpoints", checkpoints_info, "help");

  add_setshow_boolean_cmd ("checkpointing", class_support, &auto_checkpointing, _("\
Set automatic creation of checkpoints."), _("\
Show automatic creation of checkpoints."), NULL,
			   NULL,
			   NULL,
			   &setlist, &showlist);
}
