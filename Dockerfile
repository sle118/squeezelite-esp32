FROM espressif/idf:v4.4.6

# Install additional dependencies
RUN apt-get update && apt-get install -y \
  python3-protobuf 

RUN DEBIAN_FRONTEND=noninteractive TZ=America/Toronto apt-get -y install tzdata
  # Add any other dependencies you need

  # To build the image for a branch or a tag of IDF, pass --build-arg IDF_CLONE_BRANCH_OR_TAG=name.
  # To build the image with a specific commit ID of IDF, pass --build-arg IDF_CHECKOUT_REF=commit-id.
  # It is possibe to combine both, e.g.:
  #   IDF_CLONE_BRANCH_OR_TAG=release/vX.Y
  #   IDF_CHECKOUT_REF=<some commit on release/vX.Y branch>.
  # Docker build for release 4.3.5 as of 2023/05/18
  # docker build . --build-arg IDF_CHECKOUT_REF=6d04316cbe4dc35ea7e4885e9821bd9958ac996d -t sle118/squeezelite-esp32-idfv446 
  # Updating the docker image in the repository
  # docker push sle118/squeezelite-esp32-idfv446
  # or to do both:
  # docker build . --build-arg IDF_CHECKOUT_REF=6d04316cbe4dc35ea7e4885e9821bd9958ac996d -t sle118/squeezelite-esp32-idfv446 && docker push sle118/squeezelite-esp32-idfv446
  # docker build . -t sle118/squeezelite-esp32-idfv446 && docker push sle118/squeezelite-esp32-idfv446
  #docker run --isolation=process --device="class/86E0D1E0-8089-11D0-9CE4-08003E301F73" mcr.microsoft.com/windows/servercore:1809
  # (windows) To run the image interactive : 
  # docker run --rm -v %cd%:/project -w /project -it sle118/squeezelite-esp32-idfv446
  # (windows powershell)
  # docker run --rm -v ${PWD}:/project -w /project -it sle118/squeezelite-esp32-idfv446
  # (linux) To run the image interactive :
  # docker run --rm -v `pwd`:/project -w /project -it sle118/squeezelite-esp32-idfv446
  # to build the web app inside of the interactive session
  # pushd components/wifi-manager/webapp/ && npm install && npm run-script build && popd
  #
  # to run the docker with netwotrk port published on the host:
  # docker run --rm -p 5000:5000/tcp -v %cd%:/project -w /project -it sle118/squeezelite-esp32-idfv446


RUN : \
  echo Installing pygit2  ******************************************************** \
  && . /opt/esp/python_env/idf4.4_py3.8_env/bin/activate \
  && ln -sf /opt/esp/python_env/idf4.4_py3.8_env/bin/python  /usr/local/bin/python \
  && pip install pygit2 requests \
  && pip show pygit2 \ 
  && python --version \  
  && pip --version \
  && pip install protobuf  grpcio-tools \
  && rm -rf $IDF_TOOLS_PATH/dist \
  && apt-get install -y  nodejs \
  && :

# COPY docker/patches $IDF_PATH

#set idf environment variabies
ENV PATH /opt/esp/idf/components/esptool_py/esptool:/opt/esp/idf/components/espcoredump:/opt/esp/idf/components/partition_table:/opt/esp/idf/components/app_update:/opt/esp/tools/xtensa-esp32-elf/esp-2021r2-patch3-8.4.0/xtensa-esp32-elf/bin:/opt/esp/tools/xtensa-esp32s2-elf/esp-2021r2-patch3-8.4.0/xtensa-esp32s2-elf/bin:/opt/esp/tools/xtensa-esp32s3-elf/esp-2021r2-patch3-8.4.0/xtensa-esp32s3-elf/bin:/opt/esp/tools/riscv32-esp-elf/esp-2021r2-patch3-8.4.0/riscv32-esp-elf/bin:/opt/esp/tools/esp32ulp-elf/2.28.51-esp-20191205/esp32ulp-elf-binutils/bin:/opt/esp/tools/esp32s2ulp-elf/2.28.51-esp-20191205/esp32s2ulp-elf-binutils/bin:/opt/esp/tools/cmake/3.16.4/bin:/opt/esp/tools/openocd-esp32/v0.11.0-esp32-20220706/openocd-esp32/bin:/opt/esp/python_env/idf4.4_py3.8_env/bin:/opt/esp/idf/tools:$PATH
ENV GCC_TOOLS_BASE="/opt/esp/tools/xtensa-esp32-elf/esp-2021r2-patch3-8.4.0/xtensa-esp32-elf/bin/xtensa-esp32-elf-"
ENV IDF_PYTHON_ENV_PATH="/opt/esp/python_env/idf4.4_py3.8_env"
ENV OPENOCD_SCRIPTS="/opt/esp/tools/openocd-esp32/v0.10.0-esp32-20211111/openocd-esp32/share/openocd/scripts"

COPY docker/entrypoint.sh /opt/esp/entrypoint.sh
COPY components/wifi-manager/webapp/package.json /opt
ENV NODE_PATH="/v8/lib/node_modules"
ENV NODE_VERSION="8"
ENV NODE_PATH $NVM_DIR/v$NODE_VERSION/lib/node_modules
ENV PATH $IDF_PYTHON_ENV_PATH:$NVM_DIR/v$NODE_VERSION/bin:$PATH
SHELL ["/bin/bash", "--login", "-c"]

# Install NVM, Node.js, and global NPM packages
RUN wget -qO- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.1/install.sh | bash \
    && export NVM_DIR="$HOME/.nvm" \
    && . "$NVM_DIR/nvm.sh" \
    && nvm install 16 \
    && nvm alias default 16 \
    && nvm use default \
    && echo "Node.js version:" && node --version \
    && echo "NPM version:" && npm --version \
    && echo "Installing global NPM packages..." \
    && cd /opt \
    && cat ./package.json | jq '.devDependencies | keys[] as $k | "\($k)@\(.[$k])"' | xargs -t npm install --global \
    && npm install -g html-webpack-plugin \
    && :

COPY ./docker/build_tools.py /usr/sbin/build_tools.py
RUN : \
  && echo Changing permissions ********************************************************  \
  && chmod +x /opt/esp/entrypoint.sh \
  && chmod +x /usr/sbin/build_tools.py \  
  && :



ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]
CMD [ "/bin/bash" ]
