
BINARIES=lsof.exe

!INCLUDE "..\config\common.mk"

!IF $(PDB)==1
CFLAGS=$(CFLAGS) /Fdlsof.pdb
LINKPDB=/Pdb:lsof.pdb
!ENDIF

CFLAGS=$(CFLAGS) -DLSOF_VER_MAJOR=$(LSOF_VER_MAJOR) -DLSOF_VER_MINOR=$(LSOF_VER_MINOR)

BIN_OBJS=\
	 lsof.obj         \

MOD_OBJS=\
	 mod_lsof.obj     \

compile: $(BIN_OBJS) builtins.lib

lsof.exe: $(BIN_OBJS) 
	@echo $@
	@$(LINK) $(LDFLAGS) -entry:$(YENTRY) $(BIN_OBJS) $(LIBS) $(CRTLIB) ..\lib\yorilib.lib -version:$(LSOF_VER_MAJOR).$(LSOF_VER_MINOR) $(LINKPDB) -out:$@

mod_lsof.obj: lsof.c
	@echo $@
	@$(CC) -c -DYORI_BUILTIN=1 $(CFLAGS) -Fo$@ lsof.c

builtins.lib: $(MOD_OBJS)
	@echo $@
	@$(LIB32) $(LIBFLAGS) $(MOD_OBJS) -out:$@
