from __future__ import annotations
from functools import partial
from typing import ClassVar, List, Any, Dict
from typing import Callable
from google.protobuf import message, descriptor_pb2
from google.protobuf.descriptor import Descriptor, FieldDescriptor, FileDescriptor
from google.protobuf.descriptor_pb2 import FieldDescriptorProto,DescriptorProto,EnumDescriptorProto
import google.protobuf.descriptor_pool as descriptor_pool
import logging
import copy
# import custom_options_pb2 as custom

RendererType = Callable[['ProtoElement'], Dict]
class ProtoElement:
    childs:List[ProtoElement] 
    descriptor:Descriptor|FieldDescriptor
    comments: Dict[str,str] 
    enum_type:EnumDescriptorProto
    _comments: Dict[str,str] ={}
    pool:descriptor_pool.DescriptorPool
    prototypes: dict[str, type[message.Message]]
    renderer:RendererType
    package:str
    file:FileDescriptor
    message:str
    _positions: Dict[str,tuple]
    position: tuple
    options:Dict[str,any]
    _message_instance:ClassVar
    @classmethod
    def set_prototypes(cls,prototypes:dict[str, type[message.Message]]):
        cls.prototypes = prototypes
    @classmethod
    def set_comments_base(cls,comments:Dict[str,str]):
        cls._comments = comments
    @classmethod
    def set_positions_base(cls,positions:Dict[str,tuple]):
        cls._positions = positions
    @classmethod 
    def set_pool(cls,pool:descriptor_pool.DescriptorPool):
        cls.pool = pool
    @classmethod
    def set_logger(cls,logger = None):
        if not logger and not cls.logger:
            cls.logger = logging.getLogger(__name__)
            logging.basicConfig(
                level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s"
            )
        elif logger:
            cls.logger = logger
    @classmethod 
    def set_render(cls,render):
        cls.render_class = render
    def __init__(self, descriptor: Descriptor|FieldDescriptor,   parent=None):
        ProtoElement.set_logger()
        self.descriptor = descriptor
        self.file = descriptor.file
        self.package = getattr(descriptor,"file",parent).package
        self.descriptorname = descriptor.name
        self.json_name = getattr(descriptor,'json_name','')
        self.type_name = getattr(descriptor,'type_name',descriptor.name)         
        
        self.parent = parent
        self.fullname = descriptor.full_name
        self.type = getattr(descriptor,'type',FieldDescriptor.TYPE_MESSAGE) 
        
        if self.type ==FieldDescriptor.TYPE_MESSAGE:
            try:
                self._message_instance = self.prototypes[self.descriptor.message_type.full_name]()
                # self.logger.debug(f'Found instance for {self.descriptor.message_type.full_name}')
            except:
                # self.logger.error(f'Could not find instance for {self.descriptor.full_name}')
                self._message_instance = self.prototypes[self.descriptor.full_name]()
        self.label = getattr(descriptor,'label',None)
        self.childs = []
        if descriptor.has_options:
            self.options = {descr.name: value for descr, value in descriptor.GetOptions().ListFields()}
        else:
            self.options = {}
        try:
            if descriptor.containing_type.has_options:
                self.options.update({descr.name: value for descr, value in descriptor.containing_type.GetOptions().ListFields()})
        except:
            pass
                        


        self.render = partial(self.render_class, self)
        self.comments = {comment.split('.')[-1]:self._comments[comment] for comment in self._comments.keys() if comment.startswith(self.path)}
        self.position = self._positions.get(self.path)

    @property 
    def cpp_type(self)->str:
        return f'{self.package}_{self.descriptor.containing_type.name}'
    @property 
    def cpp_member(self)->str:
        return self.name
    @property
    def cpp_type_member_prefix(self)->str:
        return f'{self.cpp_type}_{self.cpp_member}'    
    @property
    def cpp_type_member(self)->str:
        return f'{self.cpp_type}.{self.cpp_member}'
    @property
    def main_message(self)->bool:
        return self.parent == None
    @property 
    def parent(self)->ProtoElement:
        return self._parent
    @parent.setter
    def parent(self,value:ProtoElement):
        self._parent = value
        if value:
            self._parent.childs.append(self)
    @property 
    def root(self)->ProtoElement:
        return self if not self.parent else self.parent
    @property
    def enum_type(self)->EnumDescriptorProto:
            return self.descriptor.enum_type
    @property
    def cpp_root(self):
        return f'{self.cpp_type}_ROOT'
    @property
    def cpp_child(self):
        return f'{self.cpp_type}_CHILD'
    @property
    def proto_file_line(self):
        # Accessing file descriptor to get source code info, adjusted for proper context
        if self.position:
            start_line, start_column, end_line = self.position
            return f"{self.file.name}:{start_line}"
        else:
            return f"{self.file.name}"

        
        
    @property
    def message_instance(self):
        return getattr(self,'_message_instance',getattr(self.parent,'message_instance',None))
    @property
    def new_message_instance(self):
        if self.type == FieldDescriptor.TYPE_MESSAGE:
            try:
                # Try to create a new instance using the full name of the message type
                return self.prototypes[self.descriptor.message_type.full_name]()
            except KeyError:
                # If the above fails, use an alternative method to create a new instance
                # Log the error if necessary
                # self.logger.error(f'Could not find instance for {self.descriptor.full_name}')
                return self.prototypes[self.descriptor.full_name]()
        else:
            # Return None or raise an exception if the type is not a message
            return None

    @property
    def tree(self):
        childs = '->('+', '.join(c.tree for c in self.childs ) + ')' if len(self.childs)>0 else ''
        return f'{self.name}{childs}' 
    @property
    def name(self):
        return self.descriptorname if len(self.descriptorname)>0 else self.parent.name if self.parent else self.package
    @property 
    def enum_values(self)->List[str]:
        return [n.name for n in getattr(self.enum_type,"values",getattr(self.enum_type,"value",[])) ]
    @property 
    def enum_values_str(self)->str:
        return ', '.join(self.enum_values)
    @property 
    def fields(self)->List[FieldDescriptor]:
        return getattr(self.descriptor,"fields",getattr(getattr(self.descriptor,"message_type",None),"fields",None))
    @property
    def _default_value(self):
        if 'default_value' in self.options:
            return self.options['default_value']
        if self.type in [FieldDescriptorProto.TYPE_INT32, FieldDescriptorProto.TYPE_INT64,
                        FieldDescriptorProto.TYPE_UINT32, FieldDescriptorProto.TYPE_UINT64,
                        FieldDescriptorProto.TYPE_SINT32, FieldDescriptorProto.TYPE_SINT64,
                        FieldDescriptorProto.TYPE_FIXED32, FieldDescriptorProto.TYPE_FIXED64,
                        FieldDescriptorProto.TYPE_SFIXED32, FieldDescriptorProto.TYPE_SFIXED64]:
            return  0
        elif self.type in [FieldDescriptorProto.TYPE_FLOAT, FieldDescriptorProto.TYPE_DOUBLE]:
            return 0.0
        elif self.type == FieldDescriptorProto.TYPE_BOOL:
            return False
        elif self.type in [FieldDescriptorProto.TYPE_STRING, FieldDescriptorProto.TYPE_BYTES]:
            return "" 
        elif self.is_enum:
            return self.enum_values[0] if self.enum_values else 0
    @property
    def detached_leading_comments(self)->str:
        return self.comments["leading"] if "detached" in self.comments else ""

    @property
    def leading_comment(self)->str:
        return self.comments["leading"] if "leading" in self.comments else ""
    @property
    def trailing_comment(self)->str:
        return self.comments["trailing"] if "trailing" in self.comments else ""
    @property
    def is_enum(self):
        return self.type == FieldDescriptorProto.TYPE_ENUM
    @property
    def path(self) -> str:
        return self.descriptor.full_name

    @property
    def enum_name(self)-> str:
        return self.type_name.split('.', maxsplit=1)[-1]
    @property
    def repeated(self)->bool:
        return self.label== FieldDescriptor.LABEL_REPEATED



            
        

