
import binascii
import os
import sys

current_dir = os.path.dirname(os.path.realpath(__file__))
protobuf_py_path = os.path.join(current_dir, '..', '..', 'build', 'protobuf', 'py')
nanopb_generator_proto_path = os.path.join(current_dir, '..', '..', 'build', 'protobuf', 'proto', 'nanopb', 'generator', 'proto')

# Adding paths to sys.path
sys.path.append(protobuf_py_path)
sys.path.append(nanopb_generator_proto_path)

import configuration_pb2  

def convert_string_to_bytes(input_string):
    # Replace Python-style escape sequences with actual bytes
    return bytes(input_string, "utf-8").decode("unicode_escape").encode("latin1")

def main():
 
    print(f'Utility to decode the content of an encoded string as copied from the console logs')
    # Prompt the user to enter the string
    input_string = input("Enter the protobuf data string: ")

    # Convert the string to bytes
    binary_data = convert_string_to_bytes(input_string)

    # Parse the binary data
    message = configuration_pb2.Config()  # Assuming the message type is Configuration
    message.ParseFromString(binary_data)

    # Now you can access fields of the message
    print(message)

if __name__ == "__main__":
    main()
