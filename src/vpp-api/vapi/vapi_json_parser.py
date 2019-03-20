#!/usr/bin/env python2
#encoding:utf-8
import json


class ParseError (Exception):
    pass


magic_prefix = "vl_api_"
magic_suffix = "_t"


def remove_magic(what):
    #移除掉what对应的前缀，后缀
    if what.startswith(magic_prefix) and what.endswith(magic_suffix):
        return what[len(magic_prefix): - len(magic_suffix)]
    return what


class Field(object):

    def __init__(self, field_name, field_type, array_len=None,
                 nelem_field=None):
        self.name = field_name
        self.type = field_type
        #数组情况下长度
        self.len = array_len
        #可变数组情况下元素数目
        self.nelem_field = nelem_field

    def __str__(self):
        if self.len is None:
            return "Field(name: %s, type: %s)" % (self.name, self.type)
        elif self.len > 0:
            return "Field(name: %s, type: %s, length: %s)" % (self.name,
                                                              self.type,
                                                              self.len)
        else:
            return (
                "Field(name: %s, type: %s, variable length stored in: %s)" %
                (self.name, self.type, self.nelem_field))

    #是否为可变长度数组
    def is_vla(self):
        return self.nelem_field is not None


    #是否有可变长度数组
    def has_vla(self):
        return self.is_vla() or self.type.has_vla()


class Alias(Field):
    pass

#类型名称
class Type(object):
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return self.name

#基础类型
class SimpleType (Type):

    def has_vla(self):
        return False

#返回msg_header1_t,msg_header2_t
def get_msg_header_defs(struct_type_class, field_class, json_parser, logger):
    return [
        #手动定义结构体msg_header1_t
        struct_type_class(['msg_header1_t',
                           ['u16', '_vl_msg_id'],#消息id
                           ['u32', 'context'],
                           ],
                          json_parser, field_class, logger
                          ),
        #手动定义结构体msg_header2_t
        struct_type_class(['msg_header2_t',
                           ['u16', '_vl_msg_id'],#消息id
                           ['u32', 'client_index'],#客户端索引
                           ['u32', 'context'],
                           ],
                          json_parser, field_class, logger
                          ),
    ]


class Struct(object):

    def __init__(self, name, fields):
        self.name = name
        self.fields = fields
        self.field_names = [n.name for n in self.fields]
        #指明结构体依赖于各成员的定义
        self.depends = [f.type for f in self.fields]

    def __str__(self):
        return "[%s]" % "], [".join([str(f) for f in self.fields])

    #如果存在有可变长度成员，则结构体为可变长度
    def has_vla(self):
        for f in self.fields:
            if f.has_vla():
                return True
        return False


#提供枚举的基类
class Enum(SimpleType):
    def __init__(self, name, value_pairs, enumtype):
        super(Enum, self).__init__(name)
        #枚举类型
        self.type = enumtype
        #名称与值对应关系
        self.value_pairs = value_pairs

    def __str__(self):
        return "Enum(%s, [%s])" % (
            self.name,
            "], [" .join(["%s => %s" % (i, j) for i, j in self.value_pairs])
        )

#定义union
class Union(Type):
    def __init__(self, name, type_pairs, crc):
        Type.__init__(self, name)
        self.crc = crc
        self.type_pairs = type_pairs
        #指明依赖于各成员的定义
        self.depends = [t for t, _ in self.type_pairs]

    def __str__(self):
        return "Union(%s, [%s])" % (
            self.name,
            "], [" .join(["%s %s" % (i, j) for i, j in self.type_pairs])
        )

    def has_vla(self):
        return False

