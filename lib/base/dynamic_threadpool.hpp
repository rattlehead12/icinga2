/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2017 Icinga Development Team (https://www.icinga.com/)  *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#ifndef DYNAMIC_THREADPOOL_H
#define DYNAMIC_THREADPOOL_H

#include "base/i2-base.hpp"
#include <stack>
#include <stddef.h>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

namespace icinga
{

class I2_BASE_API CallableContainer
{
public:
	virtual ~CallableContainer();

	virtual void operator() (void);

protected:
	CallableContainer();
};

template<class Callable>
class CallableContainerT : public CallableContainer
{
public:
	CallableContainerT(const Callable& callable) : m_Callable(callable) {}
	virtual ~CallableContainerT() {}

	virtual void operator() (void) {
		m_Callable();
	}

private:
	Callable m_Callable;
};

template<class T>
class UnbufferedChannel
{
public:
	UnbufferedChannel()
	{
	}

	~UnbufferedChannel()
	{
	}

	void Push(const T& value)
	{
		boost::mutex::scoped_lock lock(m_Mutex);
		while (m_Pushing)
			m_PushingDone.wait(lock);

		m_Pushing = true;

		m_QueuedItem = &value;
		m_Queued.notify_all();

		m_UnQueued.wait(lock);

		m_Pushing = false;
		m_PushingDone.notify_all();
	}

	bool PushFillOrKill(const T& value)
	{
		boost::mutex::scoped_lock lock(m_Mutex);
		while (m_Pushing)
			m_PushingDone.wait(lock);

		m_Pushing = true;

		bool result = m_Popping || m_PopPending;
		if (result) {
			m_QueuedItem = &value;
			m_Queued.notify_all();

			m_UnQueued.wait(lock);
		}

		m_Pushing = false;
		m_PushingDone.notify_all();

		return result;
	}

	T Pop()
	{
		boost::mutex::scoped_lock lock(m_Mutex);
		if (m_Popping) {
			++m_PopPending;
			do m_PoppingDone.wait(lock);
			while (m_Popping);
			--m_PopPending;
		}

		m_Popping = true;

		if (m_QueuedItem == NULL)
			m_Queued.wait(lock);

		T value(*m_QueuedItem);
		m_QueuedItem = NULL;
		m_UnQueued.notify_all();

		m_Popping = false;
		m_PoppingDone.notify_all();

		return value;
	}

private:
	boost::mutex m_Mutex;
	boost::condition_variable m_PushingDone, m_PoppingDone, m_Queued, m_UnQueued;
	const T *m_QueuedItem = NULL;
	size_t m_PopPending = 0u;
	bool m_Pushing = false, m_Popping = false;
};

/**
 * A dynamic thread pool.
 *
 * @ingroup base
 */
class I2_BASE_API DynamicThreadPool
{
public:
	DynamicThreadPool();
	~DynamicThreadPool();

	template<class Callable>
	void Post(const Callable& task) {
		CallableContainer *container = new CallableContainerT<Callable>(task);

		try {
			PostImpl(container);
		} catch (...) {
			delete container;
			throw;
		}
	}

private:
	boost::mutex m_Mutex;
	std::stack<boost::thread> m_Threads;
	UnbufferedChannel<CallableContainer*> m_TaskChannel;

	void PostImpl(CallableContainer *task);
	void DoTasks();
};

}

#endif /* DYNAMIC_THREADPOOL_H */

