
BINARIES=yui.exe yuishook.dll

!INCLUDE "..\config\common.mk"

!IF $(PDB)==1
CFLAGS=$(CFLAGS) /Fdyui.pdb
LINKPDB=/Pdb:yui.pdb
LINKPDB_YUISHOOK=/Pdb:yuishook.pdb
!ENDIF

CFLAGS=$(CFLAGS) -DYUI_VER_MAJOR=$(YUI_VER_MAJOR) -DYUI_VER_MINOR=$(YUI_VER_MINOR)
LDFLAGS_NT4UP=$(LDFLAGS:3.10=4.0)
LIBS=$(LIBS) user32.lib gdi32.lib comdlg32.lib

BIN_OBJS=\
	 menu.obj        \
	 resource.obj    \
	 taskbar.obj     \
	 yui.obj         \

YUISHOOK_OBJS=\
	 yuishook.obj    \

compile: $(BIN_OBJS) $(YUISHOOK_OBJS)

yui.exe: $(BIN_OBJS) 
	@echo $@
	@$(LINK) $(LDFLAGS_NT4UP) -entry:$(YENTRY) $(BIN_OBJS) $(LIBS) $(CRTLIB) ..\lib\yorilib.lib -version:$(YUI_VER_MAJOR).$(YUI_VER_MINOR) $(LINKPDB) -out:$@

yuishook.dll: $(YUISHOOK_OBJS) 
	@echo $@
	@$(LINK) $(LDFLAGS) -dll -def:yuishook.def $(YUISHOOK_OBJS) $(LIBS) $(CRTLIB) ..\lib\yorilib.lib -version:$(YUI_VER_MAJOR).$(YUI_VER_MINOR) $(LINKPDB_YUISHOOK) -out:$@

