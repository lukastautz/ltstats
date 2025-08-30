.PHONY: clean
MUSL=musl/bin/musl-gcc
MUSL_VERSION=1.2.5
BEARSSL_COMMIT=3c040368f6791553610e362401db1efff4b4c5b8
JSON_C_COMMIT=2372e9518e6ba95b48d37ec162bc7d93b297b52f
CC=${MUSL}
CFLAGS=-fno-strict-aliasing -static -Ofast -O3 -Wall -Wextra -pedantic -Werror -Wno-deprecated-declarations
default: ${MUSL} ${ELFTRUNC} ltstats_agent ltstats_server ltstats_ntp
ltstats_agent: ${MUSL} ${ELFTRUNC} libbearssl.a TA.h
	${CC} ${CFLAGS} agent.c libbearssl.a -o ltstats_agent
	strip ltstats_agent
libbearssl.a:
	git clone https://www.bearssl.org/git/BearSSL
	sh -c 'cd BearSSL && git checkout ${BEARSSL_COMMIT} && make -j$$(nproc) && cp build/libbearssl.a ..'
libjson-c.a:
	git clone https://github.com/json-c/json-c.git
	sh -c 'cd json-c && git checkout ${JSON_C_COMMIT} && export CC=../${MUSL} && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DDISABLE_JSON_POINTER=ON -DENABLE_THREADING=OFF -DDISABLE_EXTRA_LIBS=ON -DDISABLE_THREAD_LOCAL_STORAGE=ON -DHAVE_SETLOCALE=0 -DHAVE_USELOCALE=0 -DHAVE_ARC4RANDOM=0 . && make -j$$(nproc) && cp libjson-c.a ..'
TA.h: libbearssl.a
	curl -so cacert.pem https://curl.se/ca/cacert.pem
	BearSSL/build/brssl ta cacert.pem > TA.h
ltstats_server: ${MUSL} ${ELFTRUNC} libjson-c.a
	${CC} ${CFLAGS} server.c libjson-c.a -o ltstats_server
	strip ltstats_server
$(MUSL):
	curl -so musl.tar.gz https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz
	tar xf musl.tar.gz
	rm musl.tar.gz
	mv musl-${MUSL_VERSION} musl
	sh -c 'cd musl; ./configure --prefix="$$(pwd)"; make install -j$$(nproc)'
ltstats_ntp: ${MUSL} ${ELFTRUNC}
	${CC} ${CFLAGS} ntp.c -o ltstats_ntp
	strip ltstats_ntp
clean:
	rm -r musl BearSSL libbearssl.a ltstats_agent ltstats_server ltstats_ntp TA.h cacert.pem libjson-c.a json-c
