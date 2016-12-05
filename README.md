# mod\_wozozo

mod\_wozozo is an Apache module that dispatches incoming requests to [Mozart2 VM](https://github.com/mozart/mozart2).  Mozart2 is an open source implementation of Oz 3 programming language.

## Building Instruction

### 1. Build [mozart2](https://github.com/mozart/mozart2)

```
$ aptitude install clang-3.8 libclang-3.8-dev libclang-common-3.8-dev git libboost-all-dev cmake g++ openjdk-8-jdk tcl-dev tk-dev
$ git clone --recursive https://github.com/mozart/mozart2
$ cd mozart2
$ mkdir build
$ cd build
$ cmake -DCMAKE_INSTALL_PREFIX=${MOZART_INSTALL_PREFIX} \
        -DCMAKE_BUILD_TYPE=Release \
        -DClang_DIR=/usr/lib/llvm-3.8/share/clang/cmake \
        -DCMAKE_CXX_FLAGS='-I/usr/lib/llvm-3.8/include -fPIC' \
        -Dclang_bin=/usr/bin/clang-3.8 \
        -DCMAKE_INSTALL_COMPONENT=1 \
        ..
$ make
$ make install
```

**Adjust `${MOZART_INSTALL_PREFIX}` to some location (e.g. `$HOME/opt/mozart2`)**

### 2. Build mod\_wozozo

```
$ git clone https://github.com/moriyoshi/mod_wozozo
$ make APXS=${APXS} MOZART2_SRC_DIR=${MOZART_BUILDING_DIRECTORY}
```

**Adjust `${APXS}` to the path to `apxs` script.**

**Adjust `${MOZART_BUILDING_DIRECTORY}` to the directory where the mozart2 build above is done under. (e.g. `$PWD/../mozart2`)**

### 3. Testing

```
$ ${MOZART_INSTALL_PREFIX}/bin/ozc -x hello.oz
$ go run test.go &
$ CONGRATS_SERVER_HOST=localhost MOZART_INSTALL_PREFIX=${MOZART_INSTALL_PREFIX} httpd -f httpd.minimal.conf
```

and hit http://localhost:8080/hello .
