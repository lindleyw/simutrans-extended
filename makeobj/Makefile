#
# This file is part of the Simutrans-Extended project under the Artistic License.
# (see LICENSE.txt)
#

CFG ?= default
-include ../config.$(CFG)

OSTYPES       = amiga beos cygwin freebsd haiku linux mingw32 mingw64 mac

ifeq ($(findstring $(OSTYPE), $(OSTYPES)),)
  $(error Unkown OSTYPE "$(OSTYPE)", must be one of "$(OSTYPES)")
endif

PNG_CONFIG ?= pkg-config libpng

ifneq ($(PNG_CONFIG),)
	PNG_CFLAGS  := $(shell $(PNG_CONFIG) --cflags)
	PNG_LDFLAGS := $(shell $(PNG_CONFIG) --libs)
endif

CFLAGS  += $(PNG_CFLAGS)
LDFLAGS += $(PNG_LDFLAGS)

# Absolutely essential
CFLAGS += -DMAKEOBJ
CXXFLAGS += -DMAKEOBJ

STD_LIBS += -lz -lbz2 -lpng

ifeq ($(OSTYPE),cygwin)
  OS_INC   ?= -I/usr/include/mingw
  OS_OPT   ?= -mwin32
  STD_LIBS += -lwinmm
endif

ifeq ($(OSTYPE),mingw32)
  CC ?= gcc
  OS_OPT   ?= -DPNG_STATIC -DZLIB_STATIC -march=pentium
  LDFLAGS += -static-libgcc -static-libstdc++
  # we need the libraries EXACTLY in this order to link
  STD_LIBS = -lmingw32 -lpng -lz
endif

ifeq ($(OSTYPE),mac)
  CFLAGS   += -DUSE_HW -DUSE_C  -Os -fast
  CXXFLAGS   += -DUSE_HW -DUSE_C
endif

ifneq ($(OPTIMISE),)
  CFLAGS   += -O3 -fno-schedule-insns
  CXXFLAGS += -O3 -fno-schedule-insns
  ifneq ($(OSTYPE),mac)
    CFLAGS  += -minline-all-stringops
  endif
else
  CFLAGS   += -O
  CXXFLAGS += -O
endif

ifdef DEBUG
  ifeq ($(shell expr $(DEBUG) \>= 1), 1)
    CFLAGS   += -g -DDEBUG
    CXXFLAGS += -g -DDEBUG
  endif
  ifeq ($(shell expr $(DEBUG) \>= 2), 1)
    CFLAGS   += -fno-inline
    CXXFLAGS += -fno-inline
  endif
  ifeq ($(shell expr $(DEBUG) \>= 3), 1)
    CFLAGS   += -O0
    CXXFLAGS += -O0
  endif
else
  CFLAGS += -DNDEBUG
  CXXFLAGS += -DNDEBUG
endif

ifneq ($(PROFILE),)
  CFLAGS   += -pg -DPROFILE -fno-inline
  CXXFLAGS += -pg -DPROFILE -fno-inline
  LDFLAGS += -pg
endif

CFLAGS   += -Wall -Wextra -Wcast-qual -Wpointer-arith -Wcast-align -Wstrict-prototypes -Wpadded $(OS_INC) $(OS_OPT) $(FLAGS)
CXXFLAGS += -Wall -Wextra -Wcast-qual -Wpointer-arith -Wcast-align -Wpadded -std=c++14 $(OS_INC) $(OS_OPT) $(FLAGS)

# SOLO_SOURCES contains files which are unique to makeobj;
# SHARED_SOURCES contains those with the exact same object code in makeobj and simutrans;
# VARIANT_SOURCES contains those which need different .o files for makeobj and simutrans.
# At the moment they're all treated identically, of course.
SOLO_SOURCES += makeobj.cc
SOLO_SOURCES += ../descriptor/writer/bridge_writer.cc
SOLO_SOURCES += ../descriptor/writer/building_writer.cc
SOLO_SOURCES += ../descriptor/writer/citycar_writer.cc
SOLO_SOURCES += ../descriptor/writer/crossing_writer.cc
SOLO_SOURCES += ../descriptor/writer/factory_writer.cc
SOLO_SOURCES += ../descriptor/writer/get_climate.cc
SOLO_SOURCES += ../descriptor/writer/get_waytype.cc
SOLO_SOURCES += ../descriptor/writer/good_writer.cc
SOLO_SOURCES += ../descriptor/writer/groundobj_writer.cc
SOLO_SOURCES += ../descriptor/writer/ground_writer.cc
SOLO_SOURCES += ../descriptor/writer/image_writer.cc
SOLO_SOURCES += ../descriptor/writer/imagelist2d_writer.cc
SOLO_SOURCES += ../descriptor/writer/imagelist3d_writer.cc
SOLO_SOURCES += ../descriptor/writer/imagelist_writer.cc
SOLO_SOURCES += ../descriptor/writer/obj_node.cc
SOLO_SOURCES += ../descriptor/writer/obj_writer.cc
SOLO_SOURCES += ../descriptor/writer/pedestrian_writer.cc
SOLO_SOURCES += ../descriptor/writer/roadsign_writer.cc
SOLO_SOURCES += ../descriptor/writer/root_writer.cc
SOLO_SOURCES += ../descriptor/writer/sim_writer.cc
SOLO_SOURCES += ../descriptor/writer/skin_writer.cc
SOLO_SOURCES += ../descriptor/writer/sound_writer.cc
SOLO_SOURCES += ../descriptor/writer/text_writer.cc
SOLO_SOURCES += ../descriptor/writer/tree_writer.cc
SOLO_SOURCES += ../descriptor/writer/tunnel_writer.cc
SOLO_SOURCES += ../descriptor/writer/vehicle_writer.cc
SOLO_SOURCES += ../descriptor/writer/way_writer.cc
SOLO_SOURCES += ../descriptor/writer/way_obj_writer.cc
SOLO_SOURCES += ../descriptor/writer/xref_writer.cc
SOLO_SOURCES += ../descriptor/writer/pier_writer.cc
SOLO_SOURCES += ../utils/dr_rdpng.cc
SHARED_SOURCES += ../descriptor/image.cc
SHARED_SOURCES += ../dataobj/freelist.cc
SHARED_SOURCES += ../simdebug.cc
SHARED_SOURCES += ../simmem.cc
SHARED_SOURCES += ../utils/simstring.cc
SHARED_SOURCES += ../utils/searchfolder.cc
SHARED_SOURCES += ../utils/float32e8_t.cc
VARIANT_SOURCES += ../utils/simrandom.cc
VARIANT_SOURCES += ../dataobj/tabfile.cc
VARIANT_SOURCES += ../utils/log.cc

SOURCES ?= $(SOLO_SOURCES) $(SHARED_SOURCES) $(VARIANT_SOURCES)

BUILDDIR ?= build/$(CFG)
TOOL  = makeobj-extended
PROG ?= makeobj-extended

ifeq ($(origin MAKEOBJ_PROGDIR), undefined)
  MAKEOBJ_PROGDIR := ../$(BUILDDIR)/$(TOOL)
endif

BUILDDIR := ../$(BUILDDIR)

TOOL_PROGDIR = $(MAKEOBJ_PROGDIR)

include ../uncommon.mk
