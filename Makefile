#
# Makefile
#
# Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/ 
# 
# 
#  Redistribution and use in source and binary forms, with or without 
#  modification, are permitted provided that the following conditions 
#  are met:
#
#    Redistributions of source code must retain the above copyright 
#    notice, this list of conditions and the following disclaimer.
#
#    Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the 
#    documentation and/or other materials provided with the   
#    distribution.
#
#    Neither the name of Texas Instruments Incorporated nor the names of
#    its contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
#  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
#  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

ROOTDIR = /home/demon/work/dvsdk_dm3730_04030006
TARGET = $(notdir $(CURDIR))

include $(ROOTDIR)/Rules.make

# Comment this out if you want to see full compiler and linker output.
override VERBOSE = @

# Package path for the XDC tools
XDC_PATH = $(USER_XDC_PATH);../../packages;$(DMAI_INSTALL_DIR_VT20)/packages;$(CE_INSTALL_DIR)/packages;$(FC_INSTALL_DIR)/packages;$(LINK_INSTALL_DIR);$(XDAIS_INSTALL_DIR)/packages;$(CMEM_INSTALL_DIR)/packages;$(CODEC_INSTALL_DIR)/packages;$(LPM_INSTALL_DIR)/packages;$(C6ACCEL_INSTALL_DIR)/soc/packages

# Where to output configuration files
XDC_CFG		= $(TARGET)_config

# Output compiler options
XDC_CFLAGS	= $(XDC_CFG)/compiler.opt

# Output linker file
XDC_LFILE	= $(XDC_CFG)/linker.cmd

# Input configuration file
XDC_CFGFILE	= $(TARGET).cfg

# Platform (board) to build for
XDC_PLATFORM = ti.platforms.evm3530

# Target tools
XDC_TARGET = gnu.targets.arm.GCArmv5T

export CSTOOL_DIR

#wis================
LIVE_DIR = live

WIS_LIBS =	-L$(LIVE_DIR)/BasicUsageEnvironment -lBasicUsageEnvironment \
	-L$(LIVE_DIR)/UsageEnvironment -lUsageEnvironment \
	-L$(LIVE_DIR)/groupsock -lgroupsock \
	-L$(LIVE_DIR)/liveMedia -lliveMedia \
	-L$(LIVE_DIR)/appro -lWisStreamer
#wis================

# The XDC configuration tool command line
CONFIGURO = $(XDC_INSTALL_DIR)/xs xdc.tools.configuro
CONFIG_BLD = config.bld

C_FLAGS += -I$(LINUXKERNEL_INSTALL_DIR)/usr/include -I$(LINUXKERNEL_INSTALL_DIR)/arch/arm/include -DMatrix_Gui
C_FLAGS += -Wall -fexceptions -O2 -DDEMO_BENCHMARK -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_ISOC9X_SOURCE -D_LARGEFILE64_SOURCE

LD_FLAGS += -L$(LINUXLIBS_INSTALL_DIR)/lib -lpthread -lasound -lfreetype -lpng -ljpeg -lz $(WIS_LIBS) -lstdc++

COMPILE.c = $(VERBOSE) $(CSTOOL_PREFIX)gcc $(C_FLAGS) -c
LINK.c = $(VERBOSE) $(CSTOOL_PREFIX)gcc $(LD_FLAGS)
STRIP = $(CSTOOL_PREFIX)strip

SOURCES = $(wildcard *.c) 
#$(wildcard ../*.c)
HEADERS = $(wildcard *.h) 
#$(wildcard ../*.h)

OBJFILES = $(SOURCES:%.c=%.o)

.PHONY: clean install 

all:	o3530

o3530:	o3530_al

o3530_al:	$(TARGET)

install:	$(if $(wildcard $(TARGET)), install_$(TARGET))

install_$(TARGET):
	@install -d $(EXEC_DIR)
	@install $(TARGET) $(EXEC_DIR)
	@install $(TARGET).txt $(EXEC_DIR)
	@echo
	@echo Installed $(TARGET) binaries to $(EXEC_DIR)..

$(TARGET):	$(OBJFILES) $(XDC_LFILE)
	@echo
	@echo Linking $@ from $^..
	$(LINK.c) -o $@ $^ 
	$(STRIP) $(TARGET)
	cp $(TARGET) /tftpboot
	sudo cp $(TARGET) /home/demon/work/vizir-WiFi/filesystem/opt

$(OBJFILES):	%.o: %.c $(HEADERS) $(XDC_CFLAGS)
	@echo Compiling $@ from $<..
	$(COMPILE.c) $(shell cat $(XDC_CFLAGS)) -o $@ $<

$(XDC_LFILE) $(XDC_CFLAGS):	$(XDC_CFGFILE)
	@echo
	@echo ======== Building $(TARGET) ========
	@echo Configuring application using $<
	@echo
	$(VERBOSE) XDCPATH="$(XDC_PATH)" $(CONFIGURO) -o $(XDC_CFG) -t $(XDC_TARGET) -p $(XDC_PLATFORM) -b $(CONFIG_BLD) $(XDC_CFGFILE)

clean:
	@echo Removing generated files..
	$(VERBOSE) -$(RM) -rf $(XDC_CFG) $(OBJFILES) $(TARGET) *~ *.d .dep
