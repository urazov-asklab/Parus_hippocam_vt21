include ../Rules.make

LIVE_DIR = ../
LIB_SUFFIX = a

LIB = libWisStreamer.$(LIB_SUFFIX)

all:	$(LIB)

CC = $(BUILD_TOOL_PREFIX)gcc
CPLUSPLUS = $(BUILD_TOOL_PREFIX)g++

INCLUDES = -I. \
	-I$(LIVE_DIR)/BasicUsageEnvironment/include \
	-I$(LIVE_DIR)/UsageEnvironment/include \
	-I$(LIVE_DIR)/groupsock/include \
	-I$(LIVE_DIR)/liveMedia/include \
	-I$(PUBLIC_INCLUDE_DIR)

##### Change the following for your environment: 
CROSS_COMPILE?=		$(BUILD_TOOL_PREFIX)
COMPILE_OPTS =		$(INCLUDES) -lpthread -D_LINUX -g -Wall -I. -O3 -DSOCKLEN_T=socklen_t -DNO_SSTREAM=1 -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64
C =			c
C_COMPILER =		$(CROSS_COMPILE)gcc
C_FLAGS =		$(COMPILE_OPTS)
CPP =			cpp
CPLUSPLUS_COMPILER =	$(CROSS_COMPILE)g++
CPLUSPLUS_FLAGS =	$(COMPILE_OPTS) -Wall -DBSD=1
OBJ =			o
LINK =			$(CROSS_COMPILE)g++ -o 
LINK_OPTS =		-L. -s
CONSOLE_LINK_OPTS =	$(LINK_OPTS)
LIBRARY_LINK =		$(CROSS_COMPILE)ld -o 
LIBRARY_LINK_OPTS =	$(LINK_OPTS) -r -Bstatic
LIB_SUFFIX =			a

LIBS =	-L$(LIVE_DIR)/BasicUsageEnvironment -lBasicUsageEnvironment \
	-L$(LIVE_DIR)/UsageEnvironment -lUsageEnvironment \
	-L$(LIVE_DIR)/groupsock -lgroupsock \
	-L$(LIVE_DIR)/liveMedia -lliveMedia

OBJS = wis-streamer.o Err.o APPROInput.o \
	WISServerMediaSubsession.o \
	WISH264VideoServerMediaSubsession.o \
	WISAACAudioServerMediaSubsession.o

#WISPCMAudioServerMediaSubsession.o \

libWisStreamer.$(LIB_SUFFIX): $(OBJS)	
	$(LIBRARY_LINK)$@ $(LIBRARY_LINK_OPTS) $(OBJS)

.$(C).$(OBJ):
	$(C_COMPILER) -c $(C_FLAGS) $<       

.$(CPP).$(OBJ):
	$(CPLUSPLUS_COMPILER) -c $(CPLUSPLUS_FLAGS) $<	


clean:
	rm -f *.o *~
	rm -f $(EXEC)
