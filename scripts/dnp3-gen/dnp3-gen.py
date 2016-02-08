#! /usr/bin/env python
#
# Copyright (C) 2015 Open Information Security Foundation
#
# You can copy, redistribute or modify this Program under the terms of
# the GNU General Public License version 2 as published by the Free
# Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# version 2 along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

# This script generates DNP3 related source code based on definitions
# of DNP3 objects (currently the object structs).

from __future__ import print_function

import sys
import re
from cStringIO import StringIO
import yaml
import types

import jinja2

IN_PLACE_START = "/* START GENERATED CODE */"
IN_PLACE_END = "/* END GENERATED CODE */"

util_lua_dnp3_objects_c_template = """/* Copyright (C) 2015 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * DO NOT EDIT. THIS FILE IS AUTO-GENERATED.
 */

#include "suricata-common.h"

#include "app-layer-dnp3.h"
#include "app-layer-dnp3-objects.h"

#ifdef HAVE_LUA

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "util-lua.h"

/**
 * \\brief Push an object point item onto the stack.
 */
void DNP3PushPoint(lua_State *luastate, DNP3Object *object,
    DNP3Point *point)
{
    switch (DNP3_OBJECT_CODE(object->group, object->variation)) {
{% for object in objects %}
        case DNP3_OBJECT_CODE({{object.group}}, {{object.variation}}): {
            DNP3ObjectG{{object.group}}V{{object.variation}} *data = point->data;
{% for field in object.fields %}
{% if is_integer_type(field.type) %}
            lua_pushliteral(luastate, "{{field.name}}");
            lua_pushinteger(luastate, data->{{field.name}});
            lua_settable(luastate, -3);
{% elif field["type"] in ["flt32", "flt64"] %}
            lua_pushliteral(luastate, "{{field.name}}");
            lua_pushnumber(luastate, data->{{field.name}});
            lua_settable(luastate, -3);
{% elif field["type"] == "chararray" %}
            lua_pushliteral(luastate, "{{field.name}}");
            LuaPushStringBuffer(luastate, (uint8_t *)data->{{field.name}},
                strlen(data->{{field.name}}));
            lua_settable(luastate, -3);
{% elif field["type"] == "vstr4" %}
            lua_pushliteral(luastate, "{{field.name}}");
            LuaPushStringBuffer(luastate, (uint8_t *)data->{{field.name}},
                strlen(data->{{field.name}}));
            lua_settable(luastate, -3);
{% elif field.type == "bytearray" %}
            lua_pushliteral(luastate, "{{field.name}}");
            lua_pushlstring(luastate, (const char *)data->{{field.name}},
                data->{{field.len_field}});
            lua_settable(luastate, -3);
{% elif field.type == "bstr8" %}
{% for field in field.fields %}
            lua_pushliteral(luastate, "{{field.name}}");
            lua_pushinteger(luastate, data->{{field.name}});
            lua_settable(luastate, -3);
{% endfor %}
{% else %}
{{ raise("Unhandled datatype: %s" % (field.type)) }}
{% endif %}
{% endfor %}
            break;
        }
{% endfor %}
        default:
            break;
    }
}

#endif /* HAVE_LUA */

"""

