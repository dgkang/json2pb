/*
 * Copyright (c) 2013 Pavel Shramov <shramov@mexmat.net>
 *
 * json2pb is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include <errno.h>
#include <jansson.h>

#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>

#include <json2pb.h>

#include <exception>

using google::protobuf::Message;
using google::protobuf::Descriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::EnumDescriptor;
using google::protobuf::EnumValueDescriptor;
using google::protobuf::Reflection;

struct json_autoptr {
	json_t * ptr;
	json_autoptr(json_t *json) : ptr(json) {}
	~json_autoptr() { if (ptr) json_decref(ptr); }
	json_t * release() { json_t *tmp = ptr; ptr = 0; return tmp; }
};

class j2pb_error : public std::exception {
	std::string _error;
public:
	j2pb_error(const std::string &e) : _error(e) {}
	j2pb_error(const FieldDescriptor *field, const std::string &e) : _error(field->name() + ": " + e) {}
	virtual ~j2pb_error() throw() {};

	virtual const char *what() const throw () { return _error.c_str(); };
};

static std::string hex2bin(const std::string &s)
{
	if (s.size() % 2)
		throw j2pb_error("Odd hex data size");
	static const char lookup[] = ""
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0x00
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0x10
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0x20
		"\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x10\x10\x10\x10\x10\x10" // 0x30
		"\x10\x0a\x0b\x0c\x0d\x0e\x0f\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0x40
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0x50
		"\x10\x0a\x0b\x0c\x0d\x0e\x0f\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0x60
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0x70
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0x80
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0x90
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0xa0
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0xb0
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0xc0
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0xd0
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0xe0
		"\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10" // 0xf0
		"";
	std::string r;
	r.reserve(s.size() / 2);
	for (size_t i = 0; i < s.size(); i += 2) {
		char hi = lookup[s[i]];
		char lo = lookup[s[i+1]];
		if (0x80 & (hi | lo))
			throw j2pb_error("Invalid hex data: " + s.substr(i, 6));
		r.push_back((hi << 4) | lo);
	}
	return r;
}

static std::string bin2hex(const std::string &s)
{
	static const char lookup[] = "0123456789abcdef";
	std::string r;
	r.reserve(s.size() * 2);
	for (size_t i = 0; i < s.size(); i++) {
		char hi = s[i] >> 4;
		char lo = s[i] & 0xf;
		r.push_back(lookup[hi]);
		r.push_back(lookup[lo]);
	}
	return r;
}

static json_t * _pb2json(const Message& msg);
static json_t * _field2json(const Message& msg, const FieldDescriptor *field, size_t index)
{
	const Reflection *ref = msg.GetReflection();
	const bool repeated = field->is_repeated();
	json_t *jf = 0;
	switch (field->cpp_type())
	{
#define _CONVERT(type, ctype, fmt, sfunc, afunc)		\
		case FieldDescriptor::type: {			\
			const ctype value = (repeated)?		\
				ref->afunc(msg, field, index):	\
				ref->sfunc(msg, field);		\
			jf = fmt(value);			\
			break;					\
		}

		_CONVERT(CPPTYPE_DOUBLE, double, json_real, GetDouble, GetRepeatedDouble);
		_CONVERT(CPPTYPE_FLOAT, double, json_real, GetFloat, GetRepeatedFloat);
		_CONVERT(CPPTYPE_INT64, json_int_t, json_integer, GetInt64, GetRepeatedInt64);
		_CONVERT(CPPTYPE_UINT64, json_int_t, json_integer, GetUInt64, GetRepeatedUInt64);
		_CONVERT(CPPTYPE_INT32, json_int_t, json_integer, GetInt32, GetRepeatedInt32);
		_CONVERT(CPPTYPE_UINT32, json_int_t, json_integer, GetUInt32, GetRepeatedUInt32);
		_CONVERT(CPPTYPE_BOOL, bool, json_boolean, GetBool, GetRepeatedBool);
#undef _CONVERT
		case FieldDescriptor::CPPTYPE_STRING: {
			std::string scratch;
			const std::string &value = (repeated)?
				ref->GetRepeatedStringReference(msg, field, index, &scratch):
				ref->GetStringReference(msg, field, &scratch);
			if (field->type() == FieldDescriptor::TYPE_BYTES)
				jf = json_string(bin2hex(value).c_str());
			else
				jf = json_string(value.c_str());
			break;
		}
		case FieldDescriptor::CPPTYPE_MESSAGE: {
			const Message& mf = (repeated)?
				ref->GetRepeatedMessage(msg, field, index):
				ref->GetMessage(msg, field);
			jf = _pb2json(mf);
			break;
		}
		case FieldDescriptor::CPPTYPE_ENUM: {
			const EnumValueDescriptor* ef = (repeated)?
				ref->GetRepeatedEnum(msg, field, index):
				ref->GetEnum(msg, field);

			jf = json_integer(ef->number());
			break;
		}
		default:
			break;
	}
	if (!jf) throw j2pb_error(field, "Fail to convert to json");
	return jf;
}

static json_t * _pb2json(const Message& msg)
{
	const Descriptor *d = msg.GetDescriptor();
	const Reflection *ref = msg.GetReflection();
	if (!d || !ref) return 0;

	json_t *root = json_object();
	json_autoptr _auto(root);

	for (size_t i = 0; i != d->field_count(); i++)
	{
		const FieldDescriptor *field = d->field(i);
		if (!field) return 0;

		json_t *jf = 0;
		if(field->is_repeated()) {
			size_t count = ref->FieldSize(msg, field);
			if (!count) continue;

			json_autoptr array(json_array());
			for (size_t j = 0; j < count; j++)
				json_array_append_new(array.ptr, _field2json(msg, field, j));
			jf = array.release();
		} else if (ref->HasField(msg, field))
			jf = _field2json(msg, field, 0);
		else
			continue;

		json_object_set_new(root, field->name().c_str(), jf);
	}
	return _auto.release();
}

static void _json2pb(Message& msg, const json_t *root, json_error_t *error);
static void _json2field(Message &msg, const FieldDescriptor *field, json_t *jf, json_error_t *error)
{
	const Reflection *ref = msg.GetReflection();
	const bool repeated = field->is_repeated();

	switch (field->cpp_type())
	{
#define _CONVERT(type, ctype, fmt, sfunc, afunc) 		\
		case FieldDescriptor::type: {			\
			ctype value;				\
			int r = json_unpack_ex(jf, error, JSON_STRICT, fmt, &value); \
			if (r) throw j2pb_error(field, "Failed to unpack"); \
			if (repeated)				\
				ref->afunc(&msg, field, value);	\
			else					\
				ref->sfunc(&msg, field, value);	\
			break;					\
		}

		_CONVERT(CPPTYPE_DOUBLE, double, "f", SetDouble, AddDouble);
		_CONVERT(CPPTYPE_FLOAT, double, "f", SetFloat, AddFloat);
		_CONVERT(CPPTYPE_INT64, json_int_t, "I", SetInt64, AddInt64);
		_CONVERT(CPPTYPE_UINT64, json_int_t, "I", SetUInt64, AddUInt64);
		_CONVERT(CPPTYPE_INT32, json_int_t, "I", SetInt32, AddInt32);
		_CONVERT(CPPTYPE_UINT32, json_int_t, "I", SetUInt32, AddUInt32);
		_CONVERT(CPPTYPE_BOOL, int, "b", SetBool, AddBool);

		case FieldDescriptor::CPPTYPE_STRING: {
			if (!json_is_string(jf))
				throw j2pb_error(field, "Not a string");
			const char * value = json_string_value(jf);
			if (field->type() == FieldDescriptor::TYPE_BYTES)
				ref->SetString(&msg, field, hex2bin(value));
			else
				ref->SetString(&msg, field, value);
			break;
		}
		case FieldDescriptor::CPPTYPE_MESSAGE: {
			Message *mf = ref->MutableMessage(&msg, field);
			_json2pb(*mf, jf, error);
			break;
		}
		case FieldDescriptor::CPPTYPE_ENUM: {
			const EnumDescriptor *ed = field->enum_type();
			const EnumValueDescriptor *ev = 0;
			if (json_is_integer(jf)) {
				ev = ed->FindValueByNumber(json_integer_value(jf));
			} else if (json_is_string(jf)) {
				ev = ed->FindValueByName(json_string_value(jf));
			} else
				throw j2pb_error(field, "Not an integer or string");
			if (!ev)
				throw j2pb_error(field, "Enum value not found");
			ref->SetEnum(&msg, field, ev);
			break;
		}
		default:
			break;
	}
}

static void _json2pb(Message& msg, const json_t *root, json_error_t *error)
{
	const Descriptor *d = msg.GetDescriptor();
	const Reflection *ref = msg.GetReflection();
	if (!d || !ref) throw j2pb_error("No descriptor or reflection");

	for (size_t i = 0; i != d->field_count(); i++)
	{
		const FieldDescriptor *field = d->field(i);
		if (!field) throw j2pb_error("No field descriptor");

		json_t *jf = json_object_get(root, field->name().c_str());
		if (!jf) {
			if (field->is_required())
				throw j2pb_error(field, "required but missing");
			continue;
		}

		int r = 0;
		if (field->is_repeated()) {
			if (!json_is_array(jf))
				throw j2pb_error(field, "Not array");
			for (size_t j = 0; j < json_array_size(jf); j++)
				_json2field(msg, field, json_array_get(jf, j), error);
		} else
			_json2field(msg, field, jf, error);
	}
}

void json2pb(Message &msg, const char *buf, size_t size)
{
	json_t *root;
	json_error_t error;

	root = json_loadb(buf, size, 0, &error);

	if (!root)
		throw j2pb_error(std::string("Load failed: ") + error.text);

	json_autoptr _auto(root);

	if (!json_is_object(root))
		throw j2pb_error("Malformed JSON: not an object");

	_json2pb(msg, root, &error);
}

int json_dump_std_string(const char *buf, size_t size, void *data)
{
	std::string *s = (std::string *) data;
	s->append(buf, size);
	return 0;
}

std::string pb2json(const Message &msg)
{
	std::string r;

	json_t *root = _pb2json(msg);
	json_dump_callback(root, json_dump_std_string, &r, 0);
	return r;
}
