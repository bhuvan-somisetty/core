/*
 *	MetaCall Library by Parra Studios
 *	A library for providing a foreign function interface calls.
 *
 *	Copyright (C) 2016 - 2026 Vicente Eduardo Ferrer Garcia <vic798@gmail.com>
 *
 *	Licensed under the Apache License, Version 2.0 (the "License");
 *	you may not use this file except in compliance with the License.
 *	You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 *	Unless required by applicable law or agreed to in writing, software
 *	distributed under the License is distributed on an "AS IS" BASIS,
 *	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *	See the License for the specific language governing permissions and
 *	limitations under the License.
 *
 */

#include <gtest/gtest.h>

#include <metacall/metacall.h>
#include <metacall/metacall_loaders.h>
#include <metacall/metacall_value.h>

#include <atomic>
#include <chrono>
#include <thread>

class metacall_python_future_test : public testing::Test
{
public:
};

static std::atomic<long> resolved_value(0L);

static void *resolve(void *result, void *)
{
	EXPECT_EQ((enum metacall_value_id)METACALL_LONG, (enum metacall_value_id)metacall_value_id(result));

	resolved_value = metacall_value_to_long(result);

	return NULL;
}

static void *reject(void *, void *)
{
	ADD_FAILURE() << "The future should not be rejected";

	return NULL;
}

TEST_F(metacall_python_future_test, DefaultConstructor)
{
	metacall_print_info();

	ASSERT_EQ((int)0, (int)metacall_initialize());

/* Python */
#if defined(OPTION_BUILD_LOADERS_PY)
	{
		static const char buffer[] =
			"import asyncio\n"
			"def future_timeout(value):\n"
			"\tloop = asyncio.new_event_loop()\n"
			"\tf = loop.create_future()\n"
			"\tloop.call_later(0.1, f.set_result, value)\n"
			"\treturn f\n";

		ASSERT_EQ((int)0, (int)metacall_load_from_memory("py", buffer, sizeof(buffer), NULL));

		void *args[] = {
			metacall_value_create_long(15L)
		};

		void *future = metacallv("future_timeout", args);

		metacall_value_destroy(args[0]);

		ASSERT_EQ((enum metacall_value_id)METACALL_FUTURE, (enum metacall_value_id)metacall_value_id(future));

		void *ret = metacall_await_future(metacall_value_to_future(future), resolve, reject, NULL);

		EXPECT_NE((void *)NULL, (void *)ret);

		/* Wait for the timeout to resolve the future */
		for (int i = 0; i < 100 && resolved_value == 0L; ++i)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		EXPECT_EQ((long)15L, (long)resolved_value);

		metacall_value_destroy(ret);
		metacall_value_destroy(future);
	}
#endif /* OPTION_BUILD_LOADERS_PY */

	metacall_destroy();
}
