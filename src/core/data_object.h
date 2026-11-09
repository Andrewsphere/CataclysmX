#pragma once

#include "core/containers.h"
#include "core/diagnostics.h"
#include <EASTL/expected.h>
#include <EASTL/string_view.h>
#include <cstdint>

namespace cataclysmx {

enum class DataType { Boolean, Signed, Unsigned, String, List, Object };
enum class DataError { Absent, WrongType, InvalidData, DepthLimit };
template<class T> using DataResult = eastl::expected<T, DataError>;

// An owned value tree. Objects retain insertion order and have unique names.
class DataObject {
public:
    static DataObject Boolean(bool value);
    static DataObject Signed(std::int64_t value);
    static DataObject Unsigned(std::uint64_t value);
    static DataObject Text(eastl::string_view value);
    static DataObject List();
    DataType Type() const { return type_; }
    const DataObject* Find(eastl::string_view name) const;
    DataObject* Find(eastl::string_view name);
    DataResult<const DataObject*> Get(eastl::string_view name, DataType type) const;
    DataResult<bool> GetBoolean(eastl::string_view name) const;
    DataResult<std::int64_t> GetSigned(eastl::string_view name) const;
    DataResult<std::uint64_t> GetUnsigned(eastl::string_view name) const;
    DataResult<eastl::string_view> GetString(eastl::string_view name) const;
    void Set(eastl::string_view name, DataObject value);
    void SetBoolean(eastl::string_view name, bool value);
    void SetSigned(eastl::string_view name, std::int64_t value);
    void SetUnsigned(eastl::string_view name, std::uint64_t value);
    void SetString(eastl::string_view name, eastl::string_view value);
    void Append(DataObject value);
    std::size_t Size() const { return values_.size(); }
    const String& Name(std::size_t index) const;
    const DataObject& At(std::size_t index) const;
    bool BooleanValue() const;
    std::int64_t SignedValue() const;
    std::uint64_t UnsignedValue() const;
    const String& StringValue() const;
    bool operator==(const DataObject&) const = default;
private:
    DataType type_ = DataType::Object;
    bool boolean_ = false;
    std::int64_t signed_ = 0;
    std::uint64_t unsigned_ = 0;
    String string_;
    Vector<String> names_;
    Vector<DataObject> values_;
};

// The JSON boundary is implemented by the save subsystem; no JSON types escape.
String EncodeDataObject(const DataObject& value);
DataResult<DataObject> DecodeDataObject(eastl::string_view text, Diagnostics* diagnostics = nullptr);

}
