#include <chrono>
#include <iostream>
#include <pbar.hpp>
#include <thread>

int main(void) {
	using namespace std::this_thread;
	using namespace std::chrono;
	constexpr auto total = 100;
	constexpr auto ncols = 100;
	pbar::pbar bar(total, ncols);
	bar.set_description("[TASK0]");
	bar.init();	 // not always necessary
	for (std::int64_t i = 0; i < total; ++i, ++bar) {
		sleep_for(milliseconds(10));
	}
	std::cout << "done!" << std::endl;
	pbar::pbar bar1(4);
	pbar::pbar bar2(8);
	pbar::pbar bar3(16);
	bar1.set_description("[TASK1]");
	bar2.set_description("[TASK2]");
	bar3.set_description("[TASK3]");

	bar2.enable_stack();
	bar3.enable_stack();

	bar1.enable_recalc_console_width(10);  // check console width every 10 ticks

	bar1.init();
	for (auto i = 0; i < 4; ++i, ++bar1) {
		bar2.init();
		for (auto j = 0; j < 8; ++j, ++bar2) {
			bar3.init();
			for (auto k = 0; k < 16; ++k, ++bar3) {
				sleep_for(milliseconds(10));
			}
			sleep_for(milliseconds(50));
		}
		sleep_for(milliseconds(100));
	}
	std::cout << "done!" << std::endl;

	return 0;
}