#消息格式定义（消息有event,有reply之分）
class Message(object):

    def __init__(self, logger, definition, json_parser):
        struct_type_class = json_parser.struct_type_class
        field_class = json_parser.field_class
        self.request = None
        self.logger = logger
        m = definition #对message的配置
        logger.debug("Parsing message definition `%s'" % m)
        name = m[0] #取名称
        self.name = name
        logger.debug("Message name is `%s'" % name)
        ignore = True
        self.header = None
        #检查此消息是否为reply
        self.is_reply = json_parser.is_reply(self.name)
        #检查此消息是否为event
        self.is_event = json_parser.is_event(self.name)
        
        fields = []
        
        #遍历每种msg header
        for header in get_msg_header_defs(struct_type_class, field_class,
                                          json_parser, logger):
            logger.debug("Probing header `%s'" % header.name)
            if header.is_part_of_def(m[1:]):
                #m采用的是header格式，指出header
                self.header = header
                logger.debug("Found header `%s'" % header.name)
                #定义header字段
                fields.append(field_class(field_name='header',
                                          field_type=self.header))
                ignore = False
                break
            
        #没有找到此消息对应的header,且不是event,不是reply,则报错
        if ignore and not self.is_event and not self.is_reply:
            raise ParseError("While parsing message `%s': could not find all "
                             "common header fields" % name)
        for field in m[1:]:
            if len(field) == 1 and 'crc' in field:
                #记录crc
                self.crc = field['crc']
                logger.debug("Found CRC `%s'" % self.crc)
                continue
            else:
                field_type = json_parser.lookup_type_like_id(field[0])
                logger.debug("Parsing message field `%s'" % field)
                if len(field) == 2:
                    #普通变量 变量类型 变量名
                    if self.header is not None and\
                            self.header.has_field(field[1]):
                        #跳过header头部字段（已加入）
                        continue
                    p = field_class(field_name=field[1],
                                    field_type=field_type)
                elif len(field) == 3:
                    #数组型变量
                    if field[2] == 0:
                        raise ParseError(
                            "While parsing message `%s': variable length "
                            "array `%s' doesn't have reference to member "
                            "containing the actual length" % (
                                name, field[1]))
                    p = field_class(
                        field_name=field[1],
                        field_type=field_type,
                        array_len=field[2])
                elif len(field) == 4:
                    #可变长度数组型变量
                    nelem_field = None
                    for f in fields:
                        if f.name == field[3]:
                            nelem_field = f
                    if nelem_field is None:
                        raise ParseError(
                            "While parsing message `%s': couldn't find "
                            "variable length array `%s' member containing "
                            "the actual length `%s'" % (
                                name, field[1], field[3]))
                    p = field_class(
                        field_name=field[1],
                        field_type=field_type,
                        array_len=field[2],
                        nelem_field=nelem_field)
                else:
                    raise Exception("Don't know how to parse message "
                                    "definition for message `%s': `%s'" %
                                    (m, m[1:]))
                logger.debug("Parsed field `%s'" % p)
                fields.append(p)
        self.fields = fields
        #指明结构体依琐于各成员类型的定义
        self.depends = [f.type for f in self.fields]
        logger.debug("Parsed message: %s" % self)

    def __str__(self):
        return "Message(%s, [%s], {crc: %s}" % \
            (self.name,
             "], [".join([str(f) for f in self.fields]),
             self.crc)

#对应api.json中的"types"
class StructType (Type, Struct):

    def __init__(self, definition, json_parser, field_class, logger):
        t = definition
        logger.debug("Parsing struct definition `%s'" % t)
        name = t[0] #定义的类型名称
        
        #收集结构体成员
        fields = []
        for field in t[1:]:
            if len(field) == 1 and 'crc' in field:
                #设置crc属性
                self.crc = field['crc']
                continue
            #通过类型名称查询对应的类型
            field_type = json_parser.lookup_type_like_id(field[0])
            logger.debug("Parsing type field `%s'" % field)
            if len(field) == 2:
                #普通变量形式（变量类型 变量名）
                p = field_class(field_name=field[1],
                                field_type=field_type)
            elif len(field) == 3:
                #数组形式（变量类型 变量名[数组长度])
                if field[2] == 0:
                    raise ParseError("While parsing type `%s': array `%s' has "
                                     "variable length" % (name, field[1]))
                p = field_class(field_name=field[1],
                                field_type=field_type,
                                array_len=field[2])
            elif len(field) == 4:
                #可变数组形式(变量类型 变量名称[取大数组长度] 数组长度字段名称
                nelem_field = None
                for f in fields:
                    #记录数组长度的字段必须在field中已出现
                    if f.name == field[3]:
                        nelem_field = f
                if nelem_field is None:
                    raise ParseError(
                        "While parsing message `%s': couldn't find "
                        "variable length array `%s' member containing "
                        "the actual length `%s'" % (
                            name, field[1], field[3]))
                p = field_class(field_name=field[1],
                                field_type=field_type,
                                array_len=field[2],
                                nelem_field=nelem_field)
            else:
                #不支持期它形式
                raise ParseError(
                    "Don't know how to parse field `%s' of type definition "
                    "for type `%s'" % (field, t))
            #记录已识别的字段
            fields.append(p)
        Type.__init__(self, name)
        Struct.__init__(self, name, fields)

    def __str__(self):
        return "StructType(%s, %s)" % (Type.__str__(self),
                                       Struct.__str__(self))

    def has_field(self, name):
        return name in self.field_names

    #检查definition是否以self的字段开头
    def is_part_of_def(self, definition):
        #按索引遍历每个field
        for idx in range(len(self.fields)):
            field = definition[idx]
            p = self.fields[idx]
            #检查field名称是否与p相等
            if field[1] != p.name:
                return False
            if field[0] != p.type.name:
                raise ParseError(
                    "Unexpected field type `%s' (should be `%s'), "
                    "while parsing msg/def/field `%s/%s/%s'" %
                    (field[0], p.type, p.name, definition, field))
        return True


