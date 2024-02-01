#######################################
# VERSION 1
# #####       Manual:             #####
#
# 	EXE_NAME=my_prj
# 	SOURCES += foo.c/xx/pp
# 	SOURCES += $(wildcard, path/.c)
# 	SOURCES += $(call rwildcard,.,*.cpp)
# 	INCDIR += $(dir ($(call rwildcard,.,*.h)))
# 	MAKE_EXECUTABLE=yes
# 	MAKE_BINARY=no
# 	MAKE_SHARED_LIB=yes
# 	MAKE_STATIC_LIB=yes
#   PPDEFS=STM32 VAR0=12
# 
# 	BUILDDIR = build$(TCHAIN)
# 
#   EXT_LIBS+=setupapi
#   LIBDIR+=
#
# 	FOREIGN_MAKE_TARGETS=extlib/build/libext.so
#
# #####       Other: 		      #####
# 	TCHAIN = x86_64-w64-mingw32-
#   LDFLAGS += -static (on win c++ mingw)
# 	CREATE_MAP=yes
# 	CREATE_LST=yes
# 	COLORIZE=yes
# 	VERBOSE=yes
#	.prebuild:
#	$(SOURCES): dependencies
#######################################

#######################################
# build tool's names
#######################################
CC=$(TCHAIN)gcc
CXX=$(TCHAIN)g++
OC=$(TCHAIN)objcopy
AR=$(TCHAIN)ar
RL=$(TCHAIN)ranlib
LST=$(TCHAIN)objdump
rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

