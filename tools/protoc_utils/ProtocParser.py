# !/usr/bin/env python
from functools import partial
import sys
import json
from typing import Callable, Dict, List
import argparse
from abc import ABC, abstractmethod
import google.protobuf.descriptor_pool as descriptor_pool

from google.protobuf import message_factory,message
from google.protobuf.message_factory import GetMessageClassesForFiles
from google.protobuf.compiler import plugin_pb2 as plugin
from google.protobuf.descriptor import FieldDescriptor, Descriptor, FileDescriptor
from google.protobuf.descriptor_pb2 import FileDescriptorProto, DescriptorProto, FieldDescriptorProto,FieldOptions
from urllib import parse
from ProtoElement import ProtoElement
import logging

logger = logging.getLogger(__name__)
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)

   
class ProtocParser(ABC) :

    request:plugin.CodeGeneratorRequest
    response:plugin.CodeGeneratorResponse
    elements:List[ProtoElement] = []
    comments: Dict[str, str] = {}
    json_content = {}
    main_class_list:List[str] = []
    param_dict:Dict[str,str] = {}
    pool:descriptor_pool.DescriptorPool
    factory:message_factory

    @abstractmethod
    def render(self,element: ProtoElement):
        pass

    @abstractmethod
    def get_name(self)->str:
        pass    
    @abstractmethod
    # def start_element(self,element:ProtoElement):
    #     logger.debug(f'START Processing ELEMENT {element.path}')
    # @abstractmethod
    # def end_element(self,element:ProtoElement):
    #     logger.debug(f'END Processing ELEMENT {element.path}')
    @abstractmethod
    def end_message(self,classElement:ProtoElement):
        logger.debug(f'END Processing MESSAGE {classElement.name}')
    @abstractmethod
    def start_message(self,classElement:ProtoElement) :
        logger.debug(f'START Processing MESSAGE {classElement.name}')
    @abstractmethod
    def start_file(self,file:FileDescriptor) :
        logger.debug(f'START Processing file {file.name}')
    @abstractmethod
    def end_file(self,file:FileDescriptor) :
        logger.debug(f'END Processing file {file.name}')
   
    def __init__(self,data):
        self.request = plugin.CodeGeneratorRequest.FromString(data)
        self.response = plugin.CodeGeneratorResponse()
        logger.info(f'Received ${self.get_name()} parameter(s): {self.request.parameter}')
        params = self.request.parameter.split(',')
        self.param_dict = {p.split('=')[0]: parse.unquote(p.split('=')[1]) for p in params if '=' in p}
        if not 'const_prefix' in self.param_dict:
            self.param_dict['const_prefix'] = ""
            logger.warn("No option passed for const_prefix. No prefix will be used for option init_from_mac")
        self.main_class_list = self.get_arg(name= 'main_class',split=True,split_char='!')
        if 'path' in self.param_dict:
            self.param_dict['path'] = self.param_dict['path'].split('?')
            for p in self.param_dict['path']:
                logger.debug(f'Adding to path: {p}')
                sys.path.append(p)
        import customoptions_pb2 as custom__options__pb2
        
    def get_arg(self,name:str,default=None,split:bool=False,split_char:str=';'):
        result = self.param_dict.get(name, default)
        if result and len(result) == 0:
            if not default:
                logger.error(f'Plugin parameter {name} not found')
                result = None
            else:
                result = default
                logger.warn(f'Plugin parameter {name} not found. Defaulting to {str(default)}')
        if split and result:
            result = result.split(split_char)
        logger.debug(f'Returning argument {name}={str(result)}')
        return result
    def get_name_attr(self,proto_element):
        attributes = ['package','name']
        for att in attributes:
            if hasattr(proto_element, att):
                return att
        return None
    def interpret_path(self,path, proto_element):
        if not path:
            if hasattr(proto_element,"name"):
                return proto_element.name
            else:
                return ''

        # Get the next path element
        path_elem = path[0]
        name_att = self.get_name_attr(proto_element)
        if name_att:
            elem_name = getattr(proto_element, name_att)  
            elem_sep = '.'
        else:
            elem_name = ''
            elem_sep = ''

        # Ensure the proto_element has a DESCRIPTOR attribute
        if hasattr(proto_element, 'DESCRIPTOR'):
            # Use the DESCRIPTOR to access field information
            descriptor = proto_element.DESCRIPTOR

            # Get the field name from the descriptor
            try:
                field = descriptor.fields_by_number[path_elem]
            except:
                return None
            
            field_name = field.name
            field_name = field_name.lower().replace('_field_number', '')

            # Access the field if it exists
            if field_name == "extension" :
                return field_name

            elif hasattr(proto_element, field_name):
                next_element = getattr(proto_element, field_name)
                if isinstance(next_element,list):
                    # If the next element is a list, use the next path element as an index
                    return f'{elem_name}{elem_sep}{self.interpret_path(path[1:], next_element[path[1]])}'
                else:
                    # If it's not a list, just continue with the next path element
                    return f'{elem_name}{elem_sep}{self.interpret_path(path[1:], next_element)}'
        else: 
            return f'{elem_name}{elem_sep}{self.interpret_path(path[1:], proto_element[path_elem])}'
        # If the path cannot be interpreted, return None or raise an error
        return None


    def extract_comments(self,proto_file: FileDescriptorProto):
        for location in proto_file.source_code_info.location:
            # The path is a sequence of integers identifying the syntactic location
            path = tuple(location.path)
            leading_comments = location.leading_comments.strip()
            trailing_comments = location.trailing_comments.strip()
            if len(location.leading_detached_comments)>0:
                logger.debug('found detached comments')

            leading_detached_comments = '\r\n'.join(location.leading_detached_comments)
            if len(leading_comments) == 0 and len(trailing_comments) == 0 and len(leading_detached_comments) == 0:
                continue
            # Interpret the path and map it to a specific element
            # This is where you'll need to add logic based on your protobuf structure
            element_identifier = self.interpret_path(path, proto_file)
            if element_identifier is not None:
                self.comments[f"{element_identifier}.leading"] = leading_comments
                self.comments[f"{element_identifier}.trailing"] = trailing_comments
                self.comments[f"{element_identifier}.detached"] = leading_detached_comments
                


    def get_comments(self,field: FieldDescriptorProto, proto_file: FileDescriptorProto,message: DescriptorProto):
        if hasattr(field,'name') :
            name  = getattr(field,'name')
            commentspath = f"{proto_file.package}.{message.name}.{name}"
            if commentspath in self.comments:
                return  commentspath,self.comments[commentspath]
        return None,None

    def get_nested_message(self, field: FieldDescriptorProto, proto_file: FileDescriptorProto):
        # Handle nested message types
        if field.type != FieldDescriptorProto.TYPE_MESSAGE:
            return None

        nested_message_name = field.type_name.split('.')[-1]
        # logger.debug(f'Looking for {field.type_name} ({nested_message_name}) in {nested_list}')

        nested_message= next((m for m in proto_file.message_type if m.name == nested_message_name), None)
        if not nested_message:
            # logger.debug(f'Type {nested_message_name} was not found in file {proto_file.name}. Checking in processed list: {processed_list}')
            nested_message = next((m for m in self.elements if m.name == nested_message_name), None)
            if not nested_message:
                logger.error(f'Could not locate message class {field.type_name} ({nested_message_name})')
        return nested_message

    def process_message(self,message: ProtoElement, parent:ProtoElement = None )->ProtoElement:
        if not message:
            return 

        if not message.fields:
            logger.warn(f"{message.path} doesn't have fields!")
            return
        for field in message.fields:
            element = ProtoElement(
                    parent=message,
                    descriptor=field
                )
            logging.debug(f'Element: {element.path}')
            if getattr(field,"message_type",None):
                self.process_message(element,message)

    @property
    def packages(self)->List[str]:
        return list(set([proto_file.package for proto_file in self.request.proto_file if proto_file.package]))
    @property
    def file_set(self)->List[FileDescriptor]:
        return list(set([ self.pool.FindMessageTypeByName(message).file for message in self.main_class_list if self.pool.FindMessageTypeByName(message)]))
   
    @property
    def proto_files(self)->List[FileDescriptorProto]:
        return list(
                proto_file for proto_file in self.request.proto_file if
                not proto_file.name.startswith("google/") 
                and not proto_file.name.startswith("nanopb") 
                and not proto_file.package.startswith("google.protobuf")
            )
    

    def get_main_messages_from_file(self,fileDescriptor:FileDescriptor)->List[Descriptor]:
        return [message for name,message in fileDescriptor.message_types_by_name.items() if message.full_name in self.main_class_list]
    def process(self) -> None:
        if len(self.proto_files) == 0:
            logger.error('No protocol buffer file selected for processing')
            return
        self.setup()
        logger.info(f'Processing message(s) {", ".join([name for name in self.main_class_list ])}')

        for fileObj in self.file_set :
            self.start_file(fileObj)
            for message in self.get_main_messages_from_file(fileObj):
                element = ProtoElement( descriptor=message )
                self.start_message(element)
                self.process_message(element)
                self.end_message(element)
            self.end_file(fileObj)
        sys.stdout.buffer.write(self.response.SerializeToString())

    def setup(self):

        for proto_file in self.proto_files:
            logger.debug(f"Extracting comments from : {proto_file.name}")
            self.extract_comments(proto_file)
        self.pool = descriptor_pool.DescriptorPool()
        self.factory = message_factory.MessageFactory(self.pool)
        for proto_file in self.request.proto_file:
            logger.debug(f'Adding {proto_file.name} to pool')
            self.pool.Add(proto_file)
        self.messages = GetMessageClassesForFiles([f.name for f in self.request.proto_file], self.pool)                        
        ProtoElement.set_pool(self.pool)
        ProtoElement.set_render(self.render)
        ProtoElement.set_logger(logger)
        ProtoElement.set_comments_base(self.comments)        
        ProtoElement.set_prototypes(self.messages)

    @property
    def main_messages(self)->List[ProtoElement]:
        return [ele for ele in self.elements if ele.main_message ]
    
    def get_message_descriptor(self, name) -> Descriptor:
        for package in self.packages:
            qualified_name = f'{package}.{name}' if package else name
            
            try:
                descriptor = self.pool.FindMessageTypeByName(qualified_name)
                if descriptor:
                    return descriptor
            except:
                pass
        return None

    @classmethod
    def get_data(cls):
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

