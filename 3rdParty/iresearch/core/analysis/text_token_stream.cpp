////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 by EMC Corporation, All Rights Reserved
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
/// Copyright holder is EMC Corporation
///
/// @author Andrey Abramov
/// @author Vasiliy Nabatchikov
////////////////////////////////////////////////////////////////////////////////

#include <cctype> // for std::isspace(...)
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <rapidjson/rapidjson/document.h> // for rapidjson::Document, rapidjson::Value
#include <rapidjson/rapidjson/writer.h> // for rapidjson::Writer
#include <rapidjson/rapidjson/stringbuffer.h> // for rapidjson::StringBuffer
#include <unicode/brkiter.h> // for icu::BreakIterator

#if defined(_MSC_VER)
  #pragma warning(disable: 4512)
#endif

  #include <unicode/normalizer2.h> // for icu::Normalizer2

#if defined(_MSC_VER)
  #pragma warning(default: 4512)
#endif

#include <unicode/translit.h> // for icu::Transliterator

#if defined(_MSC_VER)
  #pragma warning(disable: 4229)
#endif

  #include <unicode/uclean.h> // for u_cleanup

#if defined(_MSC_VER)
  #pragma warning(default: 4229)
#endif

#include "libstemmer.h"

#include "utils/hash_utils.hpp"
#include "utils/locale_utils.hpp"
#include "utils/log.hpp"
#include "utils/misc.hpp"
#include "utils/runtime_utils.hpp"
#include "utils/thread_utils.hpp"
#include "utils/utf8_path.hpp"

#include "text_token_stream.hpp"

NS_ROOT
NS_BEGIN(analysis)

// -----------------------------------------------------------------------------
// --SECTION--                                                     private types
// -----------------------------------------------------------------------------

struct text_token_stream::state_t {
  std::shared_ptr<icu::BreakIterator> break_iterator;
  icu::UnicodeString data;
  icu::Locale icu_locale;
  std::locale locale;
  std::shared_ptr<const icu::Normalizer2> normalizer;
  const options_t& options;
  const stopwords_t& stopwords;
  std::shared_ptr<sb_stemmer> stemmer;
  std::string tmp_buf; // used by processTerm(...)
  std::shared_ptr<icu::Transliterator> transliterator;
  state_t(const options_t& opts, const stopwords_t& stopw) :
    icu_locale("C"), options(opts), stopwords(stopw) {
    // NOTE: use of the default constructor for Locale() or
    //       use of Locale::createFromName(nullptr)
    //       causes a memory leak with Boost 1.58, as detected by valgrind
    icu_locale.setToBogus(); // set to uninitialized
  }
};

NS_END // analysis
NS_END // ROOT

NS_LOCAL

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------
struct cached_options_t: public irs::analysis::text_token_stream::options_t {
  std::string key_;
  irs::analysis::text_token_stream::stopwords_t stopwords_;

  cached_options_t(
      irs::analysis::text_token_stream::options_t &&options, 
      irs::analysis::text_token_stream::stopwords_t&& stopwords)
    : irs::analysis::text_token_stream::options_t(std::move(options)),
      stopwords_(std::move(stopwords)){
  }
};


static std::unordered_map<irs::hashed_string_ref, cached_options_t> cached_state_by_key;
static std::mutex mutex;
static auto icu_cleanup = irs::make_finally([]()->void{
  // this call will release/free all memory used by ICU (for all users)
  // very dangerous to call if ICU is still in use by some other code
  //u_cleanup();
});

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------


