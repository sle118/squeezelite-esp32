#!/bin/bash

# ================================================================================
# Script for use on build server for generating scripts and documentation that can be distributed with
# the release bundle
# ================================================================================

# Location of partitions.csv relative to this script
partitionsCsv="./partitions.csv"

mkdir -p ./build

# File to output readme instructions to
outputReadme="./build/README.txt"

# File to output bash script to
outputBashScript="./build/writeSequeezeEsp.sh"

# File to output bat script to
outputBatScript="./build/writeSequeezeEsp.bat"

# The name of partitions to ignore from partitions.csv
paritionsToIgnore=(
  "nvs"
  "phy_init"
)

# Function that maps partition name to actual bin file
# defaults to "[PARTION_NAME_FROM_CSV].bin"
function partitionNameToBinFile {
  if [[ "$1" == "otadata" ]]; then
    echo "ota_data_initial.bin"
  elif [[ "$1" == "ota_0" ]]; then
    echo "squeezelite.bin"
  else
    echo $1.bin
  fi
}

# write parameters for esptool.py
writeParameters="$writeParameters write_flash"
writeParameters="$writeParameters --flash_mode dio --flash_freq 80m --flash_size detect"

# bootloader.bin and partitions.bin not in partitions.csv so manually add here
partitionsParameters=" 0x1000 bootloader/bootloader.bin"
partitionsParameters="$partitionsParameters 0x8000 partitions.bin"

# ==============================================================================

# Loop over partitions.csv and add partition bins and offsets to partitionsParameters
{
  read;
  read;
  while read -r line
  do
      partitionName=$(echo $line | awk -F', ' '{printf "%s", $1}' | tr -d '"')
      partitionOffset=$(echo $line | awk -F', ' '{printf "%s", $4}' | tr -d '"')
      partitionFile=$(partitionNameToBinFile $partitionName)

      if [[ " ${paritionsToIgnore[@]} " =~ " ${partitionName} " ]]; then
          continue
      fi

      partitionsParameters="$partitionsParameters $partitionOffset $partitionFile"
  done
} < $partitionsCsv

# Write README Instructions
if [ ! -f "$outputReadme" ]; then
  touch $outputReadme
fi

echo "" >> $outputReadme
echo "Below you'll find details on how to flash squeezelite-esp on different platforms" >> $outputReadme
echo "In all cases your squeezelite-esp will start in recovery mode. Setup Wifi and" >> $outputReadme
echo "then click on reboot within the system tab. And the squeezelite-esp should boot" >> $outputReadme
echo "into full mode" >> $outputReadme
echo "" >> $outputReadme
echo "====LINUX====" >> $outputReadme
echo "To flash sequeezelite run the following script:" >> $outputReadme
echo "$outputBashScript [PORT_HERE] [BAUD_RATE]" >> $outputReadme
echo "e.g. $outputBashScript /dev/ttyUSB0 115200" >> $outputReadme
echo "" >> $outputReadme
echo "====WINDOWS====" >> $outputReadme
echo "To flash sequeezelite run the following script:" >> $outputReadme
echo "$outputBatScript [PORT_HERE] [BAUD_RATE]" >> $outputReadme
echo "e.g. $outputBatScript COM11 115200" >> $outputReadme
echo "" >> $outputReadme
echo "If you don't know how to run the BAT file with arguments then you can" >> $outputReadme
echo "edit the bat file in Notepad. Open the file up and edit the following:" >> $outputReadme
echo "Change 'set port=%1' to 'set port=[PORT_HERE]'. E.g. 'set port=COM11'" >> $outputReadme
echo "Change 'set baud=%2' to 'set baud=[BAUD_RATE]'. E.g. 'set baud=115200'" >> $outputReadme
echo "" >> $outputReadme
echo "====MANUAL====" >> $outputReadme
echo "Python esptool.py --port [PORT_HERE] --baud [BAUD_RATE] $writeParameters $partitionsParameters" >> $outputReadme


# Write Linux BASH File
if [ ! -f "$outputBashScript" ]; then
  touch $outputBashScript
fi

echo "#!/bin/bash" >> $outputBashScript
echo >> $outputBashScript
echo "port=\$1" >> $outputBashScript
echo "baud=\$2" >> $outputBashScript
linuxFlashCommand="Python esptool.py --port \$port --baud \$baud"
echo "$linuxFlashCommand $writeParameters $partitionsParameters" >> $outputBashScript

# Write Windows BAT File
if [ ! -f "$outputBatScript" ]; then
  touch $outputBatScript
fi

echo "echo off" >> $outputBatScript
echo "" >> $outputBatScript
echo "set port=%1" >> $outputBatScript
echo "set baud=%2" >> $outputBatScript
windowsFlashCommand="Python esptool.py --port %port% --baud %baud%"
echo "$windowsFlashCommand $writeParameters $partitionsParameters" >> $outputBatScript
