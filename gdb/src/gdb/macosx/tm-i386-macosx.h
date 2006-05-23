#ifndef __GDB_TM_i386_MACOSX_H__
#define __GDB_TM_i386_MACOSX_H__


int
i386_fast_show_stack (int show_frames, int get_names,
                     unsigned int count_limit, unsigned int print_limit,
                     unsigned int *count,
                     void (print_fun) (struct ui_out * uiout, int frame_num,
                                       CORE_ADDR pc, CORE_ADDR fp));
#define FAST_COUNT_STACK_DEPTH(show_frames, get_names, count_limit, print_limit, count, print_fun) \
  (i386_fast_show_stack (show_frames, get_names, count_limit, print_limit, count, print_fun))


#endif /* __GDB_TM_i386_MACOSX_H__ */
