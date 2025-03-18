#!/opt/esp/python_env/idf4.4_py3.8_env/bin/python
import os

import logging
import json
from pathlib import Path
from typing import Dict, List
from google.protobuf.compiler import plugin_pb2 as plugin
from google.protobuf.descriptor_pb2 import FileDescriptorProto, DescriptorProto, FieldDescriptorProto,FieldOptions
from google.protobuf.descriptor import FieldDescriptor, Descriptor, FileDescriptor
from ProtoElement import ProtoElement
from ProtocParser import ProtocParser


logger = logging.getLogger(__name__)
logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
)
class JsonParser(ProtocParser) :
    def start_message(self,message:ProtoElement) :
        super().start_message(message)
    def end_message(self,message:ProtoElement):        
        super().end_message(message)
        jsonmessage = message.render()    
        respfile = self.response.file.add()
        respfile.name = f'{message.fullname}_pb2.json'
        logger.info(f"Creating new template json file: {respfile.name}")
        respfile.content = json.dumps(jsonmessage, indent=2) + "\r\n"

    def start_file(self,file:FileDescriptor) :
        super().start_file(file)
        self.jsonmessages = {}
    def end_file(self,file:ProtoElement) :
        super().end_file(file)


    def get_name(self)->str:
           return 'protoc_plugin_json'
   
    def add_comment_if_exists(element, comment_type: str, path: str) -> dict:
        comment = getattr(element, f"{comment_type}_comment", "").strip()
        return {f"__{comment_type}_{path}": comment} if comment else {}    
    def repeated_render(self,element:ProtoElement,obj:any):
        return [obj] if element.repeated else obj
    def render(self,element: ProtoElement) -> Dict:
        result = {}
        if len(element.childs)>0:
            oneof = getattr(element.descriptor,'containing_oneof',None)
            if oneof:
                result[f'__one_of_{element.name}'] = f'Choose only one structure for {oneof.full_name}'
            for child in element.childs:
                child_result = child.render()
                if len(child.childs) > 0:
                    result[child.name] = child_result
                elif isinstance(child_result, dict):
                    result.update(child_result)
        
        else:
            result.update({
                **({f'__comments_{element.name}': element.leading_comment} if element.leading_comment.strip() else {}),
                **({f'__values_{element.name}': element.enum_values_str} if element.is_enum else {}),
                element.name: element._default_value,
                **({f'__comments2_{element.name}': element.trailing_comment} if element.trailing_comment.strip() else {})
            })
            
            optnames = ['init_from_mac', 'const_prefix','read_only','default_value']
            if len(element.options)>0:
                 logger.debug(f'{element.name} has options')
            for optname in [optname for optname in optnames if optname in element.options]:
                logger.debug(f"{element.name} [{optname} = {element.options[optname]}]")
        return self.repeated_render(element,result)
    

if __name__ == '__main__':
    data = ProtocParser.get_data()
    logger.info(f"Generating blank json file(s)")
    protocParser:JsonParser = JsonParser(data)
    protocParser.process()
    logger.debug('Done generating JSON file(s)')    