output_json_dnp3_objects_template = """/* Copyright (C) 2015 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * DO NOT EDIT. THIS FILE IS AUTO-GENERATED.
 */

#include "suricata-common.h"

#include "util-crypt.h"

#include "app-layer-dnp3.h"
#include "app-layer-dnp3-objects.h"

void OutputJsonDNP3SetItem(json_t *js, DNP3Object *object,
    DNP3Point *point)
{

    switch (DNP3_OBJECT_CODE(object->group, object->variation)) {
{% for object in objects %}
        case DNP3_OBJECT_CODE({{object.group}}, {{object.variation}}): {
            DNP3ObjectG{{object.group}}V{{object.variation}} *data = point->data;
{% for field in object.fields %}
{% if is_integer_type(field.type) %}
            json_object_set_new(js, "{{field.name}}",
                json_integer(data->{{field.name}}));
{% elif field.type in ["flt32", "flt64"] %}
            json_object_set_new(js, "{{field.name}}",
                json_real(data->{{field.name}}));
{% elif field.type == "bytearray" %}
            unsigned long {{field.name}}_b64_len = data->{{field.len_field}} * 2;
            uint8_t {{field.name}}_b64[{{field.name}}_b64_len];
            Base64Encode(data->{{field.name}}, data->{{field.len_field}},
                {{field.name}}_b64, &{{field.name}}_b64_len);
            json_object_set_new(js, "data->{{field.name}}",
                json_string((char *){{field.name}}_b64));
{% elif field.type == "vstr4" %}
            json_object_set_new(js, "data->{{field.name}}", json_string(data->{{field.name}}));
{% elif field.type == "chararray" %}
            if (data->{{field.len_field}} > 0) {
                /* First create a null terminated string as not all versions
                 * of jansson have json_stringn. */
                char tmpbuf[data->{{field.len_field}} + 1];
                memcpy(tmpbuf, data->{{field.name}}, data->{{field.len_field}});
                tmpbuf[data->{{field.len_field}}] = '\\0';
                json_object_set_new(js, "{{field.name}}", json_string(tmpbuf));
            } else {
                json_object_set_new(js, "{{field.name}}", json_string(""));
            }
{% elif field.type == "bstr8" %}
{% for field in field.fields %}
            json_object_set_new(js, "{{field.name}}",
                json_integer(data->{{field.name}}));
{% endfor %}
{% else %}
{{ raise("Unhandled datatype: %s" % (field.type)) }}
{% endif %}
{% endfor %}
            break;
        }
{% endfor %}
        default:
            SCLogDebug("Unknown object: %d:%d", object->group,
                object->variation);
            break;
    }

}
"""

def has_freeable_types(fields):
    freeable_types = [
        "bytearray",
    ]
    for field in fields:
        if field["type"] in freeable_types:
            return True
    return False

def is_integer_type(datatype):
    integer_types = [
        "uint64",
        "uint32",
        "uint24",
        "uint16",
        "uint8",
        "int64",
        "int32",
        "int16",
        "int8",
        "dnp3time",
    ]
    return datatype in integer_types

def to_type(datatype):
    type_map = {
        "uint8": "uint8_t",
    }
    if datatype in type_map:
        return type_map[datatype]
    else:
        raise Exception("Unknown datatype: %s" % (datatype))

def generate(template, filename, context):
    print("Generating %s." % (filename))
    try:
        env = jinja2.Environment(trim_blocks=True)
        output = env.from_string(template).render(context)
        with open(filename, "w") as fileobj:
            fileobj.write(output)
    except Exception as err:
        print("Failed to generate %s: %s" % (filename, err))
        sys.exit(1)

def raise_helper(msg):
    raise Exception(msg)

def gen_object_structs(context):
    """ Generate structs for all the define DNP3 objects. """

    template = """
{% for object in objects %}
typedef struct DNP3ObjectG{{object.group}}V{{object.variation}}_ {
{% for field in object.fields %}
{% if field.type == "bstr8" %}
{% for field in field.fields %}
    uint8_t {{field.name}}:{{field.width}};
{% endfor %}
{% else %}
{% if field.type == "int16" %}
    int16_t {{field.name}};
{% elif field.type == "int32" %}
    int32_t {{field.name}};
{% elif field.type == "uint8" %}
    uint8_t {{field.name}};
{% elif field.type == "uint16" %}
    uint16_t {{field.name}};
{% elif field.type == "uint24" %}
    uint32_t {{field.name}};
{% elif field.type == "uint32" %}
    uint32_t {{field.name}};
{% elif field.type == "uint64" %}
    uint64_t {{field.name}};
{% elif field.type == "flt32" %}
    float {{field.name}};
{% elif field.type == "flt64" %}
    double {{field.name}};
{% elif field.type == "dnp3time" %}
    uint64_t {{field.name}};
{% elif field.type == "bytearray" %}
    uint8_t *{{field.name}};
{% elif field.type == "vstr4" %}
    char {{field.name}}[5];
{% elif field.type == "chararray" %}
    char {{field.name}}[{{field.size}}];
{% else %}
    {{ raise("Unknown datatype type '%s' for object %d:%d" % (
           field.type, object.group, object.variation)) }}
{% endif %}
{% endif %}
{% endfor %}
{% if object.extra_fields %}
{% for field in object.extra_fields %}
{% if field.type == "uint8" %}
    uint8_t {{field.name}};
{% elif field.type == "uint16" %}
    uint16_t {{field.name}};
{% elif field.type == "uint32" %}
    uint32_t {{field.name}};
{% else %}
    {{ raise("Unknown datatype: %s" % (field.type)) }}
{% endif %}
{% endfor %}
{% endif %}
} DNP3ObjectG{{object.group}}V{{object.variation}};

{% endfor %}
"""

    filename = "src/app-layer-dnp3-objects.h"
    try:
        env = jinja2.Environment(trim_blocks=True)
        code = env.from_string(template).render(context)
        content = open(filename).read()
        content = re.sub(
            "(%s).*(%s)" % (re.escape(IN_PLACE_START), re.escape(IN_PLACE_END)),
            r"\1%s\2" % (code), content, 1, re.M | re.DOTALL)
        open(filename, "w").write(content)
        print("Updated %s." % (filename))
    except Exception as err:
        print("Failed to update %s: %s" % (filename, err), file=sys.stderr)
        sys.exit(1)

