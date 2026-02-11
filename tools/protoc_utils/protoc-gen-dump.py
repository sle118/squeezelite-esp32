#!/usr/bin/env python3
import argparse
import sys
import os
from google.protobuf.compiler import plugin_pb2 as plugin
from google.protobuf.compiler.plugin_pb2 import  CodeGeneratorResponse
from urllib import parse

import logging
logger = logging.getLogger(__name__)
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)

# def main():
#     data = sys.stdin.buffer.read()
    # with open("C:/Users/sle11/Documents/VSCode/squeezelite-esp32/protobuf/generated/src/code_generator_request.bin", "wb") as file:
    #     file.write(data)

# if __name__ == "__main__":
#     main()



def process(
    request: plugin.CodeGeneratorRequest, response: CodeGeneratorResponse, data
) -> None:
    logger.debug(f'Received parameter(s): {request.parameter}')
    params = request.parameter.split(',')
    param_dict = {p.split('=')[0]: parse.unquote(p.split('=')[1]) for p in params if '=' in p}
    param_dict['path'] = param_dict['path'].split('?')
    basename = "code_generator_request.bin"
    binpath = os.path.join(param_dict.get('binpath', './'),basename)
    file:CodeGeneratorResponse.File = response.file.add()
    file.name = f"{basename}.txt"
    file.content = f'Generated binary file {binpath}'
    logger.info(f"Dumping CodeGeneratorRequest object to : {binpath}")
    
    with open(binpath, "wb") as file:
        file.write(data)
def GetData():
    
    parser = argparse.ArgumentParser(description='Process protobuf and JSON files.')
    parser.add_argument('--source', help='Python source file', default=None)
    args = parser.parse_args()
    if args.source:
        logger.info(f'Loading request data from {args.source}')
        with open(args.source, 'rb') as file:
            data = file.read()
    else:
        data = sys.stdin.buffer.read()
    return data

def main():
    data = GetData()
    request = plugin.CodeGeneratorRequest.FromString(data)
    response = CodeGeneratorResponse()
    process(request, response,data)
    sys.stdout.buffer.write(response.SerializeToString())
    logger.info('Done dumping request')



if __name__ == '__main__':
    main()

