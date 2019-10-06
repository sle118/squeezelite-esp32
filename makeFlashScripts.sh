#!/bin/bash

# Location of partitions.csv relative to this script
partitionsCsv="./partitions.csv"

# File to output readme instructions to
outputReadme="./flash-scripts/README.txt"

# File to output bash script to
outputBashScript="./flash-scripts/writeSequeezeEsp.sh"

# File to output bat script to
outputBatScript="./flash-scripts/writeSequeezeEsp.bat"

# The name of partitions to ignore from partitions.csv
paritionsToIgnore=(
  "nvs"
  "phy_init"
  "storage"
  "coredump"
)

# Function that maps partition name to actual bin file
# defaults to "[PARTION_NAME_FROM_CSV].bin"
function partitionNameToBinFile {
  if [[ "$1" == "otadata" ]]; then
    echo "ota_data_initial.bin"
  elif [[ "$1" == "ota_0" || "$1" == "factory" ]]; then
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
