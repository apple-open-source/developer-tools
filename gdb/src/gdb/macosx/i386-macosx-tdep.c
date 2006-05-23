/* Mac OS X support for GDB, the GNU debugger.
   Copyright 1997, 1998, 1999, 2000, 2001, 2002, 2005
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
#include "frame.h"
#include "inferior.h"
#include "gdbcore.h"
#include "target.h"
#include "floatformat.h"
#include "symtab.h"
#include "regcache.h"
#include "i387-tdep.h"
#include "i386-tdep.h"
#include "osabi.h"
#include "ui-out.h"
#include "symtab.h"
#include "frame.h"

#include "i386-macosx-thread-status.h"

#include <mach/thread_status.h>

#include "i386-macosx-tdep.h"

/* Unused on the x86 side of things -- this is only needed to
   help distinguish between ppc32 and ppc64 binaries on the PPC side. */

enum gdb_osabi osabi_seen_in_attached_dyld = GDB_OSABI_UNKNOWN;

extern int backtrace_past_main;

#define supply_unsigned_int(regnum, val)\
store_unsigned_integer (buf, 4, val); \
regcache_raw_supply (current_regcache, regnum, buf);

#define collect_unsigned_int(regnum, addr)\
regcache_raw_collect (current_regcache, regnum, buf); \
(* (addr)) = extract_unsigned_integer (buf, 4);

void
i386_macosx_fetch_gp_registers (gdb_i386_thread_state_t *sp_regs)
{
  gdb_byte buf[4];
  supply_unsigned_int (0, sp_regs->eax);
  supply_unsigned_int (1, sp_regs->ecx);
  supply_unsigned_int (2, sp_regs->edx);
  supply_unsigned_int (3, sp_regs->ebx);
  supply_unsigned_int (4, sp_regs->esp);
  supply_unsigned_int (5, sp_regs->ebp);
  supply_unsigned_int (6, sp_regs->esi);
  supply_unsigned_int (7, sp_regs->edi);
  supply_unsigned_int (8, sp_regs->eip);
  supply_unsigned_int (9, sp_regs->efl);
  supply_unsigned_int (10, sp_regs->cs);
  supply_unsigned_int (11, sp_regs->ss);
  supply_unsigned_int (12, sp_regs->ds);
  supply_unsigned_int (13, sp_regs->es);
  supply_unsigned_int (14, sp_regs->fs);
  supply_unsigned_int (15, sp_regs->gs);
}

void
i386_macosx_store_gp_registers (gdb_i386_thread_state_t *sp_regs)
{
  unsigned char buf[4];
  collect_unsigned_int (0, &sp_regs->eax);
  collect_unsigned_int (1, &sp_regs->ecx);
  collect_unsigned_int (2, &sp_regs->edx);
  collect_unsigned_int (3, &sp_regs->ebx);
  collect_unsigned_int (4, &sp_regs->esp);
  collect_unsigned_int (5, &sp_regs->ebp);
  collect_unsigned_int (6, &sp_regs->esi);
  collect_unsigned_int (7, &sp_regs->edi);
  collect_unsigned_int (8, &sp_regs->eip);
  collect_unsigned_int (9, &sp_regs->efl);
  collect_unsigned_int (10, &sp_regs->cs);
  collect_unsigned_int (11, &sp_regs->ss);
  collect_unsigned_int (12, &sp_regs->ds);
  collect_unsigned_int (13, &sp_regs->es);
  collect_unsigned_int (14, &sp_regs->fs);
  collect_unsigned_int (15, &sp_regs->gs);
}

/* Fetching the the registers from the inferior into our reg cache.
   FP_REGS is a structure that mirrors the Mach structure
   struct i386_float_state.  The "hw_fu_state" buffer inside that
   structure mirrors the Mach struct i386_fx_save, which is identical
   to the FXSAVE/FXRSTOR instructions' format.  */

void
i386_macosx_fetch_fp_registers (gdb_i386_thread_fpstate_t *fp_regs)
{
  i387_swap_fxsave (current_regcache, &fp_regs->hw_fu_state);
  i387_supply_fxsave (current_regcache, -1, &fp_regs->hw_fu_state);
}

/* Get the floating point registers from our local register cache
   and stick them in FP_REGS in for sending to the inferior via a
   syscall.  If the local register cache has valid FP values, this
   function returns 1.  If the local register cache does not have
   valid FP values -- and so FP_REGS should not be pushed into the
   inferior -- this function returns 0.  */

