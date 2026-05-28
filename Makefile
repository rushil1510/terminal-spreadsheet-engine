# ----------------------------------------------------------------------------
# terminal-spreadsheet-engine -- build system
#
# Targets:
#   make           Build the CLI binary (`./sheet`). Default target.
#   make cli       Same as `make`.
#   make gui       Build the ncurses TUI binary (`./sheet`, requires -lncurses).
#   make testbench Build the non-interactive test binary (`./sheet`).
#   make test      Build testbench + tester, then run every testcase in
#                  ./testcases through `./sheet 1000 1000` and diff the
#                  result against ./expected_outputs/text_files via tester.
#   make clean     Remove all build artefacts.
#
# All three sheet targets emit the same executable name (`sheet`) so the
# `./sheet R C` invocation in the README works regardless of which front-end
# was built.
# ----------------------------------------------------------------------------

CC       = gcc
CFLAGS   = -c -O2 -Wall
LDFLAGS  = -lm
LDFLAGS_GUI = -lm -lncurses

TARGET_CLI       = cli
TARGET_GUI       = gui
TARGET_TESTBENCH = testbench
TARGET_TEST      = test

SRC_CORE       = core/config.c core/helpers.c
SRC_CONTAINERS = containers/int_stack.c containers/memo_stack.c containers/avl_set.c
SRC_WORKBOOK   = workbook/workbook.c
SRC_ENGINE     = engine/engine.c
SRC_PARSER     = parser/label.c parser/command.c

SRC_COMMON = $(SRC_CORE) $(SRC_CONTAINERS) $(SRC_WORKBOOK) $(SRC_ENGINE) $(SRC_PARSER)

SRC_CLI  = $(SRC_COMMON) ui/cli/cli.c                     apps/main_cli.c
SRC_GUI  = $(SRC_COMMON) ui/tui/tui.c ui/tui/render.c     apps/main_gui.c
SRC_TEST = $(SRC_COMMON)                                  apps/main_test.c

OBJ_CLI  = $(SRC_CLI:.c=.o)
OBJ_GUI  = $(SRC_GUI:.c=.o)
OBJ_TEST = $(SRC_TEST:.c=.o)

.PHONY: all cli gui testbench test clean

all: $(TARGET_CLI)

$(TARGET_CLI): $(OBJ_CLI)
	$(CC) $(OBJ_CLI) -o sheet $(LDFLAGS)
	find . -name "*.o" -delete

$(TARGET_GUI): $(OBJ_GUI)
	$(CC) $(OBJ_GUI) -o sheet $(LDFLAGS_GUI)
	find . -name "*.o" -delete

$(TARGET_TESTBENCH): $(OBJ_TEST)
	$(CC) $(OBJ_TEST) -o sheet $(LDFLAGS)
	find . -name "*.o" -delete

$(TARGET_TEST): $(TARGET_TESTBENCH)
	$(CC) apps/tester.c -o tester $(LDFLAGS)
	@mkdir -p outputs
	@for file in testcases/*; do \
		filename=$$(basename $$file); \
		./sheet 1000 1000 < testcases/$$filename > outputs/$$filename; \
		./tester outputs/$$filename expected_outputs/text_files/$$filename; \
	done
	rm -f tester

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

clean:
	find . -name "*.o" -delete
	rm -f sheet tester
