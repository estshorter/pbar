#include <chrono>
#include <iostream>
#include <pbar.hpp>
#include <thread>

using namespace std::this_thread;
using namespace std::chrono;

void example_pbar(void) {
	constexpr auto total_ = 80;
	constexpr auto ncols = 100;
	pbar::pbar bar(total_, ncols, "[TASK0]");
	bar.init();							 // not always necessary
	bar.enable_recalc_console_width(1);	 // check console width every tick
	bar.disable_time_measurement();
	for (std::int64_t i = 0; i < total_; ++i, ++bar) {
		sleep_for(milliseconds(20));
	}
	std::cout << "TASK0 done!" << std::endl;
	pbar::pbar bar1(4, "[TASK1]");
	pbar::pbar bar2(8, "[TASK2]");
	pbar::pbar bar3(16, "[TASK3]");

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
	std::cout << "TASK1-3 done!" << std::endl;
}

void example_spinner(void) {
	// spinner example
	using namespace std::chrono;
	using namespace std::this_thread;
	auto spin = pbar::spinner("Loading...", 100ms);
	spin.start();
	sleep_for(milliseconds(3000));
	spin.ok();
	spin = pbar::spinner("Loading2...", 100ms);
	spin.start();
	spin << "msg" << std::endl;
	spin.warn("msg2\n");
	sleep_for(milliseconds(3000));
	spin.err();
}

int main(void) { 
	example_pbar();
	
	example_spinner();
	
	return 0; }