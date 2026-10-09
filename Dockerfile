FROM ubuntu:20.04


ARG DEBIAN_FRONTEND=noninteractive
# To build the image for a branch or a tag of IDF, pass --build-arg IDF_CLONE_BRANCH_OR_TAG=name.
# To build the image with a specific commit ID of IDF, pass --build-arg IDF_CHECKOUT_REF=commit-id.
# It is possibe to combine both, e.g.:
#   IDF_CLONE_BRANCH_OR_TAG=release/vX.Y
#   IDF_CHECKOUT_REF=<some commit on release/vX.Y branch>.
# Docker build for ESP-IDF v4.4.8
# docker build -t sle118/squeezelite-esp32-idfv448:4.4.8 .
# Updating the docker image in the repository
# docker push sle118/squeezelite-esp32-idfv448:4.4.8
# or to do both:
# docker build -t sle118/squeezelite-esp32-idfv448:4.4.8 . && docker push sle118/squeezelite-esp32-idfv448:4.4.8
#
# (windows) To run the image interactive : 
# docker run --rm -v %cd%:/project -w /project -it sle118/squeezelite-esp32-idfv448:4.4.8
# (windows powershell)
# docker run --rm -v ${PWD}:/project -w /project -it sle118/squeezelite-esp32-idfv448:4.4.8
# (linux) To run the image interactive :
# docker run --rm -v `pwd`:/project -w /project -it sle118/squeezelite-esp32-idfv448:4.4.8
# to build the web app inside of the interactive session
# pushd components/wifi-manager/webapp/ && npm install && npm run-script build && popd
#
# to run the docker with netwotrk port published on the host:
# (windows)
# docker run --rm -p 5000:5000/tcp -v %cd%:/project -w /project -it sle118/squeezelite-esp32-idfv448:4.4.8
# (linux)
# docker run --rm -p 5000:5000/tcp -v `pwd`:/project -w /project -it sle118/squeezelite-esp32-idfv448:4.4.8


ARG IDF_CLONE_URL=https://github.com/espressif/esp-idf.git
ARG IDF_CLONE_BRANCH_OR_TAG=v4.4.8
ARG IDF_CHECKOUT_REF=e499576efdb086551abe309a72899302f82077b7
ARG PUNCOVER_CHECKOUT_REF=1fff5e04fcac4e975846b19677c3e5eb2b520b5a

ENV IDF_PATH=/opt/esp/idf
ENV IDF_TOOLS_PATH=/opt/esp

SHELL ["/bin/bash", "--login", "-c"]

