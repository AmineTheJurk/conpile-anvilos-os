#ifndef ANVIL_GUI_H
#define ANVIL_GUI_H

#include "types.h"

void gui_run(void);
void gui_term_print(const char* s);
void gui_term_putchar(char c);
void gui_open_terminal(void);
void gui_open_files(const char* path);
void gui_open_sysinfo(void);
void gui_open_editor(const char* path);
void gui_run_anv_popup(const char* path);

void ash_init(void);
void ash_handle_char(char c);
void ash_exec_line(const char* line);
void ash_get_cwd(char* out, size_t sz);

#endif
