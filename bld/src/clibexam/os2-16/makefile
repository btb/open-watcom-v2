compiler  = wcc
cflags    = -zq -bt=os2 -ml -bm -wx
linkflags = sys os2 debug all op q
cdebug    = -d2

!include ../master.mif

all: $(FN_ALL) $(FN_MACRO) $(FN_FILE) $(FN_STRING) $(FN_ALLN) &
     $(FN_DOSCALL) $(FN_THREAD) $(FN_OS2WIN32) $(FN_WQO) $(FN_16BIT) &
     $(FN_SPAWN)

{..\}.c.exe:
    $(compiler) $(cflags) $(cdebug) $<
    wlink $(linkflags) name $* file $*.obj