int
i386_macosx_store_fp_registers (gdb_i386_thread_fpstate_t *fp_regs)
{
  memset (fp_regs, 0, sizeof (gdb_i386_thread_fpstate_t));
  fp_regs->fpkind = GDB_i386_FP_SSE2; /* Corresponds to Mach's FP_FXSR */
  fp_regs->initialized = 1;
  fp_regs->exc_status = 0;
  i387_fill_fxsave ((unsigned char *) &fp_regs->hw_fu_state, -1);
  i387_swap_fxsave (current_regcache, &fp_regs->hw_fu_state);

  return 1;
}

static CORE_ADDR
i386_macosx_thread_state_addr_1 (CORE_ADDR start_of_func, CORE_ADDR pc,
                                 CORE_ADDR ebp, CORE_ADDR esp);

/* On entry to _sigtramp, the ESP points to the start of a
   'struct sigframe' (cf xnu's bsd/dev/i386/unix_signal.c).
   The 6th word in the struct sigframe is a 'struct ucontext *'.

   struct ucontext (cf sys/_types.h)'s 7th word is a 
   __darwin_size_t uc_mcsize to indicate the size of the
   saved machine context, and the 8th word is a struct mcontext *
   pointing to the saved context structure.

   struct mcontext (cf i386/ucontext.h) has three structures in it,
     i386_exception_state_t
     i386_thread_state_t
     i386_float_state_t
   this function has to return the address of the struct i386_thread_state
   (defined in <mach/i386/thread_status.h>) inside the struct mcontext.  */

static CORE_ADDR
i386_macosx_thread_state_addr (struct frame_info *frame)
{
  gdb_byte buf[4];
  CORE_ADDR esp, ebp;
  frame_unwind_register (frame, I386_ESP_REGNUM, buf);
  esp = extract_unsigned_integer (buf, 4);
  frame_unwind_register (frame, I386_EBP_REGNUM, buf);
  ebp = extract_unsigned_integer (buf, 4);
  return i386_macosx_thread_state_addr_1 (get_frame_func (frame), 
                                          get_frame_pc (frame), ebp, esp);
}

static CORE_ADDR
i386_macosx_thread_state_addr_1 (CORE_ADDR start_of_func, CORE_ADDR pc,
                                 CORE_ADDR ebp, CORE_ADDR esp)
{
  int offset = 0;
  CORE_ADDR push_ebp_addr = 0;
  CORE_ADDR mov_esp_ebp_addr = 0;
  CORE_ADDR address_of_struct_sigframe;
  CORE_ADDR address_of_struct_ucontext;
  CORE_ADDR address_of_struct_mcontext;
  int limit;

  /* We begin our function with a fun little hand-rolled prologue parser.
     These sorts of things NEVER come back to bite us years down the road,
     no sir-ee bob.  The only saving grace is that _sigtramp() is a tough
     function to screw up as it stands today.  Oh, and if we get this wrong,
     signal backtraces should break outright and we get nice little testsuite 
     failures. */

  limit = min (pc - start_of_func + 1, 16);
  while (offset < limit)
    {
      if (!push_ebp_addr)
        {
          /* push   %ebp   [ 0x55 ] */
          if (read_memory_unsigned_integer (start_of_func + offset, 1) == 0x55)
            {
              push_ebp_addr = start_of_func + offset;
              offset++;
            }
          else
            {
              /* If this isn't the push %ebp, and we haven't seen push %ebp yet,
                 skip whatever insn we're sitting on and keep looking for 
                 push %ebp.  It must occur before mov %esp, %ebp.  */
              offset++;
              continue;
            }
        }

      /* We've already seen push %ebp */
      /* Look for mov %esp, %ebp  [ 0x89 0xe5 || 0x8b 0xec ] */
      if (read_memory_unsigned_integer (start_of_func + offset, 2) == 0xe589
          || read_memory_unsigned_integer (start_of_func + offset, 2) == 0xec8b)
        {
          mov_esp_ebp_addr = start_of_func + offset;
          break;
        }
      offset++;  /* I'm single byte stepping through unknown instructions.  
                    SURELY this won't cause an improper match, cough cough. */
    }
  if (!push_ebp_addr || !mov_esp_ebp_addr)
    error ("Unable to analyze the prologue of _sigtramp(), giving up.");

  if (pc <= push_ebp_addr)
    address_of_struct_sigframe = esp + 0;

  if (pc > push_ebp_addr && pc <= mov_esp_ebp_addr)
    address_of_struct_sigframe = esp + 4;

  if (pc > mov_esp_ebp_addr)
    address_of_struct_sigframe = ebp + 4;

  address_of_struct_ucontext = read_memory_unsigned_integer 
                                (address_of_struct_sigframe + 20, 4);

  /* the element 'uc_mcontext' -- the pointer to the struct sigcontext -- 
     is 28 bytes into the 'struct ucontext' */
  address_of_struct_mcontext = read_memory_unsigned_integer 
                                (address_of_struct_ucontext + 28, 4); 

  return address_of_struct_mcontext + 12;
}