# We need libpython2.7 due to GDB tools
# we also need npm 8 for the webapp to work
RUN : \
  && apt-get update \
  && apt-get install -y \
    apt-utils \
    build-essential \
    bison \
    ca-certificates \
    ccache \
    check \
    curl \
    flex \
    git \
    git-lfs \    
    gperf \
    lcov \
    libbsd-dev \    
    libpython3.8 \
    libffi-dev \
    libncurses-dev \
    libusb-1.0-0-dev \
    make \
    ninja-build \
    python3.8 \
    python3-pip \
    python3-venv \
    ruby \
    unzip \
    wget \
    xz-utils \
    zip \
   	npm \
  	nodejs \
  && apt-get autoremove -y \
  && rm -rf /var/lib/apt/lists/* \
  && update-alternatives --install /usr/bin/python python /usr/bin/python3 10 \
  && python -m pip install --upgrade \
    pip \
    virtualenv \
  && :

RUN : \
  && cd /opt \
  && git clone --depth 1 --branch 0.6.1 https://github.com/HBehrens/puncover.git \
  && cd puncover \
  && git checkout $PUNCOVER_CHECKOUT_REF \
  && python -m pip install . \
  && :

RUN : \
  && echo IDF_CHECKOUT_REF=$IDF_CHECKOUT_REF IDF_CLONE_BRANCH_OR_TAG=$IDF_CLONE_BRANCH_OR_TAG \
  && git clone --depth 1 --branch "$IDF_CLONE_BRANCH_OR_TAG" \
      --recurse-submodules --shallow-submodules \
      "$IDF_CLONE_URL" "$IDF_PATH" \
	&& if [ -n "$IDF_CHECKOUT_REF" ]; then \
      cd $IDF_PATH \
  &&  git checkout $IDF_CHECKOUT_REF \
  &&  git submodule update --init --recursive; \
    fi \
  && update-ca-certificates --fresh \
  && :

# Keep this costly, version-pinned toolchain install in its own layer. Changes to
# later CI helpers or web tooling can then reuse it from Docker's cache.
RUN : \
  && $IDF_PATH/tools/idf_tools.py --non-interactive install required \
  && $IDF_PATH/tools/idf_tools.py --non-interactive install cmake \
  && $IDF_PATH/tools/idf_tools.py --non-interactive install-python-env \
  && :
RUN : \
  echo Installing pygit2  ******************************************************** \
  && . "$IDF_PATH/export.sh" \
  && ln -sf "$(command -v python)" /usr/local/bin/python \
  && pip install pygit2 requests \
  && pip show pygit2 \ 
  && python --version \  
  && pip --version \
  && pip install protobuf  grpcio-tools \
  && rm -rf $IDF_TOOLS_PATH/dist \
  && :

# Let ESP-IDF export.sh provide version-specific tool paths at runtime.
ENV GCC_TOOLS_BASE="/opt/esp/tools/xtensa-esp32-elf/esp-2021r2-patch5-8.4.0/xtensa-esp32-elf/bin/xtensa-esp32-elf-"
ENV IDF_PATH="/opt/esp/idf"
ENV IDF_PYTHON_ENV_PATH="/opt/esp/python_env/idf4.4_py3.8_env"
ENV IDF_TOOLS_EXPORT_CMD="/opt/esp/idf/export.sh"
ENV IDF_TOOLS_INSTALL_CMD="/opt/esp/idf/install.sh"
ENV IDF_TOOLS_PATH="/opt/esp"
ENV PATH="$IDF_PYTHON_ENV_PATH:$IDF_PATH/tools:$PATH"
# Ccache is installed, enable it by default

# The constraint file has been downloaded and the right Python package versions installed. No need to check and
# download this at every invocation of the container.
ENV IDF_PYTHON_CHECK_CONSTRAINTS=no

# Ccache is installed, enable it by default
ENV IDF_CCACHE_ENABLE=1

# Install QEMU runtime dependencies
RUN : \
  && apt-get update && apt-get install -y -q \
    libglib2.0-0 \
    libpixman-1-0 \
  && rm -rf /var/lib/apt/lists/* \
  && :

# Install QEMU
ARG QEMU_VER=esp-develop-20220919
ARG QEMU_DIST=qemu-${QEMU_VER}.tar.bz2
ARG QEMU_SHA256=f6565d3f0d1e463a63a7f81aec94cce62df662bd42fc7606de4b4418ed55f870
RUN : \
  && wget --no-verbose https://github.com/espressif/qemu/releases/download/${QEMU_VER}/${QEMU_DIST} \
  && echo "${QEMU_SHA256} *${QEMU_DIST}" | sha256sum --check --strict - \
  && tar -xf ${QEMU_DIST} -C /opt \
  && rm ${QEMU_DIST} \
  && :

COPY docker/entrypoint.sh /opt/esp/entrypoint.sh
COPY components/wifi-manager/webapp/package.json /opt

# Install nvm with node and npm
# RUN wget -qO- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.1/install.sh | bash \
#     && export NVM_DIR="$([ -z "${XDG_CONFIG_HOME-}" ] && printf %s "${HOME}/.nvm" || printf %s "${XDG_CONFIG_HOME}/nvm")" \
#     && [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh" \
#     && nvm install $NODE_VERSION \
#     && nvm alias default $NODE_VERSION \
#     && nvm use default \
#     && echo installing nodejs version 16  \
#     && curl -sL https://deb.nodesource.com/setup_16.x | bash - \
#     && echo installing node modules  \
#     && cd /opt \
#     && nvm use default \
#     && npm install -g \  
#     && :    

RUN : \
  && curl -fsSL https://deb.nodesource.com/setup_16.x | bash - \
  && apt-get install -y nodejs jq \
  && echo installing dev node modules globally \
  && cd /opt \
  && cat ./package.json | jq '.devDependencies | keys[] as $k | "\($k)@\(.[$k])"' | xargs -t npm install --global \
  && echo installing npm global packages \
  && node --version \
  && npm install -g \  
  && :      
RUN : \
  && npm install -g html-webpack-plugin 


ENV NODE_PATH=/usr/lib/node_modules
COPY ./docker/build_tools.py /usr/sbin/build_tools.py
RUN : \
  && echo Changing permissions ********************************************************  \
  && chmod +x /opt/esp/entrypoint.sh \
  && chmod +x /usr/sbin/build_tools.py \  
  && :



ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]
CMD [ "/bin/bash" ]