#######################################
# color & verbose settings
#######################################
ifeq ($(strip $(COLORIZE)),yes)
	CLRED=\e[31m
	CLGRN=\e[32m
	CLYEL=\e[33m
	CLRST=\e[0m
	VECHO_=printf
else
	CLRED=
	CLGRN=
	CLYEL=
	CLRST=
	VECHO_=printf
endif

ifneq ($(VERBOSE),yes)
	Q:=@
	VECHO=@$(VECHO_)
else
	Q:= 
	VECHO=@true
endif

#######################################
# manage output files suffixes
#######################################
ifeq ($(SHARED_LIB_EXT),)
	SHARED_LIB_EXT=.so
else
	SHARED_LIB_EXT:=.$(SHARED_LIB_EXT)
endif

ifeq ($(STATIC_LIB_EXT),)
	STATIC_LIB_EXT=.a
else
	STATIC_LIB_EXT:=.$(STATIC_LIB_EXT)
endif

ifneq ($(EXE_EXT),)
	EXE_EXT:=.$(EXE_EXT)
endif

ifeq ($(EXE_NAME),)
	EXE_NAME=program
endif

ifeq ($(BUILDDIR),)
	BUILDDIR=build
endif

ifeq ($(MAKE_BINARY),)
	ifeq ($(MAKE_EXECUTABLE),)
		ifeq ($(MAKE_SHARED_LIB),)
			ifeq ($(MAKE_STATIC_LIB),)
				MAKE_EXECUTABLE:=yes
			endif
		endif
	endif
endif

OBJDIR:=$(BUILDDIR)/obj
UPDIR:=_updir_

EXT_OBJECTS+=$(FOREIGN_MAKE_TARGETS)

#######################################
# C/C++ flags tuning
#######################################
FLAGS=-c -Wall

ifneq ($(CDIALECT),)
FLAGS+=-O$(OPT_LVL)
endif

FLAGS+=$(DBG_OPTS)
FLAGS+=-MMD -MP 
FLAGS+=$(MCPU) $(addprefix -I,$(INCDIR)) $(addprefix -D,$(PPDEFS)) 

ifneq ($(CDIALECT),)
CFLAGS+=-std=$(CDIALECT)
endif

ifneq ($(CXXDIALECT),)
CXXFLAGS+=-std=$(CXXDIALECT)
endif

LDFLAGS+=$(MCPU) 
ifeq ($(strip $(CREATE_MAP)),yes)
LDFLAGS+=-Wl,-Map=$(BUILDDIR)/$(EXE_NAME).map
endif
LDFLAGS+=$(addprefix -T,$(LDSCRIPT))

#######################################
# sort sources & generate object names
#######################################
INCDIR:=$(sort $(foreach _HEADERS, $(INCDIR), $(_HEADERS)))

CXX_SOURCES:=$(filter %.cpp %.cxx %.c++ %.cc %.C, $(SOURCES))
CXX_OBJECTS:=$(CXX_SOURCES:.cpp=.o)
CXX_OBJECTS:=$(CXX_OBJECTS:.cxx=.o)
CXX_OBJECTS:=$(CXX_OBJECTS:.c++=.o)
CXX_OBJECTS:=$(CXX_OBJECTS:.cc=.o)
CXX_OBJECTS:=$(CXX_OBJECTS:.C=.o)

C_SOURCES:=$(filter %.c, $(SOURCES))
C_OBJECTS:=$(C_SOURCES:.c=.o)

S_SOURCES:=$(filter %.s %.S, $(SOURCES))
S_OBJECTS:=$(S_SOURCES:.s=.o)
S_OBJECTS:=$(S_OBJECTS:.S=.o)

CXX_OBJECTS:=$(subst ..,$(UPDIR),$(addprefix $(OBJDIR)/, $(CXX_OBJECTS)))
C_OBJECTS:=$(subst ..,$(UPDIR),$(addprefix $(OBJDIR)/, $(C_OBJECTS)))
S_OBJECTS:=$(subst ..,$(UPDIR),$(addprefix $(OBJDIR)/, $(S_OBJECTS)))

#######################################
# decide which linker to use C or C++
#######################################
ifeq ($(strip $(CXX_SOURCES)),)
	LD=$(CC)
else
	LD=$(CXX)
endif

#######################################
# final list of objects
#######################################
LINK_OBJECTS=$(S_OBJECTS) $(C_OBJECTS) $(CXX_OBJECTS) $(EXT_OBJECTS)

#######################################
# list of desired artefacts
#######################################
EXECUTABLE:=$(BUILDDIR)/$(EXE_NAME)$(EXE_EXT)
BINARY:=$(BUILDDIR)/$(basename $(EXE_NAME)).bin
SHARED_LIB:=$(BUILDDIR)/$(basename lib$(EXE_NAME))$(SHARED_LIB_EXT)
STATIC_LIB:=$(BUILDDIR)/$(basename lib$(EXE_NAME))$(STATIC_LIB_EXT)
LISTING:=$(BUILDDIR)/$(basename $(EXE_NAME)).lst

ifeq ($(strip $(MAKE_BINARY)),yes)
	ARTEFACTS+=$(BINARY)
endif
ifeq ($(strip $(MAKE_EXECUTABLE)),yes)
	ARTEFACTS+=$(EXECUTABLE)
endif

ifeq ($(strip $(MAKE_SHARED_LIB)),yes)
	ARTEFACTS+=$(SHARED_LIB)
	CFLAGS+=-fpic
endif

ifeq ($(strip $(MAKE_STATIC_LIB)),yes)
	ARTEFACTS+=$(STATIC_LIB)
endif

ifeq ($(strip $(CREATE_LST)),yes)
	ARTEFACTS+=$(LISTING)
endif

#######################################
# CLEAN
#######################################
.PHONY: all clean debug-make

all: $(ARTEFACTS)

debug-make:
	@echo "C:        " $(C_SOURCES)
	@echo "CXX:      " $(CXX_SOURCES)
	@echo "S:        " $(S_SOURCES)
	@echo ""
	@echo "Co:       " $(C_OBJECTS)
	@echo "CXXo:     " $(CXX_OBJECTS)
	@echo "So:       " $(S_OBJECTS)
	@echo ""
	@echo "Lnk Obj:  " $(LINK_OBJECTS)
	@echo "Artefacts:" $(ARTEFACTS)
	@echo ""
	@echo "Sources:  " $(SOURCES)
	@echo "INCDIR:   " $(INCDIR)

ifneq ($(FOREIGN_MAKE_TARGETS),)
clean: clean_foreign_targets
endif

clean:
	$(Q)rm -fr $(BUILDDIR)

$(LINK_OBJECTS): Makefile

#######################################
# assembler targets
#######################################
.SECONDEXPANSION:
$(OBJDIR)/%.o: $$(subst $(UPDIR),..,%.s) $(SELFDEP)
	@mkdir -p $(@D)
	$(VECHO) ' [$(CLGRN)S$(CLRST)]   $< ...\n'
	$(Q)$(CC) $(FLAGS) $(CFLAGS) $< -o $@

.SECONDEXPANSION:
$(OBJDIR)/%.o: $$(subst $(UPDIR),..,%.S) $(SELFDEP)
	@mkdir -p $(@D)
	$(VECHO) ' [$(CLGRN)S$(CLRST)]   $< ...\n'
	$(Q)$(CC) $(FLAGS) $(CFLAGS) $< -o $@

#######################################
# C targets
#######################################
.SECONDEXPANSION:
$(OBJDIR)/%.o: $$(subst $(UPDIR),..,%.c) $(SELFDEP)
	@mkdir -p $(@D)
	$(VECHO) ' [$(CLGRN)C$(CLRST)]   $< ...\n'
	$(Q)$(CC) $(FLAGS) $(CFLAGS) $< -o $@

#######################################
# C++ targets
#######################################
.SECONDEXPANSION:
$(OBJDIR)/%.o: $$(subst $(UPDIR),..,%.cpp) $(SELFDEP)
	@mkdir -p $(@D)
	$(VECHO) ' [$(CLYEL)C++$(CLRST)] $< ...\n'
	$(Q)$(CXX) $(FLAGS) $(CXXFLAGS) $< -o $@

.SECONDEXPANSION:
$(OBJDIR)/%.o: $$(subst $(UPDIR),..,%.C) $(SELFDEP)
	@mkdir -p $(@D)
	$(VECHO) ' [$(CLYEL)C++$(CLRST)] $< ...\n'
	$(Q)$(CXX) $(FLAGS) $(CXXFLAGS) $< -o $@

.SECONDEXPANSION:
$(OBJDIR)/%.o: $$(subst $(UPDIR),..,%.cxx) $(SELFDEP)
	@mkdir -p $(@D)
	$(VECHO) ' [$(CLYEL)C++$(CLRST)] $< ...\n'
	$(Q)$(CXX) $(FLAGS) $(CXXFLAGS) $< -o $@

.SECONDEXPANSION:
$(OBJDIR)/%.o: $$(subst $(UPDIR),..,%.cc) $(SELFDEP)
	@mkdir -p $(@D)
	$(VECHO) ' [$(CLYEL)C++$(CLRST)] $< ...\n'
	$(Q)$(CXX) $(FLAGS) $(CXXFLAGS) $< -o $@

.SECONDEXPANSION:
$(OBJDIR)/%.o: $$(subst $(UPDIR),..,%.c++) $(SELFDEP)
	@mkdir -p $(@D)
	$(VECHO) ' [$(CLYEL)C++$(CLRST)] $< ...\n'
	$(Q)$(CXX) $(FLAGS) $(CXXFLAGS) $< -o $@

#######################################
# Build targets
#######################################
$(EXECUTABLE): $(LINK_OBJECTS) $(BUILDDIR)
	$(VECHO) ' [$(CLRED)L$(CLRST)]   $(CLRED)$@$(CLRST) ...\n'
	$(Q)$(LD) -o $@  $(LDFLAGS) $(subst ..,up,$(LINK_OBJECTS)) $(EXT_OBJECTS) $(addprefix -L,$(LIBDIR)) $(addprefix -l,$(EXT_LIBS)) 

$(BINARY): $(EXECUTABLE) $(BUILDDIR)
	$(VECHO) ' [$(CLRED)B$(CLRST)]   $(CLRED)$@$(CLRST) ...\n'
	$(Q)$(OC) -O binary $< $(@)

$(SHARED_LIB): $(LINK_OBJECTS) $(BUILDDIR)
	$(VECHO)  ' [$(CLRED)L$(CLRST)]   $(CLRED)$@$(CLRST) ...\n'
	$(Q)$(LD) -shared -o $@  $(LDFLAGS) $(subst ..,up,$(LINK_OBJECTS)) $(EXT_OBJECTS) $(addprefix -L,$(LIBDIR)) $(addprefix -l,$(EXT_LIBS)) 

$(STATIC_LIB): $(LINK_OBJECTS) $(BUILDDIR)
	$(VECHO)  ' [$(CLRED)AR$(CLRST)]  $(CLRED)$@$(CLRST) ...\n'
	$(Q)$(AR) -rc $@  $(subst ..,up,$(LINK_OBJECTS)) $(EXT_OBJECTS) 
	$(VECHO)  ' [$(CLRED)RL$(CLRST)]  $(CLRED)$@$(CLRST) ...\n'
	$(Q)$(RL) $@ 

$(LISTING) : $(EXECUTABLE)
	$(VECHO) ' [$(CLRED)LST$(CLRST)]   $(CLRED)$@$(CLRST) ...\n'
	$(Q)$(LST) -d -t -S $< >$(@)

$(BUILDDIR):
	@mkdir $(BUILDDIR)

#######################################
# Include header dependencies
#######################################
-include $(C_OBJECTS:.o=.d)
-include $(CXX_OBJECTS:.o=.d)

#######################################
# Foreign targets dependencies
#######################################
.PHONY: $(FOREIGN_MAKE_TARGETS)
$(FOREIGN_MAKE_TARGETS):
	$(MAKE) -C $(subst build/,,$(dir $@))

.PHONY: clean_foreign_targets
clean_foreign_targets:
	$(foreach var,$(FOREIGN_MAKE_TARGETS),$(MAKE) -C $(subst build/,,$(dir $(var))) clean;)