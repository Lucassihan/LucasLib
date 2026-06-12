# VEXcode makefile 2019_03_26_01

# show output of compile and link steps (set to 1 to show)
VERBOSE = 0

# include toolchain options
include vex/mkenv.mk

# bump C++ standard from gnu++11 (set in vex/mkenv.mk) so we get
# designated initializers like {.timeout = 2000} in function args.
# clang 8 (bundled with VEX) supports this under the gnu++2a draft.
CXX_FLAGS := $(patsubst -std=gnu++11,-std=gnu++2a,$(CXX_FLAGS))

# location of the project source cpp and c files
SRC_C  = $(wildcard src/*.cpp)
SRC_C += $(wildcard src/*.c)
SRC_C += $(wildcard src/*/*.cpp)
SRC_C += $(wildcard src/*/*.c)

OBJ = $(addprefix $(BUILD)/, $(addsuffix .o, $(basename $(SRC_C))))

# location of include files that c and cpp files depend on
SRC_H  = $(wildcard include/*.h)
SRC_H += $(wildcard include/*/*.h)

# additional dependencies
SRC_A  =

# project header file locations
INC_F  = include
INC_F += include/misc
INC_F += include/movement

# build targets
all: $(BUILD)/$(PROJECT).bin

# include build rules
include vex/mkrules.mk