class JsonParser(object):
    def __init__(self, logger, files, simple_type_class=SimpleType,
                 enum_class=Enum, union_class=Union,
                 struct_type_class=StructType, field_class=Field,
                 message_class=Message, alias_class=Alias):
        self.services = {}
        #按名称索引message
        self.messages = {}
        self.enums = {}
        self.unions = {}
        self.aliases = {}
        #定义基础数据类型对应的辅助类，定义其它自定义类型
        self.types = {
            x: simple_type_class(x) for x in [
                'i8', 'i16', 'i32', 'i64',
                'u8', 'u16', 'u32', 'u64',
                'f64'
            ]
        }

        self.types['string'] = simple_type_class('vl_api_string_t')
        #记录services/xx/reply属性取值
        self.replies = set()
        #记录services/xx/events属性取值
        self.events = set()
        self.simple_type_class = simple_type_class
        self.enum_class = enum_class
        self.union_class = union_class
        self.struct_type_class = struct_type_class
        self.field_class = field_class
        self.alias_class = alias_class
        self.message_class = message_class

        self.exceptions = []
        self.json_files = []
        self.types_by_json = {}
        #记录在xx文件中有哪些enum
        self.enums_by_json = {}
        self.unions_by_json = {}
        self.aliases_by_json = {}
        self.messages_by_json = {}
        self.logger = logger
        #遍历要解析的文件
        for f in files:
            self.parse_json_file(f)
        self.finalize_parsing()

    #解析具体的一个api.json文件
    def parse_json_file(self, path):
        self.logger.info("Parsing json api file: `%s'" % path)
        self.json_files.append(path)
        self.types_by_json[path] = []
        self.enums_by_json[path] = []
        self.unions_by_json[path] = []
        self.aliases_by_json[path] = []
        self.messages_by_json[path] = {}
        with open(path) as f:
            j = json.load(f)
            #遍历obj中的services属性成员
            for k in j['services']:
                #不容许重复
                if k in self.services:
                    raise ParseError("Duplicate service `%s'" % k)
                #记录json文件中的services内部各属性
                self.services[k] = j['services'][k]
                #记录reply属性
                self.replies.add(self.services[k]["reply"])
                if "events" in self.services[k]:
                    for x in self.services[k]["events"]:
                        self.events.add(x)
                        
            #遍历obj中的枚举（enums）数组成员，看ip.api.json文件
            for e in j['enums']:
                name = e[0] #名称
                value_pairs = e[1:-1] #取中间的value部分
                enumtype = self.types[e[-1]["enumtype"]] #取enumtype配置
                enum = self.enum_class(name, value_pairs, enumtype) #构造enum对应的原数据类
                self.enums[enum.name] = enum #实现枚举与metaclass的映射
                self.logger.debug("Parsed enum: %s" % enum)
                self.enums_by_json[path].append(enum)
            
            exceptions = []
            progress = 0
            last_progress = 0
            while True:
                #处理union定义
                for u in j['unions']:
                    name = u[0] #解析union的名称
                    if name in self.unions:
                        progress = progress + 1
                        continue
                    try:
                        type_pairs = [[self.lookup_type_like_id(t), n]
                                      for t, n in u[1:-1]]
                        crc = u[-1]["crc"] #解析crc
                        union = self.union_class(name, type_pairs, crc)
                        progress = progress + 1
                    except ParseError as e:
                        exceptions.append(e)
                        continue
                    self.unions[union.name] = union
                    self.logger.debug("Parsed union: %s" % union)
                    self.unions_by_json[path].append(union)
                    
                #处理aliases定义（查看igmp.api.json)
                for name, body in j['aliases'].iteritems():
                    if name in self.aliases:
                        progress = progress + 1
                        continue
                    if 'length' in body:
                        array_len = body['length']
                    else:
                        array_len = None
                    #取内部对象的type配置
                    t = self.types[body['type']]
                    alias = self.alias_class(name, t, array_len)
                    self.aliases[name] = alias
                    self.logger.debug("Parsed alias: %s" % alias)
                    self.aliases_by_json[path].append(alias)
                    
                #处理types定义（为obj中的数组类型），处理为struct
                for t in j['types']:
                    if t[0] in self.types:
                        #此type已处理
                        progress = progress + 1
                        continue
                    try:
                        type_ = self.struct_type_class(t, self,
                                                       self.field_class,
                                                       self.logger)
                        if type_.name in self.types:
                            #重复的类型定义
                            raise ParseError(
                                "Duplicate type `%s'" % type_.name)
                        progress = progress + 1
                    except ParseError as e:
                        exceptions.append(e)
                        continue
                    self.types[type_.name] = type_
                    self.types_by_json[path].append(type_)
                    self.logger.debug("Parsed type: %s" % type_)
                if not exceptions:
                    # finished parsing
                    break
                if progress <= last_progress:
                    # cannot make forward progress
                    self.exceptions.extend(exceptions)
                    break
                exceptions = []
                last_progress = progress
                progress = 0
            prev_length = len(self.messages)
            processed = []
            while True:
                exceptions = []
                
                #处理message定义
                for m in j['messages']:
                    if m in processed:
                        continue
                    try:
                        msg = self.message_class(self.logger, m, self)
                        if msg.name in self.messages:
                            raise ParseError(
                                "Duplicate message `%s'" % msg.name)
                    except ParseError as e:
                        exceptions.append(e)
                        continue
                    self.messages[msg.name] = msg
                    self.messages_by_json[path][msg.name] = msg
                    processed.append(m)
                if prev_length == len(self.messages):
                    # cannot make forward progress ...
                    self.exceptions.extend(exceptions)
                    break
                prev_length = len(self.messages)

    #通过名称查找类型
    def lookup_type_like_id(self, name):
        #除掉magic字符
        mundane_name = remove_magic(name)
        #按优先级，依次查询types,enums,unions,aliases
        if name in self.types:
            return self.types[name]
        elif name in self.enums:
            return self.enums[name]
        elif name in self.unions:
            return self.unions[name]
        elif name in self.aliases:
            return self.aliases[name]
        #按去除magic字符之后的模式进行查询
        elif mundane_name in self.types:
            return self.types[mundane_name]
        elif mundane_name in self.enums:
            return self.enums[mundane_name]
        elif mundane_name in self.unions:
            return self.unions[mundane_name]
        elif mundane_name in self.aliases:
            return self.aliases[mundane_name]
        #没有发现相应的type定义
        raise ParseError(
            "Could not find type, enum or union by magic name `%s' nor by "
            "mundane name `%s'" % (name, mundane_name))

    #检查此message是否为reply
    def is_reply(self, message):
        return message in self.replies

    def is_event(self, message):
        return message in self.events

    #services指出了消息的请求应答方式，通过service查找message的应答类型，再查应答类型的定义
    def get_reply(self, message):
        return self.messages[self.services[message]['reply']]

    def finalize_parsing(self):
        if len(self.messages) == 0:
            for e in self.exceptions:
                self.logger.warning(e)
        for jn, j in self.messages_by_json.items():
            remove = []
            for n, m in j.items():
                try:
                    if not m.is_reply and not m.is_event:
                        try:
                            m.reply = self.get_reply(n)
                            if "stream" in self.services[m.name]:
                                m.reply_is_stream = \
                                    self.services[m.name]["stream"]
                            else:
                                m.reply_is_stream = False
                            m.reply.request = m
                        except:
                            raise ParseError(
                                "Cannot find reply to message `%s'" % n)
                except ParseError as e:
                    self.exceptions.append(e)
                    remove.append(n)

            self.messages_by_json[jn] = {
                k: v for k, v in j.items() if k not in remove}
