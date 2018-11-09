#ifndef HEV_IRIS_WSI_INPUT_H_
#define HEV_IRIS_WSI_INPUT_H_
/*! \file
 * \brief \ref iris::wsi user-input definitions.
 */

#include <bitset>
#include <cstdint>

namespace iris::wsi {

//! \brief Keyboard keys.
enum Keys {
  kUnknown = 0,

  kSpace = 32,
  kApostrophe = 39,
  kComma = 44,
  kMinus = 45,
  kPeriod = 46,
  kSlash = 47,
  k0 = 48,
  k1 = 49,
  k2 = 50,
  k3 = 51,
  k4 = 52,
  k5 = 53,
  k6 = 54,
  k7 = 55,
  k8 = 56,
  k9 = 57,
  kSemicolon = 59,
  kEqual = 61,
  kA = 65,
  kB = 66,
  kC = 67,
  kD = 68,
  kE = 69,
  kF = 70,
  kG = 71,
  kH = 72,
  kI = 73,
  kJ = 74,
  kK = 75,
  kL = 76,
  kM = 77,
  kN = 78,
  kO = 79,
  kP = 80,
  kQ = 81,
  kR = 82,
  kS = 83,
  kT = 84,
  kU = 85,
  kV = 86,
  kW = 87,
  kX = 88,
  kY = 89,
  kZ = 90,
  kLeftBracket = 91,
  kBackslash = 92,
  kRightBracket = 93,
  kGraveAccent = 96,

  kEscape = 156,
  kEnter = 157,
  kTab = 158,
  kBackspace = 159,
  kInsert = 160,
  kDelete = 161,
  kRight = 162,
  kLeft = 163,
  kDown = 164,
  kUp = 165,
  kPageUp = 166,
  kPageDown = 167,
  kHome = 168,
  kEnd = 169,

  kCapsLock = 180,
  kScrollLock = 181,
  kNumLock = 182,
  kPrintScreen = 183,
  kPause = 184,

  kF1 = 190,
  kF2 = 191,
  kF3 = 192,
  kF4 = 193,
  kF5 = 194,
  kF6 = 195,
  kF7 = 196,
  kF8 = 197,
  kF9 = 198,
  kF10 = 199,
  kF11 = 200,
  kF12 = 201,
  kF13 = 202,
  kF14 = 203,
  kF15 = 204,
  kF16 = 205,
  kF17 = 206,
  kF18 = 207,
  kF19 = 208,
  kF20 = 209,
  kF21 = 210,
  kF22 = 211,
  kF23 = 212,
  kF24 = 213,

  kKeypad0 = 220,
  kKeypad1 = 221,
  kKeypad2 = 222,
  kKeypad3 = 223,
  kKeypad4 = 224,
  kKeypad5 = 225,
  kKeypad6 = 226,
  kKeypad7 = 227,
  kKeypad8 = 228,
  kKeypad9 = 229,
  kKeypadDecimal = 230,
  kKeypadDivide = 231,
  kKeypadMultiply = 232,
  kKeypadSubtract = 233,
  kKeypadAdd = 234,
  kKeypadEnter = 235,
  kKeypadEqual = 236,

  kLeftShift = 240,
  kLeftControl = 241,
  kLeftAlt = 242,
  kLeftSuper = 243,
  kRightShift = 244,
  kRightControl = 245,
  kRightAlt = 246,
  kRightSuper = 247,
  kMenu = 248,

  kMaxKeys = 255
}; // enum class Keys

//! \brief Tracks the current state of a keyboard.
class Keyset {
public:
  //! \brief The maximum number of keys that are tracked.
  static constexpr std::size_t kMaxKeys = Keys::kMaxKeys;

private:
  using bitset = std::bitset<kMaxKeys>;

public:
  /*! \brief Indicates if the \a key is currently pressed.
   * \return true if \a key is pressed, false if not.
   */
  constexpr bool operator[](Keys key) const {
    return keys_[static_cast<std::size_t>(key)];
  }

  //! \brief Get a reference to the status of \a key.
  typename bitset::reference operator[](Keys key) {
    return keys_[static_cast<std::size_t>(key)];
  }

  //! \brief Set the pressed status of \a key to \a value.
  Keyset& set(Keys key, bool value = true) noexcept {
    keys_.set(static_cast<std::size_t>(key), value);
    return *this;
  }

  //! \brief Clear the pressed status of \a key.
  Keyset& reset(Keys key) noexcept {
    keys_.reset(static_cast<std::size_t>(key));
    return *this;
  }

private:
  bitset keys_;
}; // class Keyset

//! \brief Mouse buttons.
enum Buttons : std::uint8_t {
  kButtonLeft,
  kButtonRight,
  kButtonMiddle,
  kButton4,
  kButton5,

  kMaxButtons = 5
}; // enum class Buttons

//! \brief Tracks the current state of a mouse.
class Buttonset {
  //! \brief The maximum number of buttons that are tracked.
  static constexpr std::size_t kMaxButtons = Buttons::kMaxButtons;

private:
  using bitset = std::bitset<kMaxButtons>;

public:
  /*! \brief Indicates if the \a button is currently pressed.
   * \return true if \a button is pressed, false if not.
   */
  constexpr bool operator[](Buttons button) const {
    return buttons_[static_cast<std::size_t>(button)];
  }

  //! \brief Get a reference to the status of \a key.
  typename bitset::reference operator[](Buttons button) {
    return buttons_[static_cast<std::size_t>(button)];
  }

  //! \brief Set the pressed status of \a button to \a value.
  Buttonset& set(Buttons button, bool value = true) noexcept {
    buttons_.set(static_cast<std::size_t>(button), value);
    return *this;
  }

  //! \brief Clear the pressed status of \a button.
  Buttonset& reset(Buttons button) noexcept {
    buttons_.reset(static_cast<std::size_t>(button));
    return *this;
  }

private:
  bitset buttons_;
}; // class Buttonset

} // namespace iris::wsi

#endif // HEV_IRIS_WSI_INPUT_H_

