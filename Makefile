# Makefile - libezxam 逆向重建库一键构建（v77 起支持多环境）
# 作者：赵良军
#
# ============ 快速用法 ============
#   make              # moc + 编译 + 链接，产出 libezxam_reconstructed.so.1.0.0
#   make clean        # 清除 build/*.o、*.d、moc、链接日志与最终库
#   make rebuild      # 先 clean 再构建（干净生成）
#   make V=1          # 显示实际执行的命令
#   make verify       # 构建后校验导出面与原库一致
#
# ============ 调试日志开关 ============
#   make EZXAM_DEBUG=1   # AMLOG/AMVLOG 恢复为必写（排查问题用，默认 0 = 全部静默）
#   make AM_RING=1       # 打开 ring_put 环形调用埋点（SIGUSR1 转储，默认 0）
#   两者均默认关闭：日志格式串不进产物、零运行时开销。
#   永远保留的非调试设施：BOOT 分隔行、崩溃现场抓取、安装互斥修复。
#
# ============ 环境支持 ============
# 支持两类构建环境，Makefile 自动检测（uname）并设置默认值：
#   1) Cygwin / MSYS(Git-Bash)（Windows）：
#      工具链与 Qt 源码默认取 Cygwin 家目录，可用环境变量覆盖：
#        make TOOLCHAIN_BIN=D:/cygwin/home/ezx/arm-linux-gcc/bin QTDIR=D:/cygwin/home/ezx/qt-2.3.8
#      MSYS/Cygwin 工具链混用时路径统一为 Windows 风格 D:/...（三方都能解析，
#      由 cygpath -m 自动转换）。
#   2) Linux / WSL：
#      把 ARM 工具链放到任意目录（默认假设 $(HOME)/ezx/arm-linux-gcc/bin，
#      moc 在同一 bin 下、Qt2.3.8 源码在 $(HOME)/ezx/qt-2.3.8），或显式指定：
#        make TOOLCHAIN_BIN=$HOME/ezx/arm-linux-gcc/bin QTDIR=$HOME/ezx/qt-2.3.8
#   所需工具：arm-linux-g++、moc（Qt2 的）、make、bash、python3（仅 verify 用）。
#   本 Makefile 不依赖任何 WorkBuddy 特定路径或工具。
#
# ============ 技术说明（保持不变） ============
#   1. 与 build_win.sh + link_win.sh 等价，并增加增量编译与头文件依赖追踪。
#   2. 平台符号(CM_SEC_/DRM_SP_/UTIL_ 等)保持未定义(U)，由设备端
#      libezxappbase/libdmnative 等在加载期解析；stubs/ 桩库只提供 ELF
#      内部符号(_init/_fini/_DYNAMIC)，不定义任何平台函数符号。
#   3. 原库是 prelink 固定基址库，用 fixed_base.lds 复现布局
#      (LOAD 0x41b28000 / 数据段 0x41b73000)，配合 -fPIC 使 vtable 的
#      ABS32/GLOB_DAT 重定位与原库一致。
#   4. *.moc 由 moc 从 Q_OBJECT 头文件生成，被对应 .cpp 末尾
#      #include "*.moc"，因此不单独编译，但作为依赖参与构建。

# ---------------- 工程根目录（自动推导 = Makefile 所在目录） ----------------
SRC_RAW := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

# ---------------- 宿主平台检测 ----------------
#   CYGWIN_NT-* / MSYS_NT-* → Windows 侧（工具带 .exe 后缀，需 cygwin1.dll）
#   Linux                   → WSL / 原生 Linux
UNAME_S := $(shell uname -s 2>/dev/null)
ifneq (,$(findstring CYGWIN,$(UNAME_S))$(findstring MSYS,$(UNAME_S)))
  HOST_OS := cygwin
else ifneq (,$(findstring Linux,$(UNAME_S)))
  HOST_OS := linux
else
  HOST_OS := unknown
endif

