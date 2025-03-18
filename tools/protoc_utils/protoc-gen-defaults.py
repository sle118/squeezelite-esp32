#!/opt/esp/python_env/idf4.4_py3.8_env/bin/python
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
from google.protobuf.json_format import Parse


logger = logging.getLogger(__name__)
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
def is_iterable(obj):
    try:
        iter(obj)
        return True
    except TypeError:
        return False
class BinDefaultsParser(ProtocParser) :
    def start_message(self,message:ProtoElement) :
        super().start_message(message)
    def end_message(self,message:ProtoElement):        
        super().end_message(message)
        self.has_error = False
        default_structure = message.render()    
        if not default_structure:
            logger.warning(f'No default values for {message.name}')
            return
        respfile = self.response.file.add()

        outfilename =  f'{message.name.lower()}.bin'
        output_directory = os.path.join(self.param_dict.get('defaultspath', '.'),"defaults")
        output_path = os.path.join(output_directory, outfilename)

        os.makedirs(output_directory, exist_ok=True)

        with open(output_path, 'wb') as bin_file:
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
        options = element.options['cust_field'] if 'cust_field' in element.options else None
        if len(element.childs)>0:
            oneof = getattr(element.descriptor,'containing_oneof',None)
            if oneof:
                # we probably shouldn't set default values here
                pass
            has_render = False
            for child in element.childs:
                try:
                    rendered = child.render()
                
                    if rendered:
                        has_render = True
                        # try:
                        if child.type == FieldDescriptor.TYPE_MESSAGE:
                            target_field = getattr(element.message_instance, child.name)
                            if child.label == FieldDescriptor.LABEL_REPEATED:
                                # If the field is repeated, iterate over the array and add each instance
                                if is_iterable(rendered) and not isinstance(rendered, str):
                                    for instance in rendered:
                                        target_field.add().CopyFrom(instance)
                                else:
                                    target_field.add().CopyFrom(rendered)
                            else:
                                # For non-repeated fields, use CopyFrom
                                target_field.CopyFrom(rendered)
                        elif child.repeated:
                            try:
                                getattr(element.message_instance,child.name).extend(rendered)
                            except:
                                getattr(element.message_instance,child.name).extend( [rendered])
                        else:
                            setattr(element.message_instance,child.name,rendered)
                        # except:
                        #     logger.error(f'Unable to assign value from {child.fullname} to {element.fullname}')
                        element.message_instance.SetInParent()
                except Exception as e:
                    logger.error(f'{child.proto_file_line} Rendering default values failed for {child.name} of {child.path} in file {child.file.name}: {e}')
                    raise e
                if getattr(options, 'v_msg', None):
                    has_render = True
                    v_msg = getattr(options, 'v_msg', None)
                    try:
                        if element.repeated:
                # Create a list to hold the message instances
                            message_instances = []

                            # Parse each element of the JSON array
                            for json_element in json.loads(v_msg):
                                new_instance = element.new_message_instance
                                Parse(json.dumps(json_element), new_instance)
                                message_instances.append(new_instance)
                            element.message_instance.SetInParent()
                            return message_instances

                            # Copy each instance to the appropriate field in the parent message
                            
                            # repeated_field = getattr(element.message_instance, child.name)
                            # for instance in message_instances:
                            #     repeated_field.add().CopyFrom(instance)
                        else:
                            # If the field is not repeated, parse the JSON string directly
                            Parse(v_msg, element.message_instance)
                            element.message_instance.SetInParent()
                    except Exception as e:
                        # Handle parsing errors, e.g., log them
                        logger.error(f"{element.proto_file_line} Error parsing json default value {v_msg} as JSON. {e}")   
                        raise e

            if not has_render:
                return None
            
        else:
            
            default_value = None
            msg_options = element.options['cust_msg'] if 'cust_msg' in element.options else None
            init_from_mac = getattr(options,'init_from_mac', False) or getattr(msg_options,'init_from_mac', False) 

            
            global_name = getattr(options,'global_name', None)
            const_prefix = getattr(options,'const_prefix', self.param_dict['const_prefix'])
            if init_from_mac:
                default_value = f'{const_prefix}@@init_from_mac@@'
        
            elif element.descriptor.cpp_type  == FieldDescriptor.CPPTYPE_STRING:
                default_value = default_value = getattr(options,'v_string', None)
            elif element.descriptor.cpp_type == FieldDescriptor.CPPTYPE_ENUM:
                if options is not None:
                    try:
                        enum_value = getattr(options,'v_enum', None) or getattr(options,'v_string', None)
                        if enum_value is not None:
                            default_value = element.enum_values.index(enum_value)
                    except: 
                        raise ValueError(f'Invalid default value {default_value} for {element.path}')

            # Handling integer types
            elif element.descriptor.cpp_type in [FieldDescriptor.CPPTYPE_INT32, FieldDescriptor.CPPTYPE_INT64,
                                                FieldDescriptor.CPPTYPE_UINT32, FieldDescriptor.CPPTYPE_UINT64]:
                if element.descriptor.cpp_type in [FieldDescriptor.CPPTYPE_INT32, FieldDescriptor.CPPTYPE_INT64]:
                    default_value = getattr(options, 'v_int32', getattr(options, 'v_int64', None))
                else:
                    default_value = getattr(options, 'v_uint32', getattr(options, 'v_uint64', None))

                if default_value is not None:
                    int_value= int(default_value) 
                    if element.descriptor.cpp_type in [FieldDescriptor.CPPTYPE_UINT32, FieldDescriptor.CPPTYPE_UINT64] and int_value < 0:
                        raise ValueError(f"Negative value for unsigned int type trying to assign {element.path} = {default_value}")
                    default_value = int_value

            # Handling float and double types
            elif element.descriptor.cpp_type in [FieldDescriptor.CPPTYPE_DOUBLE, FieldDescriptor.CPPTYPE_FLOAT]:
                default_value = getattr(options, 'v_double', getattr(options, 'v_float', None))
                if default_value is not None:
                    float_value = float(default_value)
                    if '.' not in str(default_value):
                        raise ValueError(f"Integer string for float/double type trying to assign {element.path} = {default_value}")
                    default_value = float_value

            # Handling boolean type
            elif element.descriptor.cpp_type == FieldDescriptor.CPPTYPE_BOOL:
                if options is not None:
                    default_value = getattr(options, 'v_bool', False)
                    if isinstance(default_value, str):
                        if default_value.lower() in ['true', 'false']:
                            default_value = default_value.lower() == 'true'
                        else:
                            raise ValueError(f'Invalid boolean value trying to assign {element.path} = {default_value}')

            # Handling bytes type
            elif element.descriptor.cpp_type == FieldDescriptor.CPPTYPE_BYTES:
                default_value = getattr(options, 'v_bytes', b'')
            elif element.descriptor.cpp_type == FieldDescriptor.TYPE_MESSAGE:
                pass

            if default_value is not None:
                element.message_instance.SetInParent()
                return self.repeated_render(element, default_value)
            else:
                return None
        return element.message_instance

if __name__ == '__main__':
    data = ProtocParser.get_data()
    logger.info(f"Generating binary files for defaults")
    protocParser:BinDefaultsParser = BinDefaultsParser(data)
    protocParser.process()
    logger.debug('Done generating JSON file(s)')    
