////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2019 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Andrey Abramov
/// @author Vasiliy Nabatchikov
////////////////////////////////////////////////////////////////////////////////

#include "libstemmer.h"
#include "rapidjson/rapidjson/document.h" // for rapidjson::Document
#include <rapidjson/rapidjson/writer.h> // for rapidjson::Writer
#include <rapidjson/rapidjson/stringbuffer.h> // for rapidjson::StringBuffer
#include "utils/locale_utils.hpp"

#include "text_token_stemming_stream.hpp"

NS_LOCAL

const irs::string_ref localeParamName = "locale";

////////////////////////////////////////////////////////////////////////////////
/// @brief args is a jSON encoded object with the following attributes:
///        "locale"(string): the locale to use for stemming <required>
////////////////////////////////////////////////////////////////////////////////
irs::analysis::analyzer::ptr make_json(const irs::string_ref& args) {
  rapidjson::Document json;

  if (json.Parse(args.c_str(), args.size()).HasParseError()) {
    IR_FRMT_ERROR(
      "Invalid jSON arguments passed while constructing text_token_stemming_stream, arguments: %s",
      args.c_str()
    );

    return nullptr;
  }

  try {
    switch (json.GetType()) {
     case rapidjson::kStringType:
      return irs::memory::make_shared<irs::analysis::text_token_stemming_stream>(
        json.GetString() // required
      );
     case rapidjson::kObjectType:
      if (json.HasMember(localeParamName.c_str()) && json[localeParamName.c_str()].IsString()) {
        return irs::memory::make_shared<irs::analysis::text_token_stemming_stream>(
          json[localeParamName.c_str()].GetString() // required
        );
      }
     default: // fall through
      IR_FRMT_ERROR(
        "Missing '%s' while constructing text_token_stemming_stream from jSON arguments: %s",
        localeParamName.c_str(),
        args.c_str()
      );
    }
  } catch (...) {
    IR_FRMT_ERROR(
      "Caught error while constructing text_token_stemming_stream from jSON arguments: %s",
      args.c_str()
    );
    IR_LOG_EXCEPTION();
  }

  return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief builds analyzer config from internal options in json format
/// @param locale reference to analyzer`s locale
/// @param definition string for storing json document with config 
///////////////////////////////////////////////////////////////////////////////
bool make_json_config( const std::string& locale,  std::string& definition) {
  rapidjson::Document json;
  json.SetObject();

  rapidjson::Document::AllocatorType& allocator = json.GetAllocator();

  // locale
  json.AddMember(rapidjson::Value::StringRefType(localeParamName.c_str(), 
                     static_cast<rapidjson::SizeType>(localeParamName.size())),
                 rapidjson::Value(locale.c_str(), 
                     static_cast<rapidjson::SizeType>(locale.length())), 
                 allocator);

  //output json to string
  rapidjson::StringBuffer buffer;
  rapidjson::Writer< rapidjson::StringBuffer> writer(buffer);
  json.Accept(writer);
  definition = buffer.GetString();
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief args is a language to use for stemming
////////////////////////////////////////////////////////////////////////////////
irs::analysis::analyzer::ptr make_text(const irs::string_ref& args) {
  try {
    return irs::memory::make_shared<irs::analysis::text_token_stemming_stream>(
      args
    );
  } catch (...) {
    IR_FRMT_ERROR(
      "Caught error while constructing text_token_stemming_stream TEXT arguments: %s",
      args.c_str()
    );
    IR_LOG_EXCEPTION();
  }

  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief build config string in 'text' format
////////////////////////////////////////////////////////////////////////////////
bool make_text_config(const std::string& locale, std::string& definition) {
  definition = locale; 
  return true;
}

REGISTER_ANALYZER_JSON(irs::analysis::text_token_stemming_stream, make_json);
REGISTER_ANALYZER_TEXT(irs::analysis::text_token_stemming_stream, make_text);

NS_END

NS_ROOT
NS_BEGIN(analysis)

DEFINE_ANALYZER_TYPE_NAMED(text_token_stemming_stream, "stem")

text_token_stemming_stream::text_token_stemming_stream(
    const irs::string_ref& locale
): analyzer(text_token_stemming_stream::type()),
   attrs_(4), // increment + offset + payload + term
   locale_(irs::locale_utils::locale(locale, irs::string_ref::NIL, true)), // true == convert to unicode
   term_eof_(true) {
  attrs_.emplace(inc_);
  attrs_.emplace(offset_);
  attrs_.emplace(payload_);
  attrs_.emplace(term_);
}

/*static*/ void text_token_stemming_stream::init() {
  REGISTER_ANALYZER_JSON(text_token_stemming_stream, make_json); // match registration above
  REGISTER_ANALYZER_TEXT(text_token_stemming_stream, make_text); // match registration above
}

/*static*/ analyzer::ptr text_token_stemming_stream::make(
    const string_ref& locale
) {
  return make_text(locale);
}

bool text_token_stemming_stream::next() {
  if (term_eof_) {
    return false;
  }

  term_eof_ = true;

  return true;
}

bool text_token_stemming_stream::reset(const irs::string_ref& data) {
  if (!stemmer_) {
    stemmer_.reset(
      sb_stemmer_new(
        std::string(irs::locale_utils::language(locale_)).c_str(), nullptr // defaults to utf-8
      ),
      [](sb_stemmer* ptr)->void{ sb_stemmer_delete(ptr); }
    );
  }

  term_.value(irs::bytes_ref::NIL); // reset
  term_buf_.clear();
  term_eof_ = true;

  // convert to UTF8 for use with 'stemmer_'
  // valid conversion since 'locale_' was created with internal unicode encoding
  if (!irs::locale_utils::append_internal(term_buf_, data, locale_)) {
    IR_FRMT_ERROR(
      "Failed to parse UTF8 value from token: %s",
      data.c_str()
    );

    return false;
  }

  offset_.start = 0;
  offset_.end = data.size();
  payload_.value = ref_cast<uint8_t>(data);
  term_eof_ = false;

  // ...........................................................................
  // find the token stem
  // ...........................................................................
  if (stemmer_) {
    if (term_buf_.size() > irs::integer_traits<int>::const_max) {
      IR_FRMT_WARN(
        "Token size greater than the supported maximum size '%d', truncating token: %s",
        irs::integer_traits<int>::const_max, data.c_str()
      );
      term_buf_.resize(irs::integer_traits<int>::const_max);
    }

    static_assert(sizeof(sb_symbol) == sizeof(char), "sizeof(sb_symbol) != sizeof(char)");
    const auto* value = reinterpret_cast<sb_symbol const*>(term_buf_.c_str());

    value = sb_stemmer_stem(stemmer_.get(), value, (int)term_buf_.size());

    if (value) {
      static_assert(sizeof(irs::byte_type) == sizeof(sb_symbol), "sizeof(irs::byte_type) != sizeof(sb_symbol)");
      term_.value(irs::bytes_ref(
        reinterpret_cast<const irs::byte_type*>(value),
        sb_stemmer_length(stemmer_.get())
      ));

      return true;
    }
  }

  // ...........................................................................
  // use the value of the unstemmed token
  // ...........................................................................
  static_assert(sizeof(irs::byte_type) == sizeof(char), "sizeof(irs::byte_type) != sizeof(char)");
  term_.value(irs::ref_cast<irs::byte_type>(term_buf_));

  return true;
}

bool text_token_stemming_stream::to_string( 
    const ::irs::text_format::type_id& format,
    std::string& definition) const {
  if (::irs::text_format::json == format) {
    return make_json_config(locale_utils::name(locale_), definition);
  } else if (::irs::text_format::text == format) {
    return make_text_config(locale_utils::name(locale_), definition);
  }

  return false;
}

NS_END // analysis
NS_END // ROOT

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------