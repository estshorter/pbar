#include <chrono>
#include <iostream>
#include <pbar.hpp>
#include <thread>

void example_pbar(void) {
	using namespace std::this_thread;
	using namespace std::chrono;
	constexpr auto total_ = 30;
	constexpr auto ncols = 100;
	constexpr auto description = "[TASK0]";
	pbar::pbar bar(total_, ncols, description);
	bar.enable_recalc_console_width(1);	 // check console width every tick
	bar.disable_time_measurement();
	bar.init();	 // show a bar with zero progress
	for (auto i = 0; i < total_; ++i, ++bar) {
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
	bar1 << "msg1" << std::endl;		   // to stdout
	bar1.warn("msg2\n");				   // to stderr

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
}

void example_spinner(void) {
	using namespace std::chrono;
	using namespace std::this_thread;
	constexpr auto text = "Loading1... ";
	constexpr auto interval = 80ms;
	auto spin = pbar::spinner(text, interval);
	spin.start();
	sleep_for(1500ms);
	spin.ok();
	spin = pbar::spinner("Loading2...");
	spin.start();
	spin << "msg1" << std::endl;  // to stdout
	spin.warn("msg2\n");		  // to stderr
	sleep_for(1500ms);
	spin.err();
}

int main(void) {
	example_pbar();
	example_spinner();

	return 0;
}