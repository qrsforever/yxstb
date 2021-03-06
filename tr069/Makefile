
CROSS_COMPILE := arm-hisiv200-linux-gnueabi-

#
# This is the define of complier in here.
# the varible  must be defined outside.
#
# Define make variables
AS      = $(CROSS_COMPILE)as
LD      = $(CROSS_COMPILE)gcc
CC      = $(CROSS_COMPILE)gcc

# NOTE: We must use c++ and not g++ to avoid GPL contamination
CXX     = $(CROSS_COMPILE)c++
AR      = $(CROSS_COMPILE)ar
NM      = $(CROSS_COMPILE)nm
STRIP   = $(CROSS_COMPILE)strip
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
RANLIB  = $(CROSS_COMPILE)ranlib
MKDIR   = mkdir -p
MV      = mv
RMDIR	= rm -rf
CP		= cp

DEFINES = -D__LINUX_ARM_ARCH__=5 -march=armv5te -mtune=arm9tdmi -mlittle-endian
DEFINES += -DPLATFORM_V200 -DDEBUG_BUILD

CFLAGS  = -g -Wall -Werror


TOOLCHAIN_DIR=$(shell dirname $(shell dirname $(shell which arm-hisiv200-linux-gnueabi-gcc)))

TARGET = ./objs/libtr069.a


INCLUDES = -I./v2_include
INCLUDES += -I../porting/tr069/v2
INCLUDES += -I../../third_party/include
INCLUDES += -I../include

DEFINES := -DTR069_INDEPENDS -DGUANGDONG


CFLAGS		:= -g -Wall -Werror -O2
CXXFLAGS	:= 

#v200 library

ARFLAGS = -curs

SRCDIR = .
OBJDIR = ./objs

OBJS = 
OBJS += $(OBJDIR)/v2_src/tr069_api.obj
OBJS += $(OBJDIR)/v2_src/tr069_config.obj
OBJS += $(OBJDIR)/v2_src/tr069_diag.obj
OBJS += $(OBJDIR)/v2_src/tr069_hmac_sha1.obj
OBJS += $(OBJDIR)/v2_src/tr069_ftp.obj
OBJS += $(OBJDIR)/v2_src/tr069_http.obj
OBJS += $(OBJDIR)/v2_src/tr069_main.obj
OBJS += $(OBJDIR)/v2_src/tr069_md5.obj
OBJS += $(OBJDIR)/v2_src/tr069_method.obj
OBJS += $(OBJDIR)/v2_src/tr069_param.obj
OBJS += $(OBJDIR)/v2_src/tr069_soap.obj
OBJS += $(OBJDIR)/v2_src/tr069_stdex.obj
OBJS += $(OBJDIR)/v2_src/tr069_stun.obj
OBJS += $(OBJDIR)/v2_src/tr069_timer.obj
OBJS += $(OBJDIR)/v2_src/tr069_tr106.obj
OBJS += $(OBJDIR)/v2_param/app_aes.obj
OBJS += $(OBJDIR)/tr069_port0.obj


.PHONY: test clean build_time

all: builddir $(TARGET)

builddir:
	$(MKDIR) $(OBJDIR)
	$(MKDIR) $(OBJDIR)/v2_param
	$(MKDIR) $(OBJDIR)/v2_src

$(TARGET): $(OBJS)
	$(RM) $(TARGET)
	$(AR) -rcus $@ $(OBJS)

#
# 编译 C 文件
#
$(OBJDIR)/%.obj:$(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -o $@ -c  $<

#
# 清除目标文件
#
clean:
	$(RMDIR) $(OBJDIR)
