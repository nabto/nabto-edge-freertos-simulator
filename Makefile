CC := gcc
BIN := nabto_demo

BUILD_DIR := build
SRC_DIR := $(abspath src)

KERNEL_DIR_REL := ./FreeRTOS/Kernel
KERNEL_DIR := $(abspath $(KERNEL_DIR_REL))
TCP_DIR_REL := ./FreeRTOS/FreeRTOS-Plus-TCP
TCP_DIR := $(abspath $(TCP_DIR_REL))

INCLUDE_DIRS := -I.
INCLUDE_DIRS += -I${SRC_DIR}
INCLUDE_DIRS += -I${KERNEL_DIR}/include
INCLUDE_DIRS += -I${KERNEL_DIR}/portable/ThirdParty/GCC/Posix
INCLUDE_DIRS += -I${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils
INCLUDE_DIRS += -I${KERNEL_DIR}/Demo/Common/include
INCLUDE_DIRS += -I${TCP_DIR}/portable/NetworkInterface/linux/
INCLUDE_DIRS += -I${TCP_DIR}/include/
INCLUDE_DIRS += -I${TCP_DIR}/portable/Compiler/GCC/

SOURCE_FILES := $(wildcard ${SRC_DIR}/*.c)
SOURCE_FILES += $(wildcard ${KERNEL_DIR}/*.c)
# Memory manager (use malloc() / free() )
SOURCE_FILES += ${KERNEL_DIR}/portable/MemMang/heap_3.c
# posix port
SOURCE_FILES += ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
SOURCE_FILES += ${KERNEL_DIR}/portable/ThirdParty/GCC/Posix/port.c

# FreeRTOS TCP
ifeq ("x", "y")
SOURCE_FILES += ${TCP_DIR}/FreeRTOS_DNS.c
SOURCE_FILES += ${TCP_DIR}/FreeRTOS_DHCP.c
SOURCE_FILES += ${TCP_DIR}/FreeRTOS_ARP.c
SOURCE_FILES += ${TCP_DIR}/FreeRTOS_TCP_WIN.c
SOURCE_FILES += ${TCP_DIR}/FreeRTOS_Stream_Buffer.c
SOURCE_FILES += ${TCP_DIR}/portable/BufferManagement/BufferAllocation_2.c
SOURCE_FILES += ${TCP_DIR}/FreeRTOS_IP.c
SOURCE_FILES += ${TCP_DIR}/FreeRTOS_TCP_IP.c
SOURCE_FILES += ${TCP_DIR}/FreeRTOS_UDP_IP.c
SOURCE_FILES += ${TCP_DIR}/FreeRTOS_Sockets.c
SOURCE_FILES += ${TCP_DIR}/portable/NetworkInterface/linux/NetworkInterface.c
endif

CFLAGS := -ggdb3 -O0 -DprojCOVERAGE_TEST=0 -D_WINDOWS_
LDFLAGS := -ggdb3 -O0 -pthread -lpcap

OBJ_FILES = $(SOURCE_FILES:%.c=$(BUILD_DIR)/%.o)

DEP_FILE = $(OBJ_FILES:%.o=%.d)

${BIN} : $(BUILD_DIR)/$(BIN)

${BUILD_DIR}/${BIN} : ${OBJ_FILES}
	-mkdir -p ${@D}
	$(CC) $^ $(CFLAGS) $(INCLUDE_DIRS) ${LDFLAGS} -o $@


-include ${DEP_FILE}

${BUILD_DIR}/%.o : %.c
	-mkdir -p $(@D)
	$(CC) $(CFLAGS) ${INCLUDE_DIRS} -MMD -c $< -o $@

.PHONY: clean

clean:
	-rm -rf $(BUILD_DIR)