def gen_object_decoders(context):
    """ Generate decoders for all defined DNP3 objects. """

    template = """
{% for object in objects %}
{% if object.packed %}
static int DNP3DecodeObjectG{{object.group}}V{{object.variation}}(const uint8_t **buf, uint32_t *len,
    uint8_t prefix_code, uint32_t start, uint32_t count,
    DNP3PointList *points)
{
    DNP3ObjectG{{object.group}}V{{object.variation}} *object = NULL;
    int bytes = (count / 8) + 1;
    uint32_t prefix = 0;
    int index = start;

    if (!DNP3ReadPrefix(buf, len, prefix_code, &prefix)) {
        goto error;
    }

    for (int i = 0; i < bytes; i++) {

        uint8_t octet;

        if (!DNP3ReadUint8(buf, len, &octet)) {
            goto error;
        }

        for (int j = 0; j < 8 && count; j = j + {{object.fields[0].width}}) {

            object = SCCalloc(1, sizeof(*object));
            if (unlikely(object == NULL)) {
                goto error;
            }

{% if object.fields[0].width == 1 %}
            object->{{object.fields[0].name}} = (octet >> j) & 0x1;
{% elif object.fields[0].width == 2 %}
            object->{{object.fields[0].name}} = (octet >> j) & 0x3;
{% else %}
#error "Unhandled field width: {{object.fields[0].width}}"
{% endif %}

            if (!DNP3AddPoint(points, object, index, prefix_code, prefix)) {
                goto error;
            }

            object = NULL;
            count--;
            index++;
        }

    }

    return 1;
error:
    if (object != NULL) {
        SCFree(object);
    }
    return 0;
}

{% else %}
static int DNP3DecodeObjectG{{object.group}}V{{object.variation}}(const uint8_t **buf, uint32_t *len,
    uint8_t prefix_code, uint32_t start, uint32_t count,
    DNP3PointList *points)
{
    DNP3ObjectG{{object.group}}V{{object.variation}} *object = NULL;
    uint32_t prefix = 0;
    uint32_t index = start;
{% if object._track_offset %}
    uint32_t offset;
{% endif %}
{% if object.constraints %}

{% for (key, val) in object.constraints.items() %}
{% if key == "require_size_prefix" %}
    if (!DNP3PrefixIsSize(prefix_code)) {
        goto error;
    }
{% elif key == "require_prefix_code" %}
    if (prefix_code != {{val}}) {
        goto error;
    }
{% else %}
{{ raise("Unhandled constraint: %s" % (key)) }}
{% endif %}
{% endfor %}
{% endif %}

    while (count--) {

        object = SCCalloc(1, sizeof(*object));
        if (unlikely(object == NULL)) {
            goto error;
        }

        if (!DNP3ReadPrefix(buf, len, prefix_code, &prefix)) {
            goto error;
        }
{% if object._track_offset %}

        offset = *len;
{% endif %}

{% for field in object.fields %}
{% if field.type == "int16" %}
        if (!DNP3ReadUint16(buf, len, (uint16_t *)&object->{{field.name}})) {
            goto error;
        }
{% elif field.type == "int32" %}
        if (!DNP3ReadUint32(buf, len, (uint32_t *)&object->{{field.name}})) {
            goto error;
        }
{% elif field.type == "uint8" %}
        if (!DNP3ReadUint8(buf, len, &object->{{field.name}})) {
            goto error;
        }
{% elif field.type == "uint16" %}
        if (!DNP3ReadUint16(buf, len, &object->{{field.name}})) {
            goto error;
        }
{% elif field.type == "uint24" %}
        if (!DNP3ReadUint24(buf, len, &object->{{field.name}})) {
            goto error;
        }
{% elif field.type == "uint32" %}
        if (!DNP3ReadUint32(buf, len, &object->{{field.name}})) {
            goto error;
        }
{% elif field.type == "uint64" %}
        if (!DNP3ReadUint64(buf, len, &object->{{field.name}})) {
            goto error;
        }
{% elif field.type == "flt32" %}
        if (!DNP3ReadUint32(buf, len, (uint32_t *)&object->{{field.name}})) {
            goto error;
        }
{% elif field.type == "flt64" %}
        if (!DNP3ReadUint64(buf, len, (uint64_t *)&object->{{field.name}})) {
            goto error;
        }
{% elif field.type == "dnp3time" %}
        if (!DNP3ReadUint48(buf, len, &object->{{field.name}})) {
            goto error;
        }
{% elif field.type == "vstr4" %}
        if (*len < 4) {
            goto error;
        }
        memcpy(object->{{field.name}}, *buf, 4);
        object->{{field.name}}[4] = '\\\\0';
        *buf += 4;
        *len -= 4;
{% elif field.type == "bytearray" %}
{% if field.len_from_prefix %}
        object->{{field.len_field}} = prefix - (offset - *len);
{% endif %}
        if (object->{{field.len_field}} > 0) {
            if (*len < object->{{field.len_field}}) {
                /* Not enough data. */
                goto error;
            }
            object->{{field.name}} = SCCalloc(1, object->{{field.len_field}});
            if (unlikely(object->{{field.name}} == NULL)) {
                goto error;
            }
            memcpy(object->{{field.name}}, *buf, object->{{field.len_field}});
            *buf += object->{{field.len_field}};
            *len -= object->{{field.len_field}};
        }
{% elif field.type == "chararray" %}
{% if field.len_from_prefix %}
        object->{{field.len_field}} = prefix - (offset - *len);
{% endif %}
        if (object->{{field.len_field}} > 0) {
            memcpy(object->{{field.name}}, *buf, object->{{field.len_field}});
            *buf += object->{{field.len_field}};
            *len -= object->{{field.len_field}};
        }
        object->{{field.name}}[object->{{field.len_field}}] = '\\\\0';
{% elif field.type == "bstr8" %}
        {
            uint8_t octet;
            if (!DNP3ReadUint8(buf, len, &octet)) {
                goto error;
            }
{% set shift = 0 %}
{% for field in field.fields %}
{% if field.width == 1 %}
            object->{{field.name}} = (octet >> {{shift}}) & 0x1;
{% elif field.width == 2 %}
            object->{{field.name}} = (octet >> {{shift}}) & 0x3;
{% elif field.width == 4 %}
            object->{{field.name}} = (octet >> {{shift}}) & 0xf;
{% elif field.width == 7 %}
            object->{{field.name}} = (octet >> {{shift}}) & 0x7f;
{% else %}
{{ raise("Unhandled width of %d." % (field.width)) }}
{% endif %}
{% set shift = shift + field.width %}
{% endfor %}
        }
{% else %}
{{ raise("Unhandled datatype '%s' for object %d:%d." % (field.type,
       object.group, object.variation)) }}
{% endif %}
{% endfor %}

        if (!DNP3AddPoint(points, object, index, prefix_code, prefix)) {
            goto error;
        }

        object = NULL;
        index++;
    }

    return 1;
error:
    if (object != NULL) {
        SCFree(object);
    }

    return 0;
}

{% endif %}
{% endfor %}

void DNP3FreeObjectPoint(int group, int variation, void *point)
{
    switch(DNP3_OBJECT_CODE(group, variation)) {
{% for object in objects %}
{% if f_has_freeable_types(object.fields) %}
        case DNP3_OBJECT_CODE({{object.group}}, {{object.variation}}): {
            DNP3ObjectG{{object.group}}V{{object.variation}} *object = (DNP3ObjectG{{object.group}}V{{object.variation}} *) point;
{% for field in object.fields %}
{% if field.type == "bytearray" %}
            if (object->{{field.name}} != NULL) {
                SCFree(object->{{field.name}});
            }
{% endif %}
{% endfor %}
            break;
        }
{% endif %}
{% endfor %}
        default:
            break;
    }
    SCFree(point);
}

/**
 * \\\\brief Decode a DNP3 object.
 *
 * \\\\retval 0 on success. On failure a positive integer corresponding
 *     to a DNP3 application layer event will be returned.
 */
int DNP3DecodeObject(int group, int variation, const uint8_t **buf,
    uint32_t *len, uint8_t prefix_code, uint32_t start,
    uint32_t count, DNP3PointList *points)
{
    int rc = 0;

    switch (DNP3_OBJECT_CODE(group, variation)) {
{% for object in objects %}
        case DNP3_OBJECT_CODE({{object.group}}, {{object.variation}}):
            rc = DNP3DecodeObjectG{{object.group}}V{{object.variation}}(buf, len, prefix_code, start, count,
                points);
            break;
{% endfor %}
        default:
            return DNP3_DECODER_EVENT_UNKNOWN_OBJECT;
    }

    return rc ? 0 : DNP3_DECODER_EVENT_MALFORMED;
}

"""

    try:
        filename = "src/app-layer-dnp3-objects.c"
        env = jinja2.Environment(trim_blocks=True, lstrip_blocks=True)
        code = env.from_string(template).render(context)
        content = open(filename).read()
        content = re.sub(
            "(%s).*(%s)" % (re.escape(IN_PLACE_START), re.escape(IN_PLACE_END)),
            r"\1%s\2" % (code), content, 1, re.M | re.DOTALL)
        open(filename, "w").write(content)
        print("Updated %s." % (filename))
    except Exception as err:
        print("Failed to update %s: %s" % (filename, err), file=sys.stderr)
        sys.exit(1)

