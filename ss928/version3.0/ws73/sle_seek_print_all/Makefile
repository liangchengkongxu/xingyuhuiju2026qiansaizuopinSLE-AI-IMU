# 交叉编译 WS73 SLE 扫描打印工具（aarch64，与海鸥派 SS928 / rk3568 库一致）
# 用法:
#   make
#   make deploy BOARD=root@192.168.1.168

WS73_SDK   ?= $(abspath ../../../星闪WS73原厂SDK/extracted)
CROSS      ?= aarch64-linux-gnu-
CC         = $(CROSS)gcc
STRIP      = $(CROSS)strip

TARGET     = sle_seek_print_all
SRCS       = sle_seek_print_main.c sle_seek_print_client.c

INCDIR_TOP = .
INCDIR_COMMON = $(WS73_SDK)/include/bsle/common/
INCDIR_SLE    = $(WS73_SDK)/include/bsle/sle/
INCDIR_SEC    = $(WS73_SDK)/driver/platform/libc_sec/include/
INCDIR_DRV    = $(WS73_SDK)/driver/platform/drv/include/
INCDIR_PLA    = $(WS73_SDK)/driver/platform/osal/include/
LIBDIR        = $(WS73_SDK)/application/lib/rk3568/sle

CFLAGS  += -Wall -O2 -fPIC
CFLAGS  += -I$(INCDIR_TOP) -I$(INCDIR_COMMON) -I$(INCDIR_SLE)
CFLAGS  += -I$(INCDIR_SEC) -I$(INCDIR_DRV) -I$(INCDIR_PLA)
LDFLAGS += -lm -lpthread -lrt -ldl -Wl,--gc-sections

OBJS = $(SRCS:.c=.o)

.PHONY: all clean deploy check-sdk

all: check-sdk $(TARGET)

check-sdk:
	@test -f $(LIBDIR)/libsle_host.a || (echo "missing $(LIBDIR)/libsle_host.a"; exit 1)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -Xlinker "-(" $(LIBDIR)/*.a -Xlinker "-)"
	$(STRIP) --strip-unneeded -s $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

BOARD ?= root@192.168.1.168
deploy: $(TARGET)
	scp $(TARGET) $(BOARD):/opt/sample/ws73/sle_seek_print_all
	ssh $(BOARD) 'chmod +x /opt/sample/ws73/sle_seek_print_all'
	@echo "Deployed to /opt/sample/ws73/sle_seek_print_all"
