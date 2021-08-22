#pragma once
#include <chrono>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

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

#ifdef _WIN32
DWORD enable_escape_sequence() {
	auto hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD dwMode_orig_;
	if (hOutput == INVALID_HANDLE_VALUE) {
		throw std::runtime_error("GetStdHandle failed.");
	}
	if (!GetConsoleMode(hOutput, &dwMode_orig_)) {
		throw std::runtime_error("GetConsoleMode failed.");
	}
	if (!SetConsoleMode(hOutput, dwMode_orig_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
									 DISABLE_NEWLINE_AUTO_RETURN)) {
		throw std::runtime_error("SetConsoleMode failed. cannot set virtual terminal flag.s");
	}
	return dwMode_orig_;
}
#endif

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
	u8cout& operator=(const u8cout& other) {
		oss.str("");
		oss.clear();
		oss << other.oss.str();
		return *this;
	}
	u8cout& operator=(u8cout&& other) noexcept {
		oss = std::move(other.oss);
		return *this;
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
	pbar(std::uint64_t total, const std::string& desc = "")
		: pbar(total, static_cast<std::uint64_t>(detail::get_console_width().value_or(1) - 1),
			   desc){};

	pbar(std::uint64_t total, std::uint64_t ncols, const std::string& desc = "")
		: total_(total), ncols_(ncols), desc_(desc) {
		init_variables(total_);
	}

	~pbar() {
		if (enable_stack_) {
			return;
		}
		u8cout << "\x1b[?25h";	// show cursor
#ifdef _WIN32
		auto hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		if (hOutput == INVALID_HANDLE_VALUE) {
			std::cerr << "GetStdHandle failed. cannot reset console mode." << std::endl;
		}
		if (!SetConsoleMode(hOutput, dwMode_orig_)) {
			std::cerr << "SetConsoleMode failed. cannot reset console mode." << std::endl;
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
		std::int64_t width_non_brackets_base = desc_.size() + 2 * digit_ + 8;
		std::int64_t width_non_brackets_time = 0;
		if (enable_time_measurement_) {
			width_non_brackets_time += detail::get_digit(static_cast<std::int64_t>(vel)) + 23;
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

		u8cout << ESC_CLEAR_LINE << '\r';
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
		u8cout << closing_bracket_char_ << " " << std::setw(digit_) << prog << "/" << total_;
		if (enable_time_measurement_) {
			u8cout << " [" << std::setfill('0');
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
				   << std::setprecision(2) << vel << "it/s]";
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
#if __cplusplus > 201703L  // for C++20
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
	std::ostream& operator<<(T&& obj) {
		if (ncols_ > 0) {
			u8cout << ESC_CLEAR_LINE << '\r';
			u8cout << std::forward<T>(obj);
			return u8cout;
		} else {
			std::cout << std::forward<T>(obj);
			return std::cout;
		}
	}

	template <class T>
	void warn(T&& msg) {
		static_assert(std::is_constructible_v<std::string, T>,
					  "std::string(T) must be constructible");
		if (is_cerr_connected_to_terminal_ && ncols_ > 0) {
			std::cerr << ESC_CLEAR_LINE << '\r';
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

	pbar& operator=(const pbar& other) {
		total_ = other.total_;
		digit_ = other.digit_;
		recalc_cycle_ = other.recalc_cycle_;
		epoch_ = other.epoch_;
		enable_stack_ = other.enable_stack_;
		leave_ = other.leave_;
		enable_time_measurement_ = other.enable_time_measurement_;
		is_cerr_connected_to_terminal_ = other.is_cerr_connected_to_terminal_;
		interrupted_ = other.interrupted_;
		u8cout = other.u8cout;
		return *this;
	}
	pbar& operator=(pbar&& other) noexcept {
		digit_ = std::move(other.digit_);
		total_ = std::move(other.total_);
		recalc_cycle_ = std::move(other.recalc_cycle_);
		epoch_ = std::move(other.epoch_);
		enable_stack_ = std::move(other.enable_stack_);
		leave_ = std::move(other.leave_);
		enable_time_measurement_ = std::move(other.enable_time_measurement_);
		is_cerr_connected_to_terminal_ = std::move(other.is_cerr_connected_to_terminal_);
		interrupted_ = std::move(other.interrupted_);
		u8cout = std::move(other.u8cout);
		return *this;
	}

   private:
	void init_variables(std::uint64_t total) {
		digit_ = detail::get_digit(total);
		is_cerr_connected_to_terminal_ = is_cerr_connected_to_terminal();
		if (!enable_stack_) {
#ifdef _WIN32
			dwMode_orig_ = detail::enable_escape_sequence();
#endif
			u8cout << "\x1b[?25l";	// hide cursor
		}
		if (total_ == 0) throw std::runtime_error("total_ must be greater than zero");
	}
	std::uint64_t total_ = 0;
	std::uint64_t ncols_ = 80;
	std::optional<std::uint64_t> progress_ = std::nullopt;
	// following members with "char_" text must consist of one character
#if __cplusplus > 201703L  // for C++20
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
	DWORD dwMode_orig_;
#endif
};

class spinner {
   public:
	spinner(std::string text, std::chrono::milliseconds interval = std::chrono::milliseconds(200))
		: interval_(interval), text_(text) {}
	~spinner() { stop(); }

	void start() {
		if (thr_) {
			throw std::runtime_error("spinner is already working");
		}
		active_ = true;
#ifdef _WIN32
		dwMode_orig_ = detail::enable_escape_sequence();
#endif
		u8cout << "\x1b[?25l";

		thr_ = std::thread([&]() {
			size_t c = 0;

			while (active_) {
				{
					std::lock_guard lock(mtx_);
					u8cout << '\r';
#if !defined(_WIN32) && __cplusplus > 201703L  // for C++20
					std::u8string spinner_char = spinner_chars_[c];
					u8cout << reinterpret_cast<const char*>(spinner_char.data());
#else
					u8cout << spinner_chars_[c];
#endif
					u8cout << ' ' << text_;
					c = (c + 1) % spinner_chars_.size();
					u8cout.flush();
				}
				std::this_thread::sleep_for(interval_);
			}
		});
	}

	void stop() {
		u8cout << "\x1b[?25h";
		if (!thr_) {
			return;
		}

#ifdef _WIN32
		auto hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		if (hOutput == INVALID_HANDLE_VALUE) {
			std::cerr << "GetStdHandle failed. cannot reset console mode." << std::endl;
		}
		if (!SetConsoleMode(hOutput, dwMode_orig_)) {
			std::cerr << "SetConsoleMode failed. cannot reset console mode." << std::endl;
		}
#endif

		active_ = false;
		thr_->join();
		thr_ = std::nullopt;
	}

	void ok() {
		stop();
		u8cout << '\r';
#if __cplusplus > 201703L  // for C++20
		u8cout << reinterpret_cast<const char*>(u8"✔");
#else
		u8cout << u8"✔";
#endif
		u8cout << text_ << " [SUCCESS]" << std::endl;
		u8cout.flush();
	}

	void err() {
		u8cout << '\r';
#if __cplusplus > 201703L  // for C++20
		u8cout << reinterpret_cast<const char*>(u8"✖");
#else
		u8cout << u8"✖";
#endif
		u8cout << text_ << " [FAILURE]" << std::endl;
		u8cout.flush();
	}

	template <typename T>
	std::ostream& operator<<(T&& obj) {
		std::lock_guard lock(mtx_);
#ifdef _WIN32
		if (_isatty(_fileno(stdout))) {
#else
		if (isatty(fileno(stdout))) {
#endif
			u8cout << ESC_CLEAR_LINE << '\r';
			u8cout << std::forward<T>(obj);
			return u8cout;
		} else {
			std::cout << std::forward<T>(obj);
			return std::cout;
		}
	}

	template <class T>
	void warn(T&& msg) {
		static_assert(std::is_constructible_v<std::string, T>,
					  "std::string(T) must be constructible");
		std::lock_guard lock(mtx_);
#ifdef _WIN32
		if (_isatty(_fileno(stderr))) {
#else
		if (isatty(fileno(stderr))) {
#endif
			std::cerr << ESC_CLEAR_LINE << '\r';
		}
		std::cerr << std::forward<T>(msg);
	}

	spinner& operator=(const spinner& other) {
		if (thr_ || other.thr_) {
			throw std::runtime_error("spinner is working");
		}
		interval_ = other.interval_;
		text_ = other.text_;
#ifdef _WIN32
		dwMode_orig_ = other.dwMode_orig_;
#endif
		active_ = other.active_;
		thr_ = std::nullopt;
		u8cout = u8cout;
		return *this;
	}

	spinner& operator=(spinner&& other) {
		other.stop();
		interval_ = std::move(other.interval_);
		text_ = std::move(other.text_);
#ifdef _WIN32
		dwMode_orig_ = std::move(other.dwMode_orig_);
#endif
		active_ = std::move(other.active_);
		thr_ = std::nullopt;
		u8cout = std::move(u8cout);
		return *this;
	}

   private:
#ifdef _WIN32
	inline static const std::vector<std::string> spinner_chars_ = {{"|", "/", "-", "\\"}};
#else
#if __cplusplus > 201703L  // for C++20
	inline static const std::vector<std::u8string> spinner_chars_ = {
		u8"⠋", u8"⠙", u8"⠹", u8"⠸", u8"⠼", u8"⠴", u8"⠦", u8"⠧", u8"⠇", u8"⠏"};
#else
	inline static const std::vector<std::string> spinner_chars_ = {
		{u8"⠋", u8"⠙", u8"⠹", u8"⠸", u8"⠼", u8"⠴", u8"⠦", u8"⠧", u8"⠇", u8"⠏"}};
#endif
#endif
	inline static const std::string ESC_CLEAR_LINE = "\x1b[2K";
	std::chrono::milliseconds interval_;
	std::string text_;
	bool active_ = false;
	std::optional<std::thread> thr_ = std::nullopt;
	std::mutex mtx_;
	detail::u8cout u8cout;
#ifdef _WIN32
	DWORD dwMode_orig_;
#endif
};

}  // namespace pbar