CC ?= gcc
TCC ?= tcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
	-Wformat=2 -Wnull-dereference -Wconversion -Werror \
	-D_FORTIFY_SOURCE=2 -fstack-protector-strong
INCLUDES := -I src -I runtime -I third_party
CORE := src/safe.c src/model.c src/style.c src/codegen.c src/gen_nk.c src/gen_ret.c src/project.c src/ide.c

.PHONY: all test tcc clean

all: build/tccrad

build/tccrad: $(CORE) src/ide_ui.c src/main.c src/nk_glue.h third_party/nuklear.h third_party/nuklear_xlib.h
	mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -c src/ide_ui.c -o build/ide_ui.o
	$(CC) $(CFLAGS) $(INCLUDES) -c $(CORE) 
	mv *.o build/ 2>/dev/null || true
	$(CC) -std=c11 -Wall -Wno-unused-function -Wno-conversion $(INCLUDES) -c src/main.c -o build/main.o
	$(CC) $(CFLAGS) -o build/tccrad build/*.o -lX11 -lm

build/test_core: tests/test_core.c $(CORE)
	mkdir -p build
	$(CC) $(CFLAGS) -I src -I runtime -o build/test_core tests/test_core.c \
		src/safe.c src/model.c src/style.c src/codegen.c src/gen_nk.c src/gen_ret.c

test: build/test_core build/tccrad
	mkdir -p /tmp/tccrad-test
	./build/test_core /tmp/tccrad-test
	$(CC) -std=c11 -Wall -Wextra -Werror -c /tmp/tccrad-test/app.c \
		-I runtime -I third_party -I /tmp/tccrad-test -o /tmp/tccrad-test/app.o
	$(TCC) -std=c11 -c /tmp/tccrad-test/app.c -I runtime -I third_party -I /tmp/tccrad-test -o /tmp/tccrad-test/app_tcc.o
	$(CC) -std=c11 -Wall -Wextra -Werror -DTCCRAD_BACKEND=2 -c /tmp/tccrad-test/app.c \
		-I runtime -I tests/stubs -I /tmp/tccrad-test -o /tmp/tccrad-test/app_ui.o
	$(CC) -std=c11 -Wall -Wextra -Werror -DTCCRAD_BACKEND=3 -c /tmp/tccrad-test/app.c \
		-I runtime -I tests/stubs -I /tmp/tccrad-test -o /tmp/tccrad-test/app_lv.o
	$(TCC) -std=c11 -I runtime -I third_party -I /tmp/tccrad-test \
		/tmp/tccrad-test/main.c /tmp/tccrad-test/app.c -lX11 -lm -o /tmp/tccrad-test/app

tcc: $(CORE) src/ide_ui.c src/main.c
	mkdir -p build
	$(TCC) -std=c11 -Wall $(INCLUDES) -o build/tccrad-tcc \
		src/main.c src/ide_ui.c $(CORE) -lX11 -lm

clean:
	rm -rf build
