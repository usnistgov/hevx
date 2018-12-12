#ifndef HEV_IRIS_ERROR_H_
#define HEV_IRIS_ERROR_H_
/*! \file
 * \brief \ref iris::Error definition.
 */

#include <string>
#include <system_error>

namespace iris {

//! \brief IRIS errors.
enum class Error {
  kNone = 0,              //!< No error
  kInitializationFailed,  //!< Initialization failed for some reason.
  kAlreadyInitialized,    //!< The renderer has already been initialized.
  kNoPhysicalDevice,      //!< No physical device available.
  kFileLoadFailed,        //!< File load failed.
  kFileNotSupported,      //!< File is not supported.
  kFileParseFailed,       //!< Parsing a file failed.
  kControlMessageInvalid, //!< Control message invalid.
  kControlMessageFailed,  //!< Control message failed.
  kShaderCompileFailed,   //!< Shader compilation failed.
};

//! \brief Implements std::error_category for \ref Error
class ErrorCategory : public std::error_category {
public:
  virtual ~ErrorCategory() noexcept {}

  //! \brief Get the name of this category.
  virtual const char* name() const noexcept override {
    return "iris::Error";
  }

  //! \brief Convert an int representing an Error into a std::string.
  virtual std::string message(int code) const override {
    using namespace std::string_literals;
    switch (static_cast<Error>(code)) {
    case Error::kNone: return "none"s;
    case Error::kInitializationFailed: return "initialization failed"s;
    case Error::kAlreadyInitialized: return "already initialized"s;
    case Error::kNoPhysicalDevice: return "no physical device"s;
    case Error::kFileLoadFailed: return "file load failed"s;
    case Error::kFileNotSupported: return "file not supported"s;
    case Error::kFileParseFailed: return "file parse failed"s;
    case Error::kControlMessageInvalid: return "control message invalid"s;
    case Error::kControlMessageFailed: return "control message failed"s;
    case Error::kShaderCompileFailed: return "shader compile failed"s;
    }
    return "unknown"s;
  }
}; // class ErrorCategory

//! The global instance of the ErrorCategory.
inline ErrorCategory const gErrorCategory;

/*! \brief Get the global instance of the ErrorCategory.
 * \return \ref gErrorCategory
 */
inline std::error_category const& GetErrorCategory() {
  return gErrorCategory;
}

/*! \brief Make a std::error_code from a \ref Error.
 * \return std::error_code
 */
inline std::error_code make_error_code(Error e) noexcept {
  return std::error_code(static_cast<int>(e), GetErrorCategory());
}

} // namespace iris

namespace std {

template <>
struct is_error_code_enum<iris::Error> : public true_type {};

} // namespace std

#endif // HEV_IRIS_ERROR_H_