ifeq ($(HOST_OS),cygwin)
  # 关键：moc.exe / arm-linux-g++.exe 是 Cygwin 程序（依赖 cygwin1.dll）。
  # MSYS make 驱动时必须把 /d/cygwin/bin 放进 PATH，否则子 shell 报 "No such file"。
  EXEEXT := .exe
  CYGBIN ?= /d/cygwin/bin
  export PATH := $(CYGBIN):$(PATH)
  # 【重要】MSYS/Git-Bash 会把 -Wl,-rpath,/usr/lib/ezx/lib 里的 /usr/... 自动改写成
  # Windows 路径（实测变成 "D:/Program Files/.../usr/lib/ezx/lib"），导致设备上
  # RPATH 完全失效、库加载不到 libqte-mt.so.2。下面两行关闭参数路径转换。
  export MSYS_NO_PATHCONV := 1
  export MSYS2_ARG_CONV_EXCL := *
  # 工程根目录转 Windows 风格 D:/...（MSYS make、Cygwin moc/g++ 都能正确解析）
  SRC := $(shell cygpath -m "$(SRC_RAW)" 2>/dev/null || echo "$(SRC_RAW)")
  TOOLCHAIN_BIN ?= D:/cygwin/home/ezx/arm-linux-gcc/bin
  QTDIR         ?= D:/cygwin/home/ezx/qt-2.3.8
else
  EXEEXT :=
  SRC := $(SRC_RAW)
  TOOLCHAIN_BIN ?= $(HOME)/ezx/arm-linux-gcc/bin
  QTDIR         ?= $(HOME)/ezx/qt-2.3.8
endif

BUILD := $(SRC)/build

# ---------------- 工具 ----------------
SHELL := /bin/bash
CXX := $(TOOLCHAIN_BIN)/arm-linux-g++$(EXEEXT)
MOC := $(TOOLCHAIN_BIN)/moc$(EXEEXT)

# ---------------- 编译选项 ----------------
# 顺序关键：重写的 include/ezx 在前，原始 genheader 的 $(QTDIR)/include/ezx 在后
CXXFLAGS := -pipe -DQWS -fno-exceptions -fno-rtti -O2 -D_REENTRANT -fPIC
CXXFLAGS += -MMD -MP        # 生成 .d 依赖文件
# 调试开关传递（默认全关；make EZXAM_DEBUG=1 / AM_RING=1 打开，见文件头说明）
CXXFLAGS += -DEZXAM_DEBUG=$(if $(EZXAM_DEBUG),$(EZXAM_DEBUG),0)
CXXFLAGS += -DAM_RING_ENABLE=$(if $(AM_RING),$(AM_RING),0)
INCLUDES := -I$(SRC)/include/ezx -I$(SRC)/include -I$(QTDIR)/include/qt -I$(QTDIR)/include/ezx
PREINCLUDE := -include $(SRC)/include/qt_preinclude.h

# ---------------- 链接选项 ----------------
LDS  := $(SRC)/fixed_base.lds
# RPATH 必须与原库一致（readelf -d orig_local.so 实证）：
#   Library rpath: [/usr/lib/ezx/lib:/usr/lib/ezx/lib/inputmethods]
# 原库是 DT_RPATH（非 RUNPATH，binutils 2.14 默认行为）。缺 RPATH 时，
# 以绝对路径 dlopen 本库的进程无法解析 NEEDED(libqte-mt.so.2 等)——
# 实测 t_dlopen /mmc/mmca1/v23.so 报 "libqte-mt.so.2: cannot open"。
LDFLAGS := -shared -Wl,-soname,libezxam.so.1 -Wl,-T,$(LDS)
LDFLAGS += -Wl,-rpath,/usr/lib/ezx/lib -Wl,-rpath,/usr/lib/ezx/lib/inputmethods
# 导出白名单（v51）：白名单 = 原厂库 .dynsym 全部已定义符号（gen_exports.py 生成）。
# 不加这个的话，本库会把源码里出现的所有 Qt2 模板实例和我们自己的调试符号
# （amLogWrite / ring_* / crash_install …）一起导出，共多出 109 个；这些多余
# 提供者在别的进程里可能抢占同名 UND（典型隐患：QString::~QString、
# QWidget::setWFlags、QMap<QString,QString>::operator= 等）。
# 用匿名版本节点：只改可见性，不引入 @V1 版本后缀，与原库（无版本节）一致。
EXPORTS := $(SRC)/exports.map
LDFLAGS += -Wl,--version-script,$(EXPORTS)
QTLIBS := -L$(QTDIR)/lib -lqte-mt
STUBS  := $(SRC)/stubs
STUBLIBS := $(STUBS)/libezxappbase.so.1 \
            $(STUBS)/libdmnative.so.02 \
            $(STUBS)/libezxphone.so.1 \
            $(STUBS)/libezx-cmseccert.so \
            $(STUBS)/libezxdrmsp.so.1 \
            $(STUBS)/libpthread.so.0