////////////////////////////////////////////////////////////////////////////////
/// @brief retrieves a set of ignored words from FS at the specified custom path
////////////////////////////////////////////////////////////////////////////////
bool get_stopwords(
    irs::analysis::text_token_stream::stopwords_t& buf,
    const std::locale& locale,
    const irs::string_ref& path = irs::string_ref::NIL
) {
  auto language = irs::locale_utils::language(locale);
  irs::utf8_path stopword_path;
  auto* custom_stopword_path =
    !path.null()
    ? path.c_str()
    : irs::getenv(irs::analysis::text_token_stream::STOPWORD_PATH_ENV_VARIABLE);

  if (custom_stopword_path) {
    bool absolute;

    stopword_path = irs::utf8_path(custom_stopword_path);

    if (!stopword_path.absolute(absolute)) {
      IR_FRMT_ERROR("Failed to determine absoluteness of path: %s",
                    stopword_path.utf8().c_str());

      return false;
    }

    if (!absolute) {
      stopword_path = irs::utf8_path(true) /= custom_stopword_path;
    }
  }
  else {
    // use CWD if the environment variable STOPWORD_PATH_ENV_VARIABLE is undefined
    stopword_path = irs::utf8_path(true);
  }

  try {
    bool result;

    if (!stopword_path.exists_directory(result) || !result
        || !(stopword_path /= language).exists_directory(result) || !result) {
      IR_FRMT_ERROR("Failed to load stopwords from path: %s", stopword_path.utf8().c_str());

      return false;
    }

    irs::analysis::text_token_stream::stopwords_t stopwords;
    auto visitor = [&stopwords, &stopword_path](
        const irs::utf8_path::native_char_t* name
    )->bool {
      auto path = stopword_path;
      bool result;

      path /= name;

      if (!path.exists_file(result)) {
        IR_FRMT_ERROR("Failed to identify stopword path: %s", path.utf8().c_str());

        return false;
      }

      if (!result) {
        return true; // skip non-files
      }

      std::ifstream in(path.native());

      if (!in) {
        IR_FRMT_ERROR("Failed to load stopwords from path: %s", path.utf8().c_str());

        return false;
      }

      for (std::string line; std::getline(in, line);) {
        size_t i = 0;

        // find first whitespace
        for (size_t length = line.size(); i < length && !std::isspace(line[i]); ++i);

        // skip lines starting with whitespace
        if (i > 0) {
          stopwords.insert(line.substr(0, i));
        }
      }

      return true;
    };

    if (!stopword_path.visit_directory(visitor, false)) {
      return false;
    }

    buf.insert(stopwords.begin(), stopwords.end());

    return true;
  } catch (...) {
    IR_FRMT_ERROR("Caught error while loading stopwords from path: %s", stopword_path.utf8().c_str());
    IR_LOG_EXCEPTION();
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief builds a set of stopwords for options
/// load rules:
/// 'explicit_stopwords' + 'stopwordsPath' = load from both
/// 'explicit_stopwords' only - load from 'explicit_stopwords'
/// 'stopwordsPath' only - load from 'stopwordsPath'
///  none (empty explicit_Stopwords  and flg explicit_stopwords_set not set) - load from default location
////////////////////////////////////////////////////////////////////////////////
bool build_stopwords(const irs::analysis::text_token_stream::options_t& options,
  irs::analysis::text_token_stream::stopwords_t& buf) {
  if (!options.explicit_stopwords.empty()) {
    // explicit stopwords always go
    buf.insert(options.explicit_stopwords.begin(), options.explicit_stopwords.end()); 
  }

  if (options.stopwordsPath.empty() || options.stopwordsPath[0] != 0) {
    // we have a custom path. let`s try loading
    auto locale = irs::locale_utils::locale(options.locale);
    // if we have stopwordsPath - do not  try default location. Nothing to do there anymore
    return get_stopwords(buf, locale, options.stopwordsPath); 
  }
  else if (!options.explicit_stopwords_set && options.explicit_stopwords.empty()) {
    //  no stopwordsPath, explicit_stopwords empty and not marked as valid - load from defaults
    auto locale = irs::locale_utils::locale(options.locale);
    return get_stopwords(buf, locale);
  }

  return true;
}


////////////////////////////////////////////////////////////////////////////////
/// @brief create an analyzer based on the supplied cache_key and options
////////////////////////////////////////////////////////////////////////////////
irs::analysis::analyzer::ptr construct(
  const irs::string_ref& cache_key,
  irs::analysis::text_token_stream::options_t&& options,
  irs::analysis::text_token_stream::stopwords_t&& stopwords
) {
  static auto generator = [](
      const irs::hashed_string_ref& key,
      cached_options_t& value
  ) NOEXCEPT {
    if (key.null()) {
      return key;
    }

    value.key_ = key;

    // reuse hash but point ref at value
    return irs::hashed_string_ref(key.hash(), value.key_);
  };
  cached_options_t* options_ptr;

  {
    SCOPED_LOCK(mutex);

    options_ptr = &(irs::map_utils::try_emplace_update_key(
      cached_state_by_key,
      generator,
      irs::make_hashed_ref(cache_key, std::hash<irs::string_ref>()),
      std::move(options),
      std::move(stopwords)
    ).first->second);
  }

  return irs::memory::make_unique<irs::analysis::text_token_stream>(
    *options_ptr,
    options_ptr->stopwords_
  );
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an analyzer based on the supplied cache_key
////////////////////////////////////////////////////////////////////////////////
irs::analysis::analyzer::ptr construct(
  const irs::string_ref& cache_key
) {
  {
    SCOPED_LOCK(mutex);
    auto itr = cached_state_by_key.find(
      irs::make_hashed_ref(cache_key, std::hash<irs::string_ref>())
    );

    if (itr != cached_state_by_key.end()) {
      return irs::memory::make_unique<irs::analysis::text_token_stream>(
        itr->second,
        itr->second.stopwords_
      );
    }
  }

  try {
    irs::analysis::text_token_stream::options_t options;
    irs::analysis::text_token_stream::stopwords_t stopwords;
    options.locale = cache_key; // interpret the cache_key as a locale name
    
    if (!build_stopwords(options, stopwords)) {
      IR_FRMT_WARN("Failed to retrieve 'stopwords' while constructing text_token_stream with cache key: %s", 
                   cache_key.c_str());

      return nullptr;
    }

    return construct(cache_key, std::move(options), std::move(stopwords));
  } catch (...) {
    IR_FRMT_ERROR("Caught error while constructing text_token_stream cache key: %s", 
                  cache_key.c_str());
    IR_LOG_EXCEPTION();
  }

  return nullptr;
}

bool process_term(
  irs::analysis::text_token_stream::bytes_term& term,
  irs::analysis::text_token_stream::state_t& state,
  icu::UnicodeString const& data
) {
  // ...........................................................................
  // normalize unicode
  // ...........................................................................
  icu::UnicodeString word;
  auto err = UErrorCode::U_ZERO_ERROR; // a value that passes the U_SUCCESS() test

  state.normalizer->normalize(data, word, err);

  if (!U_SUCCESS(err)) {
    word = data; // use non-normalized value if normalization failure
  }

  // ...........................................................................
  // case-convert unicode
  // ...........................................................................
  switch (state.options.case_convert) {
   case irs::analysis::text_token_stream::options_t::case_convert_t::LOWER:
    word.toLower(state.icu_locale); // inplace case-conversion
    break;
   case irs::analysis::text_token_stream::options_t::case_convert_t::UPPER:
    word.toUpper(state.icu_locale); // inplace case-conversion
    break;
   default:
    {} // NOOP
  };

  // ...........................................................................
  // collate value, e.g. remove accents
  // ...........................................................................
  if (state.transliterator) {
    state.transliterator->transliterate(word); // inplace translitiration
  }

  std::string& word_utf8 = state.tmp_buf;

  word_utf8.clear();
  word.toUTF8String(word_utf8);

  // ...........................................................................
  // skip ignored tokens
  // ...........................................................................
  if (state.stopwords.find(word_utf8) != state.stopwords.end()) {
    return false;
  }

  // ...........................................................................
  // find the token stem
  // ...........................................................................
  if (state.stemmer) {
    static_assert(sizeof(sb_symbol) == sizeof(char), "sizeof(sb_symbol) != sizeof(char)");
    const sb_symbol* value = reinterpret_cast<sb_symbol const*>(word_utf8.c_str());

    value = sb_stemmer_stem(state.stemmer.get(), value, (int)word_utf8.size());

    if (value) {
      static_assert(sizeof(irs::byte_type) == sizeof(sb_symbol), "sizeof(irs::byte_type) != sizeof(sb_symbol)");
      term.value(irs::bytes_ref(reinterpret_cast<const irs::byte_type*>(value), sb_stemmer_length(state.stemmer.get())));

      return true;
    }
  }

  // ...........................................................................
  // use the value of the unstemmed token
  // ...........................................................................
  static_assert(sizeof(irs::byte_type) == sizeof(char), "sizeof(irs::byte_type) != sizeof(char)");
  term.value(irs::bstring(irs::ref_cast<irs::byte_type>(word_utf8).c_str(), word_utf8.size()));

  return true;
}

const irs::string_ref localeParamName           = "locale";
const irs::string_ref caseConvertParamName      = "caseConvert";
const irs::string_ref stopwordsParamName        = "stopwords";
const irs::string_ref stopwordsPathParamName    = "stopwordsPath";
const irs::string_ref noAccentParamName         = "noAccent";
const irs::string_ref noStemParamName           = "noStem";

const std::unordered_map<
    std::string, 
    irs::analysis::text_token_stream::options_t::case_convert_t> case_convert_map = {
  { "lower", irs::analysis::text_token_stream::options_t::case_convert_t::LOWER },
  { "none", irs::analysis::text_token_stream::options_t::case_convert_t::NONE },
  { "upper", irs::analysis::text_token_stream::options_t::case_convert_t::UPPER },
};

///////////////////////////////////////////////////////////////////////////////
/// @brief builds analyzer config from internal options in json format
/// @param options reference to analyzer options storage
/// @param definition string for storing json document with config 
///////////////////////////////////////////////////////////////////////////////
bool make_json_config(
    const irs::analysis::text_token_stream::options_t& options,
    std::string& definition) {

  rapidjson::Document json;
  json.SetObject();

  rapidjson::Document::AllocatorType& allocator = json.GetAllocator();

  // locale
  json.AddMember(rapidjson::Value::StringRefType(
                     localeParamName.c_str(), 
                     static_cast<rapidjson::SizeType>(localeParamName.size())), 
                 rapidjson::Value(
                     options.locale.c_str(), 
                     static_cast<rapidjson::SizeType>(options.locale.length())), 
                 allocator);

  // case convert
  {
    auto case_value = std::find_if(case_convert_map.begin(), case_convert_map.end(),
      [&options](const decltype(case_convert_map)::value_type& v) {
        return v.second == options.case_convert; 
      });

    if (case_value != case_convert_map.end()) {
      json.AddMember(rapidjson::Value::StringRefType(
                         caseConvertParamName.c_str(), 
                         static_cast<rapidjson::SizeType>(caseConvertParamName.size())),
                     rapidjson::Value(
                         case_value->first.c_str(), 
                         static_cast<rapidjson::SizeType>(case_value->first.length())), 
                     allocator);
    } else {
      IR_FRMT_ERROR(
        "Invalid case_convert value in text analyzer options: %d",
        static_cast<int>(options.case_convert));
      return false;
    }
  }
 
  // stopwords
  if(!options.explicit_stopwords.empty() || options.explicit_stopwords_set) { 
    // explicit_stopwords_set  marks that even empty stopwords list is valid
    rapidjson::Value stopwordsArray(rapidjson::kArrayType);
    for (const auto& stopword : options.explicit_stopwords) {
      stopwordsArray.PushBack(
          rapidjson::StringRef(stopword.c_str(), 
          static_cast<rapidjson::SizeType>(stopword.size())), allocator);
    }
    json.AddMember(rapidjson::Value::StringRefType(
                       stopwordsParamName.c_str(), 
                       static_cast<rapidjson::SizeType>(stopwordsParamName.size())),
                   stopwordsArray.Move(), 
                   allocator);
  }

  // noAccent
  json.AddMember(rapidjson::Value::StringRefType(
                     noAccentParamName.c_str(), 
                     static_cast<rapidjson::SizeType>(noAccentParamName.size())),
                 rapidjson::Value(options.no_accent),
                 allocator);

  //noStem
  json.AddMember(rapidjson::Value::StringRefType(
                     noStemParamName.c_str(), 
                     static_cast<rapidjson::SizeType>(noStemParamName.size())),
                 rapidjson::Value(options.no_stem), 
                 allocator);
  
  //stopwords path
  if (options.stopwordsPath.empty() || options.stopwordsPath[0] != 0 ) { 
    // if stopwordsPath is set  - output it (empty string is also valid value =  use of CWD)
    json.AddMember(rapidjson::Value::StringRefType(
                       stopwordsPathParamName.c_str(), 
                       static_cast<rapidjson::SizeType>(stopwordsPathParamName.size())),
                   rapidjson::Value(
                       options.stopwordsPath.c_str(), 
                       static_cast<rapidjson::SizeType>(options.stopwordsPath.length())), 
                   allocator);
  }

  //output json to string
  rapidjson::StringBuffer buffer;
  rapidjson::Writer< rapidjson::StringBuffer> writer(buffer);
  json.Accept(writer);
  definition = buffer.GetString();
  return true;
}



////////////////////////////////////////////////////////////////////////////////
/// @brief args is a jSON encoded object with the following attributes:
///        "locale"(string): locale of the analyzer <required>
///        "caseConvert"(string enum): modify token case using "locale"
///        "noAccent"(bool): remove accents
///        "noStem"(bool): disable stemming
///        "stopwords([string...]): set of words to ignore 
///        "stopwordsPath"(string): custom path, where to load stopwords
///  if none of stopwords and stopwordsPath specified, stopwords are loaded from default location
////////////////////////////////////////////////////////////////////////////////
irs::analysis::analyzer::ptr make_json(const irs::string_ref& args) {
  rapidjson::Document json;
  if (json.Parse(args.c_str(), args.size()).HasParseError()) {
    IR_FRMT_ERROR(
      "Invalid jSON arguments passed while constructing text_token_stream, arguments: %s",
      args.c_str()
    );

    return nullptr;
  }

  if (json.IsString()) {
    return construct(args);
  }

  if (!json.IsObject()
      || !json.HasMember(localeParamName.c_str())
      || !json[localeParamName.c_str()].IsString()
     ) {

    IR_FRMT_WARN("Missing '%s' while constructing text_token_stream from jSON arguments: %s",
                 localeParamName.c_str(), args.c_str());

    return nullptr;
  }

  try {
    typedef irs::analysis::text_token_stream::options_t options_t;
    options_t options;

    options.locale = json[localeParamName.c_str()].GetString(); // required

    if (json.HasMember(caseConvertParamName.c_str())) {
      auto& case_convert = json[caseConvertParamName.c_str()]; // optional string enum

      if (!case_convert.IsString()) {
        IR_FRMT_WARN("Non-string value in '%s' while constructing text_token_stream from jSON arguments: %s",
                     caseConvertParamName.c_str(), args.c_str());

        return nullptr;
      }

      auto itr = case_convert_map.find(case_convert.GetString());

      if (itr == case_convert_map.end()) {
        IR_FRMT_WARN("Invalid value in '%s' while constructing text_token_stream from jSON arguments: %s",
                     caseConvertParamName.c_str(),  args.c_str());

        return nullptr;
      }

      options.case_convert = itr->second;
    }

    
    if (json.HasMember(stopwordsParamName.c_str())) {
      auto& stop_words = json[stopwordsParamName.c_str()]; // optional string array

      if (!stop_words.IsArray()) {
        IR_FRMT_WARN("Invalid value in '%s' while constructing text_token_stream from jSON arguments: %s", 
                     stopwordsParamName.c_str(), args.c_str());

        return nullptr;
      }
      options.explicit_stopwords_set = true; // mark  - we have explicit list (even if it is empty)
      for (auto itr = stop_words.Begin(), end = stop_words.End();
           itr != end;
           ++itr) {
        if (!itr->IsString()) {
          IR_FRMT_WARN("Non-string value in '%s' while constructing text_token_stream from jSON arguments: %s",
                       stopwordsParamName.c_str(), args.c_str());

          return nullptr;
        }
        options.explicit_stopwords.emplace(itr->GetString());
      }
      if (json.HasMember(stopwordsPathParamName.c_str())) {
        auto& ignored_words_path = json[stopwordsPathParamName.c_str()]; // optional string

        if (!ignored_words_path.IsString()) {
          IR_FRMT_WARN("Non-string value in '%s' while constructing text_token_stream from jSON arguments: %s", 
                       stopwordsPathParamName.c_str(), args.c_str());

          return nullptr;
        }
        options.stopwordsPath = ignored_words_path.GetString();
      }
    } else if (json.HasMember(stopwordsPathParamName.c_str())) {
      auto& ignored_words_path = json[stopwordsPathParamName.c_str()]; // optional string

      if (!ignored_words_path.IsString()) {
        IR_FRMT_WARN("Non-string value in '%s' while constructing text_token_stream from jSON arguments: %s", 
                     stopwordsPathParamName.c_str(), args.c_str());

        return nullptr;
      }
      options.stopwordsPath = ignored_words_path.GetString();
    } 

    if (json.HasMember(noAccentParamName.c_str())) {
      auto& no_accent = json[noAccentParamName.c_str()]; // optional bool

      if (!no_accent.IsBool()) {
        IR_FRMT_WARN("Non-boolean value in '%s' while constructing text_token_stream from jSON arguments: %s", 
                     noAccentParamName.c_str(), args.c_str());

        return nullptr;
      }

      options.no_accent = no_accent.GetBool();
    }

    if (json.HasMember(noStemParamName.c_str())) {
      auto& no_stem = json[noStemParamName.c_str()]; // optional bool

      if (!no_stem.IsBool()) {
        IR_FRMT_WARN("Non-boolean value in '%s' while constructing text_token_stream from jSON arguments: %s", 
                     noStemParamName.c_str(), args.c_str());

        return nullptr;
      }

      options.no_stem = no_stem.GetBool();
    }

    irs::analysis::text_token_stream::stopwords_t stopwords;
    if (!build_stopwords(options, stopwords)) {
      IR_FRMT_WARN(
          "Failed to retrieve 'stopwords' from path while constructing text_token_stream from jSON arguments: %s", 
          args.c_str());

      return nullptr;
    }

    return construct(args, std::move(options), std::move(stopwords));
  } catch (...) {
    IR_FRMT_ERROR("Caught error while constructing text_token_stream from jSON arguments: %s", 
                  args.c_str());
    IR_LOG_EXCEPTION();
  }

  return nullptr;
}



////////////////////////////////////////////////////////////////////////////////
/// @brief args is a locale name
////////////////////////////////////////////////////////////////////////////////
irs::analysis::analyzer::ptr make_text(const irs::string_ref& args) {
  return construct(args);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief build config string in 'text' format
////////////////////////////////////////////////////////////////////////////////
bool make_text_config(const irs::analysis::text_token_stream::options_t& options, std::string& definition) {
  definition = options.locale; // only locale available with text config (see make_text)
  return true;
}

REGISTER_ANALYZER_JSON(irs::analysis::text_token_stream, make_json);
REGISTER_ANALYZER_TEXT(irs::analysis::text_token_stream, make_text);

NS_END

NS_ROOT
NS_BEGIN(analysis)



// -----------------------------------------------------------------------------
// --SECTION--                                                  static variables
// -----------------------------------------------------------------------------

char const* text_token_stream::STOPWORD_PATH_ENV_VARIABLE = "IRESEARCH_TEXT_STOPWORD_PATH";

// -----------------------------------------------------------------------------
// --SECTION--                                                  static functions
// -----------------------------------------------------------------------------

DEFINE_ANALYZER_TYPE_NAMED(text_token_stream, "text")

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

text_token_stream::text_token_stream(const options_t& options, const stopwords_t& stopwords)
  : analyzer(text_token_stream::type()),
    attrs_(3), // offset + bytes_term + increment
    state_(memory::make_unique<state_t>(options, stopwords)) {
  attrs_.emplace(offs_);
  attrs_.emplace(term_);
  attrs_.emplace(inc_);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

/*static*/ void text_token_stream::init() {
  REGISTER_ANALYZER_JSON(text_token_stream, make_json); // match registration above
  REGISTER_ANALYZER_TEXT(text_token_stream, make_text); // match registration above
}

/*static*/ analyzer::ptr text_token_stream::make(
    const irs::string_ref& locale
) {
  return make_text(locale);
}

bool text_token_stream::reset(const string_ref& data) {
  if (state_->icu_locale.isBogus()) {
    state_->locale = irs::locale_utils::locale(
      state_->options.locale, irs::string_ref::NIL, true // true == convert to unicode, required for ICU and Snowball
    );
    state_->icu_locale = icu::Locale(
      std::string(irs::locale_utils::language(state_->locale)).c_str(),
      std::string(irs::locale_utils::country(state_->locale)).c_str()
    );

    if (state_->icu_locale.isBogus()) {
      return false;
    }
  }

  auto err = UErrorCode::U_ZERO_ERROR; // a value that passes the U_SUCCESS() test

  if (!state_->normalizer) {
    // reusable object owned by ICU
    state_->normalizer.reset(
      icu::Normalizer2::getNFCInstance(err), [](const icu::Normalizer2*)->void{}
    );

    if (!U_SUCCESS(err) || !state_->normalizer) {
      state_->normalizer.reset();

      return false;
    }
  }

  if (state_->options.no_accent && !state_->transliterator) {
    // transliteration rule taken verbatim from: http://userguide.icu-project.org/transforms/general
    icu::UnicodeString collationRule("NFD; [:Nonspacing Mark:] Remove; NFC"); // do not allocate statically since it causes memory leaks in ICU

    // reusable object owned by *this
    state_->transliterator.reset(icu::Transliterator::createInstance(
      collationRule, UTransDirection::UTRANS_FORWARD, err
    ));

    if (!U_SUCCESS(err) || !state_->transliterator) {
      state_->transliterator.reset();

      return false;
    }
  }

  if (!state_->break_iterator) {
    // reusable object owned by *this
    state_->break_iterator.reset(icu::BreakIterator::createWordInstance(
      state_->icu_locale, err
    ));

    if (!U_SUCCESS(err) || !state_->break_iterator) {
      state_->break_iterator.reset();

      return false;
    }
  }

  // optional since not available for all locales
  if (!state_->options.no_stem && !state_->stemmer) {
    // reusable object owned by *this
    state_->stemmer.reset(
      sb_stemmer_new(
        std::string(irs::locale_utils::language(state_->locale)).c_str(),
        nullptr // defaults to utf-8
      ),
      [](sb_stemmer* ptr)->void{ sb_stemmer_delete(ptr); }
    );
  }

  // ...........................................................................
  // convert encoding to UTF8 for use with ICU
  // ...........................................................................
  std::string data_utf8;

  // valid conversion since 'locale_' was created with internal unicode encoding
  if (!irs::locale_utils::append_internal(data_utf8, data, state_->locale)) {
    return false; // UTF8 conversion failure
  }

  if (data_utf8.size() > irs::integer_traits<int32_t>::const_max) {
    return false; // ICU UnicodeString signatures can handle at most INT32_MAX
  }

  state_->data = icu::UnicodeString::fromUTF8(
    icu::StringPiece(data_utf8.c_str(), (int32_t)(data_utf8.size()))
  );

  // ...........................................................................
  // tokenise the unicode data
  // ...........................................................................
  state_->break_iterator->setText(state_->data);

  return true;
}

bool text_token_stream::next() {
  // ...........................................................................
  // find boundaries of the next word
  // ...........................................................................
  for (auto start = state_->break_iterator->current(),
       end = state_->break_iterator->next();
       icu::BreakIterator::DONE != end;
       start = end, end = state_->break_iterator->next()) {
    // ...........................................................................
    // skip whitespace and unsuccessful terms
    // ...........................................................................
    if (UWordBreak::UBRK_WORD_NONE == state_->break_iterator->getRuleStatus()
        || !process_term(term_, *state_, state_->data.tempSubString(start, end - start))) {
      continue;
    }

    offs_.start = start;
    offs_.end = end;
    return true;
  }

  return false;
}

bool text_token_stream::to_string(
    const ::irs::text_format::type_id& format,
    std::string& definition) const {
  if (::irs::text_format::json == format) {
    return make_json_config(state_->options, definition);
  } else if (::irs::text_format::text == format) {
    return make_text_config(state_->options, definition);
  }

  return false;
}

NS_END // analysis
NS_END // ROOT

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------