/* Offsets into the struct i386_thread_state where we'll find the saved regs. */
/* From <mach/i386/thread_status.h and i386-tdep.h */
static int i386_macosx_thread_state_reg_offset[] =
{
   0 * 4,   /* EAX */
   2 * 4,   /* ECX */
   3 * 4,   /* EDX */
   1 * 4,   /* EBX */
   7 * 4,   /* ESP */
   6 * 4,   /* EBP */
   5 * 4,   /* ESI */
   4 * 4,   /* EDI */
  10 * 4,   /* EIP */
   9 * 4,   /* EFLAGS */
  11 * 4,   /* CS */
   8,       /* SS */
  12 * 4,   /* DS */
  13 * 4,   /* ES */
  14 * 4,   /* FS */
  15 * 4    /* GS */
};

static CORE_ADDR
i386_integer_to_address (struct gdbarch *gdbarch, struct type *type, 
                         const gdb_byte *buf)
{
  gdb_byte *tmp = alloca (TYPE_LENGTH (builtin_type_void_data_ptr));
  LONGEST val = unpack_long (type, buf);
  store_unsigned_integer (tmp, TYPE_LENGTH (builtin_type_void_data_ptr), val);
  return extract_unsigned_integer (tmp,
                                   TYPE_LENGTH (builtin_type_void_data_ptr));
}

static void
i386_macosx_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* We support the SSE registers.  */
  tdep->num_xmm_regs = I386_NUM_XREGS - 1;
  set_gdbarch_num_regs (gdbarch, I386_SSE_NUM_REGS);

  set_gdbarch_skip_trampoline_code (gdbarch, macosx_skip_trampoline_code);

  set_gdbarch_in_solib_return_trampoline (gdbarch,
                                          macosx_in_solib_return_trampoline);

  tdep->struct_return = reg_struct_return;

  tdep->sigcontext_addr = i386_macosx_thread_state_addr;
  tdep->sc_reg_offset = i386_macosx_thread_state_reg_offset;
  tdep->sc_num_regs = 16;

  tdep->jb_pc_offset = 20;
  set_gdbarch_integer_to_address (gdbarch, i386_integer_to_address);
}

static enum gdb_osabi
i386_mach_o_osabi_sniffer (bfd *abfd)
{
  if (strcmp (bfd_get_target (abfd), "mach-o-le") == 0
      || strcmp (bfd_get_target (abfd), "mach-o-fat") == 0)
    return GDB_OSABI_DARWIN;

  return GDB_OSABI_UNKNOWN;
}


/*
 * This is set to the FAST_COUNT_STACK macro for i386.  The return value
 * is 1 if no errors were encountered traversing the stack, and 0 otherwise.
 * It sets COUNT to the stack depth.  If SHOW_FRAMES is 1, then it also
 * emits a list of frame info bits, with the pc & fp for each frame to
 * the current UI_OUT.  If GET_NAMES is 1, it also emits the names for
 * each frame (though this slows the function a good bit.)
 */

/*
 * COUNT_LIMIT parameter sets a limit on the number of frames that
 * will be counted by this function.  -1 means unlimited.
 *
 * PRINT_LIMIT parameter sets a limit on the number of frames for
 * which the full information is printed.  -1 means unlimited.
 *
 */

