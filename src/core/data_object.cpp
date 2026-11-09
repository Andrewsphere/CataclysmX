#include "core/data_object.h"
#include <cstdlib>

namespace cataclysmx {
namespace { void Require(bool condition) { if (!condition) { std::abort(); } } }
DataObject DataObject::Boolean(bool value) { DataObject r; r.type_ = DataType::Boolean; r.boolean_ = value; return r; }
DataObject DataObject::Signed(std::int64_t value) { DataObject r; r.type_ = DataType::Signed; r.signed_ = value; return r; }
DataObject DataObject::Unsigned(std::uint64_t value) { DataObject r; r.type_ = DataType::Unsigned; r.unsigned_ = value; return r; }
DataObject DataObject::Text(eastl::string_view value) { DataObject r; r.type_ = DataType::String; r.string_.assign(value.data(), value.size()); return r; }
DataObject DataObject::List() { DataObject r; r.type_ = DataType::List; return r; }
const DataObject* DataObject::Find(eastl::string_view name) const {
    Require(type_ == DataType::Object);
    for (std::size_t i = 0; i < names_.size(); ++i) { if (names_[i] == name) { return &values_[i]; } }
    return nullptr;
}
DataObject* DataObject::Find(eastl::string_view name) { return const_cast<DataObject*>(static_cast<const DataObject&>(*this).Find(name)); }
DataResult<const DataObject*> DataObject::Get(eastl::string_view name, DataType type) const {
    if (type_ != DataType::Object) { return eastl::unexpected(DataError::WrongType); }
    const auto* value = Find(name);
    if (!value) { return eastl::unexpected(DataError::Absent); }
    if (value->Type() != type) { return eastl::unexpected(DataError::WrongType); }
    return value;
}
DataResult<bool> DataObject::GetBoolean(eastl::string_view name) const { auto r = Get(name, DataType::Boolean); if (!r) { return eastl::unexpected(r.error()); } return (*r)->BooleanValue(); }
DataResult<std::int64_t> DataObject::GetSigned(eastl::string_view name) const { auto r = Get(name, DataType::Signed); if (!r) { return eastl::unexpected(r.error()); } return (*r)->SignedValue(); }
DataResult<std::uint64_t> DataObject::GetUnsigned(eastl::string_view name) const { auto r = Get(name, DataType::Unsigned); if (!r) { return eastl::unexpected(r.error()); } return (*r)->UnsignedValue(); }
DataResult<eastl::string_view> DataObject::GetString(eastl::string_view name) const { auto r = Get(name, DataType::String); if (!r) { return eastl::unexpected(r.error()); } return eastl::string_view((*r)->StringValue()); }
void DataObject::Set(eastl::string_view name, DataObject value) {
    Require(type_ == DataType::Object);
    if (auto* existing = Find(name)) { *existing = eastl::move(value); return; }
    names_.emplace_back(name.data(), name.size()); values_.push_back(eastl::move(value));
}
void DataObject::SetBoolean(eastl::string_view name, bool value) { Set(name, Boolean(value)); }
void DataObject::SetSigned(eastl::string_view name, std::int64_t value) { Set(name, Signed(value)); }
void DataObject::SetUnsigned(eastl::string_view name, std::uint64_t value) { Set(name, Unsigned(value)); }
void DataObject::SetString(eastl::string_view name, eastl::string_view value) { Set(name, Text(value)); }
void DataObject::Append(DataObject value) { Require(type_ == DataType::List); values_.push_back(eastl::move(value)); }
const String& DataObject::Name(std::size_t index) const { Require(type_ == DataType::Object && index < names_.size()); return names_[index]; }
const DataObject& DataObject::At(std::size_t index) const { Require(index < values_.size()); return values_[index]; }
bool DataObject::BooleanValue() const { Require(type_ == DataType::Boolean); return boolean_; }
std::int64_t DataObject::SignedValue() const { Require(type_ == DataType::Signed); return signed_; }
std::uint64_t DataObject::UnsignedValue() const { Require(type_ == DataType::Unsigned); return unsigned_; }
const String& DataObject::StringValue() const { Require(type_ == DataType::String); return string_; }
}
