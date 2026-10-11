// doctest's main(), shared by all the test programs, with a watchdog: if a test case runs for longer than 90 seconds, it says which one (and which subcase) and exits. CTest's own timeout only says which program hung.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace
{
	// Less than the CTest timeout, so the watchdog gets to report first. TG_TEST_WATCHDOG_SECONDS overrides it.
	int g_nWatchdogSeconds = 90;

	std::mutex g_lock;
	bool g_bInTestCase = false;
	std::string g_testCase;
	std::string g_subcase;
	std::chrono::steady_clock::time_point g_started;

	struct WatchdogListener : public doctest::IReporter
	{
		explicit WatchdogListener(const doctest::ContextOptions &) {}

		void test_case_start(const doctest::TestCaseData &testCase) override
		{
			std::lock_guard<std::mutex> guard(g_lock);
			g_bInTestCase = true;
			g_testCase = testCase.m_name;
			g_subcase.clear();
			g_started = std::chrono::steady_clock::now();
		}

		void test_case_reenter(const doctest::TestCaseData &) override
		{
			std::lock_guard<std::mutex> guard(g_lock);
			g_subcase.clear();
		}

		void test_case_end(const doctest::CurrentTestCaseStats &) override
		{
			std::lock_guard<std::mutex> guard(g_lock);
			g_bInTestCase = false;
		}

		void subcase_start(const doctest::SubcaseSignature &subcase) override
		{
			std::lock_guard<std::mutex> guard(g_lock);
			g_subcase = subcase.m_name.c_str();
		}

		void report_query(const doctest::QueryData &) override {}
		void test_run_start() override {}
		void test_run_end(const doctest::TestRunStats &) override {}
		void test_case_exception(const doctest::TestCaseException &) override {}
		void subcase_end() override {}
		void log_assert(const doctest::AssertData &) override {}
		void log_message(const doctest::MessageData &) override {}
		void test_case_skipped(const doctest::TestCaseData &) override {}
	};

	void Watchdog()
	{
		for (;;)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));

			std::string testCase, subcase;
			{
				std::lock_guard<std::mutex> guard(g_lock);
				if (!g_bInTestCase || std::chrono::steady_clock::now() - g_started <= std::chrono::seconds(g_nWatchdogSeconds))
					continue;

				testCase = g_testCase;
				subcase = g_subcase;
			}

			fprintf(stderr, "\nWATCHDOG: test case \"%s\"%s%s%s has run for over %d seconds; giving up\n",
				testCase.c_str(), subcase.empty() ? "" : " (subcase \"", subcase.c_str(), subcase.empty() ? "" : "\")", g_nWatchdogSeconds);
			fflush(stderr);

			// Not abort(): doctest catches its signal and tries to finish the test run, which a hung test case can block forever
			_Exit(EXIT_FAILURE);
		}
	}
}

REGISTER_LISTENER("watchdog", 1, WatchdogListener);

int main(int argc, char **argv)
{
	const char *lpszSeconds = getenv("TG_TEST_WATCHDOG_SECONDS");
	if (lpszSeconds && atoi(lpszSeconds) > 0)
		g_nWatchdogSeconds = atoi(lpszSeconds);

	std::thread(Watchdog).detach();

	doctest::Context context(argc, argv);
	return context.run();
}
