// Copyright (c) 2023 dingodb.com, Inc. All Rights Reserved
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "record_encoder.h"

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <string>

// #include "common/helper.h"
#include "serial/utils/keyvalue.h"  // IWYU pragma: keep

namespace dingodb {

static bool StringToBool(const std::string& str) {
  return !(str == "0" || str == "false");
}
static int32_t StringToInt32(const std::string& str) {
  return std::strtol(str.c_str(), nullptr, 10);
}
static int64_t StringToInt64(const std::string& str) {
  return std::strtoll(str.c_str(), nullptr, 10);
}
static float StringToFloat(const std::string& str) {
  return std::strtof(str.c_str(), nullptr);
}
static double StringToDouble(const std::string& str) {
  return std::strtod(str.c_str(), nullptr);
}

RecordEncoderV1::RecordEncoderV1(
    int schema_version,
    std::shared_ptr<std::vector<std::shared_ptr<BaseSchema>>> schemas,
    long common_id) {
  this->le_ = IsLE();
  Init(schema_version, schemas, common_id);
}

RecordEncoderV1::RecordEncoderV1(
    int schema_version,
    std::shared_ptr<std::vector<std::shared_ptr<BaseSchema>>> schemas,
    long common_id, bool le) {
  this->le_ = le;
  Init(schema_version, schemas, common_id);
}

void RecordEncoderV1::Init(
    int schema_version,
    std::shared_ptr<std::vector<std::shared_ptr<BaseSchema>>> schemas,
    long common_id) {
  this->schema_version_ = schema_version;
  FormatSchema(schemas, this->le_);
  this->schemas_ = schemas;
  this->common_id_ = common_id;
  int32_t* size = GetApproPerRecordSize(schemas);
  this->key_buf_size_ = size[0];
  this->value_buf_size_ = size[1];
  delete[] size;
}

void RecordEncoderV1::EncodePrefix(Buf& buf, char prefix) const {
  buf.Write(prefix);
  buf.WriteLong(common_id_);
}

void RecordEncoderV1::EncodeReverseTag(Buf& buf) const {
  buf.ReverseWrite(codec_version_);
  buf.ReverseWrite(0);
  buf.ReverseWrite(0);
  buf.ReverseWrite(0);
}

void RecordEncoderV1::EncodeSchemaVersion(Buf& buf) const {
  buf.WriteInt(schema_version_);
}

int RecordEncoderV1::Encode(char prefix, const std::vector<std::any>& record,
                            std::string& key, std::string& value) {
  int ret = EncodeKey(prefix, record, key);
  if (ret < 0) {
    return ret;
  }
  ret = EncodeValue(record, value);
  if (ret < 0) {
    return ret;
  }
  return 0;
}

int RecordEncoderV1::EncodeKey(char prefix, const std::vector<std::any>& record,
                               std::string& output) {
  Buf buf(key_buf_size_, this->le_);
  // |namespace|id| ... |tag|
  buf.EnsureRemainder(13);
  EncodePrefix(buf, prefix);
  EncodeReverseTag(buf);
  int index = 0;
  for (const auto& bs : *schemas_) {
    if (bs) {
      BaseSchema::Type type = bs->GetType();
      switch (type) {
        case BaseSchema::kBool: {
          auto bos =
              std::dynamic_pointer_cast<DingoSchema<std::optional<bool>>>(bs);
          if (bos->IsKey()) {
            bos->EncodeKey(
                &buf, std::any_cast<std::optional<bool>>(record.at(index)));
          }
          break;
        }
        case BaseSchema::kInteger: {
          auto is =
              std::dynamic_pointer_cast<DingoSchema<std::optional<int32_t>>>(
                  bs);
          if (is->IsKey()) {
            is->EncodeKey(
                &buf, std::any_cast<std::optional<int32_t>>(record.at(index)));
          }
          break;
        }
        case BaseSchema::kFloat: {
          auto fs =
              std::dynamic_pointer_cast<DingoSchema<std::optional<float>>>(bs);
          if (fs->IsKey()) {
            fs->EncodeKey(
                &buf, std::any_cast<std::optional<float>>(record.at(index)));
          }
          break;
        }
        case BaseSchema::kLong: {
          auto ls =
              std::dynamic_pointer_cast<DingoSchema<std::optional<int64_t>>>(
                  bs);
          if (ls->IsKey()) {
            ls->EncodeKey(
                &buf, std::any_cast<std::optional<int64_t>>(record.at(index)));
          }
          break;
        }
        case BaseSchema::kDouble: {
          auto ds =
              std::dynamic_pointer_cast<DingoSchema<std::optional<double>>>(bs);
          if (ds->IsKey()) {
            ds->EncodeKey(
                &buf, std::any_cast<std::optional<double>>(record.at(index)));
          }
          break;
        }
        case BaseSchema::kString: {
          auto ss = std::dynamic_pointer_cast<
              DingoSchema<std::optional<std::shared_ptr<std::string>>>>(bs);
          if (ss->IsKey()) {
            ss->EncodeKey(
                &buf,
                std::any_cast<std::optional<std::shared_ptr<std::string>>>(
                    record.at(index)));
          }
          break;
        }
        default: {
          break;
        }
      }
    }
    index++;
  }

  return buf.GetBytes(output);
}

int RecordEncoderV1::EncodeValue(const std::vector<std::any>& record,
                                 std::string& output) {
  Buf buf(value_buf_size_, this->le_);
  buf.EnsureRemainder(4);
  EncodeSchemaVersion(buf);

  for (const auto& bs : *schemas_) {
    if (bs) {
      BaseSchema::Type type = bs->GetType();
      switch (type) {
        case BaseSchema::kBool: {
          auto bos =
              std::dynamic_pointer_cast<DingoSchema<std::optional<bool>>>(bs);
          if (!bos->IsKey()) {
            bos->EncodeValue(&buf, std::any_cast<std::optional<bool>>(
                                       record.at(bos->GetIndex())));
          }
          break;
        }
        case BaseSchema::kInteger: {
          auto is =
              std::dynamic_pointer_cast<DingoSchema<std::optional<int32_t>>>(
                  bs);
          if (!is->IsKey()) {
            is->EncodeValue(&buf, std::any_cast<std::optional<int32_t>>(
                                      record.at(is->GetIndex())));
          }
          break;
        }
        case BaseSchema::kFloat: {
          auto fs =
              std::dynamic_pointer_cast<DingoSchema<std::optional<float>>>(bs);
          if (!fs->IsKey()) {
            fs->EncodeValue(&buf, std::any_cast<std::optional<float>>(
                                      record.at(fs->GetIndex())));
          }
          break;
        }
        case BaseSchema::kLong: {
          auto ls =
              std::dynamic_pointer_cast<DingoSchema<std::optional<int64_t>>>(
                  bs);
          if (!ls->IsKey()) {
            ls->EncodeValue(&buf, std::any_cast<std::optional<int64_t>>(
                                      record.at(ls->GetIndex())));
          }
          break;
        }
        case BaseSchema::kDouble: {
          auto ds =
              std::dynamic_pointer_cast<DingoSchema<std::optional<double>>>(bs);
          if (!ds->IsKey()) {
            ds->EncodeValue(&buf, std::any_cast<std::optional<double>>(
                                      record.at(ds->GetIndex())));
          }
          break;
        }
        case BaseSchema::kString: {
          auto ss = std::dynamic_pointer_cast<
              DingoSchema<std::optional<std::shared_ptr<std::string>>>>(bs);
          if (!ss->IsKey()) {
            ss->EncodeValue(
                &buf,
                std::any_cast<std::optional<std::shared_ptr<std::string>>>(
                    record.at(ss->GetIndex())));
          }
          break;
        }
        case BaseSchema::kBoolList: {
          auto ss = std::dynamic_pointer_cast<
              DingoSchema<std::optional<std::shared_ptr<std::vector<bool>>>>>(
              bs);
          if (!ss->IsKey()) {
            ss->EncodeValue(
                &buf, std::any_cast<
                          std::optional<std::shared_ptr<std::vector<bool>>>>(
                          record.at(ss->GetIndex())));
          }
          break;
        }
        case BaseSchema::kStringList: {
          auto ss = std::dynamic_pointer_cast<DingoSchema<
              std::optional<std::shared_ptr<std::vector<std::string>>>>>(bs);
          if (!ss->IsKey()) {
            ss->EncodeValue(
                &buf,
                std::any_cast<
                    std::optional<std::shared_ptr<std::vector<std::string>>>>(
                    record.at(ss->GetIndex())));
          }
          break;
        }
        case BaseSchema::kDoubleList: {
          auto ss = std::dynamic_pointer_cast<
              DingoSchema<std::optional<std::shared_ptr<std::vector<double>>>>>(
              bs);
          if (!ss->IsKey()) {
            ss->EncodeValue(
                &buf, std::any_cast<
                          std::optional<std::shared_ptr<std::vector<double>>>>(
                          record.at(ss->GetIndex())));
          }
          break;
        }
        case BaseSchema::kFloatList: {
          auto ss = std::dynamic_pointer_cast<
              DingoSchema<std::optional<std::shared_ptr<std::vector<float>>>>>(
              bs);
          if (!ss->IsKey()) {
            ss->EncodeValue(
                &buf, std::any_cast<
                          std::optional<std::shared_ptr<std::vector<float>>>>(
                          record.at(ss->GetIndex())));
          }
          break;
        }
        case BaseSchema::kIntegerList: {
          auto ss = std::dynamic_pointer_cast<DingoSchema<
              std::optional<std::shared_ptr<std::vector<int32_t>>>>>(bs);
          if (!ss->IsKey()) {
            ss->EncodeValue(
                &buf, std::any_cast<
                          std::optional<std::shared_ptr<std::vector<int32_t>>>>(
                          record.at(ss->GetIndex())));
          }
          break;
        }
        case BaseSchema::kLongList: {
          auto ss = std::dynamic_pointer_cast<DingoSchema<
              std::optional<std::shared_ptr<std::vector<int64_t>>>>>(bs);
          if (!ss->IsKey()) {
            ss->EncodeValue(
                &buf, std::any_cast<
                          std::optional<std::shared_ptr<std::vector<int64_t>>>>(
                          record.at(ss->GetIndex())));
          }
          break;
        }
        default: {
          break;
        }
      }
    }
  }

  return buf.GetBytes(output);
}

int RecordEncoderV1::EncodeKeyPrefix(char prefix,
                                     const std::vector<std::any>& record,
                                     int column_count, std::string& output) {
  Buf buf(key_buf_size_, this->le_);
  buf.EnsureRemainder(9);
  EncodePrefix(buf, prefix);
  for (const auto& bs : *schemas_) {
    if (bs) {
      BaseSchema::Type type = bs->GetType();
      switch (type) {
        case BaseSchema::kBool: {
          auto bos =
              std::dynamic_pointer_cast<DingoSchema<std::optional<bool>>>(bs);
          if (bos->IsKey()) {
            bos->EncodeKeyPrefix(&buf, std::any_cast<std::optional<bool>>(
                                           record.at(bos->GetIndex())));
          }
          break;
        }
        case BaseSchema::kInteger: {
          auto is =
              std::dynamic_pointer_cast<DingoSchema<std::optional<int32_t>>>(
                  bs);
          if (is->IsKey()) {
            is->EncodeKeyPrefix(&buf, std::any_cast<std::optional<int32_t>>(
                                          record.at(is->GetIndex())));
          }
          break;
        }
        case BaseSchema::kFloat: {
          auto fs =
              std::dynamic_pointer_cast<DingoSchema<std::optional<float>>>(bs);
          if (fs->IsKey()) {
            fs->EncodeKeyPrefix(&buf, std::any_cast<std::optional<float>>(
                                          record.at(fs->GetIndex())));
          }
          break;
        }
        case BaseSchema::kLong: {
          auto ls =
              std::dynamic_pointer_cast<DingoSchema<std::optional<int64_t>>>(
                  bs);
          if (ls->IsKey()) {
            ls->EncodeKeyPrefix(&buf, std::any_cast<std::optional<int64_t>>(
                                          record.at(ls->GetIndex())));
          }
          break;
        }
        case BaseSchema::kDouble: {
          auto ds =
              std::dynamic_pointer_cast<DingoSchema<std::optional<double>>>(bs);
          if (ds->IsKey()) {
            ds->EncodeKeyPrefix(&buf, std::any_cast<std::optional<double>>(
                                          record.at(ds->GetIndex())));
          }
          break;
        }
        case BaseSchema::kString: {
          auto ss = std::dynamic_pointer_cast<
              DingoSchema<std::optional<std::shared_ptr<std::string>>>>(bs);
          if (ss->IsKey()) {
            ss->EncodeKeyPrefix(
                &buf,
                std::any_cast<std::optional<std::shared_ptr<std::string>>>(
                    record.at(ss->GetIndex())));
          }
          break;
        }
        default: {
          break;
        }
      }
    }

    if (--column_count <= 0) {
      break;
    }
  }

  return buf.GetBytes(output);
}

int RecordEncoderV1::EncodeKeyPrefix(char prefix,
                                     const std::vector<std::string>& keys,
                                     std::string& output) {
  Buf buf(key_buf_size_, this->le_);
  buf.EnsureRemainder(9);
  EncodePrefix(buf, prefix);
  unsigned long i = 0;
  for (const auto& bs : *schemas_) {
    if (i == keys.size()) {
      break;
    }
    if (bs) {
      BaseSchema::Type type = bs->GetType();
      switch (type) {
        case BaseSchema::kBool: {
          auto bos =
              std::dynamic_pointer_cast<DingoSchema<std::optional<bool>>>(bs);
          if (bos->IsKey()) {
            bos->EncodeKeyPrefix(&buf,
                                 std::optional<bool>(StringToBool(keys[i])));
          }
          break;
        }
        case BaseSchema::kInteger: {
          auto is =
              std::dynamic_pointer_cast<DingoSchema<std::optional<int32_t>>>(
                  bs);
          if (is->IsKey()) {
            is->EncodeKeyPrefix(&buf,
                                std::optional<int32_t>(StringToInt32(keys[i])));
          }
          break;
        }
        case BaseSchema::kFloat: {
          auto fs =
              std::dynamic_pointer_cast<DingoSchema<std::optional<float>>>(bs);
          if (fs->IsKey()) {
            fs->EncodeKeyPrefix(&buf,
                                std::optional<float>(StringToFloat(keys[i])));
          }
          break;
        }
        case BaseSchema::kLong: {
          auto ls =
              std::dynamic_pointer_cast<DingoSchema<std::optional<int64_t>>>(
                  bs);
          if (ls->IsKey()) {
            ls->EncodeKeyPrefix(&buf,
                                std::optional<int64_t>(StringToInt64(keys[i])));
          }
          break;
        }
        case BaseSchema::kDouble: {
          auto ds =
              std::dynamic_pointer_cast<DingoSchema<std::optional<double>>>(bs);
          if (ds->IsKey()) {
            ds->EncodeKeyPrefix(&buf,
                                std::optional<double>(StringToDouble(keys[i])));
          }
          break;
        }
        case BaseSchema::kString: {
          auto ss = std::dynamic_pointer_cast<
              DingoSchema<std::optional<std::shared_ptr<std::string>>>>(bs);
          if (ss->IsKey()) {
            ss->EncodeKeyPrefix(&buf,
                                std::optional<std::shared_ptr<std::string>>(
                                    std::make_shared<std::string>(keys[i])));
          }
          break;
        }
        default: {
          break;
        }
      }

      ++i;
    }
  }

  return buf.GetBytes(output);
}

int RecordEncoderV1::EncodeMaxKeyPrefix(char prefix,
                                        std::string& output) const {
  if (common_id_ == INT64_MAX) {
    return -1;
  }

  Buf buf(key_buf_size_, this->le_);
  buf.EnsureRemainder(9);
  buf.Write(prefix);
  buf.WriteLong(common_id_ + 1);
  return buf.GetBytes(output);
}

int RecordEncoderV1::EncodeMinKeyPrefix(char prefix,
                                        std::string& output) const {
  Buf buf(key_buf_size_, this->le_);
  buf.EnsureRemainder(9);
  buf.Write(prefix);
  buf.WriteLong(common_id_);

  return buf.GetBytes(output);
}

}  // namespace dingodb