int
i386_fast_show_stack (int show_frames, int get_names,
                     unsigned int count_limit, unsigned int print_limit,
                     unsigned int *count,
                     void (print_fun) (struct ui_out * uiout, int frame_num,
                                       CORE_ADDR pc, CORE_ADDR fp))
{
  CORE_ADDR fp, prev_fp;
  static CORE_ADDR sigtramp_start = 0;
  static CORE_ADDR sigtramp_end = 0;
  struct frame_info *fi = NULL;
  int i = 0;
  int err = 0;
  ULONGEST next_fp = 0;
  ULONGEST pc = 0;

  if (sigtramp_start == 0)
    {
      char *name;
      struct minimal_symbol *msymbol;

      msymbol = lookup_minimal_symbol ("_sigtramp", NULL, NULL);
      if (msymbol == NULL)
        warning
          ("Couldn't find minimal symbol for \"_sigtramp\" - backtraces may be unreliable");
      else
        {
          pc = SYMBOL_VALUE_ADDRESS (msymbol);
          if (find_pc_partial_function (pc, &name,
                                        &sigtramp_start, &sigtramp_end) == 0)
            {
              err = 1;
              goto i386_count_finish;
            }
        }
    }

  /* Get the first two frames.  If anything funky is going on, it will
     be here.  The second frame helps us get above frameless functions
     called from signal handlers.  Above these frames we have to deal
     with sigtramps and alloca frames, that is about all. */

  if (show_frames)
    ui_out_begin (uiout, ui_out_type_list, "frames");

  i = 0;
  if (i >= count_limit)
    goto i386_count_finish;

  fi = get_current_frame ();
  if (fi == NULL)
    {
      err = 1;
      goto i386_count_finish;
    }

  if (show_frames && print_fun && (i < print_limit))
    print_fun (uiout, i, get_frame_pc (fi), get_frame_base (fi));
  i = 1;

  do
    {
      if (i >= count_limit)
        goto i386_count_finish;

      fi = get_prev_frame (fi);
      if (fi == NULL)
        goto i386_count_finish;

      prev_fp = fp;
      pc = get_frame_pc (fi);
      fp = get_frame_base (fi);

      if (show_frames && print_fun && (i < print_limit))
        print_fun (uiout, i, pc, fp);

      i++;

      if (!backtrace_past_main && inside_main_func (fi))
        goto i386_count_finish;
    }
  while (i < 5);

  if (i >= count_limit)
    goto i386_count_finish;

  /* gdb's idea of a stack frame is 8 bytes off from the actual
     values of EBP (gdb probably includes the saved esp and saved
     eip as part of the frame).  So pull off 8 bytes from the 
     "fp" to get an actual EBP value for walking the stack.  */

  fp = fp - 8;
  prev_fp = prev_fp - 8;
  while (1)
    {
      if ((sigtramp_start <= pc) && (pc <= sigtramp_end))
        {
          CORE_ADDR thread_state_at = 
                    i386_macosx_thread_state_addr_1 (sigtramp_start, pc, 
                                                     fp, prev_fp + 8);
          prev_fp = fp;
          if (!safe_read_memory_unsigned_integer (thread_state_at + 
                          i386_macosx_thread_state_reg_offset[I386_EBP_REGNUM], 
                           4, &fp))
            goto i386_count_finish;
          if (!safe_read_memory_unsigned_integer (thread_state_at + 
                          i386_macosx_thread_state_reg_offset[I386_EIP_REGNUM], 
                           4, &pc))
            goto i386_count_finish;
        }
      else
        {
          if (!safe_read_memory_unsigned_integer (fp, 4, &next_fp))
            goto i386_count_finish;
          if (next_fp == 0)
            goto i386_count_finish;
	  else if (fp == next_fp)
	    {
	      /* This shouldn't ever happen, but if it does we will
		 loop forever here, so protect against that.  */
	      warning ("Frame pointer point back at the previous frame");
	      err = 1;
	      goto i386_count_finish;
	    }
          if (!safe_read_memory_unsigned_integer (fp + 4, 4, &pc))
            goto i386_count_finish;
          prev_fp = fp;
          fp = next_fp;
        }

      /* Add 8 to the EBP to show the frame pointer as gdb likes
         to show it.  */

      if (show_frames && print_fun && (i < print_limit))
        print_fun (uiout, i, pc, fp + 8);
      i++;

      if (!backtrace_past_main && addr_inside_main_func (pc))
        goto i386_count_finish;

      if (i >= count_limit)
        goto i386_count_finish;
    }

i386_count_finish:
  if (show_frames)
    ui_out_end (uiout, ui_out_type_list);

  *count = i;
  return (!err);
}

void
_initialize_i386_macosx_tdep (void)
{
  gdbarch_register_osabi_sniffer (bfd_arch_unknown, bfd_target_mach_o_flavour,
                                  i386_mach_o_osabi_sniffer);

  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_DARWIN,
                          i386_macosx_init_abi);

}
