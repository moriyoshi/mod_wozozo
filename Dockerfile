FROM ubuntu:yakkety
ARG UBUNTU_MIRROR_URL
ENV MOZART_INSTALL_PREFIX /opt/mozart2
RUN echo "${UBUNTU_MIRROR_URL}"; if [ -n "${UBUNTU_MIRROR_URL}" ]; then sed -i "s#http://archive.ubuntu.com/ubuntu#${UBUNTU_MIRROR_URL}#" /etc/apt/sources.list; fi
RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y clang-3.8 libclang-3.8-dev libclang-common-3.8-dev git libboost-all-dev cmake g++ openjdk-8-jdk tcl-dev tk-dev
RUN git clone --recursive https://github.com/mozart/mozart2 /tmp/mozart2
RUN cd /tmp/mozart2 && \
    mkdir build && \
    cd build && \
    cmake -DCMAKE_INSTALL_PREFIX=${MOZART_INSTALL_PREFIX} \
          -DCMAKE_BUILD_TYPE=Release \
          -DClang_DIR=/usr/lib/llvm-3.8/share/clang/cmake \
          -DCMAKE_CXX_FLAGS='-I/usr/lib/llvm-3.8/include -fPIC' \
          -Dclang_bin=/usr/bin/clang-3.8 \
          -DCMAKE_INSTALL_COMPONENT=1 \
          .. && \
    make && \
    make install
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y apache2 apache2-dev
ADD . /tmp/mod_wozozo
WORKDIR /tmp/mod_wozozo
RUN make MOZART2_SRC_DIR=/tmp/mozart2
RUN rm -rf /tmp/mozart2
RUN ${MOZART_INSTALL_PREFIX}/bin/ozc -x hello.oz
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y golang
RUN DEBIAN_FRONTEND=noninteractive apt-get remove -y clang-3.8 libclang-3.8-dev libclang-common-3.8-dev git cmake g++ openjdk-8-jdk tcl-dev tk-dev apache2-dev
RUN apt-get clean autoclean
RUN apt-get -y autoremove
ENTRYPOINT ["/tmp/mod_wozozo/entrypoint.sh"]
