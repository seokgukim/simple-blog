CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
ANALYZER_CC ?= gcc
STATIC_FLAGS = -O0 -g -std=c11 -fanalyzer -Wall -Wextra -Wpedantic -Werror \
	-Wformat=2 -Wshadow -Wstrict-prototypes -Wmissing-prototypes
INCLUDES = -Ibuild/md4c -Ivendor/md4c -Ivendor/libyaml/include -Ibuild/itex

SOURCES = src/main.c src/metadata.c src/render.c src/math.c src/highlight.c
ITEX = build/itex/y.tab.c build/itex/lex.yy.c
MD4C = build/md4c/md4c.c build/md4c/md4c-html.c vendor/md4c/entity.c
YAML = $(addprefix vendor/libyaml/src/,api.c reader.c scanner.c parser.c loader.c)
PROJECT_FLAGS = -DBLOG_PROJECT_DIR='"$(CURDIR)"'
YAML_FLAGS = -D_POSIX_C_SOURCE=200809L -DYAML_VERSION_STRING='"0.2.5"' -DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5

build/simple-blog: $(SOURCES) src/metadata.h src/render.h src/math.h src/highlight.h build/md4c/.ready build/itex/.ready scripts/fetch-md4c.sh scripts/fetch-yaml.sh scripts/fetch-itex.sh Makefile | dependencies
	mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(PROJECT_FLAGS) $(YAML_FLAGS) $(INCLUDES) $(SOURCES) $(MD4C) $(YAML) $(ITEX) $(LDFLAGS) $(LDLIBS) -o $@

# Preserve upstream downloads; apply rendering extensions only to build copies.
build/md4c/.ready: scripts/patches/md4c-math.patch scripts/fetch-md4c.sh | dependencies
	mkdir -p build/md4c
	cp vendor/md4c/md4c.c vendor/md4c/md4c.h vendor/md4c/md4c-html.c vendor/md4c/md4c-html.h build/md4c/
	patch --batch --fuzz=0 -d build/md4c -p1 < scripts/patches/md4c-math.patch
	touch $@

build/itex/.ready: scripts/fetch-itex.sh scripts/patches/itex-safety.patch | dependencies
	mkdir -p build/itex
	cp vendor/itex2MML/itex-src/perl/lex.yy.c vendor/itex2MML/itex-src/perl/y.tab.c vendor/itex2MML/itex-src/perl/y.tab.h vendor/itex2MML/itex-src/perl/itex2MML.h build/itex/
	patch --batch --fuzz=0 -d build/itex -p1 < scripts/patches/itex-safety.patch
	touch $@

dependencies:
	sh scripts/fetch-md4c.sh
	sh scripts/fetch-yaml.sh
	sh scripts/fetch-itex.sh

test: build/simple-blog
	sh tests/smoke.sh
	python3 tests/site.py
	python3 tests/test_math.py
	python3 tests/test_highlight.py
	python3 tests/test_publish.py
	sh tests/default-output.sh

test-math: build/simple-blog
	python3 tests/test_math.py

# GCC path-sensitive analysis of project translation units, not vendor sources.
# Compile (rather than -fsyntax-only) so the analyzer actually runs. Always rerun.
check-static: build/md4c/.ready build/itex/.ready | dependencies
	mkdir -p build/static
	@status=0; for source in $(SOURCES); do \
		echo "Analyzing $$source"; \
		$(ANALYZER_CC) $(STATIC_FLAGS) $(CPPFLAGS) $(PROJECT_FLAGS) $(YAML_FLAGS) $(patsubst -I%,-isystem %,$(INCLUDES)) \
			-c "$$source" -o "build/static/$$(basename "$$source" .c).o" || status=1; \
	done; exit $$status

# Local Markdown -> fresh staged artifact -> optional Workers static-assets upload.
# The wrapper runs check-static and test before building; never uploads export/.
prepare-cf:
	python3 scripts/publish-cf.py

deploy-cf:
	python3 scripts/publish-cf.py --deploy

clean:
	rm -rf build

.PHONY: test clean dependencies test-math check-static prepare-cf deploy-cf
