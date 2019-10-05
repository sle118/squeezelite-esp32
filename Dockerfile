FROM ubuntu:18.04

RUN apt-get update && apt-get install -y git wget libncurses-dev flex bison gperf \
  python python-pip python-setuptools python-serial python-click \
  python-cryptography python-future python-pyparsing \
  python-pyelftools cmake ninja-build ccache libusb-1.0

RUN mkdir /workspace
WORKDIR /workspace

# Download and checkout known good esp-idf commit
RUN git clone --recursive https://github.com/espressif/esp-idf.git esp-idf
RUN cd esp-idf && git checkout 4dac7c7df885adaa86a5c79f2adeaf8d68667349
RUN git clone https://github.com/sle118/squeezelite-esp32.git

# Download GCC 5.2.0
RUN wget https://dl.espressif.com/dl/xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz
RUN tar -xzf xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz
RUN rm xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz

# Patch I2S in esp-idf
RUN cp /workspace/squeezelite-esp32/idf-patch/i2s.c /workspace/esp-idf/components/driver/i2s.c
RUN rm -r /workspace/squeezelite-esp32
RUN mkdir /workspace/squeezelite-esp32

# Setup PATH to use esp-idf and gcc-5.2.0
RUN touch /root/.bashrc && \
 echo export PATH="\$PATH:/workspace/xtensa-esp32-elf/bin" >> /root/.bashrc && \
 echo export IDF_PATH=/workspace/esp-idf >> /root/.bashrc

# OPTIONAL: Install vim for text editing in Bash
RUN apt-get update && apt-get install -y vim

WORKDIR /workspace/squeezelite-esp32
CMD ["bash"]
