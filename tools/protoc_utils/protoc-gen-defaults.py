# !/usr/bin/env python
import os

import logging
import json
from pathlib import Path
from typing import Dict, List
from google.protobuf.compiler import plugin_pb2 as plugin
from google.protobuf.message_factory import GetMessageClass
from google.protobuf.descriptor_pb2 import FileDescriptorProto, DescriptorProto, FieldDescriptorProto,FieldOptions
from google.protobuf.descriptor import FieldDescriptor, Descriptor, FileDescriptor
from ProtoElement import ProtoElement
from ProtocParser import ProtocParser


logger = logging.getLogger(__name__)
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
class BinDefaultsParser(ProtocParser) :
    def start_message(self,message:ProtoElement) :
        super().start_message(message)
    def end_message(self,message:ProtoElement):        
        super().end_message(message)
        self.has_error = False
        default_structure = message.render()    
        if not default_structure:
            logger.warn(f'No default values for {message.name}')
            return
        respfile = self.response.file.add()

        outfilename =  f'{message.name}_defaults_pb.bin'
        with open(os.path.join(self.param_dict.get('defaultspath','.'),outfilename), 'wb') as bin_file:
            res = default_structure.SerializeToString()
            bin_file.write(res)
            logger.info(f'Wrote {bin_file.name}')
        respfile.name = f'{outfilename}.gen'
        logger.info(f"Creating binary file for defaults: {respfile.name}")
        respfile.content = f'Content written to {respfile.name}'

    def start_file(self,file:FileDescriptor) :
        super().start_file(file)
        

    def end_file(self,file:ProtoElement) :
        super().end_file(file)


    def get_name(self)->str:
           return 'protoc_plugin_defaults'
   
    def add_comment_if_exists(element, comment_type: str, path: str) -> dict:
        comment = getattr(element, f"{comment_type}_comment", "").strip()
        return {f"__{comment_type}_{path}": comment} if comment else {}    
    def repeated_render(self,element:ProtoElement,obj:any):
        return [obj] if element.repeated else obj
    def render(self,element: ProtoElement):
        if len(element.childs)>0:
            oneof = getattr(element.descriptor,'containing_oneof',None)
            if oneof:
                # we probably shouldn't set default values here
                pass
            has_render = False
            for child in element.childs:
                rendered = child.render()
                if rendered:
                    has_render = True
                    # try:
                    if child.repeated:
                        try:
                            getattr(element.message_instance,child.name).extend(rendered)
                        except:
                            getattr(element.message_instance,child.name).extend( [rendered])

                    elif child.type == FieldDescriptor.TYPE_MESSAGE:
                        getattr(element.message_instance,child.name).CopyFrom(rendered)
                    else:
                        setattr(element.message_instance,child.name,rendered)
                    # except:
                    #     logger.error(f'Unable to assign value from {child.fullname} to {element.fullname}')
                    element.message_instance.SetInParent()
            if not has_render:
                return None
            
        else:
            
            default_value = element._default_value
            options = element.options['cust_field'] if 'cust_field' in element.options else None
            msg_options = element.options['cust_msg'] if 'cust_msg' in element.options else None
            init_from_mac = getattr(options,'init_from_mac', False) or getattr(msg_options,'init_from_mac', False) 

            
            default_value = getattr(options,'default_value', None)
            global_name = getattr(options,'global_name', None)
            const_prefix = getattr(options,'const_prefix', self.param_dict['const_prefix'])
            if init_from_mac:
                default_value = f'{const_prefix}@@init_from_mac@@'
            elif default_value:
                if element.descriptor.cpp_type  == FieldDescriptor.CPPTYPE_STRING:
                    default_value = default_value
                elif element.descriptor.cpp_type == FieldDescriptor.CPPTYPE_ENUM:
                    try:
                        default_value = element.enum_values.index(default_value)
                    except:
                        raise ValueError(f'Invalid default value {default_value} for {element.path}')
                elif element.descriptor.cpp_type in [FieldDescriptor.CPPTYPE_INT32, FieldDescriptor.CPPTYPE_INT64,
                                                    FieldDescriptor.CPPTYPE_UINT32, FieldDescriptor.CPPTYPE_UINT64]:
                    int_value = int(default_value)
                    if element.descriptor.cpp_type in [FieldDescriptor.CPPTYPE_UINT32, FieldDescriptor.CPPTYPE_UINT64] and int_value < 0:
                        raise ValueError(f"Negative value for unsigned int type trying to assign {element.path} = {default_value}")
                    default_value = int_value
                elif element.descriptor.cpp_type in [FieldDescriptor.CPPTYPE_DOUBLE, FieldDescriptor.CPPTYPE_FLOAT]:
                    float_value = float(default_value)
                    if '.' not in default_value:
                        raise ValueError(f"Integer string for float/double type trying to assign {element.path} = {default_value}")
                    default_value = float_value
                elif element.descriptor.cpp_type == FieldDescriptor.CPPTYPE_BOOL:
                    if default_value.lower() in ['true', 'false']:
                        default_value = default_value.lower() == 'true'
                    else:
                        raise ValueError(f'Invalid boolean value trying to assign {element.path} = {default_value}')
            if default_value:
                element.message_instance.SetInParent()
            return self.repeated_render(element,default_value) if default_value else None
        return element.message_instance

if __name__ == '__main__':
    data = ProtocParser.get_data()
    logger.info(f"Generating binary files for defaults")
    protocParser:BinDefaultsParser = BinDefaultsParser(data)
    protocParser.process()
    logger.debug('Done generating JSON file(s)')    
