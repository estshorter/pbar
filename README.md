# pbar
**pbar** is a progress bar library inspired by [tqdm](https://github.com/tqdm/tqdm).

![demo](https://raw.githubusercontent.com/estshorter/pbar/videos/example1.gif)

## Highlights
- Support Windows and Linux
- Support UTF-8 character (even in Windows!)
- Support output while displaying a bar
- Header only ([pbar.hpp](https://github.com/estshorter/pbar/blob/master/pbar.hpp))

Note: this library is not thread-safe.

## Requiremtents
- C++17 or later
- Support of VT100 escape sequences

## Examples
Minimum example:
```cpp
#include <chrono>
#include <iostream>
#include <pbar.hpp>
#include <thread>

int main(void) {
	using namespace std::this_thread;
	using namespace std::chrono;
	constexpr auto total = 100;
	constexpr auto ncols = 100;
	constexpr auto desc = "[TASK0]";

	pbar::pbar bar(total, ncols, desc);
	bar.init();	 // show a bar with zero progress
	for (auto i = 0; i < total; ++i, ++bar) {
		if (i == 0) {
			bar << "OUTPUT_TO_STDOUT" << std::endl;
		} else if (i == 10) {
			bar.warn("OUTPUT_TO_STDERR\n");
		}
		sleep_for(milliseconds(10));
		// bar.tick() is also fine
	}
	return 0;
}
```

Multiple bars:
```cpp
constexpr auto total_ = 30;
constexpr auto ncols = 100;
pbar::pbar bar(total_, ncols, "[TASK0]");
bar.init();							 // not always necessary
bar.enable_recalc_console_width(1);	 // check console width every tick
bar.disable_time_measurement();
for (std::int64_t i = 0; i < total_; ++i, ++bar) {
	sleep_for(milliseconds(20));
}
std::cout << "TASK0 done!" << std::endl;
constexpr auto bar1_total = 2;
constexpr auto bar2_total = 4;
constexpr auto bar3_total = 8;

pbar::pbar bar1(bar1_total, "[TASK1]");
pbar::pbar bar2(bar2_total, "[TASK2]");
pbar::pbar bar3(bar3_total, "[TASK3]");

bar2.enable_stack();
bar3.enable_stack();

bar1.enable_recalc_console_width(10);  // check console width every 10 ticks
bar1 << "msg1" << std::endl;
bar1.warn("msg2\n");

bar1.init();
for (auto i = 0; i < bar1_total; ++i, ++bar1) {
	bar2.init();
	for (auto j = 0; j < bar2_total; ++j, ++bar2) {
		bar3.init();
		for (auto k = 0; k < bar3_total; ++k, ++bar3) {
			sleep_for(10ms);
		}
		sleep_for(50ms);
	}
	sleep_for(100ms);
}
std::cout << "TASK1-3 done!" << std::endl;
```

spinner is also provided:
![demo-spinner](https://raw.githubusercontent.com/estshorter/pbar/videos/example2.gif)

``` cpp
auto spin = pbar::spinner("Loading1...", 100ms);
spin.start();
sleep_for(1500ms);
spin.ok();
spin = pbar::spinner("Loading2...", 100ms);
spin.start();
spin << "msg1" << std::endl;
spin.warn("msg2\n");
sleep_for(1500ms);
spin.err();
```