#pragma once
#include <chrono>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#pragma warning(suppress : 5105)
#include <Windows.h>
#undef NOMINMAX
#undef WIN32_LEAN_AND_MEAN
#include <io.h>
#elif defined(__linux__)
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace pbar {
namespace detail {
#ifdef _WIN32
/// <summary>マルチバイト文字（UTF8 or SJIS）からUTF16に変換する</summary>
/// <param name="enc_src">変換元の文字コードを指定する。UTF8: CP_UTF8, SJIS:
/// CP_THREAD_ACP</param>
inline std::wstring to_utf16(UINT enc_src, const std::string& src) {
	//変換先の文字列長を求めておいてから変換する (pre-flighting)
	// length_utf16にはヌル文字分も入る
	int length_utf16 = MultiByteToWideChar(enc_src, 0, src.c_str(), -1, NULL, 0);
	if (length_utf16 <= 0) {
		return L"";
	}
	std::wstring str_utf16(length_utf16, 0);
	MultiByteToWideChar(enc_src, 0, src.c_str(), -1, &str_utf16[0], length_utf16);
	return str_utf16.erase(static_cast<size_t>(length_utf16 - 1), 1);  //ヌル文字削除
}
#endif
// 0との比較をするため浮動小数点型には対応不可
template <typename T, std::enable_if_t<std::is_integral_v<T>, std::nullptr_t> = nullptr>
std::uint64_t get_digit(const T num) {
	// 0の場合、forループで演算すると戻り値が0となるのでここで1を返す
	if (0 == num) return 1;
	std::uint64_t digit = 0;
	for (T i = num; i != 0; i /= 10, digit++)
		;
	return digit;
}

std::optional<int> get_console_width() {
#ifdef _WIN32
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	bool ret = ::GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	if (ret) {
		return csbi.dwSize.X;
	}
	return std::nullopt;
#else
	struct winsize w;
	if (ioctl(fileno(stdout), TIOCGWINSZ, &w)) {
		return std::nullopt;
	}
	return w.ws_col;
#endif
}

struct u8cout : private std::streambuf, public std::ostream {
	u8cout() : std::ostream(this) {}
	void flush() {
#ifdef _WIN32
		auto str_utf16 = detail::to_utf16(CP_UTF8, oss.str());
		// std::u8cout << oss.str();
		::WriteConsoleW(::GetStdHandle(STD_OUTPUT_HANDLE), str_utf16.data(),
						static_cast<int>(str_utf16.size()), nullptr, nullptr);
		oss.str("");
		oss.clear();
#else
		std::cout.flush();
#endif
	}

   private:
	int overflow(int c) override {
#ifdef _WIN32
		oss.put(static_cast<char>(c));
		if (c == '\n') {
			flush();
		}
#else
		std::cout.put(static_cast<char>(c));
#endif
		return 0;
	}
	std::ostringstream oss;
};
}  // namespace detail

class pbar {
   public:
	pbar(std::uint64_t total)
		: pbar(total, static_cast<std::uint64_t>(detail::get_console_width().value_or(1) - 1)) {
	};
	pbar(std::uint64_t total, std::uint64_t ncols) : total_(total), ncols_(ncols) {
		init_variables(total_);
	}

	~pbar() {
#ifdef _WIN32
		if (enable_stack_) {
			return;
		}
		auto hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		if (hOutput == INVALID_HANDLE_VALUE) {
			std::cerr << "GetStdHandle failed. cannot reset console mode." << std::endl;
		}
		if (!SetConsoleMode(hOutput, dwMode_orig)) {
			std::cerr << "SetConsoleMode failed. cannot reset console mode." << std::endl;
		}
#endif
	}

	void enable_escape_sequence() {
#ifdef _WIN32
		if (enable_stack_) {
			return;
		}
		auto hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		if (hOutput == INVALID_HANDLE_VALUE) {
			throw std::runtime_error("GetStdHandle failed.");
		}
		if (!GetConsoleMode(hOutput, &dwMode_orig)) {
			throw std::runtime_error("GetConsoleMode failed.");
		}
		if (!SetConsoleMode(hOutput, dwMode_orig | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
										 DISABLE_NEWLINE_AUTO_RETURN)) {
			throw std::runtime_error("SetConsoleMode failed. cannot set virtual terminal flag.s");
		}
#endif
	}

	bool is_cerr_connected_to_terminal() {
#ifdef _WIN32
		return _isatty(_fileno(stderr));
#else
		return isatty(fileno(stderr));
#endif
	}

	void tick(std::uint64_t delta = 1) {
		using namespace std::chrono;
		// not connected to terminal
		if (ncols_ == 0) {
			return;
		}

		if (!progress_.has_value() && enable_stack_) {
			u8cout << std::endl;
		}

		if (!progress_.has_value()) {
			progress_ = 0;
			ncols_ = std::min(
				static_cast<std::uint64_t>(detail::get_console_width().value_or(1) - 1), ncols_);
		}
		std::uint64_t prog = progress_.value();
		prog += delta;
		prog = std::min(prog, total_);
		progress_ = prog;

		if (enable_recalc_console_width_ && (prog % recalc_cycle_) == 0) {
			ncols_ = std::min(
				static_cast<std::uint64_t>(detail::get_console_width().value_or(1) - 1), ncols_);
		}

		nanoseconds dt = 0s;
		seconds remaining = 0s;
		double vel = 0;

		if (enable_time_measurement_) {
			if (!epoch_) {
				epoch_ = steady_clock::now();
			} else {
				dt = steady_clock::now() - *epoch_;
			}
			if (dt.count() > 0) {
				vel = static_cast<double>(prog) / (dt.count() * 1e-9);
				remaining = seconds(static_cast<long long>(std::round((total_ - prog) / (vel))));
			}
		}
		std::int64_t width_non_brackets_base = desc_.size() + 6;
		std::int64_t width_non_brackets_time = 0;
		if (enable_time_measurement_) {
			width_non_brackets_time +=
				2 * digit_ + detail::get_digit(static_cast<std::int64_t>(vel)) + 25;
			if (auto dt_h = duration_cast<hours>(dt).count(); dt_h > 0) {
				width_non_brackets_time += 1 + detail::get_digit(dt_h);
			}
			if (auto remain_h = duration_cast<hours>(remaining).count(); remain_h > 0) {
				width_non_brackets_time += 1 + detail::get_digit(remain_h);
			}
		}
		std::uint64_t width_non_brackets = width_non_brackets_base + width_non_brackets_time;
		std::uint64_t width_brackets;
		if (ncols_ > width_non_brackets) {
			width_brackets = ncols_ - width_non_brackets;
		} else {
			disable_time_measurement();
			width_brackets = 10;
			ncols_ = width_brackets + width_non_brackets_base;
		}

		double prog_rate = static_cast<double>(prog) / total_;
		std::uint64_t num_brackets =
			static_cast<std::uint64_t>(std::round(prog_rate * width_brackets));

		auto prev = u8cout.fill(' ');

		u8cout << '\r';
		if (!desc_.empty()) {
			u8cout << desc_ << ":";
		}
		u8cout << std::setw(3) << static_cast<int>(std::round(prog_rate * 100)) << "%"
			   << opening_bracket_char_;
		for (decltype(num_brackets) _ = 0; _ < num_brackets; _++) {
			u8cout << done_char_;
		}
		for (decltype(num_brackets) _ = 0; _ < width_brackets - num_brackets; _++) {
			u8cout << todo_char_;
		}
		u8cout << closing_bracket_char_;
		if (enable_time_measurement_) {
			u8cout << " " << std::setw(digit_) << prog << "/" << total_ << " ["
				   << std::setfill('0');
			if (auto dt_h = duration_cast<hours>(dt).count() > 0) {
				u8cout << dt_h << ':';
			}
			u8cout << std::setw(2) << duration_cast<minutes>(dt).count() % 60 << ':' << std::setw(2)
				   << duration_cast<seconds>(dt).count() % 60 << '<';
			if (auto remain_h = duration_cast<hours>(remaining).count(); remain_h > 0) {
				u8cout << remain_h % 60 << ':';
			}
			u8cout << std::setw(2) << duration_cast<minutes>(remaining).count() % 60 << ':'
				   << std::setw(2) << remaining.count() % 60 << ", " << std::setw(0) << std::fixed
				   << std::setprecision(2) << vel << "s/it]";
		}
		if (progress_ == total_) {
			if (!leave_) {
				u8cout << ESC_CLEAR_LINE << '\r';
			} else {
				u8cout << "\r" << std::endl;
			}
			if (enable_stack_ && !interrupted_) {
				//カーソルを一つ上に移動
				u8cout << "\x1b[1A";
			}
			reset();
		}
		u8cout << std::setfill(prev);
		u8cout.flush();
	}

	// we assume desc_ consists of ascii characters
	void set_description(const std::string& desc) { desc_ = desc; }
	void set_description(std::string&& desc) { desc_ = std::move(desc); }
#if __cplusplus >= 202002L
	void set_description(const std::u8string& desc) {
		desc_ = reinterpret_cast<const char*>(desc.data());
	}
	void set_description(std::u8string&& desc) {
		desc_ = reinterpret_cast<const char*>(std::move(desc.data()));
	}
#endif
	void enable_stack() {
		enable_stack_ = true;
		leave_ = false;
	}
	void enable_leave() { leave_ = true; }
	void disable_leave() { leave_ = false; }
	void disable_time_measurement() { enable_time_measurement_ = false; }
	void enable_time_measurement() { enable_time_measurement_ = true; }
	void enable_recalc_console_width(std::uint64_t cycle) {
		if (cycle == 0) {
			throw std::invalid_argument("cycle must be greater than zero");
		}
		enable_recalc_console_width_ = true;
		recalc_cycle_ = cycle;
	}
	void disable_recalc_console_width() {
		enable_recalc_console_width_ = false;
		recalc_cycle_ = 0;
	}

	void reset() {
		progress_ = std::nullopt;
		epoch_ = std::nullopt;
		interrupted_ = false;
	}

	void init() { tick(0); }

	template <typename T>
	std::ostream& operator<<(const T&& obj) {
		u8cout << ESC_CLEAR_LINE << '\r';
		u8cout << std::forward<T>(obj);
		interrupted_ = true;
		return u8cout;
	}

	template <class T>
	void warn(const T&& msg) {
		static_assert(std::is_constructible_v<std::string, T>,
					  "std::string(T) must be constructible");
		if (is_cerr_connected_to_terminal_ && ncols_ > 0) {
			std::cerr << ESC_CLEAR_LINE << '\r';
			interrupted_ = true;
		}
		std::cerr << std::forward<T>(msg);
	}
	pbar& operator+=(std::uint64_t delta) {
		tick(delta);
		return *this;
	}
	pbar& operator++(void) {
		tick(1);
		return *this;
	}
	pbar& operator++(int) {
		tick(1);
		return *this;
	}

   private:
	void init_variables(std::uint64_t total) {
		digit_ = detail::get_digit(total);
		is_cerr_connected_to_terminal_ = is_cerr_connected_to_terminal();
		enable_escape_sequence();
		if (total_ == 0) throw std::runtime_error("total_ must be greater than zero");
	}
	std::uint64_t total_ = 0;
	std::uint64_t ncols_ = 80;
	std::optional<std::uint64_t> progress_ = std::nullopt;
	// following members with "char_" suffix must consist of one character
#if __cplusplus >= 202002L
	inline static const std::string done_char_ = reinterpret_cast<const char*>(u8"█");
#else
	inline static const std::string done_char_ = u8"█";
#endif
	inline static const std::string todo_char_ = " ";
	inline static const std::string opening_bracket_char_ = "|";
	inline static const std::string closing_bracket_char_ = "|";
	inline static const std::string ESC_CLEAR_LINE = "\x1b[2K";
	std::string desc_ = "";
	std::uint64_t digit_;
	std::uint64_t recalc_cycle_ = 0;
	std::optional<std::chrono::steady_clock::time_point> epoch_;
	bool enable_stack_ = false;
	bool enable_recalc_console_width_ = false;
	bool leave_ = true;
	bool enable_time_measurement_ = true;
	bool is_cerr_connected_to_terminal_ = false;
	bool interrupted_ = false;
	detail::u8cout u8cout;
#ifdef _WIN32
	DWORD dwMode_orig;
#endif
};	// namespace pbar
}  // namespace pbar