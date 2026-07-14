#ifndef NYX_NATIVES_H
#define NYX_NATIVES_H

/* Binds every native (print, clock, len, str, num, type, array/map/string
 * helpers) into vm.globals. Called once from initVM(). */
void registerNatives(void);

#endif
