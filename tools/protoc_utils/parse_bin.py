#!/opt/esp/python_env/idf4.4_py3.8_env/bin/python
import json
import os
import sys
import argparse
import importlib.util
import logging
from pathlib import Path
from google.protobuf import json_format
from google.protobuf.json_format import MessageToJson


# Assuming this script is in the same directory as the generated Python files
# script_dir = os.path.join(os.path.dirname(os.path.realpath(__file__)),'generated/src')
# sys.path.append(script_dir)

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')


def load_protobuf_module(source, includes):
    """Dynamically load a protobuf module given its file name."""
    for include in includes:
        sys.path.append(include)

    module_name = Path(source).stem
    module_file = module_name + '_pb2.py'
    module_location = Path(source).parent / module_file

    logging.info(f'Loading module {module_file} from {module_location} with includes [{", ".join(includes)}]')

    spec = importlib.util.spec_from_file_location(name=module_name, location=str(module_location))
    if spec is not None:
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        logging.info(f'Loaded protobuf module: {module_name}')
        return module

    else:
        logging.error(f'Failed to load module {module_file} from {module_location}')
        return None



def protobuf_to_dict(message):
    """Convert a protobuf message to a dictionary."""
    # Convert the protobuf message to a JSON string
    json_str = MessageToJson(message, including_default_value_fields=True)
    # Parse the JSON string back into a dictionary
    return json.loads(json_str)


def main():
    parser = argparse.ArgumentParser(description='Process protobuf and JSON files.')
    parser.add_argument('--proto_file', help='Name of the protobuf file (without extension)')
    parser.add_argument('--main_class', help='Main message class to process')
    parser.add_argument('--source', help='Source file to parse')
    parser.add_argument('--include', help='Directory where message python files can be found', default=None,action = 'append' )
    parser.add_argument('--json', help='Source JSON file(s)',action = 'append' )
    args = parser.parse_args()

    # Load the protobuf module
    logging.info(f'Loading modules')
    proto_module = load_protobuf_module(args.proto_file, args.include)

    # Determine the main message class
    main_message_class = getattr(proto_module, args.main_class)
    message = main_message_class()

    with open(args.source, 'rb') as bin_file:  # Open in binary mode
        data =bin_file.read()
        message.ParseFromString(data)
        logging.info(f'Parsed: {MessageToJson(message)}')
        
            
    # for jsonfile in args.json:
    #     output_file_base = os.path.join(args.target_dir, Path(jsonfile).stem+".bin")
    #     logging.info(f'Converting JSON file {jsonfile} to binary format')
    #     with open(jsonfile, 'r') as json_file:
    #         json_data = json.load(json_file)
    #     json_format.ParseDict(json_data, message)
    #     binary_data = message.SerializeToString()
    #     with open(output_file_base, 'wb') as bin_file:
    #         bin_file.write(binary_data)
    #     logging.info(f'Binary file written to {output_file_base}')


if __name__ == '__main__':
    main()
