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

#include <chrono>
#include <condition_variable>
#include <mutex>

class metacall_python_await_future_test : public testing::Test
{
public:
};

struct await_future_state
{
	std::mutex mutex;
	std::condition_variable cv;
	long expected;
	long value;
	bool done;
};

static void *never_executed(void *, void *)
{
	/* If we reach here, the future has been resolved or rejected with the wrong callback */
	ADD_FAILURE() << "This callback should never be executed";

	return NULL;
}

/* The future returned by metacall_await_future is resolved with the value returned by the callback */
static void *chain_resolve(void *result, void *data)
{
	struct await_future_state *state = static_cast<struct await_future_state *>(data);

	EXPECT_NE((void *)NULL, (void *)result);

	EXPECT_EQ((enum metacall_value_id)METACALL_LONG, (enum metacall_value_id)metacall_value_id(result));

	{
		std::lock_guard<std::mutex> lock(state->mutex);
		state->value = metacall_value_to_long(result);
		state->done = true;
	}

	state->cv.notify_one();

	return NULL;
}

static void *future_resolve(void *result, void *data)
{
	struct await_future_state *state = static_cast<struct await_future_state *>(data);

	EXPECT_NE((void *)NULL, (void *)result);

	EXPECT_EQ((enum metacall_value_id)METACALL_LONG, (enum metacall_value_id)metacall_value_id(result));

	EXPECT_EQ((long)state->expected, (long)metacall_value_to_long(result));

	return metacall_value_create_long(metacall_value_to_long(result) + 1L);
}

static void *future_reject(void *result, void *data)
{
	struct await_future_state *state = static_cast<struct await_future_state *>(data);

	EXPECT_NE((void *)NULL, (void *)result);

	EXPECT_EQ((enum metacall_value_id)METACALL_LONG, (enum metacall_value_id)metacall_value_id(result));

	EXPECT_EQ((long)state->expected, (long)metacall_value_to_long(result));

	return metacall_value_create_long(metacall_value_to_long(result) + 1L);
}

/* The coroutine returns a Python future, await it and then await the future returned by the await */
static void *await_future(void *result, void *data, void *(*resolve)(void *, void *), void *(*reject)(void *, void *))
{
	EXPECT_NE((void *)NULL, (void *)result);

	EXPECT_EQ((enum metacall_value_id)METACALL_FUTURE, (enum metacall_value_id)metacall_value_id(result));

	void *future = metacall_await_future(metacall_value_to_future(result), resolve, reject, data);

	EXPECT_NE((void *)NULL, (void *)future);

	EXPECT_EQ((enum metacall_value_id)METACALL_FUTURE, (enum metacall_value_id)metacall_value_id(future));

	void *chain = metacall_await_future(metacall_value_to_future(future), chain_resolve, never_executed, data);

	EXPECT_NE((void *)NULL, (void *)chain);

	metacall_value_destroy(chain);
	metacall_value_destroy(future);

	return NULL;
}

static void *await_future_resolve(void *result, void *data)
{
	return await_future(result, data, future_resolve, never_executed);
}

static void *await_future_reject(void *result, void *data)
{
	return await_future(result, data, never_executed, future_reject);
}

static void wait_future(struct await_future_state *state)
{
	std::unique_lock<std::mutex> lock(state->mutex);

	EXPECT_EQ((bool)true, (bool)state->cv.wait_for(lock, std::chrono::seconds(10), [state] { return state->done; }));

	EXPECT_EQ((long)(state->expected + 1L), (long)state->value);
}

TEST_F(metacall_python_await_future_test, DefaultConstructor)
{
	metacall_print_info();

	ASSERT_EQ((int)0, (int)metacall_initialize());

/* Python */
#if defined(OPTION_BUILD_LOADERS_PY)
	{
		static const char buffer[] =
			"import asyncio\n"
			"async def future_resolve_fn(n):\n"
			"\tloop = asyncio.get_running_loop()\n"
			"\tf = loop.create_future()\n"
			"\tloop.call_soon(f.set_result, n * 2)\n"
			"\treturn f\n"
			"async def future_reject_fn(n):\n"
			"\tloop = asyncio.get_running_loop()\n"
			"\tf = loop.create_future()\n"
			"\tloop.call_soon(f.set_exception, Exception(n * 3))\n"
			"\treturn f\n";

		ASSERT_EQ((int)0, (int)metacall_load_from_memory("py", buffer, sizeof(buffer), NULL));

		void *args[] = {
			metacall_value_create_long(7L)
		};

		/* Test resolve */
		{
			struct await_future_state state;

			state.expected = 14L;
			state.value = 0L;
			state.done = false;

			void *future = metacall_await("future_resolve_fn", args, await_future_resolve, never_executed, &state);

			EXPECT_NE((void *)NULL, (void *)future);

			EXPECT_EQ((enum metacall_value_id)METACALL_FUTURE, (enum metacall_value_id)metacall_value_id(future));

			wait_future(&state);

			metacall_value_destroy(future);
		}

		/* Test reject */
		{
			struct await_future_state state;

			state.expected = 21L;
			state.value = 0L;
			state.done = false;

			void *future = metacall_await("future_reject_fn", args, await_future_reject, never_executed, &state);

			EXPECT_NE((void *)NULL, (void *)future);

			EXPECT_EQ((enum metacall_value_id)METACALL_FUTURE, (enum metacall_value_id)metacall_value_id(future));

			wait_future(&state);

			metacall_value_destroy(future);
		}

		metacall_value_destroy(args[0]);
	}
#endif /* OPTION_BUILD_LOADERS_PY */

	metacall_destroy();
}