def preprocess_object(obj):

    valid_keys = [
        "group",
        "variation",
        "constraints",
        "extra_fields",
        "fields",
        "packed",
    ]

    valid_field_keys = [
        "type",
        "name",
        "width",
        "len_from_prefix",
        "len_field",
        "fields",
        "size",
    ]

    if "unimplemented" in obj:
        print("Object not implemented: %s:%s: %s" % (
            str(obj["group"]), str(obj["variation"]), obj["unimplemented"]))
        return None

    for key, val in obj.items():

        if key not in valid_keys:
            print("Invalid key '%s' in object %d:%d" % (
                key, obj["group"], obj["variation"]), file=sys.stderr)
            sys.exit(1)

    for field in obj["fields"]:

        for key in field.keys():
            if key not in valid_field_keys:
                print("Invalid key '%s' in object %d:%d" % (
                    key, obj["group"], obj["variation"]), file=sys.stderr)
                sys.exit(1)

        if "len_from_prefix" in field and field["len_from_prefix"]:
            obj["_track_offset"] = True
            break

        if field["type"] == "bstr8":
            width = 0
            for subfield in field["fields"]:
                width += int(subfield["width"])
            assert(width == 8)

    return obj

def main():

    definitions = yaml.load(open("scripts/dnp3-gen/dnp3-objects.yaml"))
    print("Loaded %s objects." % (len(definitions["objects"])))
    definitions["objects"] = map(preprocess_object, definitions["objects"])

    # Filter out unimplemented objects.
    definitions["objects"] = [
        obj for obj in definitions["objects"] if obj != None]

    context = {
        "raise": raise_helper,
        "objects": definitions["objects"],
        "is_integer_type": is_integer_type,
        "f_to_type": to_type,
        "f_has_freeable_types": has_freeable_types,
    }

    gen_object_structs(context)
    gen_object_decoders(context)
    generate(util_lua_dnp3_objects_c_template,
             "src/util-lua-dnp3-objects.c",
             context)
    generate(output_json_dnp3_objects_template,
             "src/output-json-dnp3-objects.c",
             context)

if __name__ == "__main__":
    sys.exit(main())
