
compile: yorilib.lib

!INCLUDE "..\config\common.mk"

DEBUG=0

OBJS=\
	 builtin.obj  \
	 cabinet.obj  \
	 call.obj     \
	 cancel.obj   \
	 clip.obj     \
	 cmdline.obj  \
	 color.obj    \
	 cshot.obj    \
	 cvthtml.obj  \
	 cvtrtf.obj   \
	 debug.obj    \
	 dyld.obj     \
	 env.obj      \
	 ep_yori.obj  \
	 filecomp.obj \
	 fileenum.obj \
	 filefilt.obj \
	 fileinfo.obj \
	 fullpath.obj \
	 group.obj    \
	 hash.obj     \
	 hexdump.obj  \
	 iconv.obj    \
	 jobobj.obj   \
	 license.obj  \
	 lineread.obj \
	 list.obj     \
	 malloc.obj   \
	 osver.obj    \
	 recycle.obj  \
	 path.obj     \
	 printf.obj   \
	 printfa.obj  \
	 scut.obj     \
	 select.obj   \
	 string.obj   \
	 strmenum.obj \
	 update.obj   \
	 util.obj     \
	 vt.obj       \

yorilib.lib: $(OBJS)
	@echo $@
	@$(LIB32) $(LIBFLAGS) $(OBJS) /out:yorilib.lib