TARGET := $(SRC)/libezxam_reconstructed.so.1.0.0

# ---------------- 文件清单 ----------------
HEADERS_QOBJECT := AM_AppDataInterface AM_AppLockManager AM_Carrier_Info \
                   AM_GroupListScrollView AM_RegistryManager AM_StandardDlg
# 注意：必须先 addprefix 目录、再 addsuffix 后缀。
#       写成 $(addprefix $(B)/,$(LIST).moc) 会导致 .moc 只追加到最后一个词，
#       其余词无后缀，从而产生 "No rule to make target build/AM_xxx" 的循环。
MOC_HDRS := $(addsuffix .h,$(addprefix $(SRC)/include/ezx/,$(HEADERS_QOBJECT)))
MOC_SRCS := $(addsuffix .moc,$(addprefix $(BUILD)/,$(HEADERS_QOBJECT)))

CPPS := $(wildcard $(SRC)/src/*.cpp)
OBJS := $(patsubst $(SRC)/src/%.cpp,$(BUILD)/%.o,$(CPPS))
DEPS := $(OBJS:.o=.d)

# ---------------- 规则 ----------------
# 关闭 make 内置隐式规则（避免 %:%.o / %:%.cpp 等规则与我们的高位规则
# 冲突，产生 build/<name> <- build/<name>.o 循环依赖和误用 cc 链接）。
MAKEFLAGS += -r

.DEFAULT_GOAL := all
.PHONY: all clean rebuild moc verify

all: $(TARGET)

# moc：仅当头文件存在且含 Q_OBJECT 才真正运行 moc；
#      否则 touch 空文件，保证 .o 的 order-only 依赖存在且避免循环。
# 坑（v32 修）：各 .cpp 末尾的 #include "X.moc" 按【源文件相对路径】找，
# 即找 src/X.moc；而本规则原先只写 build/X.moc，导致编译器一直用 src/ 下
# 上一次 build_win.sh 留下的旧 moc —— 改了信号/槽头文件也不生效。
# 现在同时产出 src/（编译实际使用）与 build/（依赖标记）。
moc: $(MOC_SRCS)

$(BUILD)/%.moc: $(SRC)/include/ezx/%.h | $(BUILD)
	@if grep -q "Q_OBJECT" "$<"; then \
	  echo "  MOC   $<"; \
	  $(MOC) $< -o $(SRC)/src/$*.moc 2>$(BUILD)/moc_$*.log || { echo "  MOC FAIL $*"; exit 1; }; \
	  cp $(SRC)/src/$*.moc $@; \
	else \
	  echo "  MOC SKIP $* (no Q_OBJECT)"; \
	  touch $@; \
	fi

$(BUILD):
	@mkdir -p $(BUILD)

# 每个 .o 依赖全部 moc（order-only，确保生成顺序）；不用普通依赖避免 make
# 内置 %:%.o 规则把它当可链接中间目标造成循环。
$(BUILD)/%.o: $(SRC)/src/%.cpp | $(BUILD) $(MOC_SRCS)
	@echo "  CXX   $<"
	$(CXX) $(CXXFLAGS) $(PREINCLUDE) $(INCLUDES) -c $< -o $@ 2>$(BUILD)/$*.log || { cat $(BUILD)/$*.log; exit 1; }

$(TARGET): $(OBJS) $(LDS) $(EXPORTS)
	@echo "  LINK  $@"
	$(CXX) $(LDFLAGS) $(OBJS) $(QTLIBS) $(STUBLIBS) -o $@ 2>$(BUILD)/link.log || { grep -v -E "find_fast_cwd|nodosfilewarning|CYGWIN|Preferred|Consult|public mailing|^$$|MS-DOS|cygwin warning|using.html" $(BUILD)/link.log; exit 1; }
	@echo "  DONE  $@ ($$(stat -c%s "$@")) bytes"

# 导出面验收：缺符号 / 多出符号都必须为 0，否则视为构建失败
verify: $(TARGET)
	@python $(SRC)/verify_exports.py $(TARGET) $(SRC)/orig_local.so

clean:
	@rm -f $(BUILD)/*.o $(BUILD)/*.d $(BUILD)/*.moc $(BUILD)/*.log $(BUILD)/moc_*.log $(TARGET)
	@echo "  CLEAN 已清除中间文件与 $(TARGET)"

rebuild: clean all

-include $(DEPS)
