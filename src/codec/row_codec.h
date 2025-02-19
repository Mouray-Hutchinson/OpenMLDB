/*
 * Copyright 2021 4Paradigm
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/glog_wapper.h"
#include "boost/algorithm/string.hpp"
#include "boost/container/deque.hpp"
#include "codec/schema_codec.h"
#include "storage/segment.h"

namespace openmldb {
namespace codec {

using ::openmldb::storage::DataBlock;

class RowCodec {
 public:
    static int32_t CalStrLength(const std::map<std::string, std::string>& str_map, const Schema& schema) {
        int32_t str_len = 0;
        for (int i = 0; i < schema.size(); i++) {
            const ::openmldb::common::ColumnDesc& col = schema.Get(i);
            if (col.data_type() == ::openmldb::type::kVarchar || col.data_type() == ::openmldb::type::kString) {
                auto iter = str_map.find(col.name());
                if (iter == str_map.end()) {
                    return -1;
                }
                if (!col.not_null() && (iter->second == "null" || iter->second == NONETOKEN)) {
                    continue;
                } else if (iter->second == "null" || iter->second == NONETOKEN) {
                    return -1;
                }
                str_len += iter->second.length();
            }
        }
        return str_len;
    }

    static int32_t CalStrLength(const std::vector<std::string>& input_value, const Schema& schema) {
        if (input_value.size() != (uint64_t)schema.size()) {
            return -1;
        }
        int32_t str_len = 0;
        for (int i = 0; i < schema.size(); i++) {
            const ::openmldb::common::ColumnDesc& col = schema.Get(i);
            if (col.data_type() == ::openmldb::type::kVarchar || col.data_type() == ::openmldb::type::kString) {
                if (!col.not_null() && (input_value[i] == "null" || input_value[i] == NONETOKEN)) {
                    continue;
                } else if (input_value[i] == "null" || input_value[i] == NONETOKEN) {
                    return -1;
                }
                str_len += input_value[i].length();
            }
        }
        return str_len;
    }

    static ::openmldb::base::Status EncodeRow(const std::vector<std::string> input_value, const Schema& schema,
                                                 uint32_t version,
                                                 std::string& row) {  // NOLINT
        if (input_value.empty() || input_value.size() != (uint64_t)schema.size()) {
            return ::openmldb::base::Status(-1, "input error");
        }
        int32_t str_len = CalStrLength(input_value, schema);
        if (str_len < 0) {
            return ::openmldb::base::Status(-1, "cal str len failed");
        }
        ::openmldb::codec::RowBuilder builder(schema);
        uint32_t size = builder.CalTotalLength(str_len);
        builder.SetSchemaVersion(version);
        row.resize(size);
        builder.SetBuffer(reinterpret_cast<int8_t*>(&(row[0])), size);
        for (int i = 0; i < schema.size(); i++) {
            const ::openmldb::common::ColumnDesc& col = schema.Get(i);
            if (!col.not_null() && (input_value[i] == "null" || input_value[i] == NONETOKEN)) {
                builder.AppendNULL();
                continue;
            } else if (input_value[i] == "null" || input_value[i] == NONETOKEN) {
                return ::openmldb::base::Status(-1, col.name() + " should not be null");
            }
            if (!builder.AppendValue(input_value[i])) {
                std::string msg = "append " + ::openmldb::type::DataType_Name(col.data_type()) + " error";
                return ::openmldb::base::Status(-1, msg);
            }
        }
        return ::openmldb::base::Status(0, "ok");
    }

    static ::openmldb::base::Status EncodeRow(const std::map<std::string, std::string>& str_map,
                                                 const Schema& schema, int32_t version,
                                                 std::string& row) {  // NOLINT
        if (str_map.empty() || str_map.size() != (uint64_t)schema.size()) {
            return ::openmldb::base::Status(-1, "input error");
        }
        int32_t str_len = CalStrLength(str_map, schema);
        if (str_len < 0) {
            return ::openmldb::base::Status(-1, "cal str len error");
        }
        ::openmldb::codec::RowBuilder builder(schema);
        builder.SetSchemaVersion(version);
        uint32_t size = builder.CalTotalLength(str_len);
        row.resize(size);
        builder.SetBuffer(reinterpret_cast<int8_t*>(&(row[0])), size);
        for (int i = 0; i < schema.size(); i++) {
            const ::openmldb::common::ColumnDesc& col = schema.Get(i);
            auto iter = str_map.find(col.name());
            if (iter == str_map.end()) {
                return ::openmldb::base::Status(-1, col.name() + " not in str_map");
            }
            if (!col.not_null() && (iter->second == "null" || iter->second == NONETOKEN)) {
                builder.AppendNULL();
                continue;
            } else if (iter->second == "null" || iter->second == NONETOKEN) {
                return ::openmldb::base::Status(-1, col.name() + " should not be null");
            }
            if (!builder.AppendValue(iter->second)) {
                std::string msg = "append " + ::openmldb::type::DataType_Name(col.data_type()) + " error";
                return ::openmldb::base::Status(-1, msg);
            }
        }
        return ::openmldb::base::Status(0, "ok");
    }


    static bool DecodeRow(const Schema& schema,  // NOLINT
                          const ::openmldb::base::Slice& value,
                          std::vector<std::string>& value_vec) {  // NOLINT
        openmldb::codec::RowView rv(schema, reinterpret_cast<int8_t*>(const_cast<char*>(value.data())), value.size());
        return DecodeRow(schema, rv, false, 0, schema.size(), &value_vec);
    }

    static bool DecodeRow(const Schema& schema, const int8_t* data, int32_t size, bool replace_empty_str, int start,
                          int len, std::vector<std::string>& values) {  // NOLINT
        openmldb::codec::RowView rv(schema, data, size);
        return DecodeRow(schema, rv, replace_empty_str, start, len, &values);
    }

    static bool DecodeRow(const Schema& schema,                   // NOLINT
                          openmldb::codec::RowView& rv,           // NOLINT
                          std::vector<std::string>& value_vec) {  // NOLINT
        return DecodeRow(schema, rv, false, 0, schema.size(), &value_vec);
    }

    static bool DecodeRow(const Schema& schema,
                          openmldb::codec::RowView& rv,  // NOLINT
                          bool replace_empty_str, int start, int length, std::vector<std::string>* value_vec) {
        int end = start + length;
        if (length <= 0) {
            return false;
        }
        for (int32_t i = 0; i < end && i < schema.size(); i++) {
            if (rv.IsNULL(i)) {
                value_vec->emplace_back(NONETOKEN);
                continue;
            }
            std::string col;
            rv.GetStrValue(i, &col);
            if (replace_empty_str && col.empty()) {
                col = EMPTY_STRING;
            }
            value_vec->emplace_back(std::move(col));
        }
        return true;
    }

};
__attribute__((unused)) static bool DecodeRows(const std::string& data, uint32_t count, const Schema& schema,
                                               std::vector<std::vector<std::string>>* row_vec) {
    openmldb::codec::RowView rv(schema);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < count; i++) {
        std::vector<std::string> row;
        const char* ch = data.c_str();
        ch += offset;
        uint32_t value_size = 0;
        memcpy(static_cast<void*>(&value_size), ch, 4);
        ch += 4;
        bool ok = rv.Reset(reinterpret_cast<int8_t*>(const_cast<char*>(ch)), value_size);
        if (!ok) {
            return false;
        }
        offset += 4 + value_size;
        if (!openmldb::codec::RowCodec::DecodeRow(schema, rv, row)) {
            return false;
        }
        for (uint64_t i = 0; i < row.size(); i++) {
            if (row[i] == openmldb::codec::NONETOKEN) {
                row[i] = "null";
            }
        }
        row_vec->push_back(std::move(row));
    }
    return true;
}

static inline void Encode(uint64_t time, const char* data, const size_t size, char* buffer, uint32_t offset) {
    buffer += offset;
    uint32_t total_size = 8 + size;
    memcpy(buffer, static_cast<const void*>(&total_size), 4);
    memrev32ifbe(buffer);
    buffer += 4;
    memcpy(buffer, static_cast<const void*>(&time), 8);
    memrev64ifbe(buffer);
    buffer += 8;
    memcpy(buffer, static_cast<const void*>(data), size);
}

static inline void Encode(uint64_t time, const DataBlock* data, char* buffer, uint32_t offset) {
    return Encode(time, data->data, data->size, buffer, offset);
}

static inline void Encode(const char* data, const size_t size, char* buffer, uint32_t offset) {
    buffer += offset;
    memcpy(buffer, static_cast<const void*>(&size), 4);
    memrev32ifbe(buffer);
    buffer += 4;
    memcpy(buffer, static_cast<const void*>(data), size);
}

static inline void Encode(const DataBlock* data, char* buffer, uint32_t offset) {
    return Encode(data->data, data->size, buffer, offset);
}
static inline int32_t EncodeRows(const std::vector<::openmldb::base::Slice>& rows, uint32_t total_block_size,
                                 std::string* body) {
    if (body == NULL) {
        PDLOG(WARNING, "invalid output body");
        return -1;
    }

    uint32_t total_size = rows.size() * 4 + total_block_size;
    if (rows.size() > 0) {
        body->resize(total_size);
    }
    uint32_t offset = 0;
    char* rbuffer = reinterpret_cast<char*>(&((*body)[0]));
    for (auto lit = rows.begin(); lit != rows.end(); ++lit) {
        ::openmldb::codec::Encode(lit->data(), lit->size(), rbuffer, offset);
        offset += (4 + lit->size());
    }
    return total_size;
}

static inline int32_t EncodeRows(const boost::container::deque<std::pair<uint64_t, ::openmldb::base::Slice>>& rows,
                                 uint32_t total_block_size, std::string* pairs) {
    if (pairs == NULL) {
        PDLOG(WARNING, "invalid output pairs");
        return -1;
    }

    uint32_t total_size = rows.size() * (8 + 4) + total_block_size;
    if (rows.size() > 0) {
        pairs->resize(total_size);
    }

    char* rbuffer = reinterpret_cast<char*>(&((*pairs)[0]));
    uint32_t offset = 0;
    for (auto lit = rows.begin(); lit != rows.end(); ++lit) {
        ::openmldb::codec::Encode(lit->first, lit->second.data(), lit->second.size(), rbuffer, offset);
        offset += (4 + 8 + lit->second.size());
    }
    return total_size;
}

// encode pk, ts and value
static inline void EncodeFull(const std::string& pk, uint64_t time, const char* data, const size_t size, char* buffer,
                              uint32_t offset) {
    buffer += offset;
    uint32_t pk_size = pk.length();
    uint32_t total_size = 8 + pk_size + size;
    DEBUGLOG("encode total size %u pk size %u", total_size, pk_size);
    memcpy(buffer, static_cast<const void*>(&total_size), 4);
    memrev32ifbe(buffer);
    buffer += 4;
    memcpy(buffer, static_cast<const void*>(&pk_size), 4);
    memrev32ifbe(buffer);
    buffer += 4;
    memcpy(buffer, static_cast<const void*>(&time), 8);
    memrev64ifbe(buffer);
    buffer += 8;
    memcpy(buffer, static_cast<const void*>(pk.c_str()), pk_size);
    buffer += pk_size;
    memcpy(buffer, static_cast<const void*>(data), size);
}
static inline void EncodeFull(const std::string& pk, uint64_t time, const DataBlock* data, char* buffer,
                              uint32_t offset) {
    return EncodeFull(pk, time, data->data, data->size, buffer, offset);
}

static inline void Decode(const std::string* str, std::vector<std::pair<uint64_t, std::string*>>& pairs) {  // NOLINT
    const char* buffer = str->c_str();
    uint32_t total_size = str->length();
    DEBUGLOG("total size %d %s", total_size, ::openmldb::base::DebugString(*str).c_str());
    while (total_size > 0) {
        uint32_t size = 0;
        memcpy(static_cast<void*>(&size), buffer, 4);
        memrev32ifbe(static_cast<void*>(&size));
        DEBUGLOG("decode size %d", size);
        buffer += 4;
        uint64_t time = 0;
        memcpy(static_cast<void*>(&time), buffer, 8);
        memrev64ifbe(static_cast<void*>(&time));
        buffer += 8;
        assert(size >= 8);
        std::string* data = new std::string(size - 8, '0');
        memcpy(reinterpret_cast<char*>(&((*data)[0])), buffer, size - 8);
        buffer += (size - 8);
        pairs.push_back(std::make_pair(time, data));
        total_size -= (size + 4);
    }
}

static inline void DecodeFull(const std::string* str,
                         std::map<std::string, std::vector<std::pair<uint64_t, std::string*>>>& value_map) { // NOLINT
    const char* buffer = str->c_str();
    uint32_t total_size = str->length();
    DEBUGLOG("total size %u %s", total_size, ::openmldb::base::DebugString(*str).c_str());
    while (total_size > 0) {
        uint32_t size = 0;
        memcpy(static_cast<void*>(&size), buffer, 4);
        memrev32ifbe(static_cast<void*>(&size));
        DEBUGLOG("decode size %u", size);
        buffer += 4;
        uint32_t pk_size = 0;
        memcpy(static_cast<void*>(&pk_size), buffer, 4);
        buffer += 4;
        memrev32ifbe(static_cast<void*>(&pk_size));
        DEBUGLOG("decode size %u", pk_size);
        assert(size > pk_size + 8);
        uint64_t time = 0;
        memcpy(static_cast<void*>(&time), buffer, 8);
        memrev64ifbe(static_cast<void*>(&time));
        buffer += 8;
        std::string pk(buffer, pk_size);
        buffer += pk_size;
        uint32_t value_size = size - 8 - pk_size;
        std::string* data = new std::string(value_size, '0');
        memcpy(reinterpret_cast<char*>(&((*data)[0])), buffer, value_size);
        buffer += value_size;
        if (value_map.find(pk) == value_map.end()) {
            value_map.insert(std::make_pair(pk, std::vector<std::pair<uint64_t, std::string*>>()));
        }
        value_map[pk].push_back(std::make_pair(time, data));
        total_size -= (size + 8);
    }
}

}  // namespace codec
}  // namespace openmldb
